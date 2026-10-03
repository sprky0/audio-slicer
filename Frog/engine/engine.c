#include "engine.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define FG_TB_DIRTY 4
#define MIX_SMOOTH_SEC 0.005

/* --- triple buffer -------------------------------------------------------- */

static void pbuf_init(fg_pbuf* b) {
	b->writeIdx = 0;
	b->readIdx = 1;
	atomic_store(&b->latest, 2);
}

static void pbuf_write(fg_pbuf* b, const fg_pattern* p) {
	memcpy(&b->slot[b->writeIdx], p, sizeof(fg_pattern));
	const int prev = atomic_exchange_explicit(&b->latest, b->writeIdx | FG_TB_DIRTY, memory_order_acq_rel);
	b->writeIdx = prev & 3;
}

static const fg_pattern* pbuf_read(fg_pbuf* b) {
	if (!(atomic_load_explicit(&b->latest, memory_order_acquire) & FG_TB_DIRTY)) {
		return NULL;
	}
	const int prev = atomic_exchange_explicit(&b->latest, b->readIdx, memory_order_acq_rel);
	b->readIdx = prev & 3;
	return &b->slot[b->readIdx];
}

/* --- rings ---------------------------------------------------------------- */

static bool cmd_push(fg_engine* e, const fg_cmd* c) {
	const unsigned head = atomic_load_explicit(&e->cmdHead, memory_order_relaxed);
	const unsigned tail = atomic_load_explicit(&e->cmdTail, memory_order_acquire);
	if (head - tail >= FG_CMD_RING) {
		return false;
	}
	e->cmds[head % FG_CMD_RING] = *c;
	atomic_store_explicit(&e->cmdHead, head + 1, memory_order_release);
	return true;
}

static bool cmd_pop(fg_engine* e, fg_cmd* c) {
	const unsigned tail = atomic_load_explicit(&e->cmdTail, memory_order_relaxed);
	const unsigned head = atomic_load_explicit(&e->cmdHead, memory_order_acquire);
	if (tail == head) {
		return false;
	}
	*c = e->cmds[tail % FG_CMD_RING];
	atomic_store_explicit(&e->cmdTail, tail + 1, memory_order_release);
	return true;
}

static void visual_cb(void* ctx, const fg_visual* v) {
	fg_engine* e = (fg_engine*)ctx;
	const unsigned head = atomic_load_explicit(&e->visHead, memory_order_relaxed);
	const unsigned tail = atomic_load_explicit(&e->visTail, memory_order_acquire);
	if (head - tail >= FG_VISUAL_RING) {
		return;   /* UI is not draining: drop */
	}
	e->visuals[head % FG_VISUAL_RING] = *v;
	atomic_store_explicit(&e->visHead, head + 1, memory_order_release);
}

static void flash_cb(void* ctx, int track, int step, int64_t at) {
	fg_engine* e = (fg_engine*)ctx;
	const unsigned head = atomic_load_explicit(&e->flashHead, memory_order_relaxed);
	const unsigned tail = atomic_load_explicit(&e->flashTail, memory_order_acquire);
	if (head - tail >= FG_FLASH_RING) {
		return;
	}
	fg_flash* f = &e->flashes[head % FG_FLASH_RING];
	f->track = track;
	f->step = step;
	f->at = at;
	atomic_store_explicit(&e->flashHead, head + 1, memory_order_release);
}

int fg_engine_poll_visuals(fg_engine* e, fg_visual* out, int max) {
	int n = 0;
	unsigned tail = atomic_load_explicit(&e->visTail, memory_order_relaxed);
	const unsigned head = atomic_load_explicit(&e->visHead, memory_order_acquire);
	while (tail != head && n < max) {
		out[n++] = e->visuals[tail % FG_VISUAL_RING];
		tail++;
	}
	atomic_store_explicit(&e->visTail, tail, memory_order_release);
	return n;
}

int fg_engine_poll_flashes(fg_engine* e, fg_flash* out, int max) {
	int n = 0;
	unsigned tail = atomic_load_explicit(&e->flashTail, memory_order_relaxed);
	const unsigned head = atomic_load_explicit(&e->flashHead, memory_order_acquire);
	while (tail != head && n < max) {
		out[n++] = e->flashes[tail % FG_FLASH_RING];
		tail++;
	}
	atomic_store_explicit(&e->flashTail, tail, memory_order_release);
	return n;
}

/* --- lifecycle ------------------------------------------------------------ */

fg_engine* fg_engine_create(int nTracks, uint32_t seed) {
	if (nTracks < 1) {
		nTracks = 1;
	}
	if (nTracks > FG_MAX_TRACKS) {
		nTracks = FG_MAX_TRACKS;
	}
	fg_engine* e = (fg_engine*)calloc(1, sizeof(fg_engine));
	if (!e) {
		return NULL;
	}
	e->nTracks = nTracks;
	e->seed = seed ? seed : 1;
	e->masterGain = 1.0;
	fg_grid_init(&e->grid, 120.0, 48000.0);
	atomic_store(&e->gridBpm, 120.0);
	for (int i = 0; i < FG_MAX_TRACKS; i++) {
		fg_track* t = &e->tracks[i];
		pbuf_init(&t->inbox);
		pbuf_init(&t->outbox);
		fg_pattern_init(&t->work);
		fg_pattern_default_tiles(&t->work);
		t->active = t->work;
		t->hasPattern = true;
		t->volume = 1.0;
		t->pan = 0.0;
		fg_seq_init(&t->seq, i, e->seed * 7919u + (uint32_t)i * 104729u);
		t->seq.onVisual = visual_cb;
		t->seq.onFlash = flash_cb;
		t->seq.ctx = e;
	}
	fg_engine_reset(e, 48000.0, 512);
	return e;
}

void fg_engine_destroy(fg_engine* e) {
	if (!e) {
		return;
	}
	for (int i = 0; i < FG_MAX_TRACKS; i++) {
		free(e->tracks[i].busL);
		free(e->tracks[i].busR);
	}
	free(e);
}

static void pan_gains(double pan, double* gl, double* gr) {
	/* equal-power pan as the Web Audio StereoPannerNode applies to a stereo
	 * input: x = pan ≤ 0 ? pan + 1 : pan */
	const double x = pan <= 0.0 ? pan + 1.0 : pan;
	*gl = cos(x * M_PI / 2.0);
	*gr = sin(x * M_PI / 2.0);
}

void fg_engine_reset(fg_engine* e, double sampleRate, int maxBlock) {
	e->sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
	e->maxBlock = maxBlock > 0 ? maxBlock : 512;
	const double bpm = e->grid.bpm;
	fg_grid_init(&e->grid, bpm, e->sampleRate);
	atomic_store(&e->now, 0);
	atomic_store(&e->gridRunning, 0);
	for (int i = 0; i < FG_MAX_TRACKS; i++) {
		fg_track* t = &e->tracks[i];
		free(t->busL);
		free(t->busR);
		t->busL = (double*)calloc((size_t)e->maxBlock, sizeof(double));
		t->busR = (double*)calloc((size_t)e->maxBlock, sizeof(double));
		for (int v = 0; v < FG_MAX_VOICES; v++) {
			fg_voice_stop(&t->voices[v]);
		}
		fg_seq_stop(&t->seq);
		double gl, gr;
		pan_gains(t->pan, &gl, &gr);
		t->curL = t->gainL = t->muted ? 0.0 : t->volume;
		t->curR = t->gainR = t->curL;
		(void)gl;
		(void)gr;
	}
}

/* --- audio thread --------------------------------------------------------- */

static void apply_cmd(fg_engine* e, const fg_cmd* c, int64_t blockStart) {
	switch (c->type) {
		case FG_CMD_PLAY_ALL: {
			const double at = (double)blockStart + (c->a > 0.0 ? c->a : 0.0);
			fg_grid_restart(&e->grid, at);
			for (int i = 0; i < e->nTracks; i++) {
				fg_track* t = &e->tracks[i];
				for (int v = 0; v < FG_MAX_VOICES; v++) {
					fg_voice_stop(&t->voices[v]);
				}
				if (t->active.nTiles > 0) {
					fg_seq_start(&t->seq, 0.0);
				}
			}
			break;
		}
		case FG_CMD_STOP_ALL:
			for (int i = 0; i < e->nTracks; i++) {
				fg_track* t = &e->tracks[i];
				fg_seq_stop(&t->seq);
				for (int v = 0; v < FG_MAX_VOICES; v++) {
					fg_voice_stop(&t->voices[v]);
				}
			}
			fg_grid_stop(&e->grid);
			break;
		case FG_CMD_TRACK_PLAY:
			if (c->track >= 0 && c->track < e->nTracks) {
				fg_track* t = &e->tracks[c->track];
				if (t->active.nTiles == 0) {
					break;
				}
				for (int v = 0; v < FG_MAX_VOICES; v++) {
					fg_voice_stop(&t->voices[v]);
				}
				if (!e->grid.running) {
					fg_grid_restart(&e->grid, (double)blockStart);
					fg_seq_start(&t->seq, 0.0);
				} else {
					/* join in phase: anchor to the current loop boundary */
					const double nowBeat = fg_grid_beat_at_sample(&e->grid, (double)blockStart);
					const double bar = (double)t->active.beats;
					fg_seq_start(&t->seq, floor(nowBeat / bar) * bar);
				}
			}
			break;
		case FG_CMD_TRACK_STOP:
			if (c->track >= 0 && c->track < e->nTracks) {
				fg_track* t = &e->tracks[c->track];
				fg_seq_stop(&t->seq);
				for (int v = 0; v < FG_MAX_VOICES; v++) {
					fg_voice_stop(&t->voices[v]);
				}
			}
			break;
		case FG_CMD_SET_TEMPO:
			fg_grid_set_tempo(&e->grid, c->a, (double)blockStart);
			break;
		case FG_CMD_GRID_SYNC:
			fg_grid_sync_phase(&e->grid, c->a, c->b, c->c);
			break;
		case FG_CMD_GRID_NUDGE:
			fg_grid_nudge(&e->grid, c->a);
			break;
		case FG_CMD_SET_MIX:
			if (c->track >= 0 && c->track < e->nTracks) {
				e->tracks[c->track].volume = c->a < 0.0 ? 0.0 : (c->a > 1.0 ? 1.0 : c->a);
				e->tracks[c->track].pan = c->b < -1.0 ? -1.0 : (c->b > 1.0 ? 1.0 : c->b);
			}
			break;
		case FG_CMD_SET_MUTE:
			if (c->track >= 0 && c->track < e->nTracks) {
				e->tracks[c->track].muted = c->a != 0.0;
			}
			break;
		default:
			break;
	}
}

void fg_engine_process(fg_engine* e, double* const* out, int nCh, int nFrames) {
	if (nFrames <= 0) {
		return;
	}
	if (nFrames > e->maxBlock) {
		nFrames = e->maxBlock;   /* never write past the buses */
	}
	const int64_t blockStart = atomic_load_explicit(&e->now, memory_order_relaxed);

	fg_cmd c;
	while (cmd_pop(e, &c)) {
		apply_cmd(e, &c, blockStart);
	}

	for (int ch = 0; ch < nCh; ch++) {
		memset(out[ch], 0, sizeof(double) * (size_t)nFrames);
	}
	const double smooth = 1.0 / (MIX_SMOOTH_SEC * e->sampleRate);

	for (int i = 0; i < e->nTracks; i++) {
		fg_track* t = &e->tracks[i];
		/* new pattern from the UI */
		const fg_pattern* np = pbuf_read(&t->inbox);
		if (np) {
			memcpy(&t->active, np, sizeof(fg_pattern));
		}
		/* sample swap: a new store cuts every voice of this track */
		fg_sample* smp = atomic_load_explicit(&t->sample, memory_order_acquire);
		if (smp != t->smp) {
			for (int v = 0; v < FG_MAX_VOICES; v++) {
				fg_voice_stop(&t->voices[v]);
			}
			t->smp = smp;
		}
		atomic_store_explicit(&t->sampleSeen, (uintptr_t)smp, memory_order_release);

		t->seq.patternDirty = false;
		fg_seq_process(&t->seq, &t->active, t->smp, &e->grid, t->voices, FG_MAX_VOICES, blockStart, nFrames, e->sampleRate);
		if (t->seq.patternDirty) {
			pbuf_write(&t->outbox, &t->active);
		}

		memset(t->busL, 0, sizeof(double) * (size_t)nFrames);
		memset(t->busR, 0, sizeof(double) * (size_t)nFrames);
		for (int v = 0; v < FG_MAX_VOICES; v++) {
			fg_voice_render(&t->voices[v], t->busL, t->busR, blockStart, nFrames);
		}

		/* volume × mute, smoothed, then equal-power pan into the output */
		const double target = t->muted ? 0.0 : t->volume;
		double gl, gr;
		pan_gains(t->pan, &gl, &gr);
		if (nCh >= 2) {
			for (int s = 0; s < nFrames; s++) {
				if (t->curL < target) {
					t->curL = t->curL + smooth < target ? t->curL + smooth : target;
				} else if (t->curL > target) {
					t->curL = t->curL - smooth > target ? t->curL - smooth : target;
				}
				const double l = t->busL[s] * t->curL;
				const double r = t->busR[s] * t->curL;
				if (t->pan <= 0.0) {
					out[0][s] += (l + r * gl) * e->masterGain;
					out[1][s] += (r * gr) * e->masterGain;
				} else {
					out[0][s] += (l * gl) * e->masterGain;
					out[1][s] += (r + l * gr) * e->masterGain;
				}
			}
		} else if (nCh == 1) {
			for (int s = 0; s < nFrames; s++) {
				if (t->curL < target) {
					t->curL = t->curL + smooth < target ? t->curL + smooth : target;
				} else if (t->curL > target) {
					t->curL = t->curL - smooth > target ? t->curL - smooth : target;
				}
				out[0][s] += 0.5 * (t->busL[s] + t->busR[s]) * t->curL * e->masterGain;
			}
		}
	}

	atomic_store_explicit(&e->now, blockStart + nFrames, memory_order_release);
	atomic_store_explicit(&e->gridRunning, e->grid.running ? 1 : 0, memory_order_release);
	atomic_store_explicit(&e->gridBpm, e->grid.bpm, memory_order_release);
}

/* --- UI thread ------------------------------------------------------------ */

bool fg_engine_push(fg_engine* e, const fg_cmd* c) {
	return cmd_push(e, c);
}

void fg_engine_play_all(fg_engine* e) {
	fg_cmd c = {FG_CMD_PLAY_ALL, -1, 0.0, 0.0, 0.0};
	cmd_push(e, &c);
}

void fg_engine_stop_all(fg_engine* e) {
	fg_cmd c = {FG_CMD_STOP_ALL, -1, 0.0, 0.0, 0.0};
	cmd_push(e, &c);
}

void fg_engine_track_play(fg_engine* e, int track) {
	fg_cmd c = {FG_CMD_TRACK_PLAY, track, 0.0, 0.0, 0.0};
	cmd_push(e, &c);
}

void fg_engine_track_stop(fg_engine* e, int track) {
	fg_cmd c = {FG_CMD_TRACK_STOP, track, 0.0, 0.0, 0.0};
	cmd_push(e, &c);
}

void fg_engine_set_tempo(fg_engine* e, double bpm) {
	fg_cmd c = {FG_CMD_SET_TEMPO, -1, bpm, 0.0, 0.0};
	cmd_push(e, &c);
}

void fg_engine_set_mix(fg_engine* e, int track, double volume, double pan) {
	fg_cmd c = {FG_CMD_SET_MIX, track, volume, pan, 0.0};
	cmd_push(e, &c);
}

void fg_engine_set_mute(fg_engine* e, int track, bool muted) {
	fg_cmd c = {FG_CMD_SET_MUTE, track, muted ? 1.0 : 0.0, 0.0, 0.0};
	cmd_push(e, &c);
}

fg_pattern* fg_engine_pattern(fg_engine* e, int track) {
	return (track >= 0 && track < e->nTracks) ? &e->tracks[track].work : NULL;
}

void fg_engine_publish(fg_engine* e, int track) {
	if (track < 0 || track >= e->nTracks) {
		return;
	}
	pbuf_write(&e->tracks[track].inbox, &e->tracks[track].work);
}

bool fg_engine_take_pattern_change(fg_engine* e, int track) {
	if (track < 0 || track >= e->nTracks) {
		return false;
	}
	const fg_pattern* p = pbuf_read(&e->tracks[track].outbox);
	if (!p) {
		return false;
	}
	memcpy(&e->tracks[track].work, p, sizeof(fg_pattern));
	return true;
}

fg_sample* fg_engine_set_sample(fg_engine* e, int track, fg_sample* s) {
	if (track < 0 || track >= e->nTracks) {
		return s;
	}
	return atomic_exchange_explicit(&e->tracks[track].sample, s, memory_order_acq_rel);
}

bool fg_engine_sample_retired(const fg_engine* e, int track, const fg_sample* old) {
	if (!old || track < 0 || track >= e->nTracks) {
		return true;
	}
	return atomic_load_explicit(&e->tracks[track].sampleSeen, memory_order_acquire) != (uintptr_t)old;
}

int64_t fg_engine_now(const fg_engine* e) {
	return atomic_load_explicit(&e->now, memory_order_acquire);
}

double fg_engine_bpm(const fg_engine* e) {
	return atomic_load_explicit(&e->gridBpm, memory_order_acquire);
}

bool fg_engine_grid_running(const fg_engine* e) {
	return atomic_load_explicit(&e->gridRunning, memory_order_acquire) != 0;
}

void fg_engine_load_session(fg_engine* e, const fg_session* s) {
	const int n = s->nTracks < e->nTracks ? s->nTracks : e->nTracks;
	for (int i = 0; i < n; i++) {
		const fg_track_state* ts = &s->tracks[i];
		e->tracks[i].work = ts->pattern;
		fg_pattern_validate(&e->tracks[i].work);
		fg_engine_publish(e, i);
		fg_engine_set_mix(e, i, ts->mix.volume, ts->mix.pan);
		fg_engine_set_mute(e, i, ts->mix.muted);
	}
	if (s->masterBpm > 0.0) {
		fg_engine_set_tempo(e, s->masterBpm);
	}
}
