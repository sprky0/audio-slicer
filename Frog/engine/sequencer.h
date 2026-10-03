/* sequencer.h — one track's scheduler. Walks the tile row against the
 * shared grid in absolute time, one block at a time: slots already past are
 * skipped, a slot in progress starts late with an offset into its material,
 * slots starting inside the block start voices at their exact sample. Step
 * modifiers resolve here (mute / rev / gain overrides, ratchet spans, and
 * the pattern actions that reorder the row live). Audio thread only. */
#pragma once

#include "frog_types.h"
#include "grid.h"
#include "pattern.h"
#include "voice.h"

#ifdef __cplusplus
extern "C" {
#endif

/* What the UI needs to draw playback: one record per sounding or silent
 * slot, in schedule order. */
typedef struct {
	int track;
	int tileIndex;
	double src;        /* source unit the slot reads */
	double w;          /* steps the slot occupies (a ratchet: the whole consumed span) */
	int64_t start;     /* absolute samples */
	int64_t stop;
	double stepInBar;  /* grid step the slot starts on (0..U) */
	bool silent;
	bool ratchet;
	fg_mod rt;         /* the ratchet modifier, for hit boundaries */
	double spanSteps;  /* ratchet span actually used */
	double wTile;      /* the covered tile's own width */
	int hits;
} fg_visual;

typedef void (*fg_visual_fn)(void* ctx, const fg_visual* v);
typedef void (*fg_flash_fn)(void* ctx, int track, int step, int64_t at);

/* A ratchet in progress: hits still to start. */
typedef struct {
	bool active;
	int hits;
	int next;
	double hitSteps[FG_MAX_HITS];
	double baseBeat;
	double stepBeats;
	int64_t spanEnd;
	fg_voice_spec spec;   /* template: region, factor, env; rate × hit rate per hit */
	fg_env env;
	fg_mod mod;
	/* FG_MODF_TAIL: the rest of the tile after the span, started as a late join */
	bool tailPending;
	int64_t slotStart;
	fg_voice_spec tailSpec;
	fg_env tailEnv;
} fg_ratchet_run;

typedef struct {
	int track;
	bool playing;
	double anchorBeat;   /* grid beat where this run's position 0 sits */
	double posBeats;     /* beats scheduled so far (monotonic) */
	int tileIndex;
	bool noMoreTiles;
	bool patternDirty;   /* a pattern action changed the row this block */
	fg_rng rng;
	fg_ratchet_run ratchet;
	fg_visual_fn onVisual;
	fg_flash_fn onFlash;
	void* ctx;
} fg_seq;

void fg_seq_init(fg_seq* s, int track, uint32_t seed);

/* Start at `anchorBeat` on the grid (may lie in the past: the scheduler
 * skips forward into the bar). */
void fg_seq_start(fg_seq* s, double anchorBeat);
void fg_seq_stop(fg_seq* s);

/* Schedule everything that sounds in [blockStart, blockStart + n) into the
 * voice pool. `pattern` is the track's active copy (mutated by pattern
 * actions; `patternDirty` is set when that happens). */
void fg_seq_process(fg_seq* s, fg_pattern* pattern, const fg_sample* smp, const fg_grid* grid,
                    fg_voice* voices, int nVoices, int64_t blockStart, int n, double sampleRate);

/* A one-shot trigger outside the sequencer (a MIDI note): play one unit of
 * source at its natural rate, scaled by `velocity` (0..1), from absolute
 * sample `at`, ringing out (no cut). Emits a visual with tileIndex −1. */
void fg_seq_trigger_unit(fg_seq* s, const fg_pattern* p, const fg_sample* smp, int unit, double velocity,
                         int64_t at, fg_voice* voices, int nVoices, double sampleRate);

/* The slot → playback policy, exposed for tests and the render tool. `ov`
 * flags come from mute / rev / gain / pitch modifiers for this pass; `envDurSec` > 0
 * sizes fades to one ratchet hit instead of the slot. Returns false for a
 * silent slot. */
typedef struct {
	bool mute;
	bool rev;
	double gain;
	double envDurSec;
	int pitch;        /* semitones from pitch modifiers, on top of master + tile (total clamped ±24) */
} fg_override;

bool fg_resolve_slot(const fg_pattern* p, const fg_sample* smp, const fg_tile* tile, double stepSec,
                     const fg_override* ov, double sampleRate, fg_voice_spec* out, fg_env* envOut);

#ifdef __cplusplus
}
#endif
