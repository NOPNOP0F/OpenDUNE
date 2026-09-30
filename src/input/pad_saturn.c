/** @file src/input/pad_saturn.c Sega Saturn control pad as mouse and keys.
 *
 * The scheme follows the Mega Drive version of the game, extended to the
 * Saturn pad's extra buttons:
 *   D-pad        move the cursor, slowly; with C faster, and fastest when
 *                both are held for about a second
 *   A            left mouse button (select, confirm, place; hold to drag)
 *   B            cancel (Esc)
 *   X Y Z R      the selected unit's command buttons 1-4 in the side bar;
 *                with a structure selected, X opens its menu (F3) and Y
 *                repairs or upgrades it
 *   L (hold)     Shift: with a command button, Ambush / Area Guard
 *   L (tap)      select your next unit and centre the view on it;
 *                C+L: your next structure
 *   Start        options (F2); C+Start: Mentat (F1)
 *   A+B+C+Start  leave for the BIOS screen
 *
 * Everything goes through the engine's own input handlers, from Video_Tick,
 * the way the other video drivers deliver mouse and keyboard events. */

#include <stddef.h>
#include <stdio.h>

#include "types.h"
#include "input.h"
#include "mouse.h"
#include "pad_saturn.h"
#include "../gfx.h"
#include "../gui/gui.h"
#include "../gui/widget.h"
#include "../house.h"
#include "../map.h"
#include "../opendune.h"
#include "../pool/pool.h"
#include "../pool/structure.h"
#include "../pool/unit.h"
#include "../structure.h"
#include "../tile.h"
#include "../unit.h"

#include "bios.h"
#include "saturn_hw.h"
#include "saturn_timer.h"
#include "smpc.h"

enum {
	SPEED_SLOW = 1,             /* pixels per frame */
	SPEED_MEDIUM = 3,
	SPEED_FAST = 6,
	FAST_AFTER_FRAMES = 60,
	TAP_FRAMES = 15             /* an L press this short is a tap, not Shift */
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

/* Dune II key code no key sends (between F12 and Scroll Lock), given to the
 * Repair/Upgrade button, which has no shortcut of its own */
enum { KEY_REPAIR_UPGRADE = 0x7C };

#define PAD_DIRECTIONS (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT)
#define PAD_RESET (PAD_A | PAD_B | PAD_C | PAD_START)

static int s_x = SCREEN_WIDTH / 2;
static int s_y = SCREEN_HEIGHT / 2;
static int s_minX = 0, s_maxX = SCREEN_WIDTH - 1;
static int s_minY = 0, s_maxY = SCREEN_HEIGHT - 1;
static bool s_positionChanged = true;
static uint16 s_previous = 0;
static int s_fastFrames = 0;
static int s_tapFrames = -1;            /* frames L has been held, -1: not a tap */
static enum { CYCLE_NONE, CYCLE_UNIT, CYCLE_STRUCTURE } s_cycle = CYCLE_NONE;

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

/* Press the selected structure's Repair/Upgrade button (widget 4), if shown. */
static void PadSaturn_RepairUpgrade(void)
{
	Widget *w = GUI_Widget_Get_ByIndex(g_widgetLinkedListHead, 4);

	if (w == NULL || w->flags.invisible) return;
	w->shortcut = KEY_REPAIR_UPGRADE;
	Input_HandleInput(KEY_REPAIR_UPGRADE);
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

	/* a short L press on its own is a tap: cycle, done in the game loop */
	if (pressed & PAD_L) {
		s_tapFrames = 0;
	} else if (s_tapFrames >= 0 && (pad & PAD_L)) {
		if (++s_tapFrames > TAP_FRAMES || (pressed & ~PAD_C) != 0) s_tapFrames = -1;
	}
	if ((released & PAD_L) && s_tapFrames >= 0) {
		s_cycle = (pad & PAD_C) ? CYCLE_STRUCTURE : CYCLE_UNIT;
		s_tapFrames = -1;
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
