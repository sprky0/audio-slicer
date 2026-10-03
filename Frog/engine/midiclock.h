/* midiclock.h — an external MIDI clock as a tempo + phase source. 24 pulses
 * per quarter note: the gaps between 0xF8 pulses, averaged over a window,
 * give the tempo; each pulse is also a phase observation ("beat n/24 was
 * at this sample") that a PLL turns into gentle grid nudges, snapping only
 * on a gross error. Start / Continue / Stop are surfaced as events. Pure
 * state machine over sample times; the engine owns the grid it steers. */
#pragma once

#include "grid.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FG_MIDICLOCK_WINDOW 49   /* pulses kept: 48 intervals ≈ two beats */

typedef enum {
	FG_MC_NONE = 0,
	FG_MC_START,
	FG_MC_CONTINUE,
	FG_MC_STOP,
	FG_MC_TEMPO   /* bpm estimate moved by ≥ 0.1 */
} fg_midiclock_event;

typedef struct {
	double sampleRate;
	bool running;         /* between Start / Continue and Stop */
	int64_t clocks;       /* pulses since the last Start */
	double bpm;           /* 0 until two pulses have arrived */
	double stamps[FG_MIDICLOCK_WINDOW];
	int nStamps;
	double smoothedErr;   /* PLL: EMA of the phase error, in samples */
	int64_t lastPulse;
} fg_midiclock;

void fg_midiclock_init(fg_midiclock* mc, double sampleRate);
void fg_midiclock_reset_timing(fg_midiclock* mc);

/* Feed one realtime status byte that arrived at absolute `sample`. Returns
 * the event it produced (FG_MC_NONE for an ordinary pulse). */
fg_midiclock_event fg_midiclock_status(fg_midiclock* mc, uint8_t status, double sample);

/* Steer `grid` from the last pulse (call right after a pulse status): maps
 * the pulse's beat onto the grid, slews the origin toward zero error and
 * snaps on a gross one. Returns the raw phase error in samples (> 0 = the
 * grid is running late). */
double fg_midiclock_pll(fg_midiclock* mc, fg_grid* grid, double sample);

/* Beat position: (pulses − 1) / 24; the first pulse after Start is beat 0. */
double fg_midiclock_beat(const fg_midiclock* mc);

#ifdef __cplusplus
}
#endif
