/* engine.h — the whole Frog engine behind one handle: tracks with their
 * sequencers and voice pools, the shared grid, the sample clock, and the
 * lock-free lanes between the UI thread and the audio thread (pattern
 * mailboxes, command queue, visual ring, sample swap). Allocates in
 * create / reset only. */
#pragma once

#include "frog_types.h"
#include "grid.h"
#include "sample.h"
#include "sequencer.h"

#include <stdatomic.h>
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
	FG_CMD_SET_MUTE      /* track; a = 0/1 */
} fg_cmd_type;

typedef struct {
	int type;
	int track;
	double a, b, c;
} fg_cmd;

/* Lock-free triple buffer of patterns (one writer, one reader). */
typedef struct {
	fg_pattern slot[3];
	atomic_int latest;   /* index | FG_TB_DIRTY */
	int writeIdx;
	int readIdx;
} fg_pbuf;

typedef struct {
	int track;
	int step;
	int64_t at;
} fg_flash;

typedef struct {
	/* UI thread side */
	fg_pattern work;            /* the UI's working copy */
	fg_pbuf inbox;              /* UI → audio */
	fg_pbuf outbox;             /* audio → UI (pattern actions) */
	_Atomic(fg_sample*) sample; /* published store */
	_Atomic(uintptr_t) sampleSeen;
	/* audio thread side */
	fg_pattern active;
	const fg_sample* smp;
	fg_seq seq;
	fg_voice voices[FG_MAX_VOICES];
	double* busL;
	double* busR;
	double volume, pan;        /* targets */
	bool muted;
	double gainL, gainR;       /* smoothed applied gains (volume × mute × pan) */
	double curL, curR;
	bool hasPattern;
} fg_track;

typedef struct {
	double sampleRate;
	int maxBlock;
	int nTracks;
	fg_track tracks[FG_MAX_TRACKS];
	fg_grid grid;
	double masterGain;
	_Atomic int64_t now;       /* samples processed since reset */
	_Atomic int gridRunning;
	_Atomic double gridBpm;
	/* UI → audio commands */
	fg_cmd cmds[FG_CMD_RING];
	atomic_uint cmdHead, cmdTail;
	/* audio → UI visuals */
	fg_visual visuals[FG_VISUAL_RING];
	atomic_uint visHead, visTail;
	fg_flash flashes[FG_FLASH_RING];
	atomic_uint flashHead, flashTail;
	uint32_t seed;
} fg_engine;

fg_engine* fg_engine_create(int nTracks, uint32_t seed);
void fg_engine_destroy(fg_engine* e);

/* Main thread, before audio runs or while it is stopped. */
void fg_engine_reset(fg_engine* e, double sampleRate, int maxBlock);

/* Audio thread: render one block into nCh output channels (2 expected). */
void fg_engine_process(fg_engine* e, double* const* out, int nCh, int nFrames);

/* --- UI thread ---------------------------------------------------------- */

bool fg_engine_push(fg_engine* e, const fg_cmd* c);
void fg_engine_play_all(fg_engine* e);
void fg_engine_stop_all(fg_engine* e);
void fg_engine_track_play(fg_engine* e, int track);
void fg_engine_track_stop(fg_engine* e, int track);
void fg_engine_set_tempo(fg_engine* e, double bpm);
void fg_engine_set_mix(fg_engine* e, int track, double volume, double pan);
void fg_engine_set_mute(fg_engine* e, int track, bool muted);

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
