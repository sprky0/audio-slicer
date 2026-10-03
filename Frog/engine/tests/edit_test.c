/* edit_test — reset order around anchors, randomize invariants. */
#include "check.h"
#include "edit.h"

#include <string.h>

static fg_pattern P;

static void row(const double* srcs, int n) {
	fg_pattern_init(&P);
	P.nTiles = n;
	for (int i = 0; i < n; i++) {
		fg_tile_init(&P.tiles[i], srcs[i], 1.0, (int)srcs[i]);
	}
}

static bool is_perm_of_units(const fg_pattern* p, int U) {
	bool seen[FG_MAX_UNITS] = {false};
	for (int i = 0; i < p->nTiles; i++) {
		if (p->tiles[i].gap) {
			continue;
		}
		const int s = (int)p->tiles[i].src;
		if (s < 0 || s >= U || seen[s]) {
			return false;
		}
		seen[s] = true;
	}
	return true;
}

int main(void) {
	/* reset order: plain row sorts back */
	const double shuffled[8] = {3, 0, 5, 1, 7, 2, 6, 4};
	row(shuffled, 8);
	P.beats = 2; P.denom = 16;   /* U = 8 */
	P.tiles[1].gain = 1.5f;      /* src 0 carries a setting */
	fg_edit_reset_order(&P);
	for (int i = 0; i < 8; i++) {
		CHECK(P.tiles[i].src == (double)i);
	}
	CHECK(P.tiles[0].gain == 1.5f);   /* settings travel with the tile */

	/* anchors: a locked tile and a gap stay in place, runs sort between them */
	row(shuffled, 8);
	P.tiles[2].locked = true;          /* src 5 locked at index 2 */
	P.tiles[5].gap = true;             /* gap at index 5 */
	fg_edit_reset_order(&P);
	CHECK(P.tiles[0].src == 0.0 && P.tiles[1].src == 3.0);
	CHECK(P.tiles[2].locked && P.tiles[2].src == 5.0);
	CHECK(P.tiles[3].src == 1.0 && P.tiles[4].src == 7.0);
	CHECK(P.tiles[5].gap);
	CHECK(P.tiles[6].src == 4.0 && P.tiles[7].src == 6.0);

	/* randomize: level 0 never moves anything */
	const double natural[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
	row(natural, 16);
	fg_rng rng; fg_rng_seed(&rng, 42);
	CHECK(!fg_edit_randomize(&P, 0.0, &rng));
	for (int i = 0; i < 16; i++) {
		CHECK(P.tiles[i].src == (double)i);
	}
	/* level 1 moves something, keeps the multiset and the lock anchored */
	P.tiles[6].locked = true;
	P.tiles[11].locked = true;
	bool moved = false;
	for (int trial = 0; trial < 20; trial++) {
		row(natural, 16);
		P.tiles[6].locked = true;
		P.tiles[11].locked = true;
		if (fg_edit_randomize(&P, 1.0, &rng)) {
			moved = true;
		}
		CHECK(P.nTiles == 16);
		CHECK(is_perm_of_units(&P, 16));
		CHECK(P.tiles[6].locked && P.tiles[6].src == 6.0);
		CHECK(P.tiles[11].locked && P.tiles[11].src == 11.0);
		CHECK_NEAR(fg_pattern_sum_w(&P), 16.0, 0.0);
	}
	CHECK(moved);
	/* buckets of different widths never swap wholesale: with locks at 6 and
	 * 11 the buckets are 6 / 4 / 4 wide, so units 0..5 stay left of the first
	 * lock */
	for (int trial = 0; trial < 20; trial++) {
		row(natural, 16);
		P.tiles[6].locked = true;
		P.tiles[11].locked = true;
		fg_edit_randomize(&P, 1.0, &rng);
		for (int i = 0; i < 6; i++) {
			CHECK(P.tiles[i].src < 6.0);
		}
	}
	/* reset all rebuilds the pristine row and keeps mods */
	P.nMods = 1;
	P.mods[0].step = 3;
	fg_edit_reset_all(&P);
	CHECK(P.nTiles == 16 && P.tiles[6].locked == false && P.nMods == 1);

	return check_report("edit_test");
}
