/* midiclock_test — the external clock path through the engine: a jittered
 * 116 bpm MIDI clock locks tempo (±0.1 bpm) and phase (mean error under
 * 2 ms, as the browser version measured), Start and Stop drive the tracks,
 * Continue rejoins in phase, and the internal source ignores clock bytes. */
#include "check.h"
#include "engine_internal.h"
#include "midiclock.h"
#include "pattern.h"

#include <stdlib.h>
#include <string.h>

#define SR 48000.0
#define BLOCK 128

static double L[BLOCK], R[BLOCK];

/* process one block, feeding the pulses that fall inside it */
typedef struct {
	double nextPulse;     /* absolute sample of the next pulse (ideal) */
	double interval;      /* samples per pulse */
	fg_rng rng;
	double jitter;        /* ± samples of delivery jitter */
	int64_t pulses;
} clock_sim;

static void sim_block(fg_engine* e, clock_sim* c, int64_t blockStart, bool send) {
	while (c->nextPulse < blockStart + BLOCK) {
		const double j = (fg_rng_unit(&c->rng) * 2.0 - 1.0) * c->jitter;
		double at = c->nextPulse + j;
		if (at < blockStart) {
			at = blockStart;
		}
		if (at >= blockStart + BLOCK) {
			at = blockStart + BLOCK - 1;
		}
		if (send) {
			fg_engine_midi(e, 0xF8, (int)(at - blockStart));
		}
		c->nextPulse += c->interval;
		c->pulses++;
	}
	double* bufs[2] = {L, R};
	fg_engine_process(e, bufs, 2, BLOCK);
}

int main(void) {
	fg_engine* e = fg_engine_create(1, 3);
	fg_engine_reset(e, SR, BLOCK);
	fg_sample* s = fg_sample_create(1, 96000, SR);
	for (int i = 0; i < 16; i++) {
		s->ch[0][i * 6000] = 1.f;
	}
	fg_engine_set_sample(e, 0, s);
	fg_engine_set_clock_source(e, FG_CLOCK_MIDI);

	const double bpm = 116.0;
	clock_sim c = {0};
	c.interval = 60.0 / bpm / 24.0 * SR;   /* ~1034 samples */
	c.jitter = 0.002 * SR;                  /* ±2 ms delivery jitter (a block's worth on the appliance) */
	fg_rng_seed(&c.rng, 99);

	/* Start at sample 0 then pulses */
	fg_engine_midi(e, 0xFA, 0);
	c.nextPulse = 0.0;
	int64_t now = 0;
	/* 4 beats of settling */
	for (int b = 0; b < (int)(4 * 60.0 / bpm * SR / BLOCK); b++) {
		sim_block(e, &c, now, true);
		now += BLOCK;
	}
	CHECK(fg_engine_external_running(e));
	CHECK(e->tracks[0].seq.playing);
	CHECK_NEAR(fg_engine_external_bpm(e), bpm, 0.5);

	/* 16 more beats: measure the grid's phase error at each ideal pulse time */
	double sumErr = 0.0, maxErr = 0.0;
	int n = 0;
	for (int b = 0; b < (int)(16 * 60.0 / bpm * SR / BLOCK); b++) {
		const int64_t blockStart = now;
		sim_block(e, &c, now, true);
		now += BLOCK;
		/* ideal position of the pulse that just passed vs the grid */
		const double pulseBeat = (double)(c.pulses - 1) / 24.0;
		const double idealAt = (double)(c.pulses - 1) * c.interval;
		if (idealAt >= blockStart && idealAt < blockStart + BLOCK) {
			const double err = fabs(fg_grid_sample_at_beat(&e->grid, pulseBeat) - idealAt) / SR;
			sumErr += err;
			maxErr = err > maxErr ? err : maxErr;
			n++;
		}
	}
	CHECK(n > 300);
	printf("phase lock: mean %.2f ms, max %.2f ms over %d pulses; bpm %.3f\n", 1e3 * sumErr / n, 1e3 * maxErr, n, fg_engine_bpm(e));
	CHECK(sumErr / n < 0.002);
	CHECK(maxErr < 0.012);
	CHECK_NEAR(fg_engine_bpm(e), bpm, 0.5);   /* the window average wobbles with the jitter */

	/* Stop halts the tracks; Continue restarts them in phase */
	fg_engine_midi(e, 0xFC, 0);
	sim_block(e, &c, now, true);
	now += BLOCK;
	CHECK(!e->tracks[0].seq.playing);
	CHECK(!fg_engine_external_running(e));
	for (int b = 0; b < 40; b++) {
		sim_block(e, &c, now, true);
		now += BLOCK;
	}
	fg_engine_midi(e, 0xFB, 0);
	sim_block(e, &c, now, true);
	now += BLOCK;
	CHECK(e->tracks[0].seq.playing);
	CHECK(fg_engine_external_running(e));
	/* anchored at a loop boundary (multiple of 4 beats) at or before now */
	const double nowBeat = fg_grid_beat_at_sample(&e->grid, (double)now);
	CHECK(e->tracks[0].seq.anchorBeat <= nowBeat && fmod(e->tracks[0].seq.anchorBeat, 4.0) == 0.0);

	/* internal source: clock bytes are ignored, the tempo stays put */
	fg_engine_stop_all(e);
	fg_engine_set_clock_source(e, FG_CLOCK_INTERNAL);
	fg_engine_set_tempo(e, 100.0);
	sim_block(e, &c, now, false);
	now += BLOCK;
	for (int b = 0; b < 200; b++) {
		sim_block(e, &c, now, true);
		now += BLOCK;
	}
	CHECK_NEAR(fg_engine_bpm(e), 100.0, 0.0);
	CHECK(!e->tracks[0].seq.playing);

	/* the plain state machine: tempo estimate and events */
	fg_midiclock mc;
	fg_midiclock_init(&mc, SR);
	CHECK(fg_midiclock_status(&mc, 0xFA, 0.0) == FG_MC_START && mc.running);
	for (int k = 0; k < 30; k++) {
		fg_midiclock_status(&mc, 0xF8, k * 1000.0);
	}
	CHECK_NEAR(mc.bpm, 60.0 * SR / (1000.0 * 24.0), 1e-9);
	CHECK_NEAR(fg_midiclock_beat(&mc), 29.0 / 24.0, 1e-12);
	CHECK(fg_midiclock_status(&mc, 0xFC, 31000.0) == FG_MC_STOP && !mc.running);
	/* a long gap resets the averaging window */
	fg_midiclock_status(&mc, 0xF8, 31000.0 + SR);
	CHECK(mc.nStamps == 1);

	fg_engine_destroy(e);
	fg_sample_free(s);
	return check_report("midiclock_test");
}
