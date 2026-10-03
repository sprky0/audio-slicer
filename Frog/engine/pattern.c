#include "pattern.h"

#include <math.h>
#include <string.h>

/* --- rng ------------------------------------------------------------------ */

void fg_rng_seed(fg_rng* r, uint32_t seed) {
	r->s = seed ? seed : 0x9E3779B9u;
}

uint32_t fg_rng_next(fg_rng* r) {
	uint32_t x = r->s;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	r->s = x;
	return x;
}

double fg_rng_unit(fg_rng* r) {
	return (fg_rng_next(r) >> 8) * (1.0 / 16777216.0);
}

/* --- grid math ------------------------------------------------------------ */

static int clampi(int v, int lo, int hi) {
	return v < lo ? lo : (v > hi ? hi : v);
}

static double clampd(double v, double lo, double hi) {
	return v < lo ? lo : (v > hi ? hi : v);
}

int fg_unit_count(const fg_pattern* p) {
	int u = (int)lround((double)p->beats * p->denom / 4.0);
	return clampi(u, 1, FG_MAX_UNITS);
}

double fg_step_beats(const fg_pattern* p) {
	return 4.0 / (p->denom > 0 ? p->denom : 16);
}

double fg_snap_unit(double u) {
	return round(u * FG_SUBSTEP) / FG_SUBSTEP;
}

void fg_tile_init(fg_tile* t, double src, double w, int colorIdx) {
	memset(t, 0, sizeof(*t));
	t->src = src;
	t->w = w;
	t->gain = 1.f;
	t->colorIdx = (uint8_t)colorIdx;
}

void fg_pattern_init(fg_pattern* p) {
	memset(p, 0, sizeof(*p));
	p->beats = 4;
	p->denom = 16;
	p->virtualStart = 0.0;
	p->virtualEnd = 1.0;
	p->start = 0.0;
	p->end = 1.0;
	p->randLevel = 50;
	p->loop = true;
}

void fg_pattern_default_tiles(fg_pattern* p) {
	const int U = fg_unit_count(p);
	p->nTiles = U;
	for (int i = 0; i < U; i++) {
		fg_tile_init(&p->tiles[i], (double)i, 1.0, i);
	}
}

double fg_pattern_sum_w(const fg_pattern* p) {
	double s = 0.0;
	for (int i = 0; i < p->nTiles; i++) {
		s += p->tiles[i].w;
	}
	return s;
}

static bool valid_beats(int b) {
	switch (b) {
		case 1: case 2: case 3: case 4: case 6: case 8: case 12: case 16:
			return true;
		default:
			return false;
	}
}

static bool valid_denom(int d) {
	return d == 2 || d == 4 || d == 8 || d == 16 || d == 32;
}

bool fg_pattern_validate(fg_pattern* p) {
	if (!valid_beats(p->beats)) {
		p->beats = 4;
	}
	if (!valid_denom(p->denom)) {
		p->denom = 16;
	}
	p->virtualStart = clampd(p->virtualStart, 0.0, 1.0);
	p->virtualEnd = clampd(p->virtualEnd, 0.0, 1.0);
	if (p->virtualEnd <= p->virtualStart) {
		p->virtualStart = 0.0;
		p->virtualEnd = 1.0;
	}
	p->start = clampd(p->start, 0.0, 1.0);
	p->end = clampd(p->end, 0.0, 1.0);
	if (p->end - p->start < 0.001) {
		p->start = 0.0;
		p->end = 1.0;
	}
	p->masterPitch = clampi(p->masterPitch, -12, 12);
	p->randLevel = clampi(p->randLevel, 0, 100);

	const int U = fg_unit_count(p);

	/* tiles: clamp fields, drop junk, then check the row invariant */
	int n = 0;
	for (int i = 0; i < p->nTiles && i < FG_MAX_TILES; i++) {
		fg_tile t = p->tiles[i];
		if (!(t.w > 0.0) || !isfinite(t.w)) {
			continue;
		}
		t.w = fg_snap_unit(t.w);
		if (t.w < FG_MIN_W) {
			continue;
		}
		if (t.gap) {
			t.src = 0.0;
		} else {
			if (!isfinite(t.src)) {
				continue;
			}
			t.src = fg_snap_unit(t.src);
		}
		t.fadeIn = (float)clampd(t.fadeIn, 0.0, 1.0);
		t.fadeOut = (float)clampd(t.fadeOut, 0.0, 1.0);
		t.gain = (float)clampd(isfinite(t.gain) ? t.gain : 1.0, 0.0, 2.0);
		t.offset = (int8_t)clampi(t.offset, -12, 12);
		if (t.curveIn >= FG_CURVE_COUNT) {
			t.curveIn = FG_CURVE_LINEAR;
		}
		if (t.curveOut >= FG_CURVE_COUNT) {
			t.curveOut = FG_CURVE_LINEAR;
		}
		p->tiles[n++] = t;
	}
	p->nTiles = n;

	bool keep = n > 0 && fabs(fg_pattern_sum_w(p) - U) < 1e-3;
	for (int i = 0; keep && i < n; i++) {
		const fg_tile* t = &p->tiles[i];
		if (!t->gap && (t->src < -FG_EPS || t->src + t->w > U + FG_EPS)) {
			keep = false;
		}
	}
	if (!keep) {
		fg_pattern_default_tiles(p);
	}

	/* mods: clamp, drop out-of-range steps and duplicates (first wins) */
	bool taken[FG_MAX_UNITS] = {false};
	int nm = 0;
	for (int i = 0; i < p->nMods && i < FG_MAX_MODS; i++) {
		fg_mod m = p->mods[i];
		if (m.step < 0 || m.step >= U || taken[m.step]) {
			continue;
		}
		if (m.action >= FG_MOD_ACTION_COUNT) {
			m.action = FG_MOD_MUTE;
		}
		if (m.fireMode > FG_FIRE_EVERY) {
			m.fireMode = FG_FIRE_PROB;
		}
		if (m.mode > FG_RATCHET_PITCH) {
			m.mode = FG_RATCHET_EVEN;
		}
		m.fireValue = (int16_t)(m.fireMode == FG_FIRE_PROB ? clampi(m.fireValue, 0, 100) : clampi(m.fireValue, 1, 16));
		m.gainAmt = (int16_t)clampi(m.gainAmt, 0, 200);
		m.subdiv = (int8_t)clampi(m.subdiv, 1, 8);
		m.subdivTo = (int8_t)clampi(m.subdivTo, 1, 8);
		m.pitchStep = (int8_t)clampi(m.pitchStep, -12, 12);
		m.lenSteps = (int8_t)clampi(m.lenSteps, 1, 16);
		taken[m.step] = true;
		p->mods[nm++] = m;
	}
	p->nMods = nm;
	return keep;
}

/* --- modifiers ------------------------------------------------------------ */

int fg_mods_in_span(const fg_pattern* p, double startStep, double w, const fg_mod** out, int max) {
	const double from = ceil(startStep - FG_EPS);
	const double to = startStep + w - FG_EPS;
	int n = 0;
	for (int i = 0; i < p->nMods && n < max; i++) {
		const fg_mod* m = &p->mods[i];
		if (m->step >= from && m->step < to) {
			out[n++] = m;
		}
	}
	return n;
}

void fg_bar_pos(double posSteps, int U, int* loopIdx, double* stepInBar) {
	const double p = fg_snap_unit(posSteps);
	const int li = (int)floor((p + FG_EPS) / U);
	*loopIdx = li;
	*stepInBar = p - (double)li * U;
}

bool fg_mod_fires(const fg_mod* m, int loopIdx, fg_rng* rng) {
	if (m->fireMode == FG_FIRE_EVERY) {
		const int n = m->fireValue > 0 ? m->fireValue : 1;
		return (loopIdx % n) == 0;
	}
	return fg_rng_unit(rng) * 100.0 < (double)m->fireValue;
}

/* --- ratchet -------------------------------------------------------------- */

int fg_ratchet_hit_steps(const fg_mod* m, double spanSteps, double* out, int max) {
	if (max > FG_MAX_HITS) {
		max = FG_MAX_HITS;
	}
	const int a = m->subdiv > 0 ? m->subdiv : 1;
	if (m->mode == FG_RATCHET_RAMP) {
		const int b = m->subdivTo > 0 ? m->subdivTo : a;
		int n = 0;
		double pos = 0.0;
		while (pos < spanSteps - FG_EPS && n < max) {
			out[n++] = pos;
			pos += 1.0 / (a + (b - a) * (pos / spanSteps));
		}
		return n;
	}
	int hits = (int)floor(spanSteps * a + FG_EPS);
	hits = clampi(hits, 1, max);
	for (int k = 0; k < hits; k++) {
		out[k] = (double)k / a;
	}
	return hits;
}

double fg_ratchet_hit_rate(const fg_mod* m, int k) {
	if (m->mode != FG_RATCHET_PITCH) {
		return 1.0;
	}
	double st = (double)k * m->pitchStep;
	st = clampd(st, -48.0, 48.0);
	return st == 0.0 ? 1.0 : pow(2.0, st / 12.0);
}
