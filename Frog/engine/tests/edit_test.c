/* edit_test — reset order around anchors, randomize invariants, packed
 * reorder and boundary-eating resize, split / merge, refill, dup, gaps-mode
 * move and resize, and grid changes with positional inheritance. */
#include "check.h"
#include "edit.h"

#include <string.h>

static fg_pattern P, O;

static void row(const double* srcs, int n) {
	fg_pattern_init(&P);
	P.beats = (n * 4) / 16 > 0 ? (n * 4) / 16 : 1;
	P.denom = 16;
	P.nTiles = n;
	for (int i = 0; i < n; i++) {
		fg_tile_init(&P.tiles[i], srcs[i], 1.0, (int)srcs[i]);
	}
}

static void natural16(void) {
	static const double nat[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
	row(nat, 16);
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
	P.beats = 2;
	P.tiles[1].gain = 1.5f;
	fg_edit_reset_order(&P);
	for (int i = 0; i < 8; i++) {
		CHECK(P.tiles[i].src == (double)i);
	}
	CHECK(P.tiles[0].gain == 1.5f);

	/* anchors: a locked tile and a gap stay in place, runs sort between them */
	row(shuffled, 8);
	P.beats = 2;
	P.tiles[2].locked = true;
	P.tiles[5].gap = true;
	fg_edit_reset_order(&P);
	CHECK(P.tiles[0].src == 0.0 && P.tiles[1].src == 3.0);
	CHECK(P.tiles[2].locked && P.tiles[2].src == 5.0);
	CHECK(P.tiles[3].src == 1.0 && P.tiles[4].src == 7.0);
	CHECK(P.tiles[5].gap);
	CHECK(P.tiles[6].src == 4.0 && P.tiles[7].src == 6.0);

	/* randomize: level 0 never moves anything; level 1 keeps the multiset and locks */
	natural16();
	fg_rng rng;
	fg_rng_seed(&rng, 42);
	CHECK(!fg_edit_randomize(&P, 0.0, &rng));
	bool moved = false;
	for (int trial = 0; trial < 20; trial++) {
		natural16();
		P.tiles[6].locked = true;
		P.tiles[11].locked = true;
		if (fg_edit_randomize(&P, 1.0, &rng)) {
			moved = true;
		}
		CHECK(P.nTiles == 16 && is_perm_of_units(&P, 16));
		CHECK(P.tiles[6].locked && P.tiles[6].src == 6.0);
		CHECK(P.tiles[11].locked && P.tiles[11].src == 11.0);
		for (int i = 0; i < 6; i++) {
			CHECK(P.tiles[i].src < 6.0);   /* 6 / 4 / 4 wide buckets never swap wholesale */
		}
	}
	CHECK(moved);

	/* move: packed reorder by index */
	natural16();
	CHECK(fg_edit_move(&P, 0, 3));
	CHECK(P.tiles[0].src == 1.0 && P.tiles[2].src == 3.0 && P.tiles[3].src == 0.0 && P.tiles[4].src == 4.0);
	CHECK(fg_edit_move(&P, 3, 0));
	CHECK(P.tiles[0].src == 0.0 && P.tiles[3].src == 3.0);
	CHECK(!fg_edit_move(&P, 2, 2) && !fg_edit_move(&P, -1, 2));

	/* locks are pinned in packed mode too (F20): not dragged, not crossed,
	 * a wall for the resize cascade, never merged */
	natural16();
	P.tiles[5].locked = true;
	CHECK(!fg_edit_move(&P, 5, 2));
	CHECK(!fg_edit_move(&P, 2, 7));
	CHECK(fg_edit_move(&P, 1, 3) && P.tiles[5].locked && P.tiles[5].src == 5.0);
	natural16();
	P.tiles[6].locked = true;
	fg_edit_resize_end(&P, 4, 3.0, &O);   /* eats tile 5 only */
	CHECK(O.tiles[4].w == 2.0 && O.tiles[5].locked && O.tiles[5].src == 6.0 && O.tiles[5].w == 1.0);
	CHECK_NEAR(fg_pattern_sum_w(&O), 16.0, 1e-9);
	fg_edit_resize_end(&P, 6, 1.0, &O);
	CHECK(O.tiles[6].w == 1.0 && O.tiles[7].w == 1.0);
	fg_edit_resize_start(&P, 7, -2.0, &O);
	CHECK(O.tiles[7].w == 1.0 && O.tiles[6].w == 1.0);
	CHECK(!fg_edit_merge(&P, 6) && !fg_edit_merge(&P, 7) && fg_edit_merge(&P, 3));
	/* a grid change to the same unit count keeps the arrangement and the modifiers */
	natural16();
	fg_edit_move(&P, 0, 5);
	P.nMods = 1;
	memset(&P.mods[0], 0, sizeof(fg_mod));
	P.mods[0].step = 3;
	P.mods[0].action = FG_MOD_MUTE;
	P.mods[0].fireValue = 100;
	{
		int nc = 16;
		fg_edit_set_grid(&P, 8, 8, &nc);   /* 16 → 16 units */
		CHECK(P.beats == 8 && P.denom == 8 && P.tiles[5].src == 0.0 && P.nMods == 1 && P.mods[0].step == 3);
	}

	/* resize end: growing eats the next tiles' heads, cascading; src pinned */
	natural16();
	fg_edit_resize_end(&P, 2, 1.5, &O);
	CHECK_NEAR(fg_pattern_sum_w(&O), 16.0, 1e-9);
	CHECK(O.tiles[2].src == 2.0 && O.tiles[2].w == 2.5);
	CHECK(O.nTiles == 15);                             /* tile 3 was fully absorbed */
	CHECK(O.tiles[3].src == 4.5 && O.tiles[3].w == 0.5); /* tile 4 lost half its head */
	CHECK(O.tiles[4].src == 5.0 && O.tiles[4].w == 1.0);
	/* shrinking the end hands width to the next tile's tail */
	fg_edit_resize_end(&P, 2, -0.5, &O);
	CHECK(O.tiles[2].w == 0.5 && O.tiles[3].src == 3.0 && O.tiles[3].w == 1.5);
	CHECK_NEAR(fg_pattern_sum_w(&O), 16.0, 1e-9);
	/* bounded: the dragged tile keeps ≥ 1/4 unit, the last tile's end is pinned */
	fg_edit_resize_end(&P, 2, -5.0, &O);
	CHECK(O.tiles[2].w == 0.25);
	fg_edit_resize_end(&P, 15, 1.0, &O);
	CHECK(memcmp(&O, &P, sizeof P) == 0);
	/* growing beyond the source: tile 14 (src 14) can grow by at most 1 unit */
	fg_edit_resize_end(&P, 14, 3.0, &O);
	CHECK(O.tiles[14].w == 2.0 && O.nTiles == 15);

	/* resize start: growing back truncates the previous tails, src pinned;
	 * shrinking eats this tile's own head */
	natural16();
	fg_edit_resize_start(&P, 5, -1.5, &O);
	CHECK_NEAR(fg_pattern_sum_w(&O), 16.0, 1e-9);
	CHECK(O.nTiles == 15);
	CHECK(O.tiles[3].src == 3.0 && O.tiles[3].w == 0.5);   /* tile 4 absorbed, tile 3 truncated */
	CHECK(O.tiles[4].src == 5.0 && O.tiles[4].w == 2.5);   /* the grown tile */
	fg_edit_resize_start(&P, 5, 0.5, &O);
	CHECK(O.tiles[5].src == 5.5 && O.tiles[5].w == 0.5 && O.tiles[4].w == 1.5);
	fg_edit_resize_start(&P, 0, -1.0, &O);
	CHECK(memcmp(&O, &P, sizeof P) == 0);

	/* split / merge */
	natural16();
	P.tiles[4].w = 4.0;
	P.nTiles = 13;
	for (int i = 5; i < 13; i++) {
		P.tiles[i] = P.tiles[i + 3];
	}
	CHECK_NEAR(fg_pattern_sum_w(&P), 16.0, 1e-9);
	P.tiles[4].fadeIn = 0.3f;
	P.tiles[4].fadeOut = 0.4f;
	P.tiles[4].locked = true;
	CHECK(fg_edit_split(&P, 4, 99));
	CHECK(P.nTiles == 14 && P.tiles[4].w == 2.0 && P.tiles[5].w == 2.0 && P.tiles[5].src == 6.0);
	CHECK(P.tiles[4].fadeIn == 0.3f && P.tiles[4].fadeOut == 0.f && P.tiles[5].fadeIn == 0.f && P.tiles[5].fadeOut == 0.4f);
	CHECK(P.tiles[5].colorIdx == 99 && !P.tiles[4].locked && !P.tiles[5].locked);
	CHECK(!fg_edit_split(&P, 0, 1));   /* w = 1: too narrow */
	CHECK(fg_edit_merge(&P, 5));
	CHECK(P.nTiles == 13 && P.tiles[4].w == 4.0 && P.tiles[4].src == 4.0);
	CHECK_NEAR(fg_pattern_sum_w(&P), 16.0, 1e-9);
	CHECK(fg_edit_merge(&P, 0));   /* first tile folds into the right one */
	CHECK(P.nTiles == 12 && P.tiles[0].src == 0.0 && P.tiles[0].w == 2.0);
	P.tiles[1].gap = true;
	CHECK(!fg_edit_merge(&P, 2));  /* across a gap: refused */

	/* refill */
	natural16();
	fg_edit_move(&P, 0, 5);
	P.tiles[5].muted = true;
	CHECK(fg_edit_refill(&P, 5));
	CHECK(P.tiles[5].src == 5.0 && !P.tiles[5].muted && P.tiles[5].colorIdx == 5);
	P.tiles[2].locked = true;
	P.tiles[2].src = 9.0;
	P.tiles[3].src = 2.0;
	fg_edit_refill_all(&P);
	CHECK(P.tiles[2].src == 9.0 && P.tiles[2].locked && P.tiles[3].src == 3.0);

	/* dup: a copy after / before, blocked by a lock, truncated at the bar edge */
	natural16();
	P.tiles[3].gain = 1.5f;
	int c = fg_edit_dup(&P, 3, +1);
	CHECK(c == 4 && P.tiles[4].src == 3.0 && P.tiles[4].gain == 1.5f && P.tiles[4].w == 1.0 && P.nTiles == 16);
	CHECK_NEAR(fg_pattern_sum_w(&P), 16.0, 1e-9);
	natural16();
	P.tiles[4].locked = true;
	CHECK(fg_edit_dup(&P, 3, +1) == -1);
	natural16();
	P.tiles[0].w = 2.0;
	P.nTiles = 15;
	for (int i = 1; i < 15; i++) {
		P.tiles[i] = P.tiles[i + 1];
	}
	c = fg_edit_dup(&P, 0, -1);   /* no room before the first tile */
	CHECK(c == -1);
	c = fg_edit_dup(&P, 14, +1);  /* the last tile: no room after */
	CHECK(c == -1);

	/* gaps mode: move leaves a gap, overwrites the target */
	natural16();
	CHECK(fg_edit_move_gaps(&P, 2, 6.5));
	CHECK_NEAR(fg_pattern_sum_w(&P), 16.0, 1e-9);
	CHECK(P.tiles[2].gap && P.tiles[2].w == 1.0);
	/* at 6.5: half of unit 6 and half of unit 7 overwritten */
	CHECK(P.tiles[6].src == 6.0 && P.tiles[6].w == 0.5);
	CHECK(P.tiles[7].src == 2.0 && P.tiles[7].w == 1.0);
	CHECK(P.tiles[8].src == 7.5 && P.tiles[8].w == 0.5);
	natural16();
	P.tiles[6].locked = true;
	CHECK(!fg_edit_move_gaps(&P, 2, 6.0));

	/* gaps mode resize: shrinking the end leaves a gap, growing overwrites,
	 * a locked clip stops the growth */
	natural16();
	fg_edit_resize_gaps(&P, 4, true, -0.5, &O);
	CHECK(O.tiles[4].w == 0.5 && O.tiles[5].gap && O.tiles[5].w == 0.5 && O.tiles[6].src == 5.0);
	CHECK_NEAR(fg_pattern_sum_w(&O), 16.0, 1e-9);
	fg_edit_resize_gaps(&P, 4, true, 2.0, &O);
	CHECK(O.tiles[4].w == 3.0 && O.tiles[5].src == 7.0);
	P.tiles[6].locked = true;
	fg_edit_resize_gaps(&P, 4, true, 3.0, &O);
	CHECK(O.tiles[4].w == 2.0 && O.tiles[5].locked);
	P.tiles[6].locked = false;
	fg_edit_resize_gaps(&P, 4, false, -0.5, &O);   /* start grows back over tile 3's tail */
	CHECK(O.tiles[3].w == 0.5 && O.tiles[4].src == 4.0 && O.tiles[4].w == 1.5);
	fg_edit_resize_gaps(&P, 4, false, 0.5, &O);    /* start shrinks: head eaten, gap in front */
	CHECK(O.tiles[4].gap && O.tiles[4].w == 0.5 && O.tiles[5].src == 4.5 && O.tiles[5].w == 0.5);

	/* grid change: settings inherit positionally, locks keep audio, mods rescale */
	natural16();
	P.tiles[1].muted = true;
	P.tiles[2].reversed = true;
	P.tiles[5].locked = true;
	P.tiles[5].src = 9.0;
	P.tiles[9].src = 5.0;
	P.nMods = 2;
	P.mods[0] = (fg_mod){4, FG_MOD_MUTE, FG_FIRE_PROB, 0, 100, 0, 1, 1, 0, 1};
	P.mods[1] = (fg_mod){15, FG_MOD_REV, FG_FIRE_PROB, 0, 100, 0, 1, 1, 0, 1};
	int nextColor = 0;
	fg_edit_set_grid(&P, 4, 32, &nextColor);   /* 16 → 32 units */
	CHECK(fg_unit_count(&P) == 32 && nextColor == 32);
	CHECK_NEAR(fg_pattern_sum_w(&P), 32.0, 1e-9);
	CHECK(P.tiles[2].muted && P.tiles[3].muted && !P.tiles[4].muted);   /* step 1 → steps 2, 3 */
	CHECK(P.tiles[4].reversed && P.tiles[5].reversed);
	/* the lock at unit 5 (src 9) becomes a 2-unit locked tile at 10 reading 18 */
	bool foundLock = false;
	double pos = 0.0;
	for (int i = 0; i < P.nTiles; i++) {
		if (P.tiles[i].locked) {
			foundLock = true;
			CHECK(pos == 10.0 && P.tiles[i].w == 2.0 && P.tiles[i].src == 18.0);
		}
		pos += P.tiles[i].w;
	}
	CHECK(foundLock);
	CHECK(P.nMods == 2 && P.mods[0].step == 8 && P.mods[1].step == 30);
	/* and back down: positions that survive keep their edits, in-betweens drop */
	fg_edit_set_grid(&P, 4, 16, &nextColor);
	CHECK(fg_unit_count(&P) == 16);
	CHECK(P.tiles[1].muted && P.tiles[2].reversed && !P.tiles[3].reversed);
	CHECK(P.nMods == 2 && P.mods[0].step == 4 && P.mods[1].step == 15);

	/* reset all rebuilds the pristine row and keeps mods */
	fg_edit_reset_all(&P);
	CHECK(P.nTiles == 16 && !P.tiles[5].locked && P.nMods == 2);

	return check_report("edit_test");
}
