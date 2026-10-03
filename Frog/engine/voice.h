/* voice.h — one sounding slice: a region of the track's sample, optionally
 * reversed, through the time-stretch stage (bypassed at factor 1), a 4-point
 * interpolating reader at the pitch / varispeed ratio, and the per-voice
 * envelope with a declick ramp at a cut. Renders additively into a track's
 * stereo bus; fixed work buffers, no allocation. */
#pragma once

#include "envelope.h"
#include "sample.h"
#include "wsola.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* What the policy resolves a slot to. */
typedef struct {
	const fg_sample* smp;
	int64_t regionStart;   /* frames into the sample */
	int64_t regionLen;
	bool reversed;
	double factor;         /* time-stretch, output / input; 1 = bypass */
	double rate;           /* reader ratio: stream frames per output frame (pitch × varispeed) */
	bool declick;          /* always ramp to silence at the cut (stretched fills, ratchet hits) */
	const fg_env* env;     /* NULL = unity */
	int64_t start;         /* absolute output sample the slot begins */
	int64_t stop;          /* absolute output sample of the cut (exclusive) */
	int64_t skipOut;       /* output frames already elapsed at `start` (late join) */
	int tileIndex;
} fg_voice_spec;

typedef struct {
	bool active;
	int64_t start;
	int64_t end;          /* exclusive: min(stop, natural end) */
	int64_t stop;
	bool rampAtStop;      /* declick ramp over the last 5 ms before `stop` */
	double rate;
	double pos;           /* read position in stream frames */
	double sampleRate;
	const fg_sample* smp;
	int64_t regionStart;
	int64_t regionLen;
	bool reversed;
	bool stretched;
	fg_wsola ws;
	fg_env_state env;
	int tileIndex;
} fg_voice;

/* Start a voice from a spec. `sampleRate` is the engine rate. Returns false
 * (voice stays inactive) when the spec has nothing audible. */
bool fg_voice_start(fg_voice* v, const fg_voice_spec* spec, double sampleRate);

/* Add this voice's frames for the block [blockStart, blockStart + n) into
 * L / R. Deactivates itself when done. */
void fg_voice_render(fg_voice* v, double* L, double* R, int64_t blockStart, int n);

void fg_voice_stop(fg_voice* v);

#ifdef __cplusplus
}
#endif
