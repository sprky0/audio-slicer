/* envelope.h — per-voice gain over time: fade in / out with a curve shape
 * each, rising to and falling from the voice's level. Shared by live
 * playback and the offline render so both produce the same envelope. */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The descriptor the policy produces: times in seconds of playback. */
typedef struct {
	double fadeInSec;
	double fadeOutSec;
	int curveIn;   /* fg_curve */
	int curveOut;
	double gain;   /* the level, ≥ 0; 1 = unity */
} fg_env;

/* Resolved against the voice's audible span. */
typedef struct {
	double fadeIn;   /* effective seconds (scaled when in + out exceed the span) */
	double fadeOut;
	double total;    /* audible span, seconds */
	double level;
	int curveIn;
	int curveOut;
	bool fadesOut;   /* the fade-out lands on the cut: no declick ramp needed */
} fg_env_state;

/* Normalised shape f(x), x 0..1 → 0..1. */
double fg_curve_shape(int curve, double x);

/* Prepare `st` for a voice audible for `totalSec`. `env` may be NULL
 * (unity, no fades). Returns st->fadesOut. */
bool fg_env_prepare(fg_env_state* st, const fg_env* env, double totalSec);

/* Gain at `tSec` since the voice started. */
double fg_env_gain(const fg_env_state* st, double tSec);

#ifdef __cplusplus
}
#endif
