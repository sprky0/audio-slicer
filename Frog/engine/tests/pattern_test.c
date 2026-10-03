/* pattern_test — unit count, defaults, validation, modifier lookup, bar
 * position, ratchet hit layout (values match the browser version). */
#include "check.h"
#include "pattern.h"

#include <string.h>

static fg_pattern P;

int main(void) {
	fg_pattern_init(&P);
	CHECK(fg_unit_count(&P) == 16);
	CHECK_NEAR(fg_step_beats(&P), 0.25, 0.0);
	P.beats = 3; P.denom = 8;
	CHECK(fg_unit_count(&P) == 6);
	P.beats = 16; P.denom = 32;
	CHECK(fg_unit_count(&P) == 128);
	P.beats = 1; P.denom = 2;  /* 0.5 rounds to 1 */
	CHECK(fg_unit_count(&P) == 1);

	/* defaults: Σw = U, native order */
	P.beats = 4; P.denom = 16;
	fg_pattern_default_tiles(&P);
	CHECK(P.nTiles == 16);
	CHECK_NEAR(fg_pattern_sum_w(&P), 16.0, 0.0);
	CHECK(P.tiles[5].src == 5.0 && P.tiles[5].colorIdx == 5 && P.tiles[5].gain == 1.f);

	/* validate keeps a legal row, drops junk mods */
	P.tiles[3].w = 2.0; P.tiles[4].w = 0.5; P.tiles[5].w = 0.5;  /* still 16 */
	P.nMods = 3;
	memset(P.mods, 0, sizeof P.mods);
	P.mods[0].step = 2; P.mods[0].action = FG_MOD_GAIN; P.mods[0].gainAmt = 500;
	P.mods[1].step = 2; P.mods[1].action = FG_MOD_REV;   /* duplicate step: dropped */
	P.mods[2].step = 40; P.mods[2].action = FG_MOD_MUTE; /* out of range: dropped */
	CHECK(fg_pattern_validate(&P));
	CHECK(P.nTiles == 16);
	CHECK(P.nMods == 1 && P.mods[0].gainAmt == 200 && P.mods[0].action == FG_MOD_GAIN);
	CHECK(P.mods[0].fireValue == 0 && P.mods[0].subdiv == 1 && P.mods[0].lenSteps == 1);

	/* a broken row (Σw ≠ U) is rebuilt */
	P.tiles[0].w = 3.0;
	CHECK(!fg_pattern_validate(&P));
	CHECK(P.nTiles == 16 && P.tiles[0].w == 1.0);
	/* a tile overhanging the selection is rebuilt too */
	P.tiles[15].src = 15.5;
	CHECK(!fg_pattern_validate(&P));
	/* gaps are fine as long as widths add up */
	P.nTiles = 2;
	P.tiles[0].gap = true; P.tiles[0].w = 8.0;
	fg_tile_init(&P.tiles[1], 0.0, 8.0, 0);
	CHECK(fg_pattern_validate(&P));
	CHECK(P.nTiles == 2 && P.tiles[0].gap);

	/* modifier lookup over a span */
	fg_pattern_default_tiles(&P);
	P.nMods = 3;
	P.mods[0].step = 4; P.mods[1].step = 5; P.mods[2].step = 7;
	for (int i = 0; i < 3; i++) { P.mods[i].action = FG_MOD_MUTE; P.mods[i].fireValue = 100; P.mods[i].subdiv = 1; P.mods[i].lenSteps = 1; }
	const fg_mod* hits[8];
	CHECK(fg_mods_in_span(&P, 4.0, 1.0, hits, 8) == 1 && hits[0]->step == 4);
	CHECK(fg_mods_in_span(&P, 4.0, 2.0, hits, 8) == 2);
	CHECK(fg_mods_in_span(&P, 3.75, 1.0, hits, 8) == 1);   /* [3.75, 4.75) covers step 4 */
	CHECK(fg_mods_in_span(&P, 5.25, 1.75, hits, 8) == 0);   /* [5.25, 7) covers only step 6 */
	CHECK(fg_mods_in_span(&P, 5.25, 2.0, hits, 8) == 1 && hits[0]->step == 7);
	CHECK(fg_mods_in_span(&P, 6.0, 0.5, hits, 8) == 0);

	/* bar position */
	int li; double sib;
	fg_bar_pos(16.0, 16, &li, &sib);
	CHECK(li == 1 && sib == 0.0);
	fg_bar_pos(15.9999999, 16, &li, &sib);   /* float dust snaps */
	CHECK(li == 1 && sib == 0.0);
	fg_bar_pos(37.5, 16, &li, &sib);
	CHECK(li == 2 && sib == 5.5);

	/* firing */
	fg_rng rng; fg_rng_seed(&rng, 1);
	fg_mod every = {0}; every.fireMode = FG_FIRE_EVERY; every.fireValue = 3;
	CHECK(fg_mod_fires(&every, 0, &rng) && !fg_mod_fires(&every, 1, &rng) && !fg_mod_fires(&every, 2, &rng) && fg_mod_fires(&every, 3, &rng));
	fg_mod never = {0}; never.fireMode = FG_FIRE_PROB; never.fireValue = 0;
	fg_mod always = {0}; always.fireMode = FG_FIRE_PROB; always.fireValue = 100;
	int fired = 0;
	for (int i = 0; i < 1000; i++) { if (fg_mod_fires(&never, 0, &rng)) fired++; }
	CHECK(fired == 0);
	for (int i = 0; i < 1000; i++) { if (fg_mod_fires(&always, 0, &rng)) fired++; }
	CHECK(fired == 1000);
	fg_mod half = {0}; half.fireMode = FG_FIRE_PROB; half.fireValue = 50;
	fired = 0;
	for (int i = 0; i < 10000; i++) { if (fg_mod_fires(&half, 0, &rng)) fired++; }
	CHECK(fired > 4700 && fired < 5300);

	/* ratchet layouts */
	double steps[FG_MAX_HITS];
	fg_mod rt = {0}; rt.mode = FG_RATCHET_EVEN; rt.subdiv = 3;
	int n = fg_ratchet_hit_steps(&rt, 2.0, steps, FG_MAX_HITS);
	CHECK(n == 6);
	CHECK_NEAR(steps[1], 1.0 / 3.0, 1e-12);
	CHECK_NEAR(steps[5], 5.0 / 3.0, 1e-12);
	rt.mode = FG_RATCHET_RAMP; rt.subdiv = 1; rt.subdivTo = 4;
	n = fg_ratchet_hit_steps(&rt, 4.0, steps, FG_MAX_HITS);
	/* accelerates: gaps shrink */
	CHECK(n > 4 && steps[0] == 0.0);
	for (int k = 2; k < n; k++) { CHECK(steps[k] - steps[k - 1] < steps[k - 1] - steps[k - 2] + 1e-12); }
	CHECK_NEAR(steps[1], 1.0, 1e-12);   /* first gap at density 1 */
	rt.mode = FG_RATCHET_PITCH; rt.pitchStep = 12;
	CHECK_NEAR(fg_ratchet_hit_rate(&rt, 0), 1.0, 0.0);
	CHECK_NEAR(fg_ratchet_hit_rate(&rt, 1), 2.0, 1e-12);
	CHECK_NEAR(fg_ratchet_hit_rate(&rt, 10), 16.0, 1e-9);   /* clamped at +48 st */
	rt.pitchStep = -12;
	CHECK_NEAR(fg_ratchet_hit_rate(&rt, 2), 0.25, 1e-12);
	rt.mode = FG_RATCHET_EVEN; rt.subdiv = 8;
	CHECK(fg_ratchet_hit_steps(&rt, 16.0, steps, FG_MAX_HITS) == FG_MAX_HITS);

	return check_report("pattern_test");
}
