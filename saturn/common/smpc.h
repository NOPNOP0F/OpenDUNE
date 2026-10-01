/** @file saturn/common/smpc.h Controller input through the SMPC. */

#ifndef SATURN_SMPC_H
#define SATURN_SMPC_H

#include <stdint.h>

/* Read the standard pad on port 1 (PAD_* bits of saturn_hw.h, 1 = pressed).
 * Returns 0 if no pad is connected. Call during V-BLANK. */
extern uint16_t Smpc_PadRead(void);

/* Issue an SMPC command without parameters (SNDOFF, ...) and wait for it. */
extern void Smpc_Command(uint8_t command);

/* Read the real-time clock: year (2 bytes), weekday << 4 | month, day,
 * hours, minutes, seconds, in BCD (SMPC User's Manual, Table 3.9). */
extern void Smpc_ReadClock(uint8_t clock[7]);

/* Non-blocking alternative for use from the VBlank-in interrupt: collects
 * the result of the previous INTBACK, then issues the next one. */
extern void Smpc_VBlank(void);

/* Pad state collected by Smpc_VBlank() (one frame old): the buttons of the
 * device on port 1. */
extern uint16_t Smpc_PadState(void);

/* Kinds of device on a port, as collected by Smpc_VBlank(). */
enum {
	SMPC_NONE,
	SMPC_PAD,           /* standard pad, or a 3D Controller in digital mode */
	SMPC_ANALOG,        /* 3D Controller in analog mode */
	SMPC_MOUSE,         /* Shuttle Mouse */
	SMPC_KEYBOARD,
	SMPC_OTHER
};

typedef struct SmpcDevice {
	uint8_t kind;
	uint8_t id;             /* SMPC peripheral ID */
	uint16_t buttons;       /* PAD_* bits (keyboards set them too); mouse:
	                         * 1 left, 2 right, 4 middle, 8 start */
	uint8_t analog[4];      /* 3D Controller: stick X, Y (0-255, 128 centre),
	                         * right and left triggers */
} SmpcDevice;

/* The first device on ports 1 and 2. */
extern void Smpc_Devices(SmpcDevice devices[2]);

/* Mouse movement since the last call (x right, y up). */
extern void Smpc_MouseMotion(int *dx, int *dy);

/* Take the next keyboard key event; returns 0 if there is none. key is the
 * keyboard's key number (PS/2 set 2 codes), make 1 on press, 0 on release. */
extern int Smpc_KeyEvent(uint8_t *key, int *make);

#endif /* SATURN_SMPC_H */
