/* grid.h — the shared musical timebase: one linear mapping between beats
 * and the sample clock. Every track schedules against it absolutely, so a
 * tempo change re-anchors once (phase-continuous) and nothing drifts. The
 * grid is passive: it never ticks, the sequencer reads it per block. */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	double bpm;
	double sampleRate;
	double originSample;  /* sample position where originBeat falls */
	double originBeat;
	bool running;         /* an anchor has been established */
} fg_grid;

void fg_grid_init(fg_grid* g, double bpm, double sampleRate);
double fg_grid_sample_at_beat(const fg_grid* g, double beat);
double fg_grid_beat_at_sample(const fg_grid* g, double sample);

/* (Re)start: beat 0 lands exactly at `atSample`. */
void fg_grid_restart(fg_grid* g, double atSample);
void fg_grid_stop(fg_grid* g);

/* Change tempo, preserving the beat phase at `atSample` (no jump). */
void fg_grid_set_tempo(fg_grid* g, double bpm, double atSample);

/* Hard external anchor: beat `beat` occurred at `sample` (MIDI Start). */
void fg_grid_sync_phase(fg_grid* g, double beat, double sample, double bpm);

/* Slide the origin by `dSamples` without touching tempo: the PLL's phase
 * correction. Negative pulls the grid earlier. */
void fg_grid_nudge(fg_grid* g, double dSamples);

#ifdef __cplusplus
}
#endif
