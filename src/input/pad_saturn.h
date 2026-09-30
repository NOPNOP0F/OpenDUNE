/** @file src/input/pad_saturn.h Sega Saturn control pad as mouse and keys. */

#ifndef PAD_SATURN_H
#define PAD_SATURN_H

extern void PadSaturn_Init(void);
extern void PadSaturn_Tick(void);
extern void PadSaturn_SetPosition(uint16 x, uint16 y);
extern void PadSaturn_SetRegion(uint16 minX, uint16 maxX, uint16 minY, uint16 maxY);

#endif /* PAD_SATURN_H */
