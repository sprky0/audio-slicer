/* parity_test — the native engine against the browser version's own bounces
 * (fixtures/parity/<case>.js.wav, produced by tools/parity/js-bounce.mjs
 * from <case>.json through the JS renderMix). Exact cases share every code
 * path's semantics and must match to within float rounding; loose cases
 * (time-stretch, pitch) differ by implementation and are compared by
 * per-unit level and pitch. */
#include "check.h"
#include "render.h"
#include "sample.h"
#include "session.h"

#include <stdlib.h>
#include <string.h>

#define RATE 44100.0
#define UNITS 16
#define UNIT 4410   /* frames per unit at 150 bpm, 1/16 */

typedef struct {
	const char* name;
	bool exact;
} parity_case;

static const parity_case CASES[] = {
	{"natural", true},
	{"edits", true},
	{"mods", true},
	{"stretch", false},
	{"pitch", false},
};

static double rms_of(const float* x, int64_t from, int64_t to) {
	double s = 0.0;
	for (int64_t i = from; i < to; i++) {
		s += (double)x[i] * x[i];
	}
	return sqrt(s / (double)(to - from));
}

static double zc_freq(const float* x, int64_t from, int64_t to, double sr) {
	int n = 0;
	for (int64_t i = from + 1; i < to; i++) {
		if ((x[i - 1] < 0.f) != (x[i] < 0.f)) {
			n++;
		}
	}
	return n / 2.0 / ((double)(to - from) / sr);
}

static double db(double v) {
	return v > 0.0 ? 20.0 * log10(v) : -200.0;
}

static bool run_case(const parity_case* c) {
	char path[512];
	fg_session* s = (fg_session*)calloc(1, sizeof(fg_session));
	snprintf(path, sizeof path, "fixtures/parity/%s.json", c->name);
	if (!fg_session_load_file(path, s)) {
		fprintf(stderr, "%s: cannot load session\n", c->name);
		free(s);
		return false;
	}
	snprintf(path, sizeof path, "fixtures/parity/%s.js.wav", c->name);
	fg_sample* js = fg_sample_load_wav(path, 0.0, 0.0);
	if (!js) {
		fprintf(stderr, "%s: no browser bounce at %s (run tools/parity/js-bounce.mjs)\n", c->name, path);
		free(s);
		return false;
	}
	CHECK(js->sampleRate == RATE && js->nCh == 2);

	fg_render_opts o = {0};
	o.sampleRate = js->sampleRate;
	o.blockSize = 128;
	o.beats = 4.0;
	o.normalize = true;
	o.seed = 1;
	o.samplesDir = "fixtures/parity";
	snprintf(path, sizeof path, "/tmp/frog-parity-%s.wav", c->name);
	fg_render_stats st = {0};
	CHECK(fg_render_session(s, &o, path, &st));
	fg_sample* mine = fg_sample_load_wav(path, 0.0, 0.0);
	CHECK(mine != NULL);
	if (!mine) {
		fg_sample_free(js);
		free(s);
		return false;
	}
	CHECK(mine->frames == js->frames);
	const int64_t n = mine->frames < js->frames ? mine->frames : js->frames;

	if (c->exact) {
		/* sample-for-sample: 16-bit on disk on our side, so one LSB of slack
		 * plus float rounding across the mix. The browser's export ends with a
		 * 5 ms declick ramp it never intended (its `stopAt < when + effDur`
		 * compares 1.6 against 1.6000000000000001 on the final slot), so the
		 * last 5 ms are left out of the comparison. */
		const int64_t tail = (int64_t)(0.005 * RATE) + 2;
		double maxd = 0.0, sq = 0.0;
		for (int ch = 0; ch < 2; ch++) {
			for (int64_t i = 0; i < n - tail; i++) {
				const double d = fabs((double)mine->ch[ch][i] - js->ch[ch][i]);
				maxd = d > maxd ? d : maxd;
				sq += d * d;
			}
		}
		const double rmsd = sqrt(sq / (double)(2 * (n - tail)));
		printf("  %-8s exact: max diff %.6f (%.1f dBFS), rms diff %.1f dBFS\n", c->name, maxd, db(maxd), db(rmsd));
		/* the browser schedules curved fades as 65-point piecewise-linear
		 * approximations (setValueCurveAtTime); at the foot of a steep log
		 * fade that is off by up to ~1e-2 for a few samples, which the exact
		 * shapes here do not reproduce */
		CHECK(maxd < 1e-2);
		CHECK(db(rmsd) < -80.0);
	} else {
		/* per unit: level within 1.5 dB and pitch within 4 % on the left */
		const int64_t unit = (int64_t)llround((double)n / UNITS);
		double worstDb = 0.0, worstPitch = 0.0;
		for (int u = 0; u < UNITS; u++) {
			const int64_t a = u * unit + unit / 20, b = (u + 1) * unit - unit / 20;
			const double lm = rms_of(mine->ch[0], a, b), lj = rms_of(js->ch[0], a, b);
			if (lj > 1e-3) {
				const double dd = fabs(db(lm) - db(lj));
				worstDb = dd > worstDb ? dd : worstDb;
				/* pitch over the first half of the unit, where the burst is strong */
				const double fm = zc_freq(mine->ch[0], a, a + unit / 2, RATE);
				const double fj = zc_freq(js->ch[0], a, a + unit / 2, RATE);
				const double rel = fabs(fm - fj) / fj;
				worstPitch = rel > worstPitch ? rel : worstPitch;
			}
		}
		printf("  %-8s loose: worst unit level diff %.2f dB, worst pitch diff %.1f %%\n", c->name, worstDb, 100.0 * worstPitch);
		CHECK(worstDb < 1.5);
		CHECK(worstPitch < 0.04);
	}
	remove(path);
	fg_sample_free(mine);
	fg_sample_free(js);
	free(s);
	return true;
}

int main(void) {
	for (size_t i = 0; i < sizeof CASES / sizeof CASES[0]; i++) {
		CHECK(run_case(&CASES[i]));
	}
	return check_report("parity_test");
}
