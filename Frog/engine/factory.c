#include "factory.h"
#include "edit.h"
#include "pattern.h"
#include "session.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One hit: adds `len` seconds of a drum voice at sample `at`. */
typedef enum { HIT_KICK, HIT_SNARE, HIT_HAT, HIT_CRASH } hit_kind;

static void render_hit(double* buf, int64_t frames, double sr, int64_t at, hit_kind kind, double vel, fg_rng* rng) {
	double len = kind == HIT_KICK ? 0.35 : kind == HIT_SNARE ? 0.25 : kind == HIT_HAT ? 0.08 : 1.2;
	const int64_t n = (int64_t)(len * sr);
	double prevNoise = 0.0;
	for (int64_t i = 0; i < n && at + i < frames; i++) {
		const double t = (double)i / sr;
		const double noise = fg_rng_unit(rng) * 2.0 - 1.0;
		const double hp = noise - prevNoise;   /* first difference: a crude high-pass */
		prevNoise = noise;
		double v = 0.0;
		switch (kind) {
			case HIT_KICK: {
				/* pitch sweep 160 → 50 Hz, the phase as its integral */
				const double k = 30.0;
				const double phase = 2.0 * FG_PI * (50.0 * t + (110.0 / k) * (1.0 - exp(-k * t)));
				v = sin(phase) * exp(-t * 9.0) + hp * 0.3 * exp(-t * 120.0);   /* body + a click */
				break;
			}
			case HIT_SNARE:
				v = sin(2.0 * FG_PI * 185.0 * t) * 0.5 * exp(-t * 22.0) + hp * 0.7 * exp(-t * 18.0);
				break;
			case HIT_HAT:
				v = hp * 0.45 * exp(-t * 60.0);
				break;
			case HIT_CRASH:
				v = hp * 0.5 * exp(-t * 3.5);
				break;
		}
		buf[at + i] += v * vel;
	}
}

fg_sample* fg_factory_clip(double sampleRate) {
	const double sr = sampleRate > 0.0 ? sampleRate : 48000.0;
	const double stepSec = 60.0 / FG_FACTORY_BPM / 4.0;   /* a sixteenth */
	const int64_t frames = (int64_t)llround(FG_FACTORY_BEATS * 60.0 / FG_FACTORY_BPM * sr);
	double* buf = (double*)calloc((size_t)frames, sizeof(double));
	if (!buf) {
		return NULL;
	}
	fg_rng rng;
	fg_rng_seed(&rng, 0xA3E7);
	/* the pattern on a sixteenth grid, four bars: a variation on the
	 * classic break (bars 1–2 alike, bar 3 pushes the second snare, bar 4
	 * displaces the kick and lands a crash) */
	static const struct { int step; hit_kind kind; double vel; } hits[] = {
		/* bar 1 */
		{0, HIT_KICK, 1.0}, {2, HIT_KICK, 0.8}, {4, HIT_SNARE, 1.0}, {7, HIT_SNARE, 0.9}, {9, HIT_SNARE, 0.35},
		{10, HIT_KICK, 1.0}, {12, HIT_SNARE, 1.0}, {15, HIT_SNARE, 0.35},
		/* bar 2 */
		{16, HIT_KICK, 1.0}, {18, HIT_KICK, 0.8}, {20, HIT_SNARE, 1.0}, {23, HIT_SNARE, 0.9}, {25, HIT_SNARE, 0.35},
		{26, HIT_KICK, 1.0}, {28, HIT_SNARE, 1.0}, {31, HIT_SNARE, 0.35},
		/* bar 3 */
		{32, HIT_KICK, 1.0}, {34, HIT_KICK, 0.8}, {36, HIT_SNARE, 1.0}, {39, HIT_SNARE, 0.9}, {41, HIT_SNARE, 0.35},
		{42, HIT_KICK, 1.0}, {45, HIT_SNARE, 1.0}, {47, HIT_SNARE, 0.35},
		/* bar 4 */
		{48, HIT_KICK, 1.0}, {50, HIT_KICK, 0.8}, {52, HIT_SNARE, 1.0}, {55, HIT_SNARE, 0.9}, {57, HIT_KICK, 1.0},
		{58, HIT_CRASH, 0.8}, {59, HIT_SNARE, 1.0}, {61, HIT_SNARE, 0.9}, {63, HIT_SNARE, 0.35},
	};
	for (int s = 0; s < FG_FACTORY_BEATS * 4; s += 2) {   /* hats on the eighths, accents on the beat */
		render_hit(buf, frames, sr, (int64_t)llround(s * stepSec * sr), HIT_HAT, (s % 4 == 0) ? 0.9 : 0.6, &rng);
	}
	for (size_t h = 0; h < sizeof hits / sizeof hits[0]; h++) {
		render_hit(buf, frames, sr, (int64_t)llround(hits[h].step * stepSec * sr), hits[h].kind, hits[h].vel, &rng);
	}
	double peak = 0.0;
	for (int64_t i = 0; i < frames; i++) {
		peak = fabs(buf[i]) > peak ? fabs(buf[i]) : peak;
	}
	const double g = peak > 0.0 ? 0.89 / peak : 1.0;   /* −1 dBFS */
	fg_sample* out = fg_sample_create(1, frames, sr);
	if (out) {
		for (int64_t i = 0; i < frames; i++) {
			out->ch[0][i] = (float)(buf[i] * g);
		}
	}
	free(buf);
	return out;
}

bool fg_factory_write_clip(const char* path) {
	fg_sample* s = fg_factory_clip(48000.0);
	if (!s) {
		return false;
	}
	double* d = (double*)malloc(sizeof(double) * (size_t)s->frames);
	if (!d) {
		fg_sample_free(s);
		return false;
	}
	for (int64_t i = 0; i < s->frames; i++) {
		d[i] = s->ch[0][i];
	}
	const double* ch[1] = {d};
	const bool ok = fg_sample_write_wav16(path, ch, 1, s->frames, 48000.0);
	free(d);
	fg_sample_free(s);
	return ok;
}

void fg_factory_session(fg_session* s, const char* samplePath, const char* fileName) {
	fg_session_init(s);
	s->masterBpm = FG_FACTORY_BPM;
	s->masterUserSet = true;
	fg_track_state* t = &s->tracks[0];
	t->bpm = FG_FACTORY_BPM;
	t->bpmManual = true;
	snprintf(t->samplePath, sizeof t->samplePath, "%s", samplePath);
	snprintf(t->fileName, sizeof t->fileName, "%s", fileName);
	int nextColor = fg_unit_count(&t->pattern);
	fg_edit_set_grid(&t->pattern, FG_FACTORY_BEATS, FG_FACTORY_DENOM, &nextColor);
	t->pattern.loop = true;
	fg_pattern_validate(&t->pattern);
}
