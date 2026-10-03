/* render.h — bounce a session offline through the same engine that plays
 * live: load samples, publish patterns, Play All at sample 0, process in
 * blocks, normalise, write a WAV. Main thread; allocates. */
#pragma once

#include "frog_types.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	double sampleRate;    /* 0 = 48000 */
	int blockSize;        /* 0 = 128 */
	double seconds;       /* length; 0 = use beats */
	double beats;         /* length in beats at the master tempo; 0 = LCM of the tracks' beat counts */
	bool normalize;       /* peak to −0.1 dBFS as the browser export does */
	uint32_t seed;        /* probability rolls; 0 = 1 */
	const char* samplesDir;   /* where track samplePath / fileName resolve; NULL = cwd */
	double maxSampleSeconds;  /* per-track source cap; 0 = none */
} fg_render_opts;

typedef struct {
	int64_t frames;
	double peak;          /* before normalisation */
	double rms;
	int tracksWithAudio;
} fg_render_stats;

bool fg_render_session(const fg_session* s, const fg_render_opts* opts, const char* outPath, fg_render_stats* stats);

#ifdef __cplusplus
}
#endif
