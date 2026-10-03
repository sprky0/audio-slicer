/* midimap_test — the CC binding table and its JSON. */
#include "check.h"
#include "midimap.h"

#include <stdlib.h>
#include <string.h>

int main(void) {
	fg_midimap m;
	fg_midimap_defaults(&m);
	CHECK(m.n == 26);
	/* omni: the same CC resolves on any channel */
	const fg_binding* b = fg_midimap_find(&m, 1, 7);
	CHECK(b && strcmp(b->target, "track.vol") == 0 && b->channel == 0);
	b = fg_midimap_find(&m, 16, 7);
	CHECK(b && strcmp(b->target, "track.vol") == 0);
	CHECK(fg_midimap_find(&m, 1, 99) == NULL);
	CHECK(fg_midimap_is_bound(&m, "pattern.randomize"));
	CHECK(!fg_midimap_is_bound(&m, "transport.bpm"));   /* hookable, no default */

	/* an explicit channel outranks the any-channel binding on the same CC */
	CHECK(fg_midimap_bind(&m, 3, 7, "master.gain"));
	b = fg_midimap_find(&m, 3, 7);
	CHECK(b && strcmp(b->target, "master.gain") == 0 && b->channel == 3);
	b = fg_midimap_find(&m, 4, 7);
	CHECK(b && strcmp(b->target, "track.vol") == 0);
	CHECK(!fg_midimap_is_bound(&m, "master.gain") || fg_midimap_find(&m, 1, 14) == NULL);   /* master.gain moved off CC 14 */
	CHECK(fg_midimap_find(&m, 1, 14) == NULL);

	/* rebinding a target drops its old row; binding onto an occupied slot evicts */
	const int n0 = m.n;
	CHECK(fg_midimap_bind(&m, 0, 7, "track.pan"));   /* evicts track.vol (same slot) and track.pan's old CC 10 */
	CHECK(m.n == n0 - 1);
	CHECK(!fg_midimap_is_bound(&m, "track.vol"));
	CHECK(fg_midimap_find(&m, 1, 10) == NULL);
	b = fg_midimap_find(&m, 1, 7);
	CHECK(b && strcmp(b->target, "track.pan") == 0);
	CHECK(fg_midimap_unbind(&m, "track.pan"));
	CHECK(!fg_midimap_unbind(&m, "track.pan"));
	CHECK(!fg_midimap_bind(&m, 17, 7, "x"));
	CHECK(!fg_midimap_bind(&m, 0, 128, "x"));
	CHECK(!fg_midimap_bind(&m, 0, 1, ""));

	/* JSON round trip */
	fg_midimap_defaults(&m);
	CHECK(fg_midimap_bind(&m, 5, 40, "slice.split"));
	char* json = fg_midimap_write(&m);
	CHECK(json && strstr(json, "\"format\":\t\"frog-midimap\"") != NULL);
	fg_midimap m2;
	CHECK(fg_midimap_parse(json, &m2));
	CHECK(m2.n == m.n);
	for (int i = 0; i < m.n; i++) {
		CHECK(m.b[i].cc == m2.b[i].cc && m.b[i].channel == m2.b[i].channel && strcmp(m.b[i].target, m2.b[i].target) == 0);
	}
	free(json);

	/* hand-written: channel omitted or "any", junk rows skipped, unknown fields ignored */
	const char* hand =
	    "{\"format\":\"frog-midimap\",\"formatVersion\":1,\"note\":\"mine\",\"bindings\":["
	    "{\"cc\":1,\"target\":\"transport.bpm\"},"
	    "{\"cc\":2,\"channel\":\"any\",\"target\":\"track.vol\"},"
	    "{\"cc\":3,\"channel\":9,\"target\":\"track.pan\"},"
	    "{\"target\":\"no.cc\"},"
	    "{\"cc\":4}]}";
	CHECK(fg_midimap_parse(hand, &m2));
	CHECK(m2.n == 3);
	CHECK(m2.b[0].channel == 0 && m2.b[1].channel == 0 && m2.b[2].channel == 9);
	CHECK(!fg_midimap_parse("{\"format\":\"other\",\"bindings\":[]}", &m2));
	CHECK(m2.n == 0);
	CHECK(!fg_midimap_parse("nonsense", &m2));

	/* file */
	const char* path = "/tmp/frog-midimap-test.json";
	fg_midimap_defaults(&m);
	CHECK(fg_midimap_save_file(path, &m));
	CHECK(fg_midimap_load_file(path, &m2));
	CHECK(m2.n == m.n);
	remove(path);
	CHECK(!fg_midimap_load_file("/nonexistent/midimap.json", &m2));

	return check_report("midimap_test");
}
