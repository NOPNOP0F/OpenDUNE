/** @file src/input/pad_saturn.c Sega Saturn control pad as mouse and keys.
 *
 * The scheme follows the Mega Drive version of the game, extended to the
 * Saturn pad's extra buttons:
 *   D-pad        move the cursor, slowly; with C faster, and fastest when
 *                both are held for about a second
 *   A            left mouse button (select, confirm, place; hold to drag)
 *   B            cancel (Esc)
 *   X Y Z R      the selected unit's command buttons 1-4 in the side bar;
 *                with a structure selected, X opens its menu (F3)
 *   L (hold)     Shift: with a command button, Ambush / Area Guard
 *   Start        options (F2); C+Start: Mentat (F1)
 *   A+B+C+Start  leave for the BIOS screen
 *
 * Everything goes through the engine's own input handlers, from Video_Tick,
 * the way the other video drivers deliver mouse and keyboard events. */

#include <stddef.h>

#include "types.h"
#include "input.h"
#include "mouse.h"
#include "pad_saturn.h"
#include "../gfx.h"
#include "../gui/gui.h"
#include "../gui/widget.h"
#include "../opendune.h"

#include "bios.h"
#include "saturn_hw.h"
#include "saturn_timer.h"
#include "smpc.h"

enum {
	SPEED_SLOW = 1,             /* pixels per frame */
	SPEED_MEDIUM = 3,
	SPEED_FAST = 6,
	FAST_AFTER_FRAMES = 60
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

#define PAD_DIRECTIONS (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT)
#define PAD_RESET (PAD_A | PAD_B | PAD_C | PAD_START)

static int s_x = SCREEN_WIDTH / 2;
static int s_y = SCREEN_HEIGHT / 2;
static int s_minX = 0, s_maxX = SCREEN_WIDTH - 1;
static int s_minY = 0, s_maxY = SCREEN_HEIGHT - 1;
static bool s_positionChanged = true;
static uint16 s_previous = 0;
static int s_fastFrames = 0;

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

void PadSaturn_Tick(void)
{
	uint16 pad = smpc_pad_state();
	uint16 pressed = pad & ~s_previous;
	uint16 released = s_previous & ~pad;
	bool leftChanged = ((pad ^ s_previous) & PAD_A) != 0;

	s_previous = pad;

	if ((pad & PAD_RESET) == PAD_RESET) BIOS_EXECDMP();

	if (pad & PAD_DIRECTIONS) {
		int speed = SPEED_SLOW;

		if (pad & PAD_C) {
			speed = (s_fastFrames >= FAST_AFTER_FRAMES) ? SPEED_FAST : SPEED_MEDIUM;
			s_fastFrames++;
		} else {
			s_fastFrames = 0;
		}
		if (pad & PAD_LEFT)  s_x -= speed;
		if (pad & PAD_RIGHT) s_x += speed;
		if (pad & PAD_UP)    s_y -= speed;
		if (pad & PAD_DOWN)  s_y += speed;
		PadSaturn_Clamp();
		s_positionChanged = true;
	} else {
		s_fastFrames = 0;
	}

	if (s_positionChanged || leftChanged) {
		s_positionChanged = false;
		Mouse_EventHandler((uint16)s_x, (uint16)s_y, (pad & PAD_A) != 0, false);
	}

	if (pressed & PAD_B) PadSaturn_KeyTap(SCANCODE_ESC);
	if (pressed & PAD_START) PadSaturn_KeyTap((pad & PAD_C) ? SCANCODE_F1 : SCANCODE_F2);

	if (pressed & PAD_L) Input_EventHandler(SCANCODE_LSHIFT);
	if (released & PAD_L) Input_EventHandler(SCANCODE_LSHIFT | SCANCODE_RELEASED);

	if (g_selectionType == SELECTIONTYPE_STRUCTURE) {
		if (pressed & PAD_X) PadSaturn_KeyTap(SCANCODE_F3);
	} else {
		if (pressed & PAD_X) PadSaturn_CommandButton(8);
		if (pressed & PAD_Y) PadSaturn_CommandButton(9);
		if (pressed & PAD_Z) PadSaturn_CommandButton(10);
		if (pressed & PAD_R) PadSaturn_CommandButton(11);
	}
}
