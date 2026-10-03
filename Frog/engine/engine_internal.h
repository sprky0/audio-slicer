/* engine_internal.h — the engine's struct bodies. C11 only (atomics): for
 * engine.c and the engine tests, never for C++ consumers (engine.h is the
 * public, opaque interface). */
#pragma once

#include "engine.h"

#include <stdatomic.h>

/* Lock-free triple buffer of patterns (one writer, one reader). */
typedef struct {
	fg_pattern slot[3];
	atomic_int latest;   /* index | FG_TB_DIRTY */
	int writeIdx;
	int readIdx;
} fg_pbuf;

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
	bool solo;
	double gainL, gainR;       /* smoothed applied gains (volume × mute × pan) */
	double curL, curR;
	bool hasPattern;
} fg_track;

struct fg_engine {
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
	/* clock sources (audio thread) */
	int clockSource;
	fg_midiclock midiClock;
	fg_midi_event midiEvents[FG_MIDI_RING];
	int nMidiEvents;
	double hostTempo;
	double hostPpq;
	bool hostRunning;
	bool hostWasRunning;
	double hostSmoothedErr;
	_Atomic double extBpm;       /* the external clock's estimate, for display */
	_Atomic int extRunning;
	/* recording (F18): the UI arms a capture buffer, the audio thread appends
	 * the block's inputs while armed; see fg_engine_record_* */
	_Atomic int recState;        /* 0 idle, 1 armed, 2 stopping (UI asked), 3 stopped (audio acked) */
	fg_sample* recBuf;           /* capacity = recBuf->frames; UI owns outside state 1/2 */
	_Atomic int64_t recFrames;   /* frames captured so far */
	int recTrack;
};
