/** @file saturn/common/smpc.c Controller input through the SMPC.
 *
 * Uses the INTBACK command with peripheral data only, 15-byte mode on both
 * ports (SMPC User's Manual, section 2.4, Figure 3.14 and Tables 3.10-3.20):
 * each port gives a status byte (0xF0 nothing, 0xF1 one device, or a
 * multitap and its number of connectors), then for each device an ID byte
 * (type in the high nibble, data size in the low one) and its data. */

#include "bios.h"
#include "saturn_hw.h"
#include "smpc.h"

enum { KEY_QUEUE = 16 };

static volatile uint16_t s_padState;
static volatile int s_pending;
static SmpcDevice s_devices[2];
static volatile int16_t s_mouseX, s_mouseY;   /* movement not taken yet */
static volatile uint8_t s_keys[KEY_QUEUE];    /* keyboard key numbers */
static volatile uint8_t s_keyMake[KEY_QUEUE];   /* 1: pressed, 0: released */
static volatile uint8_t s_keyHead, s_keyTail;

static void intback_issue(void)
{
	SMPC_SF = 1;
	SMPC_IREG(0) = 0x00;        /* no SMPC status, peripheral data only */
	SMPC_IREG(1) = 0x08;        /* PEN: return peripheral data, 15-byte mode */
	SMPC_IREG(2) = 0xF0;
	SMPC_COMREG = SMPC_CMD_INTBACK;
}

/* Decode one device's data into d, and take its mouse movement and key. */
static void device_decode(SmpcDevice *d, uint8_t id, const uint8_t *data, int size)
{
	d->id = id;
	d->buttons = 0;
	d->analog[0] = d->analog[1] = 128;
	d->analog[2] = d->analog[3] = 0;

	if (id == 0xE3 || (id >> 4) == 2) {
		/* mouse: buttons (1 = pressed), then X and Y with their signs */
		int dx, dy;
		d->kind = SMPC_MOUSE;
		if (size < 3) return;
		d->buttons = data[0] & 0x0F;
		dx = data[1] - ((data[0] & 0x10) ? 256 : 0);
		dy = data[2] - ((data[0] & 0x20) ? 256 : 0);
		if (data[0] & 0x40) dx = (dx < 0) ? -255 : 255;
		if (data[0] & 0x80) dy = (dy < 0) ? -255 : 255;
		s_mouseX = (int16_t)(s_mouseX + dx);
		s_mouseY = (int16_t)(s_mouseY + dy);
		return;
	}

	if (size >= 2) d->buttons = (uint16_t)~((data[0] << 8) | data[1]) & 0xFFF8;
	switch (id >> 4) {
		case 0:
			d->kind = SMPC_PAD;
			break;
		case 1:
			/* 3D Controller in analog mode: stick X, Y, right and left
			 * triggers (0,0 top left) */
			d->kind = SMPC_ANALOG;
			if (size >= 6) {
				d->analog[0] = data[2];
				d->analog[1] = data[3];
				d->analog[2] = data[4];
				d->analog[3] = data[5];
			}
			break;
		case 3:
			/* keyboard: pad-like first bytes, then Make/Break and the key */
			d->kind = SMPC_KEYBOARD;
			if (size >= 4 && (data[2] & 0x09) != 0) {
				uint8_t next = (uint8_t)((s_keyHead + 1) % KEY_QUEUE);
				if (next != s_keyTail) {
					s_keys[s_keyHead] = data[3];
					s_keyMake[s_keyHead] = (data[2] & 0x08) ? 1 : 0;
					s_keyHead = next;
				}
			}
			break;
		default:
			d->kind = SMPC_OTHER;
			break;
	}
}

static uint16_t intback_collect(void)
{
	int o = 0, port;

	for (port = 0; port < 2; port++) {
		SmpcDevice *d = &s_devices[port];
		uint8_t status, taps, tap;

		d->kind = SMPC_NONE;
		d->id = 0xFF;
		d->buttons = 0;
		if (o >= 32) continue;
		status = SMPC_OREG(o++);
		taps = status & 0x0F;
		for (tap = 0; tap < taps && o < 32; tap++) {
			uint8_t id = SMPC_OREG(o++);
			uint8_t data[16];
			int size = id & 0x0F, i;

			if (size == 0x0F && o < 32) size = SMPC_OREG(o++);
			if (size > 15) size = 15;
			for (i = 0; i < size && o < 32; i++) data[i] = SMPC_OREG(o++);
			if (id == 0xFF) continue;       /* empty multitap connector */
			/* the first device of the port (the only one without a multitap) */
			if (d->kind == SMPC_NONE) device_decode(d, id, data, i);
		}
	}

	/* stop collecting if the SMPC still holds data (more multitap devices) */
	if (SMPC_SR & SMPC_SR_PDE) SMPC_IREG(0) = 0x40;

	return s_devices[0].kind == SMPC_MOUSE ? 0 : s_devices[0].buttons;
}

uint16_t smpc_pad_read(void)
{
	while (SMPC_SF & 1) {}
	intback_issue();
	while (SMPC_SF & 1) {}
	return intback_collect();
}

void smpc_command(uint8_t command)
{
	/* keep the VBlank handler from issuing INTBACK in between */
	uint32_t sr = cpu_interrupts_disable();

	while (SMPC_SF & 1) {}
	SMPC_SF = 1;
	SMPC_COMREG = command;
	while (SMPC_SF & 1) {}
	s_pending = 0;              /* the output registers no longer hold pad data */

	cpu_interrupts_restore(sr);
}

void smpc_read_clock(uint8_t clock[7])
{
	uint32_t sr = cpu_interrupts_disable();
	int i;

	while (SMPC_SF & 1) {}
	SMPC_SF = 1;
	SMPC_IREG(0) = 0x01;        /* SMPC status (with the clock), no peripherals */
	SMPC_IREG(1) = 0x00;
	SMPC_IREG(2) = 0xF0;
	SMPC_COMREG = SMPC_CMD_INTBACK;
	while (SMPC_SF & 1) {}
	/* OREG0: status; OREG1-7: year (2 bytes), weekday/month, day, hours,
	 * minutes, seconds, in BCD */
	for (i = 0; i < 7; i++) clock[i] = SMPC_OREG(1 + i);
	s_pending = 0;

	cpu_interrupts_restore(sr);
}

void smpc_vblank(void)
{
	if (SMPC_SF & 1) return;    /* previous command still running */
	if (s_pending) s_padState = intback_collect();
	intback_issue();
	s_pending = 1;
}

uint16_t smpc_pad_state(void)
{
	return s_padState;
}

void smpc_devices(SmpcDevice devices[2])
{
	uint32_t sr = cpu_interrupts_disable();
	devices[0] = s_devices[0];
	devices[1] = s_devices[1];
	cpu_interrupts_restore(sr);
}

void smpc_mouse_motion(int *dx, int *dy)
{
	uint32_t sr = cpu_interrupts_disable();
	*dx = s_mouseX;
	*dy = s_mouseY;
	s_mouseX = s_mouseY = 0;
	cpu_interrupts_restore(sr);
}

int smpc_key_event(uint8_t *key, int *make)
{
	int got = 0;
	uint32_t sr = cpu_interrupts_disable();
	if (s_keyTail != s_keyHead) {
		*key = s_keys[s_keyTail];
		*make = s_keyMake[s_keyTail];
		s_keyTail = (uint8_t)((s_keyTail + 1) % KEY_QUEUE);
		got = 1;
	}
	cpu_interrupts_restore(sr);
	return got;
}
