/* session_test — a browser-version save imports field for field, survives a
 * write → parse round trip, and bad input is rejected or defaulted. */
#include "check.h"
#include "pattern.h"
#include "session.h"

#include <string.h>

int main(void) {
	fg_session* s = (fg_session*)calloc(1, sizeof(fg_session));
	fg_session* r = (fg_session*)calloc(1, sizeof(fg_session));
	CHECK(fg_session_load_file("fixtures/js-session-basic.json", s));

	CHECK(s->nTracks == 2);
	CHECK(s->nextTrackId == 2);
	CHECK_NEAR(s->masterBpm, 112.5, 0.0);
	CHECK(s->masterUserSet);
	CHECK(s->editMode == FG_EDIT_GAPS);
	CHECK(s->midiEnabled && strcmp(s->midiInputId, "input-1") == 0);

	const fg_track_state* t = &s->tracks[0];
	const fg_pattern* p = &t->pattern;
	CHECK(t->id == 0 && strcmp(t->fileName, "break.wav") == 0);
	CHECK_NEAR(t->bpm, 112.5, 0.0);
	CHECK_NEAR(t->mix.volume, 0.8, 0.0);
	CHECK_NEAR(t->mix.pan, -0.25, 0.0);
	CHECK(!t->mix.muted);
	CHECK(p->beats == 4 && p->denom == 8 && fg_unit_count(p) == 8);
	CHECK_NEAR(p->start, 0.1, 0.0);
	CHECK_NEAR(p->end, 0.9, 0.0);
	CHECK(p->masterPitch == 2 && p->randLevel == 70 && p->loop);
	CHECK(p->nTiles == 4);
	CHECK_NEAR(fg_pattern_sum_w(p), 8.0, 0.0);
	const fg_tile* b = &p->tiles[1];
	CHECK(b->src == 1.0 && b->w == 2.0 && b->offset == -3 && b->muted && b->locked && b->reversed);
	CHECK_NEAR(b->fadeIn, 0.25, 1e-7);
	CHECK_NEAR(b->fadeOut, 0.5, 1e-7);
	CHECK(b->curveIn == FG_CURVE_EXP && b->curveOut == FG_CURVE_S);
	CHECK_NEAR(b->gain, 1.5, 1e-7);
	CHECK(p->tiles[2].gap && p->tiles[2].w == 1.0);
	CHECK(p->tiles[0].gain == 1.f && p->tiles[0].curveIn == FG_CURVE_LINEAR);
	CHECK(p->nMods == 3);
	CHECK(p->mods[0].action == FG_MOD_MUTE && p->mods[0].fireMode == FG_FIRE_PROB && p->mods[0].fireValue == 50);
	const fg_mod* rt = &p->mods[1];
	CHECK(rt->step == 3 && rt->action == FG_MOD_RATCHET && rt->fireMode == FG_FIRE_EVERY && rt->fireValue == 2);
	CHECK(rt->mode == FG_RATCHET_RAMP && rt->subdiv == 2 && rt->subdivTo == 6 && rt->lenSteps == 3);
	CHECK(p->mods[2].action == FG_MOD_GAIN && p->mods[2].gainAmt == 30);

	/* the empty second track gets a default row, keeps its settings */
	const fg_track_state* u = &s->tracks[1];
	CHECK(u->fileName[0] == 0 && u->mix.muted);
	CHECK(u->pattern.beats == 2 && fg_unit_count(&u->pattern) == 8);
	CHECK(u->pattern.nTiles == 8 && !u->pattern.loop);

	/* round trip */
	char* json = fg_session_write(s);
	CHECK(json != NULL);
	CHECK(strstr(json, "\"format\":\t\"frog-session\"") != NULL || strstr(json, "\"format\": \"frog-session\"") != NULL);
	CHECK(fg_session_parse(json, r));
	CHECK(r->nTracks == 2 && r->editMode == FG_EDIT_GAPS);
	CHECK(memcmp(&r->tracks[0].pattern, &s->tracks[0].pattern, sizeof(fg_pattern)) == 0);
	CHECK(memcmp(&r->tracks[1].pattern, &s->tracks[1].pattern, sizeof(fg_pattern)) == 0);
	CHECK(strcmp(r->tracks[0].fileName, "break.wav") == 0);
	free(json);

	/* save / load through a file */
	CHECK(fg_session_save_file("/tmp/frog-session-test.json", s));
	memset(r, 0, sizeof *r);
	CHECK(fg_session_load_file("/tmp/frog-session-test.json", r));
	CHECK(r->nTracks == 2 && memcmp(&r->tracks[0].pattern, &s->tracks[0].pattern, sizeof(fg_pattern)) == 0);
	remove("/tmp/frog-session-test.json");

	/* malformed and empty inputs */
	CHECK(!fg_session_parse("{ not json", r));
	CHECK(!fg_session_parse("[1,2,3]", r));
	CHECK(fg_session_parse("{}", r));
	CHECK(r->nTracks == 1 && r->tracks[0].pattern.nTiles == 16 && r->nextTrackId == 1);
	/* a row that does not add up is rebuilt on import */
	CHECK(fg_session_parse("{\"slicers\":[{\"id\":3,\"beats\":4,\"divisionDenom\":16,\"seq\":{\"tiles\":[{\"src\":0,\"w\":5}]}}]}", r));
	CHECK(r->nTracks == 1 && r->tracks[0].pattern.nTiles == 16 && r->nextTrackId == 4);

	fg_session_init(r);
	CHECK(r->nTracks == 1 && r->tracks[0].pattern.nTiles == 16 && r->tracks[0].mix.volume == 1.0);

	free(s);
	free(r);
	return check_report("session_test");
}
