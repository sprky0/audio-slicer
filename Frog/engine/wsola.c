#include "wsola.h"
#include "frog_types.h"

#include <math.h>
#include <string.h>

static float g_win[FG_WSOLA_FRAME];
static bool g_winReady = false;

static void ensure_window(void) {
	if (g_winReady) {
		return;
	}
	for (int i = 0; i < FG_WSOLA_FRAME; i++) {
		g_win[i] = (float)(0.5 - 0.5 * cos((2.0 * FG_PI * i) / (FG_WSOLA_FRAME - 1)));
	}
	g_winReady = true;
}

float fg_wsola_window(int n) {
	ensure_window();
	return (n >= 0 && n < FG_WSOLA_FRAME) ? g_win[n] : 0.f;
}

/* Input sample i (0..inputLen-1) of channel c, honouring reversal. */
static inline float in_at(const fg_wsola* w, int c, int64_t i) {
	if (i < 0 || i >= w->inputLen) {
		return 0.f;
	}
	const int64_t src = w->reversed ? (w->regionStart + w->inputLen - 1 - i) : (w->regionStart + i);
	return w->smp->ch[c][src];
}

void fg_wsola_init(fg_wsola* w, const fg_sample* smp, int64_t regionStart, int64_t inputLen, bool reversed, double factor, int64_t startOut) {
	ensure_window();
	w->smp = smp;
	w->regionStart = regionStart;
	w->inputLen = inputLen > 0 ? inputLen : 0;
	w->reversed = reversed;
	w->nCh = smp ? smp->nCh : 0;
	w->factor = factor > 0.0 ? factor : 1.0;
	w->Ha = FG_WSOLA_HOP / w->factor;
	w->outputLen = w->inputLen > 0 ? (int64_t)llround((double)w->inputLen * w->factor) : 0;
	if (w->outputLen < 1 && w->inputLen > 0) {
		w->outputLen = 1;
	}
	/* late join: begin at the frame that covers startOut */
	int64_t f = startOut > 0 ? startOut / FG_WSOLA_HOP : 0;
	w->frameIdx = f;
	w->outPos = f * FG_WSOLA_HOP;
	w->finalized = w->outPos;
	w->prevInputPos = f > 0 ? (int64_t)llround((double)(f - 1) * w->Ha) : 0;
	memset(w->out, 0, sizeof w->out);
	memset(w->norm, 0, sizeof w->norm);
}

/* Normalised cross-correlation of input[cs..] against input[ref..] over
 * `len` samples, channel 0. */
static double ncc(const fg_wsola* w, int64_t cs, int64_t ref, int len) {
	double dot = 0.0, e1 = 0.0, e2 = 0.0;
	for (int n = 0; n < len; n++) {
		const double a = in_at(w, 0, cs + n);
		const double b = in_at(w, 0, ref + n);
		dot += a * b;
		e1 += a * a;
		e2 += b * b;
	}
	return dot / (sqrt(e1 * e2) + 1e-9);
}

/* Best lag in [-search, search] around idealStart whose frame best matches
 * the natural successor at refPos: coarse pass every 4, then ±3 around it. */
static int64_t best_lag(const fg_wsola* w, int64_t idealStart, int64_t refPos) {
	const int len = FG_WSOLA_HOP;
	int64_t best = 0;
	double bestScore = -INFINITY;
	for (int d = -FG_WSOLA_SEARCH; d <= FG_WSOLA_SEARCH; d += 4) {
		const int64_t cs = idealStart + d;
		if (cs < 0 || cs + len >= w->inputLen || refPos + len >= w->inputLen) {
			continue;
		}
		const double s = ncc(w, cs, refPos, len);
		if (s > bestScore) {
			bestScore = s;
			best = d;
		}
	}
	const int64_t centre = best;
	for (int d = -3; d <= 3; d++) {
		if (d == 0) {
			continue;
		}
		const int64_t cand = centre + d;
		if (cand < -FG_WSOLA_SEARCH || cand > FG_WSOLA_SEARCH) {
			continue;
		}
		const int64_t cs = idealStart + cand;
		if (cs < 0 || cs + len >= w->inputLen || refPos + len >= w->inputLen) {
			continue;
		}
		const double s = ncc(w, cs, refPos, len);
		if (s > bestScore) {
			bestScore = s;
			best = cand;
		}
	}
	return best;
}

/* Synthesise one more frame at outPos. */
static void step(fg_wsola* w) {
	const int64_t idealStart = (int64_t)llround((double)w->frameIdx * w->Ha);
	const int64_t delta = w->frameIdx > 0 ? best_lag(w, idealStart, w->prevInputPos + FG_WSOLA_HOP) : 0;
	int64_t start = idealStart + delta;
	if (start < 0) {
		start = 0;
	}
	if (start > w->inputLen - 1) {
		start = w->inputLen - 1;
	}
	/* the ring slots this frame touches were finalized long ago: clear them
	 * before accumulating (slots in [outPos + FRAME - HOP, outPos + FRAME)) */
	for (int n = FG_WSOLA_FRAME - FG_WSOLA_HOP; n < FG_WSOLA_FRAME; n++) {
		const int slot = (int)((w->outPos + n) % FG_WSOLA_RING);
		for (int c = 0; c < w->nCh; c++) {
			w->out[c][slot] = 0.f;
		}
		w->norm[slot] = 0.f;
	}
	for (int n = 0; n < FG_WSOLA_FRAME; n++) {
		const int64_t si = start + n;
		if (si >= w->inputLen) {
			break;
		}
		const int slot = (int)((w->outPos + n) % FG_WSOLA_RING);
		const float win = g_win[n];
		for (int c = 0; c < w->nCh; c++) {
			w->out[c][slot] += in_at(w, c, si) * win;
		}
		w->norm[slot] += win;
	}
	w->prevInputPos = start;
	w->outPos += FG_WSOLA_HOP;
	w->frameIdx++;
	/* samples below the new outPos have every contribution they will get */
	w->finalized = w->outPos;
}

float fg_wsola_get(fg_wsola* w, int ch, int64_t idx) {
	if (idx < 0 || idx >= w->outputLen || ch >= w->nCh || w->inputLen <= 0) {
		return 0.f;
	}
	/* first use after init: the ring from outPos onward must start clean,
	 * which init guarantees; produce until idx is final */
	while (idx >= w->finalized) {
		step(w);
	}
	const int slot = (int)(idx % FG_WSOLA_RING);
	const float g = w->norm[slot];
	return g > 1e-6f ? w->out[ch][slot] / g : w->out[ch][slot];
}
