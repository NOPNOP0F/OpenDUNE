/** @file src/input/pad_saturn.h Sega Saturn controllers as mouse and keys. */

#ifndef PAD_SATURN_H
#define PAD_SATURN_H

extern void PadSaturn_Init(void);

/* Carry out what the pad asked for that has to run in the game loop
 * (cycling through units and structures). */
extern void PadSaturn_GameLoop(void);
extern void PadSaturn_Tick(void);

struct Widget;

/* Move the focus or the camera; GUI_Widget_HandleEvents() calls it with the
 * widgets of the screen on show. */
extern void PadSaturn_HandleEvents(struct Widget *list);

/* The same for the text menus of GameLoop_HandleEvents() (main menu): the
 * focus moves between the lines, from left, top to right, of lineHeight. */
extern void PadSaturn_HandleMenu(uint16 left, uint16 top, uint16 right, uint16 lineHeight, uint16 lines, uint16 current);

/* Whether any controller is connected. */
extern bool PadSaturn_Connected(void);
extern void PadSaturn_SetPosition(uint16 x, uint16 y);
extern void PadSaturn_SetRegion(uint16 minX, uint16 maxX, uint16 minY, uint16 maxY);

#endif /* PAD_SATURN_H */
