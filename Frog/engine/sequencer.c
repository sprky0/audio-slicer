#include "sequencer.h"
#include "edit.h"

#include <math.h>
#include <string.h>

void fg_seq_init(fg_seq* s, int track, uint32_t seed) {
	memset(s, 0, sizeof(*s));
	s->track = track;
	fg_rng_seed(&s->rng, seed);
}

void fg_seq_start(fg_seq* s, double anchorBeat) {
	s->playing = true;
	s->anchorBeat = anchorBeat;
	s->posBeats = 0.0;
	s->tileIndex = 0;
	s->noMoreTiles = false;
	s->ratchet.active = false;
}

void fg_seq_stop(fg_seq* s) {
	s->playing = false;
	s->ratchet.active = false;
}

/* --- policy ------------------------------------------------------------- */

static double clampd(double v, double lo, double hi) {
	return v < lo ? lo : (v > hi ? hi : v);
}

bool fg_resolve_slot(const fg_pattern* p, const fg_sample* smp, const fg_tile* tile, double stepSec,
                     const fg_override* ov, double sampleRate, fg_voice_spec* out, fg_env* envOut) {
	memset(out, 0, sizeof(*out));
	const double w = tile->w > 0.0 ? tile->w : 1.0;
	const double playDur = w * stepSec;
	if (tile->gap || tile->muted || (ov && ov->mute) || !smp || smp->frames <= 0) {
		return false;
	}
	const int U = fg_unit_count(p);
	const double vspan = p->virtualEnd - p->virtualStart;
	const double span = (p->end - p->start) * vspan;   /* selection as a fraction of the whole sample */
	if (!(span > 0.0)) {
		return false;
	}
	const double selStart = p->virtualStart + p->start * vspan;
	/* region: w units from unit src, clamped to the selection */
	const double u0 = clampd(tile->src, 0.0, (double)U);
	const double u1 = clampd(tile->src + w, 0.0, (double)U);
	if (u1 <= u0) {
		return false;
	}
	const int64_t a = (int64_t)floor((selStart + (u0 / U) * span) * (double)smp->frames);
	const int64_t b = (int64_t)floor((selStart + (u1 / U) * span) * (double)smp->frames);
	if (b <= a) {
		return false;
	}
	out->smp = smp;
	out->regionStart = a;
	out->regionLen = b - a;
	out->reversed = (ov && ov->rev) ? !tile->reversed : tile->reversed;

	/* stretch: fill the slot, then pitch */
	const double unitDur = (span * (double)smp->frames / sampleRate) / U;
	const double naturalDur = w * unitDur;
	const double fill = naturalDur > 0.0 ? playDur / naturalDur : 1.0;
	int eff = p->masterPitch + tile->offset;
	eff = eff < -12 ? -12 : (eff > 12 ? 12 : eff);
	const double P = pow(2.0, eff / 12.0);
	const double rawFactor = fill * P;
	if (fabs(rawFactor - 1.0) < 0.01 && eff == 0) {
		out->factor = 1.0;
		out->rate = 1.0;
		out->declick = false;
	} else {
		out->factor = rawFactor;
		out->rate = P;
		out->declick = true;
	}

	/* envelope: fades as fractions of the slot (or of one hit), gain ceiling */
	const double envDur = (ov && ov->envDurSec > 0.0) ? ov->envDurSec : playDur;
	const double tileGain = (isfinite(tile->gain) && tile->gain >= 0.f) ? (tile->gain > 2.f ? 2.0 : tile->gain) : 1.0;
	const double level = tileGain * ((ov && isfinite(ov->gain) && ov->gain >= 0.0) ? ov->gain : 1.0);
	if (tile->fadeIn > 0.f || tile->fadeOut > 0.f || level != 1.0) {
		envOut->fadeInSec = clampd(tile->fadeIn, 0.0, 1.0) * envDur;
		envOut->fadeOutSec = clampd(tile->fadeOut, 0.0, 1.0) * envDur;
		envOut->curveIn = tile->curveIn;
		envOut->curveOut = tile->curveOut;
		envOut->gain = level;
		out->env = envOut;
	} else {
		out->env = NULL;
	}
	return true;
}

/* --- modifiers ---------------------------------------------------------- */

static void flash(fg_seq* s, const fg_mod* m, int64_t at) {
	if (s->onFlash) {
		s->onFlash(s->ctx, s->track, m->step, at);
	}
}

/* Voice-level overrides for one slot: mute / rev / gain modifiers covering
 * the tile's span, each rolled. Two rev mods cancel; gain mods multiply. */
static bool resolve_voice_mods(fg_seq* s, const fg_pattern* p, const fg_tile* tile, double posSteps, int64_t at, fg_override* ov) {
	if (p->nMods == 0) {
		return false;
	}
	int loopIdx;
	double stepInBar;
	fg_bar_pos(posSteps, fg_unit_count(p), &loopIdx, &stepInBar);
	const double w = tile->w > 0.0 ? tile->w : 1.0;
	const fg_mod* hits[FG_MAX_MODS];
	const int n = fg_mods_in_span(p, stepInBar, w, hits, FG_MAX_MODS);
	bool any = false;
	ov->mute = false;
	ov->rev = false;
	ov->gain = 1.0;
	ov->envDurSec = 0.0;
	for (int i = 0; i < n; i++) {
		const fg_mod* m = hits[i];
		if (m->action != FG_MOD_MUTE && m->action != FG_MOD_REV && m->action != FG_MOD_GAIN) {
			continue;
		}
		if (!fg_mod_fires(m, loopIdx, &s->rng)) {
			continue;
		}
		if (m->action == FG_MOD_MUTE) {
			ov->mute = true;
		} else if (m->action == FG_MOD_REV) {
			ov->rev = !ov->rev;
		} else {
			ov->gain *= clampd(m->gainAmt, 0.0, 200.0) / 100.0;
		}
		any = true;
		flash(s, m, at);
	}
	return any && (ov->mute || ov->rev || ov->gain != 1.0);
}

/* The first firing ratchet covering the slot wins. Gaps and muted tiles
 * never ratchet. */
static const fg_mod* resolve_ratchet(fg_seq* s, const fg_pattern* p, const fg_tile* tile, double posSteps, int64_t at) {
	if (p->nMods == 0 || tile->gap || tile->muted) {
		return NULL;
	}
	int loopIdx;
	double stepInBar;
	fg_bar_pos(posSteps, fg_unit_count(p), &loopIdx, &stepInBar);
	const double w = tile->w > 0.0 ? tile->w : 1.0;
	const fg_mod* hits[FG_MAX_MODS];
	const int n = fg_mods_in_span(p, stepInBar, w, hits, FG_MAX_MODS);
	for (int i = 0; i < n; i++) {
		if (hits[i]->action != FG_MOD_RATCHET || !fg_mod_fires(hits[i], loopIdx, &s->rng)) {
			continue;
		}
		flash(s, hits[i], at);
		return hits[i];
	}
	return NULL;
}

/* Pattern actions (rand / reset) fire just before their covering slot is
 * scheduled, so a modifier on the loop's first step reshapes the loop
 * including that slot. Mutates the active pattern. */
static void fire_pattern_mods(fg_seq* s, fg_pattern* p, double posSteps, int64_t at) {
	if (p->nMods == 0) {
		return;
	}
	int loopIdx;
	double stepInBar;
	const int U = fg_unit_count(p);
	fg_bar_pos(posSteps, U, &loopIdx, &stepInBar);
	double acc = 0.0, w = 1.0;
	for (int i = 0; i < p->nTiles; i++) {
		if (acc + p->tiles[i].w > stepInBar + FG_EPS) {
			w = p->tiles[i].w > 0.0 ? p->tiles[i].w : 1.0;
			break;
		}
		acc += p->tiles[i].w;
	}
	const fg_mod* hits[FG_MAX_MODS];
	const int n = fg_mods_in_span(p, stepInBar, w, hits, FG_MAX_MODS);
	for (int i = 0; i < n; i++) {
		const fg_mod* m = hits[i];
		if (m->action != FG_MOD_RAND && m->action != FG_MOD_RESET) {
			continue;
		}
		if (!fg_mod_fires(m, loopIdx, &s->rng)) {
			continue;
		}
		if (m->action == FG_MOD_RAND) {
			if (fg_edit_randomize(p, p->randLevel / 100.0, &s->rng)) {
				s->patternDirty = true;
			}
		} else {
			fg_edit_reset_order(p);
			s->patternDirty = true;
		}
		flash(s, m, at);
	}
}

/* --- scheduling --------------------------------------------------------- */

static fg_voice* free_voice(fg_voice* voices, int nVoices) {
	for (int i = 0; i < nVoices; i++) {
		if (!voices[i].active) {
			return &voices[i];
		}
	}
	/* steal the oldest */
	fg_voice* oldest = &voices[0];
	for (int i = 1; i < nVoices; i++) {
		if (voices[i].start < oldest->start) {
			oldest = &voices[i];
		}
	}
	return oldest;
}

static void emit_visual(fg_seq* s, const fg_visual* v) {
	if (s->onVisual) {
		s->onVisual(s->ctx, v);
	}
}

/* Start the ratchet hits that begin inside this block. */
static void drain_ratchet(fg_seq* s, const fg_grid* grid, fg_voice* voices, int nVoices, int64_t blockStart, int n, double sampleRate) {
	fg_ratchet_run* r = &s->ratchet;
	if (!r->active) {
		return;
	}
	const int64_t blockEnd = blockStart + n;
	while (r->next < r->hits) {
		const int k = r->next;
		const int64_t h0 = (int64_t)llround(fg_grid_sample_at_beat(grid, r->baseBeat + r->hitSteps[k] * r->stepBeats));
		if (h0 >= blockEnd) {
			return;   /* later block */
		}
		int64_t h1 = r->spanEnd;
		if (k + 1 < r->hits) {
			const int64_t nx = (int64_t)llround(fg_grid_sample_at_beat(grid, r->baseBeat + r->hitSteps[k + 1] * r->stepBeats));
			h1 = nx < r->spanEnd ? nx : r->spanEnd;
		}
		r->next++;
		if (h1 <= blockStart) {
			continue;   /* this hit's moment is already gone */
		}
		fg_voice_spec spec = r->spec;
		spec.rate = r->spec.rate * fg_ratchet_hit_rate(&r->mod, k);
		spec.declick = true;
		spec.env = r->spec.env ? &r->env : NULL;
		const int64_t late = blockStart > h0 ? blockStart - h0 : 0;
		spec.start = h0 + late;
		spec.stop = h1;
		spec.skipOut = late;
		fg_voice_start(free_voice(voices, nVoices), &spec, sampleRate);
	}
	r->active = false;
}

static void schedule_ratchet(fg_seq* s, fg_pattern* p, const fg_sample* smp, const fg_grid* grid, const fg_mod* m,
                             int tileIdx, const fg_tile* tile, double w, double stepBeats, double posSteps,
                             fg_voice* voices, int nVoices, int64_t blockStart, int n, double sampleRate) {
	double remaining = 0.0;
	for (int k = tileIdx; k < p->nTiles; k++) {
		remaining += p->tiles[k].w > 0.0 ? p->tiles[k].w : 1.0;
	}
	const double spanSteps = (m->lenSteps > 0 ? m->lenSteps : 1) < remaining ? (double)(m->lenSteps > 0 ? m->lenSteps : 1) : remaining;
	const double base = s->anchorBeat + s->posBeats;
	const double startT = fg_grid_sample_at_beat(grid, base);
	const double tileStopT = fg_grid_sample_at_beat(grid, base + w * stepBeats);
	const double spanEndT = fg_grid_sample_at_beat(grid, base + spanSteps * stepBeats);

	fg_ratchet_run* r = &s->ratchet;
	r->hits = fg_ratchet_hit_steps(m, spanSteps, r->hitSteps, FG_MAX_HITS);
	r->next = 0;
	r->baseBeat = base;
	r->stepBeats = stepBeats;
	r->spanEnd = (int64_t)llround(spanEndT);
	r->mod = *m;
	const double stepSec = (tileStopT - startT) / w / sampleRate;
	fg_override ov = {false, false, 1.0, (spanEndT - startT) / r->hits / sampleRate};
	r->active = fg_resolve_slot(p, smp, tile, stepSec, &ov, sampleRate, &r->spec, &r->env);
	r->spec.tileIndex = tileIdx;

	/* absorb the tiles that start inside the span */
	double consumed = w;
	int j = tileIdx + 1;
	while (j < p->nTiles && consumed < spanSteps - FG_EPS) {
		consumed += p->tiles[j].w > 0.0 ? p->tiles[j].w : 1.0;
		j++;
	}
	fg_visual v = {0};
	v.track = s->track;
	v.tileIndex = tileIdx;
	v.src = tile->src;
	v.w = consumed;
	v.start = (int64_t)llround(startT);
	v.stop = (int64_t)llround(fg_grid_sample_at_beat(grid, base + consumed * stepBeats));
	v.silent = !r->active;
	v.ratchet = true;
	v.rt = *m;
	v.spanSteps = spanSteps;
	v.wTile = w;
	v.hits = r->hits;
	emit_visual(s, &v);

	s->posBeats += consumed * stepBeats;
	s->tileIndex = j;
	drain_ratchet(s, grid, voices, nVoices, blockStart, n, sampleRate);
}

void fg_seq_process(fg_seq* s, fg_pattern* p, const fg_sample* smp, const fg_grid* grid,
                    fg_voice* voices, int nVoices, int64_t blockStart, int n, double sampleRate) {
	if (!s->playing) {
		return;
	}
	const int64_t blockEnd = blockStart + n;
	drain_ratchet(s, grid, voices, nVoices, blockStart, n, sampleRate);

	for (;;) {
		if (s->noMoreTiles || s->ratchet.active) {
			break;
		}
		const int len = p->nTiles;
		if (len == 0) {
			s->noMoreTiles = true;
			break;
		}
		if (s->tileIndex >= len) {
			if (p->loop) {
				s->tileIndex = 0;
			} else {
				s->noMoreTiles = true;
				break;
			}
		}
		const double stepBeats = fg_step_beats(p);
		fg_tile* tile = &p->tiles[s->tileIndex];
		double w = tile->w > 0.0 ? tile->w : 1.0;
		double tileBeats = w * stepBeats;
		const double startT = fg_grid_sample_at_beat(grid, s->anchorBeat + s->posBeats);
		double stopT = fg_grid_sample_at_beat(grid, s->anchorBeat + s->posBeats + tileBeats);

		if (startT >= (double)blockEnd) {
			break;   /* beyond this block */
		}
		if (stopT <= (double)blockStart) {
			s->posBeats += tileBeats;   /* already in the past: skip */
			s->tileIndex++;
			continue;
		}

		const double posSteps = stepBeats > 0.0 ? s->posBeats / stepBeats : 0.0;
		const int64_t startS = (int64_t)llround(startT);
		fire_pattern_mods(s, p, posSteps, startS);
		if (s->tileIndex < p->nTiles) {
			tile = &p->tiles[s->tileIndex];
			w = tile->w > 0.0 ? tile->w : 1.0;
			tileBeats = w * stepBeats;
			stopT = fg_grid_sample_at_beat(grid, s->anchorBeat + s->posBeats + tileBeats);
		}

		const fg_mod* rt = resolve_ratchet(s, p, tile, posSteps, startS);
		if (rt) {
			schedule_ratchet(s, p, smp, grid, rt, s->tileIndex, tile, w, stepBeats, posSteps, voices, nVoices, blockStart, n, sampleRate);
			continue;
		}

		const int64_t stopS = (int64_t)llround(stopT);
		const int64_t late = blockStart > startS ? blockStart - startS : 0;
		const double stepSec = (stopT - startT) / w / sampleRate;
		fg_override ov;
		const bool haveOv = resolve_voice_mods(s, p, tile, posSteps, startS, &ov);
		fg_voice_spec spec;
		fg_env env;
		const bool sounds = fg_resolve_slot(p, smp, tile, stepSec, haveOv ? &ov : NULL, sampleRate, &spec, &env);
		if (sounds) {
			spec.start = startS + late;
			spec.stop = stopS;
			spec.skipOut = late;
			spec.tileIndex = s->tileIndex;
			fg_voice_start(free_voice(voices, nVoices), &spec, sampleRate);
		}
		fg_visual v = {0};
		v.track = s->track;
		v.tileIndex = s->tileIndex;
		v.src = tile->src;
		v.w = w;
		v.start = startS;
		v.stop = stopS;
		v.silent = !sounds;
		emit_visual(s, &v);

		s->posBeats += tileBeats;
		s->tileIndex++;
	}
}
