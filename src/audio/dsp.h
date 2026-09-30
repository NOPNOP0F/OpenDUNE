/** @file src/audio/dsp.h DSP definitions. */

#ifndef DSP_H
#define DSP_H

extern void DSP_Play(const uint8 *data);
extern void DSP_Stop(void);
extern uint8 DSP_GetStatus(void);
extern bool DSP_Init(void);
extern void DSP_Uninit(void);

#if defined(SATURN)
/* Move a preloaded VOC (malloc'd, freed here) into sound RAM; returns what
 * to keep instead, and its size, or NULL if it has to be loaded when needed. */
extern void *DSP_Saturn_KeepVoc(void *voc, uint32 *size);
extern void DSP_Saturn_FreeVoc(void *data);
/* Whether a VOC file of this size can be kept in sound RAM now. */
extern bool DSP_Saturn_CanKeep(uint32 fileSize);
/* A short blip, for moving the focus with a pad; plays alongside voices,
 * music and sound effects. */
extern void DSP_Saturn_Blip(void);
/* Whether data (from DSP_Saturn_KeepVoc) is the voice playing. */
extern bool DSP_Saturn_IsPlaying(const void *data);
#endif

#endif /* DSP_H */
