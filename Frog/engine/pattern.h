/* pattern.h — grid math and pure helpers over fg_pattern: unit count,
 * default layout, validation, modifier lookup, bar position, the ratchet
 * hit layout. No allocation; safe on the audio thread. */
#pragma once

#include "frog_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* xorshift32: a cheap, seedable RNG for probability rolls and randomize. */
typedef struct {
	uint32_t s;
} fg_rng;

void fg_rng_seed(fg_rng* r, uint32_t seed);
uint32_t fg_rng_next(fg_rng* r);
double fg_rng_unit(fg_rng* r);  /* [0, 1) */

/* U = beats × denom / 4, at least 1, at most FG_MAX_UNITS. */
int fg_unit_count(const fg_pattern* p);
double fg_step_beats(const fg_pattern* p);  /* beats per step = 4 / denom */
double fg_snap_unit(double u);               /* to the 1/4 lattice */

/* Sensible empty pattern: 4 beats, 1/16, full region, loop on, no tiles. */
void fg_pattern_init(fg_pattern* p);
/* One unit-wide tile per unit in native order. */
void fg_pattern_default_tiles(fg_pattern* p);
void fg_tile_init(fg_tile* t, double src, double w, int colorIdx);
double fg_pattern_sum_w(const fg_pattern* p);

/* Clamp every field into range, drop invalid tiles / duplicate mods. If the
 * tile row no longer sums to U or any tile falls outside [0, U], rebuild the
 * default row. Returns true when the tiles were kept. */
bool fg_pattern_validate(fg_pattern* p);

/* Modifiers whose integer step lies in [startStep, startStep + w). Returns
 * the count written to `out` (at most `max`). */
int fg_mods_in_span(const fg_pattern* p, double startStep, double w, const fg_mod** out, int max);

/* Split a monotonic step position into (loopIdx, stepInBar) on the U-step
 * bar, snapping float dust to the lattice first. */
void fg_bar_pos(double posSteps, int U, int* loopIdx, double* stepInBar);

/* Does this modifier fire on this pass? */
bool fg_mod_fires(const fg_mod* m, int loopIdx, fg_rng* rng);

/* Ratchet hit start positions in steps from the span start, per mode.
 * Returns the hit count (≤ max, ≤ FG_MAX_HITS). */
int fg_ratchet_hit_steps(const fg_mod* m, double spanSteps, double* out, int max);
/* Per-hit playback-rate multiplier (pitch mode only; 1 otherwise). */
double fg_ratchet_hit_rate(const fg_mod* m, int k);

#ifdef __cplusplus
}
#endif
