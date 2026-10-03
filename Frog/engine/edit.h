/* edit.h — pure edit operations on a pattern, in place, allocation-free,
 * ported from the browser version. The row invariant Σw = U holds after
 * every operation. The sequencer fires reset_order and randomize from step
 * modifiers on the audio thread; everything else is UI-thread work on the
 * working copy, published afterwards.
 *
 * Positions and widths live on the 1/4-unit lattice. "Packed" operations
 * conserve width by trading with neighbours; "gaps" operations work on a
 * raster of U × FG_SUBSTEP cells where a move leaves silence and a drop
 * overwrites. */
#pragma once

#include "frog_types.h"
#include "pattern.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- order ----------------------------------------------------------------- */

/* Stable-sort each run of plain clips between anchors (locked tiles, gaps)
 * back to ascending `src`; every tile keeps its settings. */
void fg_edit_reset_order(fg_pattern* p);

/* Randomize at `level` 0..1: whole same-width buckets between locked tiles
 * may swap, then each bucket shuffles internally; locks stay anchored.
 * Returns true when anything moved. */
bool fg_edit_randomize(fg_pattern* p, double level, fg_rng* rng);

/* Rebuild the pristine one-unit-per-tile row (keeps mods). */
void fg_edit_reset_all(fg_pattern* p);

/* Packed reorder: move the tile at `from` so it sits at index `to`. */
bool fg_edit_move(fg_pattern* p, int from, int to);

/* --- width ------------------------------------------------------------------ */

/* Packed edge resize, as a pure function of the drag so far: `snap` is the
 * row as it was when the drag began, `dU` the lattice-snapped distance the
 * edge has moved (positive = right). The boundary eats what it crosses:
 * growing the end overwrites the following tiles' heads (their src
 * advances), growing the start overwrites the preceding tiles' tails; the
 * growing tile keeps its own src pinned. Donors that empty out are dropped.
 * Writes the result into `out` (may alias nothing in `snap`). */
void fg_edit_resize_end(const fg_pattern* snap, int i, double dU, fg_pattern* out);
void fg_edit_resize_start(const fg_pattern* snap, int i, double dU, fg_pattern* out);

/* Split tile i in half on the unit grid (w ≥ 2): the left half keeps the
 * colour and fade-in, the right half gets `newColor` and the fade-out; both
 * lose `locked`. */
bool fg_edit_split(fg_pattern* p, int i, int newColor);

/* Merge tile i into its left neighbour (the right one when i is first);
 * refuses across a gap. */
bool fg_edit_merge(fg_pattern* p, int i);

/* --- content ---------------------------------------------------------------- */

/* Replace entry i (gap or clip) with the native slice for its grid position
 * and default settings. */
bool fg_edit_refill(fg_pattern* p, int i);

/* Refill every unlocked entry. */
void fg_edit_refill_all(fg_pattern* p);

/* Stamp a copy of clip i into the adjacent cells (dir −1 before, +1 after),
 * overwriting what is there, truncated at the bar edges, blocked by a locked
 * clip. Returns the index of the copy, or −1. */
int fg_edit_dup(fg_pattern* p, int i, int dir);

/* Gaps mode: lift clip i and drop it with its left edge at `targetUnit`,
 * leaving silence behind and overwriting what it lands on; blocked by a
 * locked clip (then nothing changes). */
bool fg_edit_move_gaps(fg_pattern* p, int i, double targetUnit);

/* Gaps mode edge resize (pure, from a snapshot like the packed one): the end
 * edge grows over the cells ahead or shrinks leaving a gap; the start edge
 * grows back with src pinned or shrinks eating the clip's own head. */
void fg_edit_resize_gaps(const fg_pattern* snap, int i, bool endEdge, double dU, fg_pattern* out);

/* --- grid -------------------------------------------------------------------- */

/* Change the grid (beats / denominator). The row is rebuilt at the new unit
 * count; per-step settings re-apply positionally (new step p inherits old
 * step floor(p / r), r = Unew / Uold); locked tiles keep their audio and
 * position when they still fit; modifiers rescale their step (first
 * occupant of a collided step wins). */
void fg_edit_set_grid(fg_pattern* p, int beats, int denom, int* nextColor);

/* Unit where entry i starts. */
double fg_edit_entry_start(const fg_pattern* p, int i);

#ifdef __cplusplus
}
#endif
