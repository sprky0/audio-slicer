/* bench_stretch — CPU cost of the engine with N tracks all time-stretching
 * (tempo away from natural), as a realtime ratio. Not a pass / fail test:
 * run-tests.sh skips it; build it with the same line and run it on the
 * target (the F7.4 measurement on the Pi 3B):
 *
 *   cc -std=c11 -O2 -Iengine engine/tests/bench_stretch.c engine/<every .c> engine/third_party/cJSON.c -lm -o bench
 *   ./bench [tracks=1] [seconds=10] [bpm=100] [block=128]
 *
 * Every track plays a 1/16 row of a 2 s noise+sine sample at `bpm` (natural
 * is 120), so each slot is a stretched voice; the report is wall time per
 * second of audio, per track. Prints the share of one core at the end. */
#include "engine.h"
#include "frog_types.h"
#include "pattern.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SR 48000.0

static double now_sec(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main(int argc, char** argv) {
	const int tracks = argc > 1 ? atoi(argv[1]) : 1;
	const double seconds = argc > 2 ? atof(argv[2]) : 10.0;
	const double bpm = argc > 3 ? atof(argv[3]) : 100.0;
	const int block = argc > 4 ? atoi(argv[4]) : 128;

	fg_engine* e = fg_engine_create(tracks, 9);
	fg_engine_reset(e, SR, block);
	fg_sample* samples[FG_MAX_TRACKS] = {0};
	fg_rng rng;
	fg_rng_seed(&rng, 123);
	for (int t = 0; t < e->nTracks; t++) {
		fg_sample* s = fg_sample_create(2, (int64_t)(2.0 * SR), SR);
		for (int64_t i = 0; i < s->frames; i++) {
			const double v = 0.3 * sin(2.0 * FG_PI * (110.0 + 55.0 * t) * (double)i / SR) + 0.2 * (fg_rng_unit(&rng) * 2.0 - 1.0);
			s->ch[0][i] = (float)v;
			s->ch[1][i] = (float)(v * 0.8);
		}
		samples[t] = s;
		fg_engine_set_sample(e, t, s);
		fg_pattern* p = fg_engine_pattern(e, t);
		fg_pattern_default_tiles(p);
		p->tiles[1].reversed = true;
		p->tiles[2].fadeIn = 0.2f;
		p->tiles[2].fadeOut = 0.2f;
		p->tiles[3].offset = (int8_t)(t % 2 ? 3 : -4);   /* pitch on top of the fill */
		fg_engine_publish(e, t);
	}
	fg_engine_set_tempo(e, bpm);
	fg_engine_play_all(e);

	double* L = (double*)calloc((size_t)block, sizeof(double));
	double* R = (double*)calloc((size_t)block, sizeof(double));
	double* bufs[2] = {L, R};
	const int64_t frames = (int64_t)(seconds * SR);
	const double t0 = now_sec();
	double worst = 0.0;
	for (int64_t at = 0; at < frames; at += block) {
		const double b0 = now_sec();
		fg_engine_process(e, bufs, 2, block);
		const double dt = now_sec() - b0;
		worst = dt > worst ? dt : worst;
	}
	const double wall = now_sec() - t0;
	const double blockSec = block / SR;
	printf("bench_stretch: %d track(s), %.1f s at %.0f bpm (natural 120), block %d\n", e->nTracks, seconds, bpm, block);
	printf("  wall %.3f s = %.2f%% of one core (%.1fx realtime); worst block %.3f ms = %.1f%% of a %.2f ms period\n",
	       wall, 100.0 * wall / seconds, seconds / wall, worst * 1e3, 100.0 * worst / blockSec, blockSec * 1e3);
	printf("  per track: %.2f%% of one core\n", 100.0 * wall / seconds / e->nTracks);

	free(L);
	free(R);
	fg_engine_destroy(e);
	for (int t = 0; t < FG_MAX_TRACKS; t++) {
		fg_sample_free(samples[t]);
	}
	return 0;
}
