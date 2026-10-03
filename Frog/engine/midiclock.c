#include "midiclock.h"

#include <math.h>
#include <string.h>

#define CLOCK 0xF8
#define START 0xFA
#define CONTINUE 0xFB
#define STOP 0xFC

/* a gap longer than this means the clock paused: the window is stale */
#define STALE_GAP_SEC 0.5
/* only report a tempo that moved at least this much */
#define BPM_EPSILON 0.1
/* PLL: error beyond this snaps; under this is left alone; the fraction of
 * the smoothed error corrected per pulse */
#define PLL_SNAP_SEC 0.08
#define PLL_DEAD_SEC 0.002
#define PLL_SLEW 0.1

void fg_midiclock_init(fg_midiclock* mc, double sampleRate) {
	memset(mc, 0, sizeof(*mc));
	mc->sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
}

void fg_midiclock_reset_timing(fg_midiclock* mc) {
	mc->nStamps = 0;
	mc->bpm = 0.0;
	mc->clocks = 0;
	mc->smoothedErr = 0.0;
}

static fg_midiclock_event pulse(fg_midiclock* mc, double now) {
	mc->clocks++;
	mc->lastPulse = (int64_t)llround(now);
	if (mc->nStamps > 0 && now - mc->stamps[mc->nStamps - 1] > STALE_GAP_SEC * mc->sampleRate) {
		mc->nStamps = 0;
	}
	if (mc->nStamps == FG_MIDICLOCK_WINDOW) {
		memmove(mc->stamps, mc->stamps + 1, sizeof(double) * (FG_MIDICLOCK_WINDOW - 1));
		mc->nStamps--;
	}
	mc->stamps[mc->nStamps++] = now;
	if (mc->nStamps < 2) {
		return FG_MC_NONE;
	}
	const double span = mc->stamps[mc->nStamps - 1] - mc->stamps[0];
	const double avgIvl = span / (mc->nStamps - 1);   /* samples per pulse */
	if (!(avgIvl > 0.0)) {
		return FG_MC_NONE;
	}
	const double bpm = 60.0 * mc->sampleRate / (avgIvl * 24.0);
	if (!(bpm > 0.0) || !isfinite(bpm)) {
		return FG_MC_NONE;
	}
	if (mc->bpm == 0.0 || fabs(bpm - mc->bpm) >= BPM_EPSILON) {
		mc->bpm = bpm;
		return FG_MC_TEMPO;
	}
	return FG_MC_NONE;
}

fg_midiclock_event fg_midiclock_status(fg_midiclock* mc, uint8_t status, double sample) {
	switch (status) {
		case CLOCK:
			return pulse(mc, sample);
		case START:
			mc->running = true;
			fg_midiclock_reset_timing(mc);
			return FG_MC_START;
		case CONTINUE:
			mc->running = true;
			return FG_MC_CONTINUE;
		case STOP:
			mc->running = false;
			return FG_MC_STOP;
		default:
			return FG_MC_NONE;
	}
}

/* The first Timing Clock after Start is the downbeat (MIDI 1.0 spec), so
 * pulse n (counting from 1) marks beat (n − 1) / 24. */
double fg_midiclock_beat(const fg_midiclock* mc) {
	return mc->clocks > 0 ? (double)(mc->clocks - 1) / 24.0 : 0.0;
}

double fg_midiclock_pll(fg_midiclock* mc, fg_grid* grid, double sample) {
	if (!mc->running || !grid->running) {
		return 0.0;
	}
	const double beat = fg_midiclock_beat(mc);
	const double err = fg_grid_sample_at_beat(grid, beat) - sample;   /* > 0: grid late */
	mc->smoothedErr = mc->smoothedErr * 0.8 + err * 0.2;
	const double snap = PLL_SNAP_SEC * mc->sampleRate;
	const double dead = PLL_DEAD_SEC * mc->sampleRate;
	if (fabs(err) > snap) {
		fg_grid_nudge(grid, -err);
		mc->smoothedErr = 0.0;
	} else if (fabs(mc->smoothedErr) > dead) {
		fg_grid_nudge(grid, -mc->smoothedErr * PLL_SLEW);
	}
	return err;
}
