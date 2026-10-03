/* factory.h — the first-boot material, generated rather than shipped: a
 * four-bar drum break in the spirit of the classic breakbeat (kick / snare /
 * hat / crash synthesised from sines and noise, so nothing is licensed) and
 * the one-track session that slices it. Deterministic: the same clip every
 * run. Main thread only (allocates). */
#pragma once

#include "frog_types.h"
#include "sample.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FG_FACTORY_BPM 136.0
#define FG_FACTORY_BEATS 16   /* four bars of 4/4 */
#define FG_FACTORY_DENOM 8    /* 32 slices: eighths, touchable at 1024 px */

/* The clip, mono, at `sampleRate`. Caller frees with fg_sample_free. */
fg_sample* fg_factory_clip(double sampleRate);

/* Write the clip as 16-bit WAV at 48 kHz. */
bool fg_factory_write_clip(const char* path);

/* The session: one track reading `samplePath` (display name `fileName`),
 * 16 beats at eighths, straight row, loop on, master 136 bpm. */
void fg_factory_session(fg_session* s, const char* samplePath, const char* fileName);

#ifdef __cplusplus
}
#endif
