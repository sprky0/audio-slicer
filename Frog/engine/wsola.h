/* wsola.h — streaming WSOLA time-stretch (waveform similarity overlap-add).
 * Stretches a region of a sample in time by `factor` (output / input) while
 * keeping pitch; the caller resamples on top for pitch shift. Produces output
 * on demand into a ring, so a voice pulls exactly what it needs per block;
 * no allocation after init. Tuned for short transient-heavy slices: frame
 * 1024, Hann, 75 % overlap (hop 256), lag search ±256 (coarse-to-fine). */
#pragma once

#include "sample.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FG_WSOLA_FRAME 1024
#define FG_WSOLA_HOP 256
#define FG_WSOLA_SEARCH 256
#define FG_WSOLA_RING 8192  /* ≥ frame + a block's worth of pulls at the highest rate */

typedef struct {
	/* input: a region of a sample, optionally read backwards */
	const fg_sample* smp;
	int64_t regionStart;
	int64_t inputLen;
	bool reversed;
	int nCh;
	/* stretch */
	double factor;
	double Ha;           /* analysis hop = hop / factor */
	int64_t outputLen;
	/* synthesis state */
	int64_t frameIdx;
	int64_t outPos;      /* next frame start in output samples */
	int64_t finalized;   /* output samples < finalized are complete */
	int64_t prevInputPos;
	/* ring of accumulating / finished output, indexed by output sample mod RING */
	float out[FG_SAMPLE_MAX_CH][FG_WSOLA_RING];
	float norm[FG_WSOLA_RING];
} fg_wsola;

/* Start a stretch of [regionStart, regionStart + inputLen) by `factor`,
 * beginning synthesis so that output sample `startOut` is available first
 * (late join). */
void fg_wsola_init(fg_wsola* w, const fg_sample* smp, int64_t regionStart, int64_t inputLen, bool reversed, double factor, int64_t startOut);

/* Output sample `idx` of channel `ch`; synthesises as needed. `idx` must not
 * run more than FG_WSOLA_RING - FG_WSOLA_FRAME behind the furthest sample
 * already requested. Past the end returns 0. */
float fg_wsola_get(fg_wsola* w, int ch, int64_t idx);

/* The window, exposed for tests. */
float fg_wsola_window(int n);

#ifdef __cplusplus
}
#endif
