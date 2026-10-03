/* render_test — the offline bounce's length choices: one loop is the LCM
 * of the tracks' bars, N loops are N times that. */
#include "check.h"
#include "render.h"
#include "sample.h"
#include "session.h"

#include <stdlib.h>
#include <string.h>

int main(void) {
	fg_session* s = (fg_session*)calloc(1, sizeof(fg_session));
	CHECK(fg_session_load_file("fixtures/clicks-session.json", s));
	CHECK_NEAR(fg_render_loop_beats(s), 4.0, 0.0);
	/* a second track with a 6-beat bar: the loop is their LCM */
	s->nTracks = 2;
	s->tracks[1] = s->tracks[0];
	s->tracks[1].pattern.beats = 6;
	CHECK_NEAR(fg_render_loop_beats(s), 12.0, 0.0);
	s->nTracks = 1;

	fg_render_opts o = {0};
	o.sampleRate = 48000.0;
	o.samplesDir = "fixtures";
	fg_render_stats one = {0}, two = {0};
	CHECK(fg_render_session(s, &o, "/tmp/frog-render-test-1.wav", &one));
	o.beats = 2.0 * fg_render_loop_beats(s);
	CHECK(fg_render_session(s, &o, "/tmp/frog-render-test-2.wav", &two));
	CHECK(one.frames == (int64_t)(4.0 * 60.0 / 120.0 * 48000.0));
	CHECK(llabs(two.frames - 2 * one.frames) <= 1);
	/* the second loop repeats the first (no probability modifiers in the fixture) */
	fg_sample* a = fg_sample_load_wav("/tmp/frog-render-test-1.wav", 0.0, 0.0);
	fg_sample* b = fg_sample_load_wav("/tmp/frog-render-test-2.wav", 0.0, 0.0);
	CHECK(a && b && b->frames >= 2 * a->frames - 1);
	if (a && b) {
		double maxDiff = 0.0;
		for (int64_t i = 0; i < a->frames; i++) {
			const double d = fabs((double)b->ch[0][i] - (double)a->ch[0][i]);
			maxDiff = d > maxDiff ? d : maxDiff;
		}
		CHECK(maxDiff < 1e-3);   /* both normalised to the same peak */
	}
	fg_sample_free(b);
	/* 24-bit: the same signal at finer steps */
	o.beats = 0.0;
	o.bits = 24;
	fg_render_stats st24 = {0};
	CHECK(fg_render_session(s, &o, "/tmp/frog-render-test-24.wav", &st24));
	fg_sample* c = fg_sample_load_wav("/tmp/frog-render-test-24.wav", 0.0, 0.0);
	CHECK(c && a && c->frames == a->frames);
	if (c && a) {
		double maxDiff = 0.0;
		for (int64_t i = 0; i < a->frames; i++) {
			const double d = fabs((double)c->ch[0][i] - (double)a->ch[0][i]);
			maxDiff = d > maxDiff ? d : maxDiff;
		}
		CHECK(maxDiff < 2.0 / 32767.0);   /* within two 16-bit steps of the 16-bit file (its rounding and the /32768 read scale) */
	}
	fg_sample_free(c);
	fg_sample_free(a);
	remove("/tmp/frog-render-test-24.wav");
	remove("/tmp/frog-render-test-1.wav");
	remove("/tmp/frog-render-test-2.wav");
	free(s);
	return check_report("render_test");
}
