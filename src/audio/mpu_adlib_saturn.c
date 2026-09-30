/** @file src/audio/mpu_adlib_saturn.c Sega Saturn music: the AdLib driver in place of the MPU.
 *
 * Implements the MPU_* interface of mt32mpu.h with Westwood's AdLib driver
 * (adl_driver.cpp) playing the game's .ADL files, the DOS AdLib/Sound Blaster
 * music, on the SCSP (saturn/common/opl_scsp.c). Music and sound effects are
 * tracks of the same file, as on DOS; the driver's own priorities decide
 * between sound effects. Music plays on channels 0-5, effects on 6-8.
 *
 * The driver and the software envelopes run from the SCSP's timer interrupt
 * (every millisecond), so the music keeps its tempo while the game is busy,
 * loading from the disc for example. The MPU_* functions mask interrupts
 * while they touch the driver. */

#include <string.h>

#include "types.h"
#include "driver.h"
#include "mt32mpu.h"
#include "adl_driver.h"
#include "../file.h"

#include "bios.h"
#include "opl_scsp.h"
#include "saturn_timer.h"
#include "scsp.h"

enum {
	HANDLES = 8,
	TICK_US = 1000000 / 72,     /* the driver runs 72 times a second */
	MUSIC_FIRST = 0,
	MUSIC_LAST = 5
};

typedef struct Handle {
	bool used;
	const uint8 *file;
	uint16 track;
	bool music;
} Handle;

static Handle s_handles[HANDLES];
static const uint8 *s_loaded = NULL;    /* the file the driver plays from */
static uint16 s_musicHandle = 0xFFFF;
static uint64_t s_nextTick = 0;

/* music fade: attenuation (TL units) now, target, and the step per tick */
static int32 s_fadeAtt = 0, s_fadeTarget = 0, s_fadeStep = 0;

static void MPU_Tick(void);

static uint32 MPU_FileSize(const uint8 *file)
{
	const Driver *drivers[2];
	uint32 size;
	int i;

	drivers[0] = g_driverMusic;
	drivers[1] = g_driverSound;
	for (i = 0; i < 2; i++) {
		if (drivers[i]->content == file && File_Exists_GetSize(drivers[i]->filename, &size)) return size;
	}
	return 0;
}

bool MPU_Init(void)
{
	scsp_init();
	if (!opl_scsp_init()) return false;
	ADL_Init(opl_scsp_write);
	memset(s_handles, 0, sizeof(s_handles));
	s_nextTick = saturn_timer_us();
	scsp_timer_start(MPU_Tick);
	return true;
}

void MPU_Uninit(void)
{
	uint32_t sr = cpu_interrupts_disable();
	ADL_Load(NULL, 0);
	s_loaded = NULL;
	opl_scsp_reset();
	cpu_interrupts_restore(sr);
}

uint16 MPU_GetDataSize(void)
{
	/* the MSData buffers are not used */
	return 4;
}

uint16 MPU_SetData(uint8 *file, uint16 index, void *msdata)
{
	uint16 i;
	VARIABLE_NOT_USED(msdata);

	if (file == NULL) return 0xFFFF;
	for (i = 0; i < HANDLES; i++) {
		if (s_handles[i].used) continue;
		s_handles[i].used = true;
		s_handles[i].file = file;
		s_handles[i].track = index;
		s_handles[i].music = (msdata == g_bufferMusic->buffer);
		return i;
	}
	return 0xFFFF;
}

void MPU_ClearData(uint16 index)
{
	uint16 i;

	if (index >= HANDLES) return;
	s_handles[index].used = false;
	if (index == s_musicHandle) s_musicHandle = 0xFFFF;

	/* the engine frees a file once all its handles are cleared */
	for (i = 0; i < HANDLES; i++) {
		if (s_handles[i].used && s_handles[i].file == s_loaded) return;
	}
	{
		uint32_t sr = cpu_interrupts_disable();
		ADL_Load(NULL, 0);
		s_loaded = NULL;
		cpu_interrupts_restore(sr);
	}
}

void MPU_Play(uint16 index)
{
	Handle *h;
	uint32_t size, sr;

	if (index >= HANDLES || !s_handles[index].used) return;
	h = &s_handles[index];
	size = (h->file != s_loaded) ? MPU_FileSize(h->file) : 0;

	sr = cpu_interrupts_disable();
	if (h->file != s_loaded) {
		if (!ADL_Load(h->file, size)) {
			s_loaded = NULL;
			cpu_interrupts_restore(sr);
			return;
		}
		s_loaded = h->file;
	}
	if (h->music) {
		s_musicHandle = index;
		s_fadeAtt = s_fadeTarget = s_fadeStep = 0;
		opl_scsp_set_attenuation(MUSIC_FIRST, MUSIC_LAST, 0);
	}
	ADL_Play(h->track, 0xFF);
	cpu_interrupts_restore(sr);
}

void MPU_Stop(uint16 index)
{
	/* sound effects end by themselves, or give way by priority */
	if (index >= HANDLES || !s_handles[index].used || !s_handles[index].music) return;
	if (s_loaded != NULL) {
		uint32_t sr = cpu_interrupts_disable();
		ADL_StopMusic();
		cpu_interrupts_restore(sr);
	}
}

uint16 MPU_IsPlaying(uint16 index)
{
	int channel;
	uint16 playing = 0;
	uint32_t sr;

	if (index >= HANDLES || !s_handles[index].used) return 0;
	if (!s_handles[index].music) return 0;
	sr = cpu_interrupts_disable();
	for (channel = MUSIC_FIRST; channel <= MUSIC_LAST; channel++) {
		if (ADL_IsChannelPlaying(channel)) playing = 1;
	}
	if (ADL_IsChannelPlaying(9)) playing = 1;
	cpu_interrupts_restore(sr);
	return playing;
}

void MPU_SetVolume(uint16 index, uint16 volume, uint16 time)
{
	int32 target, ticks;
	uint32_t sr;

	/* only the music volume is used: the fade out (volume 0 over time ms);
	 * sound effects keep the volume the driver gives them */
	if (index >= HANDLES || !s_handles[index].used || !s_handles[index].music) return;

	target = (volume == 0) ? 255 : 0;
	ticks = (int32)time * 72 / 1000;
	sr = cpu_interrupts_disable();
	s_fadeTarget = target << 8;
	if (ticks <= 0) {
		s_fadeAtt = s_fadeTarget;
		s_fadeStep = 0;
		opl_scsp_set_attenuation(MUSIC_FIRST, MUSIC_LAST, (uint8)target);
	} else {
		s_fadeStep = (s_fadeTarget - s_fadeAtt) / ticks;
	}
	cpu_interrupts_restore(sr);
}

/* Every millisecond, from the SCSP timer interrupt. */
static void MPU_Tick(void)
{
	uint64_t now = saturn_timer_us();
	int ticks = 0;

	/* evenly spaced driver ticks */
	while (now >= s_nextTick) {
		if (++ticks > 4) {
			s_nextTick = now + TICK_US;
			break;
		}
		s_nextTick += TICK_US;
		ADL_Callback();
		opl_scsp_flush();

		if (s_fadeStep != 0) {
			s_fadeAtt += s_fadeStep;
			if ((s_fadeStep > 0 && s_fadeAtt >= s_fadeTarget) || (s_fadeStep < 0 && s_fadeAtt <= s_fadeTarget)) {
				s_fadeAtt = s_fadeTarget;
				s_fadeStep = 0;
			}
			opl_scsp_set_attenuation(MUSIC_FIRST, MUSIC_LAST, (uint8)(s_fadeAtt >> 8));
		}
	}
	opl_scsp_update();
}

void MPU_Interrupt(void)
{
	/* the SCSP timer interrupt runs MPU_Tick() */
}

void MPU_StartThread(uint32 usec)
{
	VARIABLE_NOT_USED(usec);
}

void MPU_StopThread(void)
{
}
