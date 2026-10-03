#include "grid.h"

#include <math.h>

static bool valid_bpm(double bpm) {
	return bpm > 0.0 && isfinite(bpm);
}

void fg_grid_init(fg_grid* g, double bpm, double sampleRate) {
	g->bpm = valid_bpm(bpm) ? bpm : 120.0;
	g->sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
	g->originSample = 0.0;
	g->originBeat = 0.0;
	g->running = false;
}

double fg_grid_sample_at_beat(const fg_grid* g, double beat) {
	return g->originSample + (beat - g->originBeat) * (60.0 / g->bpm) * g->sampleRate;
}

double fg_grid_beat_at_sample(const fg_grid* g, double sample) {
	return g->originBeat + (sample - g->originSample) / g->sampleRate * (g->bpm / 60.0);
}

void fg_grid_restart(fg_grid* g, double atSample) {
	g->originSample = atSample;
	g->originBeat = 0.0;
	g->running = true;
}

void fg_grid_stop(fg_grid* g) {
	g->running = false;
}

void fg_grid_set_tempo(fg_grid* g, double bpm, double atSample) {
	if (!valid_bpm(bpm)) {
		return;
	}
	g->originBeat = fg_grid_beat_at_sample(g, atSample);
	g->originSample = atSample;
	g->bpm = bpm;
}

void fg_grid_sync_phase(fg_grid* g, double beat, double sample, double bpm) {
	if (valid_bpm(bpm)) {
		g->bpm = bpm;
	}
	g->originBeat = beat;
	g->originSample = sample;
	g->running = true;
}

void fg_grid_nudge(fg_grid* g, double dSamples) {
	if (isfinite(dSamples)) {
		g->originSample += dSamples;
	}
}
