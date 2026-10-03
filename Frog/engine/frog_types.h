/* frog_types.h — the Frog engine's data model: caps, tiles, modifiers,
 * patterns, tracks and sessions. Plain C11 structs with fixed capacities so
 * the audio thread never allocates. The shapes mirror the browser version's
 * saved state (session.h reads and writes that JSON). */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FG_MAX_TRACKS 8
#define FG_MAX_UNITS 128    /* 16 beats × 1/32 */
#define FG_MAX_TILES 512    /* gaps-mode raster is U × FG_SUBSTEP cells */
#define FG_MAX_MODS 128     /* one per step */
#define FG_MAX_VOICES 8     /* per track */
#define FG_MAX_HITS 128     /* ratchet hits per span (16 steps × 8) */
#define FG_SUBSTEP 4        /* tile positions and widths sit on a 1/4-unit lattice */
#define FG_MIN_W (1.0 / FG_SUBSTEP)
#define FG_EPS 1e-6
#define FG_NAME_MAX 256
#define FG_PATH_MAX 512
#define FG_PI 3.14159265358979323846   /* M_PI is not strict C11 */

typedef enum {
	FG_CURVE_LINEAR = 0,
	FG_CURVE_EXP,    /* x²: slow start, late rise */
	FG_CURVE_LOG,    /* √x: fast start, long tail */
	FG_CURVE_S,      /* raised cosine */
	FG_CURVE_COUNT
} fg_curve;

typedef enum {
	FG_MOD_MUTE = 0,
	FG_MOD_REV,
	FG_MOD_GAIN,
	FG_MOD_RAND,
	FG_MOD_RESET,
	FG_MOD_RATCHET,
	FG_MOD_PITCH,      /* one-shot transposition of the covered tile's pass */
	FG_MOD_ACTION_COUNT
} fg_mod_action;

typedef enum {
	FG_FIRE_PROB = 0,  /* fireValue = chance 0..100, rolled per pass */
	FG_FIRE_EVERY      /* fireValue = N, fires on the 1st of every N loops */
} fg_fire_mode;

typedef enum {
	FG_RATCHET_EVEN = 0,  /* subdiv uniform hits per step */
	FG_RATCHET_RAMP,      /* spacing morphs subdiv → subdivTo across the span */
	FG_RATCHET_PITCH      /* even spacing, hit k shifted k·pitchStep semitones (varispeed) */
} fg_ratchet_mode;

typedef enum {
	FG_EDIT_PACK = 0,  /* tiles stay contiguous; resize borrows from the neighbour */
	FG_EDIT_GAPS       /* free placement; moving leaves silence, dropping overwrites */
} fg_edit_mode;

/* One slot in the loop: reads `w` units of source from unit `src` and
 * occupies `w` steps of time. Σw over the pattern == unit count. */
typedef struct {
	double src;        /* start unit, 0..U, on the 1/4 lattice */
	double w;          /* width in units, ≥ FG_MIN_W */
	float fadeIn;      /* 0..1 fraction of the tile's own length */
	float fadeOut;
	float gain;        /* linear 0..2, 1 = unity */
	int8_t offset;     /* per-tile pitch, semitones (UI limits ±5) */
	uint8_t colorIdx;
	uint8_t curveIn;   /* fg_curve */
	uint8_t curveOut;
	bool gap;          /* silent slot; only w is meaningful */
	bool muted;
	bool locked;
	bool reversed;
} fg_tile;

/* At most one modifier per step, pinned to the grid. */
typedef struct {
	int16_t step;       /* 0..U-1 */
	uint8_t action;     /* fg_mod_action */
	uint8_t fireMode;   /* fg_fire_mode */
	uint8_t mode;       /* fg_ratchet_mode */
	int16_t fireValue;  /* prob 0..100 or every-N 1..16 */
	int16_t gainAmt;    /* gain action: 0..200 % */
	int8_t subdiv;      /* 1..8 hits per step */
	int8_t subdivTo;    /* ramp target, 1..8 */
	int8_t pitchStep;   /* −12..12 semitones per hit */
	int8_t lenSteps;    /* 1..16 */
	int8_t pitchAmt;    /* pitch action: semitones, −12..12 (mods under one tile add) */
	uint8_t flags;      /* ratchet options, FG_MODF_* */
} fg_mod;

/* ratchet options */
#define FG_MODF_TAIL 1        /* Len shorter than the tile: the rest of the tile plays through after the hits */
#define FG_MODF_KEEP_PITCH 2  /* pitch mode shifts hits with the stretch stage (same speed) instead of varispeed */

/* Everything the sequencer reads at schedule time. Edited on the UI thread
 * and published whole to the audio thread. */
typedef struct {
	int beats;            /* {1,2,3,4,6,8,12,16} quarter notes per loop */
	int denom;            /* step = 1/denom note, {2,4,8,16,32} */
	double virtualStart;  /* zoom window, fractions of the full sample */
	double virtualEnd;
	double start;         /* selection, fractions of the virtual window */
	double end;
	int masterPitch;      /* semitones, ±12 */
	int randLevel;        /* 0..100, the Randomize amount */
	bool loop;
	int nTiles;
	int nMods;
	fg_tile tiles[FG_MAX_TILES];
	fg_mod mods[FG_MAX_MODS];
} fg_pattern;

/* Mixer settings: smoothed on the audio side, not part of the pattern. */
typedef struct {
	double volume;  /* 0..1 */
	double pan;     /* −1..1 */
	bool muted;
	bool solo;      /* while any track is soloed, the others are silent */
} fg_track_mix;

typedef struct {
	int id;
	char fileName[FG_NAME_MAX];   /* display name, as the browser version kept it */
	char samplePath[FG_PATH_MAX]; /* native: path relative to the session's samples dir */
	double bpm;                   /* derived tempo of the clip; master governs */
	bool bpmManual;
	fg_track_mix mix;
	fg_pattern pattern;
} fg_track_state;

/* The whole saved state. Large: allocate on the heap. */
typedef struct {
	int formatVersion;      /* 1 */
	double masterBpm;       /* 0 = unset */
	bool masterUserSet;
	uint8_t editMode;       /* fg_edit_mode */
	bool midiEnabled;
	char midiInputId[128];
	int nextTrackId;
	int nTracks;
	fg_track_state tracks[FG_MAX_TRACKS];
} fg_session;

#ifdef __cplusplus
}
#endif
