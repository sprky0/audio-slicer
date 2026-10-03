/* engine.h — the whole Frog engine behind one opaque handle: tracks with
 * their sequencers and voice pools, the shared grid, the sample clock, and
 * the lock-free lanes between the UI thread and the audio thread (pattern
 * mailboxes, command queue, visual ring, sample swap). Allocates in
 * create / reset only. C++ consumers see only this header; the struct
 * bodies (C11 atomics) live in engine_internal.h for engine.c and tests. */
#pragma once

#include "frog_types.h"
#include "grid.h"
#include "midiclock.h"
#include "sample.h"
#include "sequencer.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FG_CMD_RING 256
#define FG_VISUAL_RING 512
#define FG_FLASH_RING 256

typedef enum {
	FG_CMD_NONE = 0,
	FG_CMD_PLAY_ALL,     /* a = lead frames */
	FG_CMD_STOP_ALL,
	FG_CMD_TRACK_PLAY,   /* track; joins in phase when the grid runs */
	FG_CMD_TRACK_STOP,
	FG_CMD_SET_TEMPO,    /* a = bpm, phase-continuous now */
	FG_CMD_GRID_SYNC,    /* a = beat, b = sample, c = bpm (MIDI Start / PLL snap) */
	FG_CMD_GRID_NUDGE,   /* a = samples */
	FG_CMD_SET_MIX,      /* track; a = volume, b = pan */
	FG_CMD_SET_MUTE,     /* track; a = 0/1 */
	FG_CMD_CLOCK_SOURCE  /* a = fg_clock_source */
} fg_cmd_type;

/* Who drives the grid. */
typedef enum {
	FG_CLOCK_INTERNAL = 0,  /* the BPM parameter */
	FG_CLOCK_MIDI,          /* external MIDI clock: tempo from the pulses, phase from the PLL */
	FG_CLOCK_HOST           /* the plugin host's transport (tempo + PPQ position) */
} fg_clock_source;

#define FG_MIDI_RING 64

typedef struct {
	uint8_t status;
	int offset;   /* sample offset within the coming block */
} fg_midi_event;

typedef struct {
	int type;
	int track;
	double a, b, c;
} fg_cmd;

typedef struct {
	int track;
	int step;
	int64_t at;
} fg_flash;

typedef struct fg_engine fg_engine;

fg_engine* fg_engine_create(int nTracks, uint32_t seed);
void fg_engine_destroy(fg_engine* e);

/* Main thread, before audio runs or while it is stopped. */
void fg_engine_reset(fg_engine* e, double sampleRate, int maxBlock);

/* Audio thread: render one block into nCh output channels (2 expected). */
void fg_engine_process(fg_engine* e, double* const* out, int nCh, int nFrames);

/* Audio thread, before fg_engine_process() for the same block: a realtime
 * MIDI status byte (clock, start, continue, stop) at `sampleOffset` into
 * the block. Other statuses are ignored here (notes arrive with F16). */
void fg_engine_midi(fg_engine* e, uint8_t status, int sampleOffset);

/* Audio thread, before fg_engine_process(): the host's transport for the
 * coming block (plugin builds). Used only when the clock source is HOST. */
void fg_engine_host_transport(fg_engine* e, double tempo, double ppqPos, bool running);

/* --- UI thread ---------------------------------------------------------- */

void fg_engine_set_clock_source(fg_engine* e, int source);
int fg_engine_clock_source(const fg_engine* e);
/* The external clock's tempo estimate (0 = none yet) and running flag. */
double fg_engine_external_bpm(const fg_engine* e);
bool fg_engine_external_running(const fg_engine* e);

bool fg_engine_push(fg_engine* e, const fg_cmd* c);
void fg_engine_play_all(fg_engine* e);
void fg_engine_stop_all(fg_engine* e);
void fg_engine_track_play(fg_engine* e, int track);
void fg_engine_track_stop(fg_engine* e, int track);
void fg_engine_set_tempo(fg_engine* e, double bpm);
void fg_engine_set_mix(fg_engine* e, int track, double volume, double pan);
void fg_engine_set_mute(fg_engine* e, int track, bool muted);
/* Linear master level applied to the mix (default 1). */
void fg_engine_set_master_gain(fg_engine* e, double gain);

/* Edit the working copy, then publish it; the audio thread picks it up at
 * its next block. */
fg_pattern* fg_engine_pattern(fg_engine* e, int track);
void fg_engine_publish(fg_engine* e, int track);
/* A pattern action on the audio thread changed the row: copy it into the
 * working copy. Returns true when something arrived. */
bool fg_engine_take_pattern_change(fg_engine* e, int track);

/* Swap in a sample (engine rate). Returns the previous one, which the caller
 * frees once fg_engine_sample_retired() says the audio thread moved on. */
fg_sample* fg_engine_set_sample(fg_engine* e, int track, fg_sample* s);
bool fg_engine_sample_retired(const fg_engine* e, int track, const fg_sample* old);
/* Whether a store is published for the track (UI-side read). */
bool fg_engine_has_sample(const fg_engine* e, int track);

int fg_engine_poll_visuals(fg_engine* e, fg_visual* out, int max);
int fg_engine_poll_flashes(fg_engine* e, fg_flash* out, int max);
int64_t fg_engine_now(const fg_engine* e);
double fg_engine_bpm(const fg_engine* e);
bool fg_engine_grid_running(const fg_engine* e);

/* Load a session's patterns and mixes (samples are the caller's job). */
void fg_engine_load_session(fg_engine* e, const fg_session* s);

#ifdef __cplusplus
}
#endif
