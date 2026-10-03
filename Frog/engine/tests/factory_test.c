/* factory_test — the generated first-boot clip and session. */
#include "check.h"
#include "factory.h"
#include "pattern.h"
#include "session.h"

#include <stdlib.h>
#include <string.h>

static double peak_in(const fg_sample* s, int64_t from, int64_t to) {
	double p = 0.0;
	for (int64_t i = from; i < to && i < s->frames; i++) {
		p = fabs(s->ch[0][i]) > p ? fabs(s->ch[0][i]) : p;
	}
	return p;
}

int main(void) {
	fg_sample* a = fg_factory_clip(48000.0);
	CHECK(a != NULL);
	const int64_t frames = (int64_t)llround(16.0 * 60.0 / 136.0 * 48000.0);
	CHECK(a->frames == frames);
	CHECK(a->nCh == 1);
	CHECK_NEAR(peak_in(a, 0, a->frames), 0.89, 1e-3);
	/* deterministic */
	fg_sample* b = fg_factory_clip(48000.0);
	CHECK(b && b->frames == a->frames && memcmp(a->ch[0], b->ch[0], sizeof(float) * (size_t)a->frames) == 0);
	fg_sample_free(b);
	/* a kick on each bar's downbeat, and the bar 4 crash rings into the tail */
	const double bar = 4.0 * 60.0 / 136.0 * 48000.0;
	for (int k = 0; k < 4; k++) {
		const int64_t at = (int64_t)llround(k * bar);
		CHECK(peak_in(a, at, at + 2000) > 0.5);
	}
	CHECK(peak_in(a, a->frames - 4000, a->frames) > 0.02);
	fg_sample_free(a);

	fg_session* s = (fg_session*)calloc(1, sizeof(fg_session));
	fg_factory_session(s, "amen-variation.wav", "amen-variation.wav");
	CHECK(s->nTracks == 1);
	CHECK_NEAR(s->masterBpm, 136.0, 0.0);
	CHECK(s->tracks[0].pattern.beats == 16 && s->tracks[0].pattern.denom == 8);
	CHECK(fg_unit_count(&s->tracks[0].pattern) == 32);
	CHECK(s->tracks[0].pattern.nTiles == 32);
	CHECK_NEAR(fg_pattern_sum_w(&s->tracks[0].pattern), 32.0, 1e-9);
	CHECK(s->tracks[0].pattern.loop);
	CHECK(strcmp(s->tracks[0].samplePath, "amen-variation.wav") == 0);
	/* straight: every slice reads its own unit */
	bool straight = true;
	for (int i = 0; i < s->tracks[0].pattern.nTiles; i++) {
		const fg_tile* t = &s->tracks[0].pattern.tiles[i];
		straight = straight && !t->gap && !t->muted && !t->reversed && fabs(t->src - i) < 1e-9 && fabs(t->w - 1.0) < 1e-9;
	}
	CHECK(straight);
	/* round-trips through the session JSON */
	char* json = fg_session_write(s);
	fg_session* s2 = (fg_session*)calloc(1, sizeof(fg_session));
	CHECK(json && fg_session_parse(json, s2));
	CHECK(s2->tracks[0].pattern.nTiles == 32 && fabs(s2->masterBpm - 136.0) < 1e-9);
	free(json);
	free(s2);
	free(s);

	const char* path = "/tmp/frog-factory-test.wav";
	CHECK(fg_factory_write_clip(path));
	fg_sample* r = fg_sample_load_wav(path, 0.0, 0.0);
	CHECK(r && r->frames == frames);
	fg_sample_free(r);
	remove(path);
	return check_report("factory_test");
}
