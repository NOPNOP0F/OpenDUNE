/** @file saturn/common/smpc.c Controller input through the SMPC.
 *
 * Uses the INTBACK command with peripheral data only (SMPC User's Manual,
 * section 2.4 and Table 3.10). */

#include "saturn_hw.h"
#include "smpc.h"

uint16_t smpc_pad_read(void)
{
	uint16_t buttons = 0;

	while (SMPC_SF & 1) {}
	SMPC_SF = 1;
	SMPC_IREG(0) = 0x00;        /* no SMPC status, peripheral data only */
	SMPC_IREG(1) = 0x08;        /* PEN: return peripheral data, 15-byte mode */
	SMPC_IREG(2) = 0xF0;
	SMPC_COMREG = SMPC_CMD_INTBACK;
	while (SMPC_SF & 1) {}

	/* OREG0: port status (0xF1 = one device, direct), OREG1: ID (0x02 = pad) */
	if (SMPC_OREG(0) == 0xF1 && SMPC_OREG(1) == 0x02) {
		buttons = (uint16_t)~((SMPC_OREG(2) << 8) | SMPC_OREG(3)) & 0xFFF8;
	}

	/* stop collecting if the SMPC still holds data for other ports */
	if (SMPC_SR & SMPC_SR_PDE) SMPC_IREG(0) = 0x40;

	return buttons;
}
