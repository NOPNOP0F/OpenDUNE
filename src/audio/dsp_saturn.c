/** @file src/audio/dsp_saturn.c Sega Saturn implementation of the DSP.
 *
 * Voices play on one SCSP slot from sound RAM. Preloaded voices are moved
 * into sound RAM by DSP_Saturn_KeepVoc(); the engine then holds a small
 * descriptor instead of the VOC data and passes it to DSP_Play() like a VOC.
 * Any other VOC (loaded into a buffer when needed) is copied into a scratch
 * area of sound RAM and played from there. */

#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "../os/endian.h"
#include "../os/error.h"

#include "dsp.h"

#include "saturn_timer.h"
#include "scsp.h"

enum {
	VOICE_SLOT = 0,
	SCRATCH_SIZE = 32 * 1024,   /* g_readBuffer is at most 28000 bytes */
	SCRATCH_MAX = SCRATCH_SIZE - SCSP_TAIL,
	DESCRIPTOR_MAGIC = 0x534E4431   /* "SND1" */
};

/* What the engine holds for a voice that lives in sound RAM. */
typedef struct SaturnVoice {
	uint32 magic;
	int32 offset;
	uint32 samples;
	uint32 rate;
} SaturnVoice;

static bool s_ready = false;
static int32 s_scratch = -1;
static uint64_t s_endUs = 0;    /* time (saturn_timer_us()) the playing voice ends */
static int32 s_playing = -1;    /* sound RAM offset of the voice on the slot, playing or done */

/* Find the PCM data of a VOC: first block, type 1, 8-bit unsigned. */
static bool DSP_ParseVoc(const uint8 *data, const uint8 **pcm, uint32 *length, uint32 *rate)
{
	const uint8 *block;

	if (memcmp(data, "Creative Voice File", 19) != 0) return false;
	block = data + READ_LE_UINT16(data + 20);
	if (block[0] != 1) return false;

	*length = (READ_LE_UINT32(block) >> 8) - 2;
	*rate = 1000000 / (256 - block[4]);
	*pcm = block + 6;
	return true;
}

bool DSP_Init(void)
{
	scsp_init();
	s_scratch = scsp_alloc(SCRATCH_SIZE);
	s_ready = (s_scratch >= 0);
	return s_ready;
}

void DSP_Uninit(void)
{
	DSP_Stop();
	s_ready = false;
}

void DSP_Stop(void)
{
	if (!s_ready) return;
	scsp_stop(VOICE_SLOT);
	s_endUs = 0;
	s_playing = -1;
}

static void DSP_PlayFromSoundRam(int32 offset, uint32 samples, uint32 rate)
{
	scsp_play(VOICE_SLOT, offset, samples, rate, 255);
	s_playing = offset;
	s_endUs = saturn_timer_us() + (uint64_t)samples * 1000000 / rate + 1;
}

void DSP_Play(const uint8 *data)
{
	const SaturnVoice *voice = (const SaturnVoice *)data;
	const uint8 *pcm;
	uint32 length, rate;

	if (!s_ready) return;
	DSP_Stop();

	if (voice->magic == DESCRIPTOR_MAGIC) {
		DSP_PlayFromSoundRam(voice->offset, voice->samples, voice->rate);
		return;
	}

	if (!DSP_ParseVoc(data, &pcm, &length, &rate)) return;
	if (length > SCRATCH_MAX) length = SCRATCH_MAX;
	scsp_upload_u8(s_scratch, pcm, length);
	scsp_upload_tail(s_scratch + (int32)((length + 1) & ~1u));
	DSP_PlayFromSoundRam(s_scratch, length, rate);
}

uint8 DSP_GetStatus(void)
{
	return (s_endUs != 0 && saturn_timer_us() < s_endUs) ? 2 : 0;
}

void *DSP_Saturn_KeepVoc(void *voc, uint32 *size)
{
	SaturnVoice *voice;
	const uint8 *pcm;
	uint32 length, rate;
	int32 offset;

	if (!s_ready || !DSP_ParseVoc(voc, &pcm, &length, &rate)) {
		free(voc);
		return NULL;
	}

	voice = malloc(sizeof(SaturnVoice));
	offset = scsp_alloc(((length + 1) & ~1u) + SCSP_TAIL);
	if (voice == NULL || offset < 0) {
		/* sound RAM is full: the voice will be loaded when it is needed */
		if (offset >= 0) scsp_free(offset);
		free(voice);
		free(voc);
		return NULL;
	}

	scsp_upload_u8(offset, pcm, length);
	scsp_upload_tail(offset + (int32)((length + 1) & ~1u));
	free(voc);

	voice->magic = DESCRIPTOR_MAGIC;
	voice->offset = offset;
	voice->samples = length;
	voice->rate = rate;
	*size = sizeof(SaturnVoice);
	return voice;
}

void DSP_Saturn_FreeVoc(void *data)
{
	SaturnVoice *voice = data;
	if (voice == NULL) return;
	if (voice->magic == DESCRIPTOR_MAGIC) {
		/* the slot loops round the end of the last voice even after it has
		 * finished: let it go before the sound RAM is used for something else */
		if (voice->offset == s_playing) DSP_Stop();
		scsp_free(voice->offset);
	}
	free(voice);
}

bool DSP_Saturn_CanKeep(uint32 fileSize)
{
	/* the PCM data is shorter than the file */
	return s_ready && scsp_largest_free() >= fileSize + SCSP_TAIL;
}

bool DSP_Saturn_IsPlaying(const void *data)
{
	const SaturnVoice *voice = data;
	return voice != NULL && voice->magic == DESCRIPTOR_MAGIC && voice->offset == s_playing && DSP_GetStatus() != 0;
}
