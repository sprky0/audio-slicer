/* record_test — the capture lane: arm, inputs appended block by block up to
 * the capacity, stop handshake, take the trimmed sample. */
#include "check.h"
#include "engine.h"

#include <stdlib.h>
#include <string.h>

#define SR 48000.0
#define BLOCK 128

int main(void) {
	fg_engine* e = fg_engine_create(1, 2);
	fg_engine_reset(e, SR, BLOCK);
	double l[BLOCK], r[BLOCK], ol[BLOCK], orr[BLOCK];
	const double* in[2] = {l, r};
	double* out[2] = {ol, orr};

	CHECK(!fg_engine_recording(e));
	CHECK(fg_engine_record_take(e, NULL) == NULL);
	/* inputs while idle are ignored */
	for (int i = 0; i < BLOCK; i++) {
		l[i] = 0.5;
		r[i] = -0.5;
	}
	fg_engine_capture(e, in, 2, BLOCK);
	fg_engine_process(e, out, 2, BLOCK);
	CHECK(fg_engine_record_frames(e) == 0);

	/* arm for 0.01 s = 480 frames, feed 5 blocks: capped at 480 */
	CHECK(fg_engine_record_arm(e, 0, 0.01));
	CHECK(!fg_engine_record_arm(e, 0, 1.0));   /* one at a time */
	CHECK(fg_engine_recording(e));
	for (int b = 0; b < 5; b++) {
		for (int i = 0; i < BLOCK; i++) {
			l[i] = (b * BLOCK + i) / 1000.0;
			r[i] = -l[i];
		}
		fg_engine_capture(e, in, 2, BLOCK);
		fg_engine_process(e, out, 2, BLOCK);
	}
	CHECK(fg_engine_record_frames(e) == 480);
	fg_engine_record_stop(e);
	CHECK(fg_engine_recording(e) && !fg_engine_record_done(e));
	fg_engine_capture(e, in, 2, BLOCK);   /* the audio thread acknowledges */
	fg_engine_process(e, out, 2, BLOCK);
	CHECK(fg_engine_record_done(e));
	int track = -1;
	fg_sample* s = fg_engine_record_take(e, &track);
	CHECK(s != NULL && track == 0 && s->frames == 480 && s->nCh == 2);
	if (s) {
		CHECK_NEAR(s->ch[0][0], 0.0, 1e-9);
		CHECK_NEAR(s->ch[0][479], 0.479, 1e-6);
		CHECK_NEAR(s->ch[1][300], -0.300, 1e-6);
		fg_sample_free(s);
	}
	CHECK(!fg_engine_recording(e) && fg_engine_record_take(e, NULL) == NULL);

	/* mono input duplicates; a stop with nothing captured takes NULL */
	CHECK(fg_engine_record_arm(e, 0, 1.0));
	fg_engine_record_stop(e);
	fg_engine_capture(e, in, 1, BLOCK);
	CHECK(fg_engine_record_done(e) && fg_engine_record_take(e, NULL) == NULL);
	CHECK(fg_engine_record_arm(e, 0, 1.0));
	fg_engine_capture(e, in, 1, 10);
	fg_engine_record_stop(e);
	fg_engine_capture(e, in, 1, 10);
	s = fg_engine_record_take(e, &track);
	CHECK(s && s->frames == 10 && s->ch[1][3] == s->ch[0][3]);
	fg_sample_free(s);

	fg_engine_destroy(e);
	return check_report("record_test");
}
