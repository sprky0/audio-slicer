/* sequencer_test — the engine end to end at natural tempo (no stretch): slot
 * timing to the sample, reorder, mute, gap, reverse, gain, fades, late join,
 * ratchet hits, voice modifiers, a pattern action posting its change back,
 * and the visual records. The source is an impulse code: unit i carries
 * (i+1)/16 at its first frame and −0.5 at its midpoint. */
#include "check.h"
#include "engine.h"
#include "pattern.h"

#include <stdlib.h>
#include <string.h>

#define SR 48000.0
#define BLOCK 128
#define UNIT 6000           /* frames per unit: 16 units = 2 s = 4 beats at 120 bpm */
#define BAR (16 * UNIT)

static double L[4 * BAR], R[4 * BAR];

static fg_sample* make_source(void) {
	fg_sample* s = fg_sample_create(1, BAR, SR);
	for (int i = 0; i < 16; i++) {
		s->ch[0][i * UNIT] = (float)(i + 1) / 16.f;
		s->ch[0][i * UNIT + UNIT / 2] = -0.5f;
	}
	return s;
}

static void render(fg_engine* e, int64_t frames) {
	memset(L, 0, sizeof L);
	memset(R, 0, sizeof R);
	for (int64_t at = 0; at < frames; at += BLOCK) {
		const int n = (int)(frames - at < BLOCK ? frames - at : BLOCK);
		double* bufs[2] = {L + at, R + at};
		fg_engine_process(e, bufs, 2, n);
	}
}

/* drain the visual ring, keeping track 0's records */
static int poll_track0(fg_engine* e, fg_visual* out, int max) {
	fg_visual all[FG_VISUAL_RING];
	const int n = fg_engine_poll_visuals(e, all, FG_VISUAL_RING);
	int k = 0;
	for (int i = 0; i < n && k < max; i++) {
		if (all[i].track == 0) {
			out[k++] = all[i];
		}
	}
	return k;
}

/* energy outside the given sample positions */
static double residual(int64_t frames, const int64_t* keep, int nKeep) {
	double sum = 0.0;
	for (int64_t i = 0; i < frames; i++) {
		bool k = false;
		for (int j = 0; j < nKeep; j++) {
			if (keep[j] == i) {
				k = true;
			}
		}
		if (!k) {
			sum += fabs(L[i]);
		}
	}
	return sum;
}

int main(void) {
	fg_engine* e = fg_engine_create(2, 7);
	fg_engine_reset(e, SR, BLOCK);
	fg_sample* src = make_source();
	fg_engine_set_sample(e, 0, src);
	fg_engine_set_tempo(e, 120.0);

	/* 1. natural row: every unit's impulse lands at its grid slot, twice (loop) */
	fg_engine_play_all(e);
	render(e, 2 * BAR);
	int64_t keep[64];
	int nk = 0;
	for (int bar = 0; bar < 2; bar++) {
		for (int k = 0; k < 16; k++) {
			CHECK_NEAR(L[bar * BAR + k * UNIT], (k + 1) / 16.0, 1e-9);
			CHECK_NEAR(L[bar * BAR + k * UNIT + UNIT / 2], -0.5, 1e-9);
			CHECK_NEAR(R[bar * BAR + k * UNIT], (k + 1) / 16.0, 1e-9);   /* mono → both channels */
			keep[nk++] = bar * BAR + k * UNIT;
			keep[nk++] = bar * BAR + k * UNIT + UNIT / 2;
		}
	}
	CHECK_NEAR(residual(2 * BAR, keep, nk), 0.0, 1e-9);
	CHECK(fg_engine_now(e) == 2 * BAR);   /* first render since reset */
	CHECK(fg_engine_grid_running(e));
	CHECK_NEAR(fg_engine_bpm(e), 120.0, 0.0);

	/* visuals: one record per slot, in order, with sample-exact times */
	fg_visual vis[64];
	int nv = poll_track0(e, vis, 64);
	CHECK(nv == 32);
	CHECK(vis[0].start == 0 && vis[0].stop == UNIT && vis[0].tileIndex == 0 && !vis[0].silent);
	CHECK(vis[17].start == BAR + UNIT && vis[17].src == 1.0);

	/* 2. edits: swap 0/1, mute 2, gap at 3, reverse 4, gain 5, fade-in 6 */
	fg_engine_stop_all(e);
	fg_pattern* p = fg_engine_pattern(e, 0);
	fg_tile t0 = p->tiles[0];
	p->tiles[0] = p->tiles[1];
	p->tiles[1] = t0;
	p->tiles[2].muted = true;
	p->tiles[3].gap = true;
	p->tiles[4].reversed = true;
	p->tiles[5].gain = 0.5f;
	p->tiles[6].fadeIn = 0.75f;
	fg_engine_publish(e, 0);
	fg_engine_play_all(e);
	render(e, BAR);
	CHECK_NEAR(L[0], 2 / 16.0, 1e-9);
	CHECK_NEAR(L[UNIT], 1 / 16.0, 1e-9);
	CHECK_NEAR(L[2 * UNIT], 0.0, 0.0);
	CHECK_NEAR(L[2 * UNIT + UNIT / 2], 0.0, 0.0);
	CHECK_NEAR(L[3 * UNIT], 0.0, 0.0);
	/* reversed: the unit's first frame plays last */
	CHECK_NEAR(L[4 * UNIT + UNIT - 1], 5 / 16.0, 1e-9);
	CHECK_NEAR(L[4 * UNIT], 0.0, 1e-9);
	CHECK_NEAR(L[5 * UNIT], 6 / 16.0 * 0.5, 1e-9);
	/* fade-in 0.75 of the slot, linear: the midpoint impulse sits at x = 2/3 */
	CHECK_NEAR(L[6 * UNIT], 0.0, 1e-9);
	CHECK_NEAR(L[6 * UNIT + UNIT / 2], -0.5 * (0.5 / 0.75), 1e-6);
	CHECK_NEAR(L[7 * UNIT], 8 / 16.0, 1e-9);

	/* 3. late join: a second track with the same sample starts mid-bar and
	 *    lands in phase (tile 8's slot, offset into its material) */
	fg_sample* src2 = make_source();
	fg_engine_set_sample(e, 1, src2);
	fg_engine_stop_all(e);
	fg_engine_set_mute(e, 0, true);
	fg_engine_play_all(e);                      /* grid restarts at the next block */
	render(e, 50000);
	fg_engine_track_play(e, 1);                 /* 50000 into the bar, inside unit 8 */
	render(e, BAR - 50000);                     /* to the end of the bar */
	/* the output buffer now starts at absolute 50000 */
	CHECK_NEAR(L[8 * UNIT + UNIT / 2 - 50000], -0.5, 1e-9);   /* mid impulse of unit 8 at 51000 */
	CHECK_NEAR(L[9 * UNIT - 50000], 10 / 16.0, 1e-9);
	CHECK_NEAR(L[15 * UNIT - 50000], 16 / 16.0, 1e-9);
	fg_engine_set_mute(e, 0, false);
	fg_engine_track_stop(e, 1);
	fg_engine_set_mute(e, 1, true);             /* Play All restarts it too: keep it silent from here */

	/* 4. modifiers: ratchet (even ×2) on step 10, mute on 12, gain 50 % on 13 */
	fg_engine_stop_all(e);
	p = fg_engine_pattern(e, 0);
	fg_pattern_default_tiles(p);
	p->nMods = 3;
	memset(p->mods, 0, sizeof p->mods);
	p->mods[0] = (fg_mod){10, FG_MOD_RATCHET, FG_FIRE_PROB, FG_RATCHET_EVEN, 100, 0, 2, 2, 0, 1};
	p->mods[1] = (fg_mod){12, FG_MOD_MUTE, FG_FIRE_PROB, 0, 100, 0, 1, 1, 0, 1};
	p->mods[2] = (fg_mod){13, FG_MOD_GAIN, FG_FIRE_PROB, 0, 100, 50, 1, 1, 0, 1};
	fg_engine_publish(e, 0);
	poll_track0(e, vis, 64);
	fg_engine_play_all(e);
	render(e, BAR);
	CHECK_NEAR(L[10 * UNIT], 11 / 16.0, 1e-9);
	CHECK_NEAR(L[10 * UNIT + UNIT / 2], 11 / 16.0, 1e-9);   /* second hit replays from the top */
	CHECK_NEAR(L[11 * UNIT], 12 / 16.0, 1e-9);
	CHECK_NEAR(L[12 * UNIT], 0.0, 0.0);
	CHECK_NEAR(L[13 * UNIT], 14 / 16.0 * 0.5, 1e-9);
	nv = poll_track0(e, vis, 64);
	CHECK(nv == 16);
	CHECK(vis[10].ratchet && vis[10].hits == 2 && vis[10].w == 1.0 && vis[10].silent == false);
	CHECK(vis[12].silent);
	fg_flash fl[16];
	const int nf = fg_engine_poll_flashes(e, fl, 16);
	CHECK(nf == 3 && fl[0].step == 10 && fl[1].step == 12 && fl[2].step == 13);

	/* 5. a reset action on step 0 restores a swapped row before it sounds and
	 *    posts the change back to the UI */
	fg_engine_stop_all(e);
	p = fg_engine_pattern(e, 0);
	fg_pattern_default_tiles(p);
	t0 = p->tiles[0];
	p->tiles[0] = p->tiles[1];
	p->tiles[1] = t0;
	p->nMods = 1;
	p->mods[0] = (fg_mod){0, FG_MOD_RESET, FG_FIRE_PROB, 0, 100, 0, 1, 1, 0, 1};
	fg_engine_publish(e, 0);
	CHECK(!fg_engine_take_pattern_change(e, 0));
	fg_engine_play_all(e);
	render(e, BAR);
	CHECK_NEAR(L[0], 1 / 16.0, 1e-9);
	CHECK_NEAR(L[UNIT], 2 / 16.0, 1e-9);
	CHECK(fg_engine_take_pattern_change(e, 0));
	CHECK(fg_engine_pattern(e, 0)->tiles[0].src == 0.0 && fg_engine_pattern(e, 0)->tiles[1].src == 1.0);

	/* 6. sample swap retires the old store once the audio thread has moved on */
	fg_sample* src3 = make_source();
	fg_sample* old = fg_engine_set_sample(e, 0, src3);
	CHECK(old == src);
	CHECK(!fg_engine_sample_retired(e, 0, old));
	render(e, BLOCK);
	CHECK(fg_engine_sample_retired(e, 0, old));
	fg_sample_free(old);

	/* 7. loop off: the row plays once and stops */
	fg_engine_stop_all(e);
	p = fg_engine_pattern(e, 0);
	fg_pattern_default_tiles(p);
	p->nMods = 0;
	p->loop = false;
	fg_engine_publish(e, 0);
	fg_engine_play_all(e);
	render(e, 2 * BAR);
	CHECK_NEAR(L[15 * UNIT], 1.0, 1e-9);
	CHECK_NEAR(L[BAR], 0.0, 0.0);
	CHECK_NEAR(L[BAR + UNIT], 0.0, 0.0);

	fg_engine_destroy(e);
	fg_sample_free(src2);
	fg_sample_free(src3);
	return check_report("sequencer_test");
}
