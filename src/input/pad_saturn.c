/** @file src/input/pad_saturn.c Sega Saturn controllers as mouse and keys.
 *
 * The controller type comes from what is plugged in, looked at every frame:
 * a keyboard and a mouse on the two ports make a DOS-style keyboard+mouse;
 * otherwise the device on port 1 is used, or else the one on port 2. With
 * none, the game pauses with a message until one is connected.
 *
 * Standard pad (after the Mega Drive version):
 *   D-pad        UI mode: moves the focus between the buttons of the screen
 *                (on the campaign map, between the regions to choose);
 *                camera mode (missions): scrolls the map under a cursor
 *                fixed in the middle of the map view.
 *   C            in missions, a tap switches between UI and camera mode;
 *                held with the D-pad, scrolls the camera fast. Targeting
 *                and placing a structure use the camera whatever the mode.
 *   A            left mouse button (press the focused button, select,
 *                target, place; hold to drag)
 *   B            cancel (Esc)
 *   X Y Z R      the selected unit's command buttons 1-4 in the side bar;
 *                with a structure selected, X opens its menu (F3) and Y
 *                repairs or upgrades it
 *   L (hold)     Shift: with a command button, Ambush / Area Guard
 *   L (tap)      select your next unit and centre the view on it;
 *                C+L: your next structure
 *   Start        options (F2); C+Start: Mentat (F1)
 *   A+B+C+Start  leave for the BIOS screen
 * 3D Controller: the same, but the D-pad always works the UI and the analog
 *   stick the camera (no C switch): whichever was used last has the cursor.
 * Keyboard: the DOS keys (letters, F keys, Esc, Enter, Shift) go to the game
 *   as they are; the arrows work as the D-pad, Space as A and Tab as C.
 * Keyboard and mouse: as on DOS.
 *
 * The mouse pointer is only drawn with a mouse. With the pad-like
 * controllers the focused button gets a reticle, drawn on the VDP2 overlay and gliding from one button to the
 * next (lines of menus and lists change colour instead), and so does the
 * centre tile of the camera. Moving the focus makes a blip.
 *
 * Mouse and key events go to the engine from Video_Tick, as other video
 * drivers deliver them. Moving the focus and the camera needs the game's
 * state, so it happens in PadSaturn_HandleEvents(), called by
 * GUI_Widget_HandleEvents() with the widgets of the screen on show. */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "types.h"
#include "input.h"
#include "mouse.h"
#include "pad_saturn.h"
#include "../gfx.h"
#include "../audio/dsp.h"
#include "../config.h"
#include "../gui/gui.h"
#include "../gui/mentat.h"
#include "../gui/widget.h"
#include "../house.h"
#include "../map.h"
#include "../opendune.h"
#include "../pool/pool.h"
#include "../pool/structure.h"
#include "../pool/unit.h"
#include "../structure.h"
#include "../tile.h"
#include "../timer.h"
#include "../unit.h"

#include "bios.h"
#include "saturn_hw.h"
#include "saturn_timer.h"
#include "smpc.h"
#include "vdp2.h"

enum {
	TAP_FRAMES = 15,            /* a press this short is a tap, not a hold */
	REPEAT_FIRST = 18,          /* D-pad repeat for the focus, in frames */
	REPEAT_NEXT = 5,
	SCROLL_FIRST = 12,          /* and for scrolling the camera */
	SCROLL_NEXT = 4,
	STICK_DEAD = 24,            /* 3D Controller stick dead zone */
	STICK_STEP = 300,           /* stick travel summed up per tile of scroll */
	FOCUS_STALE = 5,            /* frames without PadSaturn_HandleEvents() */
	MISSING_FRAMES = 30,        /* frames with nothing connected before it counts */
	/* the cursor in camera mode: the middle of the centre tile of the map
	 * view (tile 7, 5 of the 15 x 10 shown from 0, 40) */
	CAMERA_X = 7 * 16 + 8,
	CAMERA_Y = 40 + 5 * 16 + 8,
	/* the 320x200 picture is centred in the 224 lines shown (video_saturn.c) */
	OVERLAY_TOP = (VDP2_DISPLAY_H - SCREEN_HEIGHT) / 2,

	NO_DIRECTION = 0xFFFF
};

/* PC XT scan codes, as Input_EventHandler() expects */
enum {
	SCANCODE_ESC = 0x01,
	SCANCODE_LSHIFT = 0x2A,
	SCANCODE_F1 = 0x3B,
	SCANCODE_F2 = 0x3C,
	SCANCODE_F3 = 0x3D,
	SCANCODE_RELEASED = 0x80
};

/* Dune II key codes: arrows and Return as lists take them, and one no key
 * sends (between F12 and Scroll Lock), given to the Repair/Upgrade button,
 * which has no shortcut of its own */
enum {
	KEY_RETURN = 0x2B,
	KEY_ARROW_UP = 0x60,
	KEY_ARROW_DOWN = 0x62,
	KEY_REPAIR_UPGRADE = 0x7C
};

/* Saturn keyboard key numbers (PS/2 set 2) standing in for pad buttons */
enum {
	KEY_TAB = 0x0D,
	KEY_SPACE = 0x29
};

typedef enum Controller {
	CONTROLLER_NONE,
	CONTROLLER_PAD,
	CONTROLLER_3D,
	CONTROLLER_KEYBOARD,
	CONTROLLER_KEYBOARD_MOUSE
} Controller;

#define PAD_DIRECTIONS (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT)
#define PAD_RESET (PAD_A | PAD_B | PAD_C | PAD_START)

static Controller s_controller = CONTROLLER_PAD;
static int s_x = SCREEN_WIDTH / 2;
static int s_y = SCREEN_HEIGHT / 2;
static int s_minX = 0, s_maxX = SCREEN_WIDTH - 1;
static int s_minY = 0, s_maxY = SCREEN_HEIGHT - 1;
static bool s_positionChanged = true;
static uint16 s_previous = 0;           /* buttons of the last frame */
static bool s_leftButton, s_rightButton;
static int s_repeatFrames = 0;
static int s_lTapFrames = -1;           /* frames L has been held, -1: not a tap */
static int s_cTapFrames = -1;           /* the same for C */
static bool s_keySpace, s_keyTab;       /* keyboard keys working as A and C */
static int s_missingFrames = 0;
static bool s_controllerMessage = true;     /* pause with a message when none */

/* requests from Video_Tick for PadSaturn_HandleEvents() and the game loop */
static volatile uint16 s_navigate = NO_DIRECTION;   /* move the focus */
static volatile uint16 s_scroll = NO_DIRECTION;     /* scroll the camera */
static volatile int s_stickX = 0, s_stickY = 0;     /* stick travel summed up */
static volatile bool s_toggleCamera = false;
static volatile bool s_stickLast = false;           /* 3D Controller: stick used last */
static volatile bool s_pressA = false;              /* A pressed (for PadSaturn_PickRegion()) */
static volatile uint16 s_keyA = 0;                  /* a key A sends instead of a click, or 0 */
static volatile bool s_blockA = false;              /* ignore A until it is released */
static volatile bool s_useSound = false;            /* A on the focus makes the use sound */
static enum { CYCLE_NONE, CYCLE_UNIT, CYCLE_STRUCTURE } s_cycle = CYCLE_NONE;

/* what PadSaturn_HandleEvents() found */
static bool s_camera = false;                       /* camera mode chosen with C */
static volatile bool s_cameraActive = false;        /* the cursor is the camera's */
static volatile bool s_focusActive = false;         /* the screen has buttons to focus */
static volatile uint32 s_handledFrame = 0;

static bool s_pointerVisible = true;                /* the mouse pointer is drawn */
typedef struct Rect { int x, y, w, h; } Rect;       /* w 0: none */
static Rect s_reticle = { 0, 0, 0, 0 };             /* where the reticle goes */
static Rect s_reticleDrawn = { 0, 0, 0, 0 };        /* where it is on the overlay */

void PadSaturn_Init(void)
{
	saturn_timer_set_vblank_hook(smpc_vblank);
}

static void PadSaturn_Clamp(void)
{
	if (s_x < s_minX) s_x = s_minX;
	if (s_x > s_maxX) s_x = s_maxX;
	if (s_y < s_minY) s_y = s_minY;
	if (s_y > s_maxY) s_y = s_maxY;
}

void PadSaturn_SetPosition(uint16 x, uint16 y)
{
	s_x = x;
	s_y = y;
	PadSaturn_Clamp();
	s_positionChanged = true;
}

void PadSaturn_SetRegion(uint16 minX, uint16 maxX, uint16 minY, uint16 maxY)
{
	s_minX = minX;
	s_maxX = maxX;
	s_minY = minY;
	s_maxY = maxY;
	PadSaturn_Clamp();
	s_positionChanged = true;
}

/* The sound of moving the focus: its own, as the game's effects depend on
 * the music loaded (the menus' has none that fits). */
static void PadSaturn_Blip(void)
{
	if (g_gameConfig.sounds != 0) DSP_Saturn_Blip(DSP_BLIP_FOCUS);
}

static uint32 s_secondNoteFrame = 0;    /* when the use sound's second note is due, or 0 */

/* The sound of pressing A on what is focused: a low note, then the focus
 * note (played by PadSaturn_Tick()). */
static void PadSaturn_UseSound(void)
{
	if (g_gameConfig.sounds == 0) return;
	DSP_Saturn_Blip(DSP_BLIP_USE);
	s_secondNoteFrame = saturn_timer_frames() + 4;
}

static void PadSaturn_KeyTap(uint8 scancode)
{
	Input_EventHandler(scancode);
	Input_EventHandler(scancode | SCANCODE_RELEASED);
}

/* Queue the shortcut of side bar command button index (8..11), if shown.
 * The shortcut is the first letter of the command, so it depends on the
 * language; reading it from the widget keeps it right. */
static void PadSaturn_CommandButton(uint16 index)
{
	Widget *w;

	if (g_selectionType != SELECTIONTYPE_UNIT) return;
	w = GUI_Widget_Get_ByIndex(g_widgetLinkedListHead, index);
	if (w == NULL || w->flags.invisible || w->shortcut == 0) return;
	Input_HandleInput(w->shortcut);
}

/* Press the selected structure's Repair/Upgrade button (widget 4), if shown. */
static void PadSaturn_RepairUpgrade(void)
{
	Widget *w = GUI_Widget_Get_ByIndex(g_widgetLinkedListHead, 4);

	if (w == NULL || w->flags.invisible) return;
	w->shortcut = KEY_REPAIR_UPGRADE;
	Input_HandleInput(KEY_REPAIR_UPGRADE);
}

/* Saturn keyboard key number (PS/2 set 2) to PC XT scan code, or 0. */
static uint8 PadSaturn_ScanCode(uint8 key)
{
	static const uint8 table[][2] = {
		{0x76, 0x01}, {0x05, 0x3B}, {0x06, 0x3C}, {0x04, 0x3D}, {0x0C, 0x3E}, {0x03, 0x3F},
		{0x0B, 0x40}, {0x83, 0x41}, {0x0A, 0x42}, {0x01, 0x43}, {0x09, 0x44}, {0x78, 0x57},
		{0x07, 0x58}, {0x0E, 0x29}, {0x16, 0x02}, {0x1E, 0x03}, {0x26, 0x04}, {0x25, 0x05},
		{0x2E, 0x06}, {0x36, 0x07}, {0x3D, 0x08}, {0x3E, 0x09}, {0x46, 0x0A}, {0x45, 0x0B},
		{0x4E, 0x0C}, {0x55, 0x0D}, {0x66, 0x0E}, {0x0D, 0x0F}, {0x15, 0x10}, {0x1D, 0x11},
		{0x24, 0x12}, {0x2D, 0x13}, {0x2C, 0x14}, {0x35, 0x15}, {0x3C, 0x16}, {0x43, 0x17},
		{0x44, 0x18}, {0x4D, 0x19}, {0x54, 0x1A}, {0x5B, 0x1B}, {0x5A, 0x1C}, {0x14, 0x1D},
		{0x1C, 0x1E}, {0x1B, 0x1F}, {0x23, 0x20}, {0x2B, 0x21}, {0x34, 0x22}, {0x33, 0x23},
		{0x3B, 0x24}, {0x42, 0x25}, {0x4B, 0x26}, {0x4C, 0x27}, {0x52, 0x28}, {0x12, 0x2A},
		{0x5D, 0x2B}, {0x1A, 0x2C}, {0x22, 0x2D}, {0x21, 0x2E}, {0x2A, 0x2F}, {0x32, 0x30},
		{0x31, 0x31}, {0x3A, 0x32}, {0x41, 0x33}, {0x49, 0x34}, {0x4A, 0x35}, {0x59, 0x36},
		{0x11, 0x38}, {0x29, 0x39}, {0x58, 0x3A}
	};
	size_t i;

	for (i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
		if (table[i][0] == key) return table[i][1];
	}
	return 0;
}

/* Which controller to use, from what is on the two ports; sets which port
 * holds it (the keyboard, for a keyboard and a mouse). */
static Controller PadSaturn_Detect(const SmpcDevice d[2], int *port)
{
	int i;

	if ((d[0].kind == SMPC_KEYBOARD && d[1].kind == SMPC_MOUSE) || (d[0].kind == SMPC_MOUSE && d[1].kind == SMPC_KEYBOARD)) {
		*port = (d[0].kind == SMPC_KEYBOARD) ? 0 : 1;
		return CONTROLLER_KEYBOARD_MOUSE;
	}
	for (i = 0; i < 2; i++) {
		*port = i;
		switch (d[i].kind) {
			case SMPC_PAD: case SMPC_OTHER: return CONTROLLER_PAD;
			case SMPC_ANALOG: return CONTROLLER_3D;
			case SMPC_KEYBOARD: return CONTROLLER_KEYBOARD;
			case SMPC_MOUSE: return CONTROLLER_KEYBOARD_MOUSE;     /* without keys */
			default: break;
		}
	}
	return CONTROLLER_NONE;
}

/* Keyboard keys go to the game as on DOS, but for the ones standing in for
 * pad buttons when there is no mouse. */
static void PadSaturn_Keys(bool mouse)
{
	uint8 key;
	int make;

	while (smpc_key_event(&key, &make)) {
		uint8 scancode;

		if (!mouse && key == KEY_SPACE) {
			s_keySpace = make != 0;
			continue;
		}
		if (!mouse && key == KEY_TAB) {
			s_keyTab = make != 0;
			continue;
		}
		scancode = PadSaturn_ScanCode(key);
		if (scancode != 0) Input_EventHandler(make ? scancode : (uint8)(scancode | SCANCODE_RELEASED));
	}
}

/* The keyboard and mouse: as on DOS. */
static void PadSaturn_KeyboardMouse(const SmpcDevice d[2])
{
	int dx, dy, i;
	bool left = false, right = false;

	PadSaturn_Keys(true);

	smpc_mouse_motion(&dx, &dy);
	if (dx != 0 || dy != 0) {
		s_x += dx;
		s_y -= dy;
		PadSaturn_Clamp();
		s_positionChanged = true;
	}
	for (i = 0; i < 2; i++) {
		if (d[i].kind != SMPC_MOUSE) continue;
		left = (d[i].buttons & 1) != 0;
		right = (d[i].buttons & 2) != 0;
	}
	if (s_positionChanged || left != s_leftButton || right != s_rightButton) {
		s_positionChanged = false;
		s_leftButton = left;
		s_rightButton = right;
		Mouse_EventHandler((uint16)s_x, (uint16)s_y, left, right);
	}
}

/* A D-pad direction as Map_MoveDirection() takes it (0 up, then clockwise
 * in eighths), or NO_DIRECTION. */
static uint16 PadSaturn_Direction(uint16 pad)
{
	static const uint16 directions[16] = {
		/* by bits right, left, down, up */
		NO_DIRECTION, 0, 4, NO_DIRECTION, 6, 7, 5, NO_DIRECTION,
		2, 1, 3, NO_DIRECTION, NO_DIRECTION, NO_DIRECTION, NO_DIRECTION, NO_DIRECTION
	};
	int bits = ((pad & PAD_UP) ? 1 : 0) | ((pad & PAD_DOWN) ? 2 : 0) | ((pad & PAD_LEFT) ? 4 : 0) | ((pad & PAD_RIGHT) ? 8 : 0);
	return directions[bits];
}

/* The pad-like controllers: standard pad, 3D Controller, keyboard alone. */
static void PadSaturn_Buttons(const SmpcDevice *d, Controller controller)
{
	uint16 pad = d->buttons;
	uint16 pressed, released;

	if (controller == CONTROLLER_KEYBOARD) {
		/* the arrows come as the pad bits; Space and Tab stand for A and C;
		 * everything else goes to the game as keys */
		PadSaturn_Keys(false);
		pad &= PAD_DIRECTIONS;
		if (s_keySpace) pad |= PAD_A;
		if (s_keyTab) pad |= PAD_C;
	}
	/* a new screen ignores the A that was down when it came (the press
	 * that skipped the one before) until A is released */
	if (s_blockA) {
		if (pad & PAD_A) pad &= ~PAD_A;
		else s_blockA = false;
	}
	pressed = pad & ~s_previous;
	released = s_previous & ~pad;
	s_previous = pad;

	if (controller != CONTROLLER_KEYBOARD && (pad & PAD_RESET) == PAD_RESET) BIOS_EXECDMP();

	if (pressed & PAD_A) {
		s_pressA = true;
		if (s_useSound && saturn_timer_frames() - s_handledFrame <= FOCUS_STALE) PadSaturn_UseSound();
	}

	/* the D-pad: requests for the focus and camera, repeating when held;
	 * with C held the camera scrolls a tile every frame */
	if (pad & PAD_DIRECTIONS) {
		bool repeat;

		if (pressed & PAD_DIRECTIONS) s_repeatFrames = 0;
		if (s_cameraActive && controller != CONTROLLER_3D) {
			repeat = s_repeatFrames == 0 || (pad & PAD_C) != 0 ||
				(s_repeatFrames >= SCROLL_FIRST && (s_repeatFrames - SCROLL_FIRST) % SCROLL_NEXT == 0);
		} else {
			repeat = s_repeatFrames == 0 || (s_repeatFrames >= REPEAT_FIRST && (s_repeatFrames - REPEAT_FIRST) % REPEAT_NEXT == 0);
		}
		s_repeatFrames++;
		if (controller == CONTROLLER_3D) s_stickLast = false;

		if (repeat) {
			if (s_cameraActive && controller != CONTROLLER_3D) s_scroll = PadSaturn_Direction(pad);
			else s_navigate = PadSaturn_Direction(pad);
		}
	}

	/* the 3D Controller's stick: the camera */
	if (controller == CONTROLLER_3D) {
		int sx = (int)d->analog[0] - 128, sy = (int)d->analog[1] - 128;

		if (abs(sx) < STICK_DEAD) sx = 0;
		if (abs(sy) < STICK_DEAD) sy = 0;
		if (sx != 0 || sy != 0) {
			s_stickLast = true;
			s_stickX += sx;
			s_stickY += sy;
		}
	}

	/* A: a click, or on a list the key that opens the selected line */
	if (s_keyA != 0 && saturn_timer_frames() - s_handledFrame <= FOCUS_STALE) {
		if (pressed & PAD_A) Input_HandleInput(s_keyA);
		pad &= ~PAD_A;
		pressed &= ~PAD_A;
		released &= ~PAD_A;
	}
	if (s_positionChanged || ((pressed | released) & PAD_A) != 0) {
		s_positionChanged = false;
		Mouse_EventHandler((uint16)s_x, (uint16)s_y, (pad & PAD_A) != 0, false);
	}

	if (controller == CONTROLLER_KEYBOARD) {
		/* Tab switches the camera, as a C tap does */
		if (pressed & PAD_C) s_toggleCamera = true;
		return;
	}

	if (pressed & PAD_B) PadSaturn_KeyTap(SCANCODE_ESC);
	if (pressed & PAD_START) PadSaturn_KeyTap((pad & PAD_C) ? SCANCODE_F1 : SCANCODE_F2);

	if (pressed & PAD_L) Input_EventHandler(SCANCODE_LSHIFT);
	if (released & PAD_L) Input_EventHandler(SCANCODE_LSHIFT | SCANCODE_RELEASED);

	/* taps: a short press of L or C with no other button in between */
	if (pressed & PAD_L) {
		s_lTapFrames = 0;
	} else if (s_lTapFrames >= 0 && (pad & PAD_L)) {
		if (++s_lTapFrames > TAP_FRAMES || (pad & ~(PAD_L | PAD_C)) != 0) s_lTapFrames = -1;
	}
	if ((released & PAD_L) && s_lTapFrames >= 0) {
		s_cycle = (pad & PAD_C) ? CYCLE_STRUCTURE : CYCLE_UNIT;
		s_lTapFrames = -1;
		s_cTapFrames = -1;      /* C+L isn't a C tap */
	}

	if (controller == CONTROLLER_PAD) {
		/* a tap: C alone (held with the D-pad it scrolls fast instead) */
		if (pressed & PAD_C) {
			s_cTapFrames = ((pad & ~PAD_C) == 0) ? 0 : -1;
		} else if (s_cTapFrames >= 0 && (pad & PAD_C)) {
			if (++s_cTapFrames > TAP_FRAMES || (pad & ~PAD_C) != 0) s_cTapFrames = -1;
		}
		if ((released & PAD_C) && s_cTapFrames >= 0) {
			s_toggleCamera = true;
			s_cTapFrames = -1;
		}
	}

	if (g_selectionType == SELECTIONTYPE_STRUCTURE) {
		if (pressed & PAD_X) PadSaturn_KeyTap(SCANCODE_F3);
		if (pressed & PAD_Y) PadSaturn_RepairUpgrade();
	} else {
		if (pressed & PAD_X) PadSaturn_CommandButton(8);
		if (pressed & PAD_Y) PadSaturn_CommandButton(9);
		if (pressed & PAD_Z) PadSaturn_CommandButton(10);
		if (pressed & PAD_R) PadSaturn_CommandButton(11);
	}
}

/* Brackets round the corners of a rectangle, with a dark edge outside. */
static void PadSaturn_Brackets(int x, int y, int w, int h, int light, int dark)
{
	int arm = (w < h ? w : h) / 3;
	int x1, y1;
	int i, t;

	/* just outside the rectangle, clear of what's written in it */
	x -= 3;
	y += OVERLAY_TOP - 3;
	x1 = x + w + 5;
	y1 = y + h + 5;

	if (arm < 3) arm = 3;
	if (arm > 7) arm = 7;
	for (i = -1; i <= arm; i++) {
		for (t = -1; t <= 1; t++) {
			int c = (t == -1 || i == -1) ? dark : light;
			/* horizontal arms, then vertical ones, at each corner */
			vdp2_overlay_pixel(x + i, y + t, c);  vdp2_overlay_pixel(x1 - i, y + t, c);
			vdp2_overlay_pixel(x + i, y1 - t, c); vdp2_overlay_pixel(x1 - i, y1 - t, c);
			vdp2_overlay_pixel(x + t, y + i, c);  vdp2_overlay_pixel(x1 - t, y + i, c);
			vdp2_overlay_pixel(x + t, y1 - i, c); vdp2_overlay_pixel(x1 - t, y1 - i, c);
		}
	}
}

/* Put the reticle round a rectangle (none for a width of 0); it glides
 * there in PadSaturn_ReticleTick(). */
static void PadSaturn_SetReticle(int x, int y, int w, int h)
{
	s_reticle.x = x;
	s_reticle.y = y;
	s_reticle.w = w;
	s_reticle.h = h;
}

/* A step of a value towards its target: half the way, at least 1. */
static int PadSaturn_Step(int from, int to)
{
	int d = to - from;
	if (d == 0) return to;
	return from + ((d > 0) ? (d + 1) / 2 : (d - 1) / 2);
}

/* Once a frame: move the reticle drawn on the overlay towards its place. */
static void PadSaturn_ReticleTick(void)
{
	Rect next;

	if (s_reticleDrawn.w == s_reticle.w && s_reticleDrawn.h == s_reticle.h &&
			s_reticleDrawn.x == s_reticle.x && s_reticleDrawn.y == s_reticle.y) return;

	if (s_reticle.w == 0 || s_reticleDrawn.w == 0) {
		next = s_reticle;       /* appears or goes at once */
	} else {
		next.x = PadSaturn_Step(s_reticleDrawn.x, s_reticle.x);
		next.y = PadSaturn_Step(s_reticleDrawn.y, s_reticle.y);
		next.w = PadSaturn_Step(s_reticleDrawn.w, s_reticle.w);
		next.h = PadSaturn_Step(s_reticleDrawn.h, s_reticle.h);
	}
	if (s_reticleDrawn.w > 0) PadSaturn_Brackets(s_reticleDrawn.x, s_reticleDrawn.y, s_reticleDrawn.w, s_reticleDrawn.h, VDP2_OVERLAY_CLEAR, VDP2_OVERLAY_CLEAR);
	if (next.w > 0) PadSaturn_Brackets(next.x, next.y, next.w, next.h, VDP2_OVERLAY_LIGHT, VDP2_OVERLAY_DARK);
	s_reticleDrawn = next;
}

/* Draw the mouse pointer or not, redrawing it if it is on show. */
static void PadSaturn_SetPointer(bool visible)
{
	if (visible == s_pointerVisible) return;
	if (g_mouseHiddenDepth != 0) {
		s_pointerVisible = visible;     /* drawn or not when next shown */
		return;
	}
	if (g_mouseLock != 0) return;       /* the engine is at it: next frame */
	g_mouseLock++;
	GUI_Mouse_Hide();
	s_pointerVisible = visible;
	GUI_Mouse_Show();
	g_mouseLock--;
}

bool PadSaturn_PointerVisible(void)
{
	return s_pointerVisible;
}

void PadSaturn_Tick(void)
{
	SmpcDevice d[2];
	int port = 0;
	Controller controller;

	smpc_devices(d);
	controller = PadSaturn_Detect(d, &port);
	if (controller == CONTROLLER_NONE) {
		/* not at once: the first frames after power on have no data yet */
		if (++s_missingFrames < MISSING_FRAMES) return;
	} else {
		s_missingFrames = 0;
	}
	if (controller != s_controller) {
		s_controller = controller;
		s_previous = 0;
		s_keySpace = s_keyTab = false;
	}

	switch (controller) {
		case CONTROLLER_NONE: break;
		case CONTROLLER_KEYBOARD_MOUSE: PadSaturn_KeyboardMouse(d); break;
		default: PadSaturn_Buttons(&d[port], controller); break;
	}

	if (s_secondNoteFrame != 0 && saturn_timer_frames() >= s_secondNoteFrame) {
		s_secondNoteFrame = 0;
		DSP_Saturn_Blip(DSP_BLIP_FOCUS);
	}

	/* the pointer only with a mouse; the reticle only while the focus or
	 * the camera is being looked after */
	PadSaturn_SetPointer(controller == CONTROLLER_KEYBOARD_MOUSE);
	if (controller == CONTROLLER_KEYBOARD_MOUSE || saturn_timer_frames() - s_handledFrame > FOCUS_STALE) {
		PadSaturn_SetReticle(0, 0, 0, 0);
	}
	PadSaturn_ReticleTick();
}

bool PadSaturn_Connected(void)
{
	return s_controller != CONTROLLER_NONE;
}

void PadSaturn_ShowControllerMessage(bool show)
{
	s_controllerMessage = show;
}

/* Where a widget is on the screen (as GUI_Widget_HandleEvents() works it out). */
static void PadSaturn_WidgetPosition(const Widget *w, int *x, int *y)
{
	*x = w->offsetX;
	if (w->offsetX < 0) *x += g_widgetProperties[w->parentID].width << 3;
	*x += g_widgetProperties[w->parentID].xBase << 3;
	*y = w->offsetY;
	if (w->offsetY < 0) *y += g_widgetProperties[w->parentID].height;
	*y += g_widgetProperties[w->parentID].yBase;
}

/* Whether the focus can go to a widget: shown, big enough to be a button,
 * and not the map view, minimap or scroll edges. */
static bool PadSaturn_Focusable(const Widget *w)
{
	if (w->flags.invisible || w->width < 8 || w->height < 8) return false;
	if (w->clickProc == &GUI_Widget_Viewport_Click) return false;
	if (w->clickProc == &GUI_Widget_Scrollbar_Click) return false;     /* its arrows are */
	return true;
}

static Widget *s_focus = NULL;          /* the focused widget */

/* Move the focus in a direction, or to the button nearest the cursor for
 * NO_DIRECTION: returns the widget focused then (the same one if there is
 * none that way), or NULL if the screen has nothing to focus. */
static Widget *PadSaturn_MoveFocus(Widget *list, uint16 direction)
{
	static const int dirX[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
	static const int dirY[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
	Widget *w, *best = NULL;
	long bestScore = 0;
	bool any = false;
	int fromX = s_x, fromY = s_y;

	/* a move goes from the focused widget, wherever the cursor is */
	if (direction != NO_DIRECTION && s_focus != NULL) {
		PadSaturn_WidgetPosition(s_focus, &fromX, &fromY);
		fromX += s_focus->width / 2;
		fromY += s_focus->height / 2;
	}

	for (w = list; w != NULL; w = GUI_Widget_GetNext(w)) {
		int x, y, dx, dy;
		long score;

		if (!PadSaturn_Focusable(w)) continue;
		any = true;
		if (w == s_focus && direction != NO_DIRECTION) continue;
		PadSaturn_WidgetPosition(w, &x, &y);
		dx = x + w->width / 2 - fromX;
		dy = y + w->height / 2 - fromY;

		if (direction == NO_DIRECTION) {
			score = (long)dx * dx + (long)dy * dy;
		} else {
			/* ahead in the direction; nearer and better lined up is better */
			long along = (long)dx * dirX[direction] + (long)dy * dirY[direction];
			long across = labs((long)dx * dirY[direction] - (long)dy * dirX[direction]);
			if (along <= 2) continue;
			score = along + 3 * across;
		}
		if (best == NULL || score < bestScore) {
			best = w;
			bestScore = score;
		}
	}
	if (!any) return NULL;
	if (best == NULL) return s_focus;       /* nothing that way */
	{
		int x, y;
		PadSaturn_WidgetPosition(best, &x, &y);
		PadSaturn_SetPosition((uint16)(x + best->width / 2), (uint16)(y + best->height / 2));
	}
	return best;
}

/* A number for the buttons of a list, where and how big they are: another
 * number, another screen. */
static uint32 PadSaturn_Layout(Widget *list)
{
	uint32 layout = 0;
	Widget *w;

	for (w = list; w != NULL; w = GUI_Widget_GetNext(w)) {
		int x, y;
		if (!PadSaturn_Focusable(w)) continue;
		PadSaturn_WidgetPosition(w, &x, &y);
		layout = layout * 31 + (uint32)(w->index * 1009 + x * 97 + y * 13 + w->width * 7 + w->height);
	}
	return layout;
}

/* Whether w is a widget of list the focus can go to. */
static bool PadSaturn_InList(Widget *list, Widget *w)
{
	Widget *i;

	if (w == NULL) return false;
	for (i = list; i != NULL; i = GUI_Widget_GetNext(i)) {
		if (i == w) return PadSaturn_Focusable(w);
	}
	return false;
}

/* The 3D Controller's stick, off the camera: scrolls the screen's list, a
 * line each time enough travel has been summed up. */
static void PadSaturn_StickScroll(Widget *list)
{
	Widget *w;
	int sy;
	uint32 sr;

	sr = cpu_interrupts_disable();
	sy = s_stickY;
	s_stickX = 0;
	s_stickY = 0;
	cpu_interrupts_restore(sr);
	s_stickLast = false;
	if (sy > -STICK_STEP && sy < STICK_STEP) {
		/* keep what isn't a line yet */
		sr = cpu_interrupts_disable();
		s_stickY += sy;
		cpu_interrupts_restore(sr);
		return;
	}

	for (w = list; w != NULL; w = GUI_Widget_GetNext(w)) {
		if (w->flags.invisible || w->clickProc != &GUI_Widget_Scrollbar_Click) continue;
		if (sy < 0) GUI_Widget_Scrollbar_ArrowUp_Click(w);
		else GUI_Widget_Scrollbar_ArrowDown_Click(w);
		break;
	}
	sr = cpu_interrupts_disable();
	s_stickY += (sy < 0) ? sy + STICK_STEP : sy - STICK_STEP;
	cpu_interrupts_restore(sr);
}

/* Put the reticle on the focused widget. */
static void PadSaturn_ReticleOnFocus(void)
{
	Widget *w = s_focus;
	int x, y;

	/* list lines (the Mentat's subjects) change colour instead */
	if (w == NULL || w->clickProc == &GUI_Mentat_List_Click) {
		PadSaturn_SetReticle(0, 0, 0, 0);
		return;
	}
	PadSaturn_WidgetPosition(w, &x, &y);
	PadSaturn_SetReticle(x, y, w->width, w->height);
}

/* Scroll the camera from the D-pad or the stick, the cursor in the middle. */
static void PadSaturn_Camera(void)
{
	static const uint16 stickDirections[3][3] = { { 7, 0, 1 }, { 6, NO_DIRECTION, 2 }, { 5, 4, 3 } };
	uint32 sr;
	uint16 direction;
	int sx, sy, x, y;

	sr = cpu_interrupts_disable();
	direction = s_scroll;
	s_scroll = NO_DIRECTION;
	sx = s_stickX;
	sy = s_stickY;
	cpu_interrupts_restore(sr);

	if (direction != NO_DIRECTION) Map_MoveDirection(direction);

	/* the stick: a tile each time enough travel has been summed up */
	x = (sx >= STICK_STEP) ? 1 : (sx <= -STICK_STEP) ? -1 : 0;
	y = (sy >= STICK_STEP) ? 1 : (sy <= -STICK_STEP) ? -1 : 0;
	if (x != 0 || y != 0) {
		Map_MoveDirection(stickDirections[y + 1][x + 1]);
		sr = cpu_interrupts_disable();
		s_stickX -= x * STICK_STEP;
		s_stickY -= y * STICK_STEP;
		cpu_interrupts_restore(sr);
	}

	if (s_x != CAMERA_X || s_y != CAMERA_Y) PadSaturn_SetPosition(CAMERA_X, CAMERA_Y);
	PadSaturn_SetReticle(CAMERA_X - 8, CAMERA_Y - 8, 16, 16);
}

/* A new screen: forget what was pressed for the one before. */
static void PadSaturn_NewScreen(void)
{
	/* let go of the button now, before forgetting: a release coming later
	 * would finish a click on the new screen's focused button */
	if (s_previous & PAD_A) {
		Mouse_EventHandler((uint16)s_x, (uint16)s_y, false, false);
		s_previous &= ~PAD_A;
	}
	Input_History_Clear();
	s_blockA = true;
	s_pressA = false;
}

/* Pause, with a message, until a controller is connected. */
static void PadSaturn_WaitForController(void)
{
	static bool waiting = false;
	bool gameTimer;

	if (waiting || !s_controllerMessage) return;
	waiting = true;
	gameTimer = Timer_SetTimer(TIMER_GAME, false);
	g_modalMessageUntil = &PadSaturn_Connected;
	GUI_DisplayModalMessage("No controller is connected.\rPlease connect one to controller port 1 or 2.", 0xFFFF);
	g_modalMessageUntil = NULL;
	Timer_SetTimer(TIMER_GAME, gameTimer);
	s_previous = 0xFFFF;        /* no press from what was held when connecting */
	waiting = false;
}

void PadSaturn_HandleEvents(Widget *list)
{
	static Widget *lastList = NULL;
	static uint32 lastLayout = 0;
	bool mission, targeting, camera, fresh;
	uint32 layout;
	uint16 direction;
	uint32 sr;

	if (s_controller == CONTROLLER_NONE) PadSaturn_WaitForController();
	s_handledFrame = saturn_timer_frames();
	s_keyA = 0;
	if (s_controller == CONTROLLER_KEYBOARD_MOUSE || s_controller == CONTROLLER_NONE) {
		s_focusActive = false;
		s_cameraActive = false;
		return;
	}

	targeting = g_selectionType == SELECTIONTYPE_TARGET || g_selectionType == SELECTIONTYPE_PLACE;
	mission = list != NULL && list == g_widgetLinkedListHead &&
		(targeting || g_selectionType == SELECTIONTYPE_UNIT || g_selectionType == SELECTIONTYPE_STRUCTURE);

	/* a new screen: another list, or other buttons in the same one (windows
	 * reuse theirs); not the mission's, whose side bar changes as you go */
	layout = PadSaturn_Layout(list);
	fresh = list != lastList || (list != g_widgetLinkedListHead && layout != lastLayout);
	lastLayout = layout;

	sr = cpu_interrupts_disable();
	if (s_toggleCamera && mission && s_controller != CONTROLLER_3D) s_camera = !s_camera;
	s_toggleCamera = false;
	direction = s_navigate;
	s_navigate = NO_DIRECTION;
	cpu_interrupts_restore(sr);

	/* the camera: targeting or placing, chosen with C, or the stick used last */
	camera = mission && (targeting || (s_controller == CONTROLLER_3D ? s_stickLast : s_camera));

	if (camera) {
		s_cameraActive = true;
		s_focusActive = false;
		s_keyA = 0;
		s_useSound = false;
		PadSaturn_Camera();
		lastList = list;
		return;
	}

	if (s_cameraActive) {
		/* back from the camera: focus the nearest button */
		s_cameraActive = false;
		s_focus = NULL;
	}
	PadSaturn_StickScroll(list);

	/* a new screen, or the focused widget gone: the nearest one */
	if (fresh || !PadSaturn_InList(list, s_focus)) {
		if (fresh) PadSaturn_NewScreen();
		s_focus = PadSaturn_MoveFocus(list, NO_DIRECTION);
		lastList = list;
	}
	/* on a list (the Mentat's subjects), up and down move its own selection,
	 * scrolling at the ends, and A opens it: the keys the list takes */
	if (s_focus != NULL && s_focus->clickProc == &GUI_Mentat_List_Click && (direction == 0 || direction == 4)) {
		Input_HandleInput(direction == 0 ? KEY_ARROW_UP : KEY_ARROW_DOWN);
		PadSaturn_Blip();
		direction = NO_DIRECTION;
	}
	if (direction != NO_DIRECTION && s_focus != NULL) {
		Widget *next = PadSaturn_MoveFocus(list, direction);
		if (next != s_focus) PadSaturn_Blip();
		s_focus = next;
	}
	s_keyA = (s_focus != NULL && s_focus->clickProc == &GUI_Mentat_List_Click) ? KEY_RETURN : 0;
	/* the use sound, but not for scrolling */
	s_useSound = s_focus != NULL && s_focus->clickProc != &GUI_Widget_Scrollbar_ArrowUp_Click &&
		s_focus->clickProc != &GUI_Widget_Scrollbar_ArrowDown_Click;
	s_focusActive = s_focus != NULL;
	PadSaturn_ReticleOnFocus();
}

void PadSaturn_HandleMenu(uint16 left, uint16 top, uint16 right, uint16 lineHeight, uint16 lines, uint16 current)
{
	static uint16 lastTop = 0xFFFF;
	uint16 direction;
	int line;
	uint32 sr;

	if (s_controller == CONTROLLER_NONE) PadSaturn_WaitForController();
	s_handledFrame = saturn_timer_frames();
	s_keyA = 0;
	s_useSound = true;
	if (s_controller == CONTROLLER_KEYBOARD_MOUSE || s_controller == CONTROLLER_NONE || lines == 0) {
		s_focusActive = false;
		return;
	}

	sr = cpu_interrupts_disable();
	direction = s_navigate;
	s_navigate = NO_DIRECTION;
	s_toggleCamera = false;
	cpu_interrupts_restore(sr);
	s_focusActive = true;
	s_cameraActive = false;
	PadSaturn_SetReticle(0, 0, 0, 0);       /* the focused line changes colour */

	/* the line the cursor is on, or the menu's own choice when it's elsewhere */
	if (top != lastTop || s_x < left || s_x > right || s_y < top || s_y >= top + lines * lineHeight) {
		if (top != lastTop) PadSaturn_NewScreen();
		line = current;
		lastTop = top;
	} else {
		line = (s_y - top) / lineHeight;
	}
	if (direction == 0) line = (line == 0) ? lines - 1 : line - 1;
	if (direction == 4) line = (line == lines - 1) ? 0 : line + 1;
	if (direction == 0 || direction == 4) PadSaturn_Blip();

	if (s_x != (left + right) / 2 || s_y != top + line * lineHeight + lineHeight / 2) {
		PadSaturn_SetPosition((left + right) / 2, (uint16)(top + line * lineHeight + lineHeight / 2));
	}
}

int PadSaturn_PickRegion(const int16 *x, const int16 *y, const bool *usable, int count)
{
	static const int dirX[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
	static const int dirY[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
	static int focus = -1;
	uint16 direction;
	bool pressA;
	uint32 sr;
	int i;

	if (s_controller == CONTROLLER_NONE) PadSaturn_WaitForController();
	if (saturn_timer_frames() - s_handledFrame > FOCUS_STALE) {
		focus = -1;     /* a new map */
		PadSaturn_NewScreen();
	}
	s_handledFrame = saturn_timer_frames();
	s_keyA = 0;
	s_useSound = true;
	if (s_controller == CONTROLLER_KEYBOARD_MOUSE || s_controller == CONTROLLER_NONE) return -1;

	sr = cpu_interrupts_disable();
	direction = s_navigate;
	s_navigate = NO_DIRECTION;
	pressA = s_pressA;
	s_pressA = false;
	s_toggleCamera = false;
	cpu_interrupts_restore(sr);
	s_focusActive = true;
	s_cameraActive = false;

	if (focus < 0 || focus >= count || !usable[focus]) {
		for (focus = 0; focus < count && !usable[focus]; focus++) {}
		if (focus == count) {
			focus = -1;
			return -1;
		}
		pressA = false;         /* the press that opened the map */
	}

	if (direction != NO_DIRECTION) {
		/* the nearest region ahead, better lined up counting more */
		int best = -1;
		long bestScore = 0;

		for (i = 0; i < count; i++) {
			long dx = x[i] - x[focus], dy = y[i] - y[focus], along, across, score;
			if (i == focus || !usable[i]) continue;
			along = dx * dirX[direction] + dy * dirY[direction];
			across = labs(dx * dirY[direction] - dy * dirX[direction]);
			if (along <= 0) continue;
			score = along + 3 * across;
			if (best < 0 || score < bestScore) {
				best = i;
				bestScore = score;
			}
		}
		if (best >= 0) {
			focus = best;
			PadSaturn_Blip();
		}
	}

	PadSaturn_SetReticle(x[focus], y[focus], 16, 16);
	return pressA ? focus : -1;
}

/* The next of the player's objects after index (wrapping round), or NULL. */
static Object *PadSaturn_NextObject(bool structures, uint16 index)
{
	PoolFindStruct find;
	Object *first = NULL;

	find.houseID = g_playerHouseID;
	find.type = 0xFFFF;
	find.index = 0xFFFF;

	for (;;) {
		Object *o;

		if (structures) {
			Structure *st = Structure_Find(&find);
			if (st == NULL) break;
			if (st->o.type == STRUCTURE_SLAB_1x1 || st->o.type == STRUCTURE_SLAB_2x2 || st->o.type == STRUCTURE_WALL) continue;
			o = &st->o;
		} else {
			Unit *u = Unit_Find(&find);
			if (u == NULL) break;
			if (u->o.type == UNIT_CARRYALL || u->o.type == UNIT_SANDWORM || u->o.flags.s.isNotOnMap) continue;
			o = &u->o;
		}
		if (first == NULL) first = o;
		if (index == 0xFFFF || o->index > index) return o;
	}
	return first;
}

void PadSaturn_GameLoop(void)
{
	bool structures = (s_cycle == CYCLE_STRUCTURE);
	uint16 index = 0xFFFF;
	Object *o;
	uint16 packed;

	if (s_cycle == CYCLE_NONE) return;
	s_cycle = CYCLE_NONE;
	if (g_selectionType != SELECTIONTYPE_UNIT && g_selectionType != SELECTIONTYPE_STRUCTURE) return;

	/* continue from the selected object of that kind */
	if (!structures && g_unitSelected != NULL) index = g_unitSelected->o.index;
	if (structures && g_unitSelected == NULL) {
		Structure *st = Structure_Get_ByPackedTile(g_selectionPosition);
		if (st != NULL && st->o.houseID == g_playerHouseID) index = st->o.index;
	}

	o = PadSaturn_NextObject(structures, index);
	if (o == NULL) return;

	/* as clicking the picture of the selection does: centre, then select */
	packed = Tile_PackTile(o->position);
	if (structures) Unit_Select(NULL);
	Map_SetViewportPosition(packed);
	Map_SetSelection(packed);
}
