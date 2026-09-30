/** @file saturn/common/smpc.h Controller input through the SMPC. */

#ifndef SATURN_SMPC_H
#define SATURN_SMPC_H

#include <stdint.h>

/* Read the standard pad on port 1 (PAD_* bits of saturn_hw.h, 1 = pressed).
 * Returns 0 if no pad is connected. Call during V-BLANK. */
extern uint16_t smpc_pad_read(void);

/* Issue an SMPC command without parameters (SNDOFF, ...) and wait for it. */
extern void smpc_command(uint8_t command);

/* Read the real-time clock: year (2 bytes), weekday << 4 | month, day,
 * hours, minutes, seconds, in BCD (SMPC User's Manual, Table 3.9). */
extern void smpc_read_clock(uint8_t clock[7]);

/* Non-blocking alternative for use from the VBlank-in interrupt: collects
 * the result of the previous INTBACK, then issues the next one. */
extern void smpc_vblank(void);

/* Pad state collected by smpc_vblank() (one frame old): the buttons of the
 * device on port 1. */
extern uint16_t smpc_pad_state(void);

/* Kinds of device on a port, as collected by smpc_vblank(). */
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
extern void smpc_devices(SmpcDevice devices[2]);

/* Mouse movement since the last call (x right, y up). */
extern void smpc_mouse_motion(int *dx, int *dy);

/* Take the next keyboard key event; returns 0 if there is none. key is the
 * keyboard's key number (PS/2 set 2 codes), make 1 on press, 0 on release. */
extern int smpc_key_event(uint8_t *key, int *make);

#endif /* SATURN_SMPC_H */
