/** @file saturn/common/crash.h Report SH-2 exceptions on the console. */

#ifndef SATURN_CRASH_H
#define SATURN_CRASH_H

#include <stdint.h>

/* Point the illegal-instruction and address-error vectors at crash_report. */
extern void crash_install(void);

extern void crash_report(uint32_t vector, uint32_t pc, uint32_t sr, uint32_t sp);

#endif /* SATURN_CRASH_H */
