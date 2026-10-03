/* pattern_race_test — the lock-free lanes under ThreadSanitizer: a UI
 * thread publishes patterns, swaps samples, pushes commands and drains the
 * rings while the audio thread processes blocks. Passes when TSan stays
 * quiet and every block the audio thread saw was a whole pattern. */
#include "check.h"
#include "engine_internal.h"
#include "pattern.h"

#include <pthread.h>
#include <stdatomic.h>
#include <string.h>

#define SR 48000.0
#define BLOCK 128
#define BLOCKS 4000

static fg_engine* E;
static atomic_int g_done;
static atomic_int g_torn;

static void* audio_thread(void* arg) {
	static double L[BLOCK], R[BLOCK];
	double* bufs[2] = {L, R};
	for (int b = 0; b < BLOCKS; b++) {
		fg_engine_process(E, bufs, 2, BLOCK);
		/* a torn pattern would break the row invariant */
		const fg_pattern* p = &E->tracks[0].active;
		if (fabs(fg_pattern_sum_w(p) - fg_unit_count(p)) > 1e-6) {
			atomic_fetch_add(&g_torn, 1);
		}
	}
	atomic_store(&g_done, 1);
	return NULL;
}

int main(void) {
	E = fg_engine_create(2, 3);
	fg_engine_reset(E, SR, BLOCK);
	fg_sample* cur = fg_sample_create(1, 96000, SR);
	fg_engine_set_sample(E, 0, cur);
	fg_engine_play_all(E);

	pthread_t th;
	pthread_create(&th, NULL, audio_thread, NULL);

	fg_rng rng;
	fg_rng_seed(&rng, 11);
	fg_sample* retired[64] = {0};
	int nRetired = 0;
	int published = 0, swaps = 0, changes = 0;
	fg_visual vis[64];
	fg_flash fl[16];
	while (!atomic_load(&g_done)) {
		/* edit + publish: a random legal reorder of the working row */
		fg_pattern* p = fg_engine_pattern(E, 0);
		const int i = (int)(fg_rng_unit(&rng) * p->nTiles);
		const int j = (int)(fg_rng_unit(&rng) * p->nTiles);
		const fg_tile t = p->tiles[i];
		p->tiles[i] = p->tiles[j];
		p->tiles[j] = t;
		p->nMods = 1;
		p->mods[0] = (fg_mod){0, FG_MOD_RAND, FG_FIRE_PROB, 0, 100, 0, 1, 1, 0, 1};
		fg_engine_publish(E, 0);
		published++;
		if (fg_engine_take_pattern_change(E, 0)) {
			changes++;
		}
		/* occasionally swap the sample, freeing retired ones when safe */
		if ((published % 97) == 0 && nRetired < 64) {
			fg_sample* s = fg_sample_create(1, 48000 + published, SR);
			retired[nRetired++] = fg_engine_set_sample(E, 0, s);
			swaps++;
		}
		for (int k = 0; k < nRetired; k++) {
			if (retired[k] && fg_engine_sample_retired(E, 0, retired[k])) {
				fg_sample_free(retired[k]);
				retired[k] = NULL;
			}
		}
		fg_engine_set_mix(E, 0, fg_rng_unit(&rng), fg_rng_unit(&rng) * 2.0 - 1.0);
		fg_engine_set_tempo(E, 60.0 + fg_rng_unit(&rng) * 120.0);
		fg_engine_poll_visuals(E, vis, 64);
		fg_engine_poll_flashes(E, fl, 16);
	}
	pthread_join(th, NULL);

	CHECK(published > 100);
	CHECK(swaps > 0);
	CHECK(atomic_load(&g_torn) == 0);
	printf("published %d, pattern changes taken %d, sample swaps %d\n", published, changes, swaps);

	/* everything retired by now */
	double L[BLOCK], R[BLOCK];
	double* bufs[2] = {L, R};
	fg_engine_process(E, bufs, 2, BLOCK);
	for (int k = 0; k < nRetired; k++) {
		if (retired[k]) {
			CHECK(fg_engine_sample_retired(E, 0, retired[k]));
			fg_sample_free(retired[k]);
		}
	}
	fg_sample* last = fg_engine_set_sample(E, 0, NULL);
	fg_engine_destroy(E);
	fg_sample_free(last);
	return check_report("pattern_race_test");
}
