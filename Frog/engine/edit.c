#include "edit.h"

#include <math.h>
#include <string.h>

/* insertion sort of tiles[lo, hi) by src: runs are short, stable, no alloc */
static void sort_run(fg_tile* tiles, int lo, int hi) {
	for (int i = lo + 1; i < hi; i++) {
		fg_tile key = tiles[i];
		int j = i - 1;
		while (j >= lo && tiles[j].src > key.src) {
			tiles[j + 1] = tiles[j];
			j--;
		}
		tiles[j + 1] = key;
	}
}

void fg_edit_reset_order(fg_pattern* p) {
	int runStart = 0;
	for (int i = 0; i < p->nTiles; i++) {
		if (p->tiles[i].locked || p->tiles[i].gap) {
			sort_run(p->tiles, runStart, i);
			runStart = i + 1;
		}
	}
	sort_run(p->tiles, runStart, p->nTiles);
}

void fg_edit_reset_all(fg_pattern* p) {
	fg_pattern_default_tiles(p);
}

/* Buckets are the runs of unlocked tiles between locks: [b0, lock0, b1, ...].
 * Each is described by its range in a scratch copy of the row. */
typedef struct {
	int lo, hi;   /* tile range in `scratch` */
	int wKey;     /* width in 1/4 units, the swap invariant */
} bucket;

bool fg_edit_randomize(fg_pattern* p, double level, fg_rng* rng) {
	level = level < 0.0 ? 0.0 : (level > 1.0 ? 1.0 : level);
	const int n = p->nTiles;
	if (n < 2) {
		return false;
	}
	static fg_tile scratch[FG_MAX_TILES];   /* audio thread: one caller at a time per engine */
	static bucket buckets[FG_MAX_TILES + 1];
	static int lockIdx[FG_MAX_TILES];
	static int order[FG_MAX_TILES + 1];     /* bucket permutation after whole-bucket swaps */
	memcpy(scratch, p->tiles, sizeof(fg_tile) * (size_t)n);

	int nb = 0, nl = 0;
	buckets[0].lo = 0;
	buckets[0].hi = 0;
	buckets[0].wKey = 0;
	for (int i = 0; i < n; i++) {
		if (scratch[i].locked) {
			lockIdx[nl++] = i;
			nb++;
			buckets[nb].lo = i + 1;
			buckets[nb].hi = i + 1;
			buckets[nb].wKey = 0;
		} else {
			buckets[nb].hi = i + 1;
			buckets[nb].wKey += (int)lround(scratch[i].w * FG_SUBSTEP);
		}
	}
	nb++;
	for (int b = 0; b < nb; b++) {
		order[b] = b;
	}

	bool swapped = false;
	/* (1) whole-bucket swaps among same-width partners */
	for (int a = nb - 1; a > 0; a--) {
		const bucket* A = &buckets[order[a]];
		if (A->hi == A->lo) {
			continue;
		}
		if (fg_rng_unit(rng) >= level) {
			continue;
		}
		int partners = 0;
		for (int b = 0; b < a; b++) {
			if (buckets[order[b]].wKey == A->wKey) {
				partners++;
			}
		}
		if (partners == 0) {
			continue;
		}
		int pick = (int)(fg_rng_unit(rng) * partners);
		for (int b = 0; b < a; b++) {
			if (buckets[order[b]].wKey == A->wKey) {
				if (pick == 0) {
					const int tmp = order[a];
					order[a] = order[b];
					order[b] = tmp;
					swapped = true;
					break;
				}
				pick--;
			}
		}
	}
	/* (2) within-bucket Fisher-Yates */
	for (int b = 0; b < nb; b++) {
		bucket* B = &buckets[b];
		for (int k = B->hi - B->lo - 1; k > 0; k--) {
			if (fg_rng_unit(rng) >= level) {
				continue;
			}
			const int j = (int)(fg_rng_unit(rng) * (k + 1));
			if (j == k) {
				continue;
			}
			const fg_tile t = scratch[B->lo + k];
			scratch[B->lo + k] = scratch[B->lo + j];
			scratch[B->lo + j] = t;
			swapped = true;
		}
	}
	if (!swapped) {
		return false;
	}
	/* interleave buckets (in their new order) and locks back into the row */
	int out = 0;
	for (int b = 0; b < nb; b++) {
		const bucket* B = &buckets[order[b]];
		for (int i = B->lo; i < B->hi; i++) {
			p->tiles[out++] = scratch[i];
		}
		if (b < nl) {
			p->tiles[out++] = scratch[lockIdx[b]];
		}
	}
	return true;
}
