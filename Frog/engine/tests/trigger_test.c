/* trigger_test — MIDI note triggers: note 36 + n plays unit n of the track
 * on that channel at its natural rate, scaled by velocity, one-shot and
 * ringing out; notes for other tracks or outside the grid do nothing. */
#include "check.h"
#include "engine.h"
#include "pattern.h"

#include <stdlib.h>
#include <string.h>

#define SR 48000.0
#define BLOCK 128
#define UNIT 6000

static double L[48000], R[48000];

static fg_sample* make_source(void) {
	fg_sample* s = fg_sample_create(2, 16 * UNIT, SR);
	for (int i = 0; i < 16; i++) {
		for (int c = 0; c < 2; c++) {
			s->ch[c][i * UNIT] = (float)(i + 1) / 16.f;
			s->ch[c][i * UNIT + UNIT / 2] = -0.5f;
		}
	}
	return s;
}

static void render(fg_engine* e, int64_t frames) {
	memset(L, 0, sizeof L);
	memset(R, 0, sizeof R);
	for (int64_t at = 0; at < frames; at += BLOCK) {
		double* bufs[2] = {L + at, R + at};
		fg_engine_process(e, bufs, 2, BLOCK);
	}
}

int main(void) {
	fg_engine* e = fg_engine_create(2, 5);
	fg_engine_reset(e, SR, BLOCK);
	fg_sample* a = make_source();
	fg_sample* b = make_source();
	fg_engine_set_sample(e, 0, a);
	fg_engine_set_sample(e, 1, b);
	render(e, BLOCK);   /* the stores are picked up */

	/* unit 0 on track 1 (channel 1) at full velocity, offset 10 into the block */
	fg_engine_midi_msg(e, 0x90, 36, 127, 10);
	render(e, 2 * UNIT);
	CHECK_NEAR(L[10], 1.0 / 16.0, 1e-9);
	CHECK_NEAR(L[10 + UNIT / 2], -0.5, 1e-9);
	CHECK_NEAR(L[10 + UNIT], 0.0, 0.0);   /* one unit, then silence: no sequencer running */

	/* unit 5 on track 2 (channel 2) at half velocity */
	fg_engine_midi_msg(e, 0x91, 41, 64, 0);
	render(e, UNIT);
	CHECK_NEAR(L[0], 6.0 / 16.0 * (64.0 / 127.0), 1e-9);

	/* velocity 0 is a note off; out-of-range notes and channels do nothing */
	fg_engine_midi_msg(e, 0x90, 36, 0, 0);
	fg_engine_midi_msg(e, 0x90, 35, 100, 0);
	fg_engine_midi_msg(e, 0x90, 36 + 16, 100, 0);
	fg_engine_midi_msg(e, 0x95, 36, 100, 0);
	render(e, BLOCK);
	double peak = 0.0;
	for (int i = 0; i < BLOCK; i++) {
		peak = fabs(L[i]) > peak ? fabs(L[i]) : peak;
	}
	CHECK_NEAR(peak, 0.0, 0.0);

	/* a trigger while the sequencer runs adds to the loop */
	fg_engine_play_all(e);
	fg_engine_midi_msg(e, 0x90, 40, 127, 0);   /* unit 4 on top of both tracks' unit 0 slots */
	render(e, BLOCK);
	CHECK_NEAR(L[0], 2.0 * (1.0 / 16.0) + 5.0 / 16.0, 1e-9);

	/* visuals: the trigger's record has tileIndex −1 */
	fg_visual vis[64];
	const int n = fg_engine_poll_visuals(e, vis, 64);
	bool sawTrigger = false;
	for (int i = 0; i < n; i++) {
		if (vis[i].tileIndex == -1 && vis[i].src == 4.0) {
			sawTrigger = true;
		}
	}
	CHECK(sawTrigger);

	fg_engine_destroy(e);
	fg_sample_free(a);
	fg_sample_free(b);
	return check_report("trigger_test");
}
