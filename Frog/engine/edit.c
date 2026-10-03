#include "edit.h"

#include <math.h>
#include <string.h>

#define RES FG_SUBSTEP
#define MAX_CELLS (FG_MAX_UNITS * FG_SUBSTEP)

static double clampd(double v, double lo, double hi) {
	return v < lo ? lo : (v > hi ? hi : v);
}

static int clampi(int v, int lo, int hi) {
	return v < lo ? lo : (v > hi ? hi : v);
}

/* --- order ----------------------------------------------------------------- */

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

bool fg_edit_move(fg_pattern* p, int from, int to) {
	if (from < 0 || from >= p->nTiles || to < 0 || to >= p->nTiles || from == to) {
		return false;
	}
	const fg_tile t = p->tiles[from];
	if (from < to) {
		memmove(&p->tiles[from], &p->tiles[from + 1], sizeof(fg_tile) * (size_t)(to - from));
	} else {
		memmove(&p->tiles[to + 1], &p->tiles[to], sizeof(fg_tile) * (size_t)(from - to));
	}
	p->tiles[to] = t;
	return true;
}

/* Buckets are the runs of unlocked tiles between locks: [b0, lock0, b1, ...]. */
typedef struct {
	int lo, hi;   /* tile range in `scratch` */
	int wKey;     /* width in 1/4 units, the swap invariant */
} bucket;

bool fg_edit_randomize(fg_pattern* p, double level, fg_rng* rng) {
	level = clampd(level, 0.0, 1.0);
	const int n = p->nTiles;
	if (n < 2) {
		return false;
	}
	static fg_tile scratch[FG_MAX_TILES];   /* one caller at a time per engine */
	static bucket buckets[FG_MAX_TILES + 1];
	static int lockIdx[FG_MAX_TILES];
	static int order[FG_MAX_TILES + 1];
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
			buckets[nb].wKey += (int)lround(scratch[i].w * RES);
		}
	}
	nb++;
	for (int b = 0; b < nb; b++) {
		order[b] = b;
	}

	bool swapped = false;
	for (int a = nb - 1; a > 0; a--) {
		const bucket* A = &buckets[order[a]];
		if (A->hi == A->lo || fg_rng_unit(rng) >= level) {
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

/* --- packed resize ----------------------------------------------------------- */

static void drop_empty(fg_pattern* p) {
	int n = 0;
	for (int i = 0; i < p->nTiles; i++) {
		if (p->tiles[i].w > FG_EPS) {
			p->tiles[n++] = p->tiles[i];
		}
	}
	p->nTiles = n;
}

void fg_edit_resize_end(const fg_pattern* snap, int i, double dU, fg_pattern* out) {
	memcpy(out, snap, sizeof(fg_pattern));
	if (i < 0 || i >= snap->nTiles - 1) {
		return;   /* the last tile's end is pinned to the cut */
	}
	const double U = fg_unit_count(snap);
	const fg_tile* s = snap->tiles;
	const int last = snap->nTiles - 1;
	dU = fg_snap_unit(dU);
	double want = dU > 0.0 ? fmin(dU, U - s[i].src - s[i].w) : fmax(dU, FG_MIN_W - s[i].w);
	double moved = 0.0;
	if (want > 0.0) {
		double need = want;
		for (int k = i + 1; k <= last && need > FG_EPS; k++) {
			const double give = fmin(need, s[k].w);
			out->tiles[k].src = s[k].src + give;   /* head overwritten */
			out->tiles[k].w = s[k].w - give;
			need -= give;
		}
		moved = want - need;
	} else if (want < 0.0) {
		double surplus = -want;
		for (int k = i + 1; k <= last && surplus > FG_EPS; k++) {
			const double recv = fmin(surplus, U - s[k].src - s[k].w);
			out->tiles[k].w = s[k].w + recv;
			surplus -= recv;
		}
		moved = -(-want - surplus);
	}
	out->tiles[i].w = s[i].w + moved;   /* src pinned */
	drop_empty(out);
}

void fg_edit_resize_start(const fg_pattern* snap, int i, double dU, fg_pattern* out) {
	memcpy(out, snap, sizeof(fg_pattern));
	if (i <= 0 || i >= snap->nTiles) {
		return;   /* the first tile's start is pinned to the cut */
	}
	const double U = fg_unit_count(snap);
	const fg_tile* s = snap->tiles;
	dU = fg_snap_unit(dU);
	double want = fmin(fmax(dU, -(U - s[i].src - s[i].w)), s[i].w - FG_MIN_W);
	double moved = 0.0;
	if (want < 0.0) {
		double need = -want;
		for (int k = i - 1; k >= 0 && need > FG_EPS; k--) {
			const double give = fmin(need, s[k].w);
			out->tiles[k].w = s[k].w - give;   /* tail truncated */
			need -= give;
		}
		moved = -(-want - need);
	} else if (want > 0.0) {
		double surplus = want;
		for (int k = i - 1; k >= 0 && surplus > FG_EPS; k--) {
			const double recv = fmin(surplus, U - s[k].src - s[k].w);
			out->tiles[k].w = s[k].w + recv;
			surplus -= recv;
		}
		moved = want - surplus;
	}
	if (moved > 0.0) {
		out->tiles[i].src = s[i].src + moved;   /* shrink: head eaten */
	}
	out->tiles[i].w = s[i].w - moved;
	drop_empty(out);
}

/* --- split / merge ------------------------------------------------------------ */

bool fg_edit_split(fg_pattern* p, int i, int newColor) {
	if (i < 0 || i >= p->nTiles || p->nTiles >= FG_MAX_TILES) {
		return false;
	}
	const fg_tile t = p->tiles[i];
	if (t.gap || t.w < 2.0) {
		return false;
	}
	const double left = floor(t.w / 2.0);
	fg_tile a = t, b = t;
	a.w = left;
	a.fadeOut = 0.f;
	a.curveOut = FG_CURVE_LINEAR;
	a.locked = false;
	b.src = t.src + left;
	b.w = t.w - left;
	b.fadeIn = 0.f;
	b.curveIn = FG_CURVE_LINEAR;
	b.colorIdx = (uint8_t)newColor;
	b.locked = false;
	memmove(&p->tiles[i + 2], &p->tiles[i + 1], sizeof(fg_tile) * (size_t)(p->nTiles - i - 1));
	p->tiles[i] = a;
	p->tiles[i + 1] = b;
	p->nTiles++;
	return true;
}

bool fg_edit_merge(fg_pattern* p, int i) {
	if (p->nTiles <= 1 || i < 0 || i >= p->nTiles || p->tiles[i].gap) {
		return false;
	}
	const double U = fg_unit_count(p);
	if (i > 0 && !p->tiles[i - 1].gap) {
		fg_tile* a = &p->tiles[i - 1];
		const double w = fmin(a->w + p->tiles[i].w, U - a->src);
		if (w <= a->w) {
			return false;   /* no room in the source: nothing would change */
		}
		/* the absorbed width must stay in the row: cap means a shortfall, so
		 * only merge when the full width fits */
		if (w < a->w + p->tiles[i].w - FG_EPS) {
			return false;
		}
		a->w = w;
		memmove(&p->tiles[i], &p->tiles[i + 1], sizeof(fg_tile) * (size_t)(p->nTiles - i - 1));
		p->nTiles--;
		return true;
	}
	if (i == 0 && p->nTiles > 1 && !p->tiles[1].gap) {
		fg_tile* b = &p->tiles[1];
		const double w = b->w + p->tiles[0].w;
		if (p->tiles[0].src + w > U + FG_EPS) {
			return false;
		}
		b->src = p->tiles[0].src;
		b->w = w;
		memmove(&p->tiles[0], &p->tiles[1], sizeof(fg_tile) * (size_t)(p->nTiles - 1));
		p->nTiles--;
		return true;
	}
	return false;
}

/* --- raster (gaps mode, dup) --------------------------------------------------- */

typedef struct {
	int16_t tile;    /* index into the source row, −1 = empty */
	int16_t srcSub;  /* source position in 1/4 units */
} cell;

static int rasterize(const fg_pattern* p, cell* cells) {
	const int n = fg_unit_count(p) * RES;
	for (int k = 0; k < n; k++) {
		cells[k].tile = -1;
		cells[k].srcSub = 0;
	}
	int pos = 0;
	for (int i = 0; i < p->nTiles && pos < n; i++) {
		const fg_tile* t = &p->tiles[i];
		const int w = (int)lround(t->w * RES);
		const int base = (int)lround(t->src * RES);
		for (int k = 0; k < w && pos + k < n; k++) {
			cells[pos + k].tile = t->gap ? -1 : (int16_t)i;
			cells[pos + k].srcSub = (int16_t)(base + k);
		}
		pos += w;
	}
	return n;
}

static bool range_has_locked(const fg_pattern* p, const cell* cells, int start, int w, int except) {
	for (int k = start; k < start + w; k++) {
		const int t = cells[k].tile;
		if (t >= 0 && t != except && p->tiles[t].locked) {
			return true;
		}
	}
	return false;
}

static void paint(const fg_pattern* p, cell* cells, int n, int tile, int start, int w, int srcBase) {
	for (int k = 0; k < w && start + k < n; k++) {
		const int t = cells[start + k].tile;
		if (t >= 0 && t != tile && p->tiles[t].locked) {
			continue;   /* never overwrite a locked cell */
		}
		cells[start + k].tile = (int16_t)tile;
		cells[start + k].srcSub = (int16_t)(srcBase + k);
	}
}

/* Coalesce runs of the same tile with contiguous source into clips, runs of
 * empty cells into gaps. `src` is a copy of the row the cells index into. */
static void rebuild(fg_pattern* p, const fg_tile* src, const cell* cells, int n) {
	int out = 0;
	int k = 0;
	while (k < n && out < FG_MAX_TILES) {
		const int t = cells[k].tile;
		int j = k + 1;
		if (t < 0) {
			while (j < n && cells[j].tile < 0) {
				j++;
			}
			fg_tile_init(&p->tiles[out], 0.0, (double)(j - k) / RES, 0);
			p->tiles[out].gap = true;
		} else {
			while (j < n && cells[j].tile == t && cells[j].srcSub == cells[j - 1].srcSub + 1) {
				j++;
			}
			p->tiles[out] = src[t];
			p->tiles[out].src = (double)cells[k].srcSub / RES;
			p->tiles[out].w = (double)(j - k) / RES;
		}
		out++;
		k = j;
	}
	p->nTiles = out;
}

double fg_edit_entry_start(const fg_pattern* p, int i) {
	double s = 0.0;
	for (int k = 0; k < i && k < p->nTiles; k++) {
		s += p->tiles[k].w;
	}
	return s;
}

bool fg_edit_refill(fg_pattern* p, int i) {
	if (i < 0 || i >= p->nTiles) {
		return false;
	}
	const double start = fg_edit_entry_start(p, i);
	const double w = p->tiles[i].w;
	fg_tile_init(&p->tiles[i], start, w, (int)lround(start));
	return true;
}

void fg_edit_refill_all(fg_pattern* p) {
	double s = 0.0;
	for (int i = 0; i < p->nTiles; i++) {
		const double w = p->tiles[i].w;
		if (!p->tiles[i].locked) {
			fg_tile_init(&p->tiles[i], s, w, (int)lround(s));
		}
		s += w;
	}
}

int fg_edit_dup(fg_pattern* p, int i, int dir) {
	if (i < 0 || i >= p->nTiles || p->tiles[i].gap) {
		return -1;
	}
	static cell cells[MAX_CELLS];
	static fg_tile src[FG_MAX_TILES];
	const int n = rasterize(p, cells);
	int selStart = -1, selCount = 0;
	for (int k = 0; k < n; k++) {
		if (cells[k].tile == i) {
			if (selStart < 0) {
				selStart = k;
			}
			selCount++;
		}
	}
	if (selStart < 0) {
		return -1;
	}
	int start = dir > 0 ? selStart + selCount : selStart - selCount;
	int w = selCount;
	if (start < 0) {
		w += start;
		start = 0;
	}
	if (start + w > n) {
		w = n - start;
	}
	if (w <= 0 || range_has_locked(p, cells, start, w, i)) {
		return -1;
	}
	memcpy(src, p->tiles, sizeof(fg_tile) * (size_t)p->nTiles);
	/* the copy is a new entry: give it a slot in the source row */
	if (p->nTiles >= FG_MAX_TILES) {
		return -1;
	}
	const int copyIdx = p->nTiles;
	src[copyIdx] = p->tiles[i];
	src[copyIdx].locked = false;
	paint(p, cells, n, copyIdx, start, w, (int)lround(p->tiles[i].src * RES));
	rebuild(p, src, cells, n);
	/* find the copy: the entry starting at cell `start` */
	int pos = 0;
	for (int k = 0; k < p->nTiles; k++) {
		if (pos == start) {
			return k;
		}
		pos += (int)lround(p->tiles[k].w * RES);
	}
	return -1;
}

bool fg_edit_move_gaps(fg_pattern* p, int i, double targetUnit) {
	if (i < 0 || i >= p->nTiles || p->tiles[i].gap) {
		return false;
	}
	static cell cells[MAX_CELLS];
	static fg_tile src[FG_MAX_TILES];
	const int n = rasterize(p, cells);
	for (int k = 0; k < n; k++) {
		if (cells[k].tile == i) {
			cells[k].tile = -1;
		}
	}
	const int wCells = (int)lround(p->tiles[i].w * RES);
	int start = (int)lround(fg_snap_unit(targetUnit) * RES);
	start = clampi(start, 0, n - wCells);
	if (range_has_locked(p, cells, start, wCells, i)) {
		return false;
	}
	memcpy(src, p->tiles, sizeof(fg_tile) * (size_t)p->nTiles);
	paint(p, cells, n, i, start, wCells, (int)lround(p->tiles[i].src * RES));
	rebuild(p, src, cells, n);
	return true;
}

void fg_edit_resize_gaps(const fg_pattern* snap, int i, bool endEdge, double dU, fg_pattern* out) {
	memcpy(out, snap, sizeof(fg_pattern));
	if (i < 0 || i >= snap->nTiles || snap->tiles[i].gap) {
		return;
	}
	static cell cells[MAX_CELLS];
	const int n = rasterize(snap, cells);
	int first = -1, count = 0;
	const int base = (int)lround(snap->tiles[i].src * RES);
	for (int k = 0; k < n; k++) {
		if (cells[k].tile == i) {
			if (first < 0) {
				first = k;
			}
			count++;
		}
	}
	if (first < 0) {
		return;
	}
	for (int k = 0; k < n; k++) {
		if (cells[k].tile == i) {
			cells[k].tile = -1;
		}
	}
	const int dCells = (int)lround(fg_snap_unit(dU) * RES);
	const int minC = 1;
	int start = first, wCells = count, srcBase = base;
	if (endEdge) {
		wCells = count + dCells > minC ? count + dCells : minC;
		wCells = wCells < n - start ? wCells : n - start;
		wCells = wCells < n - srcBase ? wCells : n - srcBase;
		for (int k = 0; k < wCells; k++) {
			const int t = cells[start + k].tile;
			if (t >= 0 && t != i && snap->tiles[t].locked) {
				wCells = k > count ? k : count;
				break;
			}
		}
	} else {
		const int end = first + count;
		start = clampi(first + dCells, 0, end - minC);
		for (int q = end - 1; q >= start; q--) {
			const int t = cells[q].tile;
			if (t >= 0 && t != i && snap->tiles[t].locked) {
				start = q + 1;
				break;
			}
		}
		srcBase = base + (start > first ? start - first : 0);
		wCells = end - start;
		wCells = wCells < n - start ? wCells : n - start;
		wCells = wCells < n - srcBase ? wCells : n - srcBase;
	}
	paint(snap, cells, n, i, start, wCells, srcBase);
	rebuild(out, snap->tiles, cells, n);
}

/* --- grid --------------------------------------------------------------------- */

void fg_edit_set_grid(fg_pattern* p, int beats, int denom, int* nextColor) {
	static fg_pattern old;
	memcpy(&old, p, sizeof(fg_pattern));
	const int Uold = fg_unit_count(&old);
	p->beats = beats;
	p->denom = denom;
	fg_pattern_validate(p);   /* clamps beats / denom; the row is rebuilt below anyway */
	const int Unew = fg_unit_count(p);
	fg_pattern_default_tiles(p);
	if (nextColor) {
		*nextColor = Unew;
	}
	if (Uold <= 0 || old.nTiles == 0) {
		return;
	}
	const double r = (double)Unew / Uold;

	/* per-step settings re-apply positionally: new step q inherits the old
	 * tile sounding at old unit floor(q / r) */
	for (int q = 0; q < Unew; q++) {
		const double oldUnit = floor(q / r);
		double pos = 0.0;
		for (int i = 0; i < old.nTiles; i++) {
			const fg_tile* t = &old.tiles[i];
			if (oldUnit + FG_EPS >= pos && oldUnit < pos + t->w - FG_EPS) {
				if (!t->gap) {
					fg_tile* d = &p->tiles[q];
					d->muted = t->muted;
					d->reversed = t->reversed;
					d->offset = t->offset;
					d->fadeIn = t->fadeIn;
					d->fadeOut = t->fadeOut;
					d->curveIn = t->curveIn;
					d->curveOut = t->curveOut;
					d->gain = t->gain;
				}
				break;
			}
			pos += t->w;
		}
	}

	/* locked tiles keep their audio and loop position, scaled onto the new
	 * grid, painted over the default row */
	static cell cells[MAX_CELLS];
	static fg_tile src[FG_MAX_TILES + 1];
	const int n = rasterize(p, cells);
	memcpy(src, p->tiles, sizeof(fg_tile) * (size_t)p->nTiles);
	int extra = p->nTiles;
	double pos = 0.0;
	bool painted = false;
	for (int i = 0; i < old.nTiles; i++) {
		const fg_tile* t = &old.tiles[i];
		if (t->locked && !t->gap && extra < FG_MAX_TILES) {
			const int start = (int)lround(pos * r * RES);
			const int w = (int)lround(t->w * r * RES);
			const int sb = (int)lround(t->src * r * RES);
			if (w >= 1 && start >= 0 && start + w <= n && sb + w <= Unew * RES) {
				src[extra] = *t;
				src[extra].src = (double)sb / RES;
				paint(p, cells, n, extra, start, w, sb);
				extra++;
				painted = true;
			}
		}
		pos += t->w;
	}
	if (painted) {
		rebuild(p, src, cells, n);
	}

	/* modifiers: rescale onto the new grid, first occupant of a step wins */
	bool taken[FG_MAX_UNITS] = {false};
	int nm = 0;
	for (int i = 0; i < old.nMods; i++) {
		fg_mod m = old.mods[i];
		const int step = (int)lround(m.step * r);
		if (step < 0 || step >= Unew || taken[step]) {
			continue;
		}
		m.step = (int16_t)step;
		taken[step] = true;
		p->mods[nm++] = m;
	}
	p->nMods = nm;
}
