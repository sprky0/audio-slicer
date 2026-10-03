/* edit.h — pure edit operations on a pattern, in place, allocation-free.
 * The two the sequencer fires from step modifiers (reset order, randomize)
 * live here from F6; the interactive set (move, resize, split, merge, dup,
 * refill, gaps raster) arrives with F14. */
#pragma once

#include "frog_types.h"
#include "pattern.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Stable-sort each run of plain clips between anchors (locked tiles, gaps)
 * back to ascending `src`; every tile keeps its settings. */
void fg_edit_reset_order(fg_pattern* p);

/* Randomize at `level` 0..1: whole same-width buckets between locked tiles
 * may swap, then each bucket shuffles internally; locks stay anchored.
 * Returns true when anything moved. */
bool fg_edit_randomize(fg_pattern* p, double level, fg_rng* rng);

/* Rebuild the pristine one-unit-per-tile row (keeps mods). */
void fg_edit_reset_all(fg_pattern* p);

#ifdef __cplusplus
}
#endif
