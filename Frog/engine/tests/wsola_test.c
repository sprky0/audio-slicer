/* wsola_test — the streaming stretcher: identity at factor 1, length and
 * pitch preservation at 2× and 0.5×, reversed input, late join, and the
 * voice-level pitch paths through the engine (semitone offset = pitch shift
 * with the slot length kept; ratchet pitch mode = varispeed). */
#include "check.h"
#include "frog_types.h"
#include "engine.h"
#include "pattern.h"
#include "wsola.h"

#include <stdlib.h>
#include <string.h>

#define SR 48000.0

static fg_sample* sine(double hz, double secs, double amp) {
	const int64_t n = (int64_t)(secs * SR);
	fg_sample* s = fg_sample_create(1, n, SR);
	for (int64_t i = 0; i < n; i++) {
		s->ch[0][i] = (float)(amp * sin(2.0 * FG_PI * hz * (double)i / SR));
	}
	return s;
}

/* frequency from zero crossings over [from, to) */
static double zc_freq(const double* x, int64_t from, int64_t to) {
	int crossings = 0;
	for (int64_t i = from + 1; i < to; i++) {
		if ((x[i - 1] < 0.0) != (x[i] < 0.0)) {
			crossings++;
		}
	}
	return crossings / 2.0 / ((double)(to - from) / SR);
}

static double rms(const double* x, int64_t from, int64_t to) {
	double s = 0.0;
	for (int64_t i = from; i < to; i++) {
		s += x[i] * x[i];
	}
	return sqrt(s / (double)(to - from));
}

static double* pull(fg_wsola* w, int64_t from, int64_t to) {
	double* out = (double*)calloc((size_t)(to - from), sizeof(double));
	for (int64_t i = from; i < to; i++) {
		out[i - from] = fg_wsola_get(w, 0, i);
	}
	return out;
}

int main(void) {
	fg_sample* src = sine(440.0, 1.0, 0.5);
	const int64_t N = src->frames;
	fg_wsola* w = (fg_wsola*)calloc(1, sizeof(fg_wsola));

	/* window endpoints */
	CHECK_NEAR(fg_wsola_window(0), 0.0, 1e-6);
	CHECK_NEAR(fg_wsola_window(FG_WSOLA_FRAME / 2), 1.0, 1e-3);

	/* 1. identity: factor 1 reproduces the input (lag search finds 0) */
	fg_wsola_init(w, src, 0, N, false, 1.0, 0);
	CHECK(w->outputLen == N);
	double* y = pull(w, 0, N);
	double err = 0.0;
	for (int64_t i = 0; i < N - FG_WSOLA_FRAME; i++) {
		err += fabs(y[i] - src->ch[0][i]);
	}
	CHECK(err / (double)(N - FG_WSOLA_FRAME) < 1e-4);
	free(y);

	/* 2. factor 2: twice as long, still 440 Hz, same level */
	fg_wsola_init(w, src, 0, N, false, 2.0, 0);
	CHECK(w->outputLen == 2 * N);
	y = pull(w, 0, 2 * N);
	CHECK_NEAR(zc_freq(y, 2000, 2 * N - 4000), 440.0, 2.0);
	CHECK_NEAR(rms(y, 2000, 2 * N - 4000), 0.5 / sqrt(2.0), 0.03);
	CHECK_NEAR(fg_wsola_get(w, 0, 2 * N + 10), 0.0, 0.0);   /* past the end */
	free(y);

	/* 3. factor 0.5: half as long, still 440 Hz */
	fg_wsola_init(w, src, 0, N, false, 0.5, 0);
	CHECK(w->outputLen == N / 2);
	y = pull(w, 0, N / 2);
	CHECK_NEAR(zc_freq(y, 1000, N / 2 - 2000), 440.0, 3.0);
	CHECK_NEAR(rms(y, 1000, N / 2 - 2000), 0.5 / sqrt(2.0), 0.03);
	free(y);

	/* 4. reversed input at factor 1 equals the reversed signal */
	fg_wsola_init(w, src, 0, N, true, 1.0, 0);
	y = pull(w, 0, N);
	err = 0.0;
	for (int64_t i = 0; i < N - FG_WSOLA_FRAME; i++) {
		err += fabs(y[i] - src->ch[0][N - 1 - i]);
	}
	CHECK(err / (double)(N - FG_WSOLA_FRAME) < 1e-4);
	free(y);

	/* 5. late join: starting at output 5000 yields a valid stretch from there
	 *    (a different lag chain than a from-zero run, same pitch and level,
	 *    continuous from the first requested sample); the region offset is
	 *    honoured and the length is the region's */
	fg_wsola_init(w, src, 1000, N - 1000, false, 1.5, 5000);
	CHECK(w->outputLen == (int64_t)llround((N - 1000) * 1.5));
	double* late = pull(w, 5000, 12000);
	CHECK_NEAR(zc_freq(late, 0, 7000), 440.0, 4.0);
	CHECK_NEAR(rms(late, 0, 7000), 0.5 / sqrt(2.0), 0.03);
	CHECK(rms(late, 0, 300) > 0.2);   /* no silent lead-in */
	free(late);

	/* 6. through the engine: a +12 st tile keeps its slot but sounds an
	 *    octave up; a ratchet in pitch mode plays hit k at k·12 st varispeed;
	 *    at a slower tempo the fill stretches without changing pitch */
	fg_engine* e = fg_engine_create(1, 5);
	fg_engine_reset(e, SR, 128);
	fg_sample* bed = sine(440.0, 2.0, 0.5);   /* 16 units of 0.125 s at 120 bpm; mono → −3 dB pan law */
	const double M = sqrt(0.5);
	fg_engine_set_sample(e, 0, bed);
	fg_pattern* p = fg_engine_pattern(e, 0);
	fg_pattern_default_tiles(p);
	p->tiles[0].offset = 12;
	p->tiles[1].offset = -12;
	p->nMods = 1;
	p->mods[0] = (fg_mod){4, FG_MOD_RATCHET, FG_FIRE_PROB, FG_RATCHET_PITCH, 100, 0, 2, 2, 12, 1};
	fg_engine_publish(e, 0);
	fg_engine_set_tempo(e, 120.0);
	fg_engine_play_all(e);
	const int64_t BAR = (int64_t)(2.0 * SR);
	double* L = (double*)calloc((size_t)BAR, sizeof(double));
	double* R = (double*)calloc((size_t)BAR, sizeof(double));
	for (int64_t at = 0; at < BAR; at += 128) {
		double* bufs[2] = {L + at, R + at};
		fg_engine_process(e, bufs, 2, 128);
	}
	const int64_t U = 6000;
	CHECK_NEAR(zc_freq(L, 200, U - 300), 880.0, 8.0);          /* +12 st */
	CHECK_NEAR(zc_freq(L, U + 200, 2 * U - 300), 220.0, 4.0);  /* −12 st */
	CHECK_NEAR(zc_freq(L, 2 * U + 100, 3 * U - 100), 440.0, 4.0);
	CHECK_NEAR(zc_freq(L, 4 * U + 100, 4 * U + 2900), 440.0, 8.0);   /* ratchet hit 0 */
	CHECK_NEAR(zc_freq(L, 4 * U + 3100, 5 * U - 100), 880.0, 12.0);  /* hit 1: +12 st varispeed */
	CHECK_NEAR(rms(L, 2 * U + 100, 3 * U - 100), M * 0.5 / sqrt(2.0), 0.03);
	CHECK_NEAR(rms(L, 200, U - 300), M * 0.5 / sqrt(2.0), 0.05);

	/* slower tempo: slots are 1.5× longer, pitch stays */
	fg_engine_stop_all(e);
	p->tiles[0].offset = 0;
	p->tiles[1].offset = 0;
	p->nMods = 0;
	fg_engine_publish(e, 0);
	fg_engine_set_tempo(e, 80.0);
	fg_engine_play_all(e);
	const int64_t BAR80 = (int64_t)(3.0 * SR);
	double* L2 = (double*)calloc((size_t)BAR80, sizeof(double));
	double* R2 = (double*)calloc((size_t)BAR80, sizeof(double));
	for (int64_t at = 0; at < BAR80; at += 128) {
		double* bufs[2] = {L2 + at, R2 + at};
		fg_engine_process(e, bufs, 2, 128);
	}
	const int64_t U80 = 9000;
	CHECK_NEAR(zc_freq(L2, 300, U80 - 400), 440.0, 4.0);
	CHECK_NEAR(zc_freq(L2, 7 * U80 + 300, 8 * U80 - 400), 440.0, 4.0);
	CHECK_NEAR(rms(L2, 300, U80 - 400), M * 0.5 / sqrt(2.0), 0.03);
	/* the stretched fill reaches the end of its slot (no gap before the cut) */
	CHECK(rms(L2, U80 - 700, U80 - 300) > 0.14);

	free(L);
	free(R);
	free(L2);
	free(R2);
	fg_engine_destroy(e);
	fg_sample_free(bed);
	fg_sample_free(src);
	free(w);
	return check_report("wsola_test");
}
