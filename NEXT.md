# Where we are / next steps

Working notes for jsloop. Updated 2026-07-10, branch
`feature-slice-drag-sequencer`. (Completed work lives in git history; this file is
the current shape + what's next.)

## Current shape

Vanilla-JS, no-build browser audio slicer + tile sequencer. Load a clip, set a
region, and it's cut into a grid you arrange into a loop. Multi-instance, all
locked to one master clock.

- **Timing model — Beats + Step.** The selection is `beats` quarter-notes long
  (**Beats** control, {1,2,3,4,6,8,12,16}, default 4) → `BPM = 60·beats/selDur`.
  **Step** (1/2…1/32, default 1/16) is an independent subdivision. The slice grid
  is derived: `cells = beats × stepDenom/4` (`cellCount()` in main.js). Fixed
  quarter-note beat unit for now.
- **Tile sequencer.** Ordered variable-width tiles over the grid; drag-reorder /
  edge-resize / split (dbl-click) / merge (shift-click) / per-step pitch (wheel),
  live while playing. **Click a tile — or an empty gap — to select** it; the
  selection highlights via background tint + white waveform (no border/movement)
  and shows a clip's length handles. A **"Slice" settings** group (right of Step,
  disabled when nothing is selected) acts on the selection:
  - **All** — broadcast toggle: while lit, Mute/Rev/Fades/Refill act on EVERY
    slice (fill-up semantics: mixed → all on; all on → all off). Lock/Dup stay
    per-slice (positional). Transient UI, not persisted.
  - **Mute** / **Lock** (clip only) — Lock protects a slice from being overwritten
    (moves/dups blocked, resize-grow clamps) and from Randomize (stays pinned); it
    shows an orange ring + 🔒. `tile.locked`, persisted.
  - **Rev** — reverse the slice (sample flip baked into the region buffer;
    rev-aware region/stretch cache keys; mirrored tile wave + ◀ badge).
    `tile.reversed`, persisted, export-faithful.
  - **F.In / F.Out** — per-slice fade envelope, stored as 0..1 fractions of the
    slice's length, applied as per-voice gain automation at schedule time
    (`envelope.js` — shared curve registry, linear today; live engine + WAV
    export use the same code). `tile.fadeIn/fadeOut`, persisted.
  - **Dup ◀ / Dup ▶** (clip only) — stamp a clone before/after, overwriting under it.
  - **Refill** (anything non-pristine) — set the entry's `src` to its grid
    position + default settings (fills a gap; resets a moved slice; clears
    offset/mute/rev/fades). With All: restores everything except locked slices.
  - (Removed: the old per-tile mute dot and the redundant details-panel "Slice" button.)
  - **Resize semantics — the boundary EATS what it moves over.** Drag right →
    the next clip's head is overwritten (`src` advances); drag left → the
    previous clip's tail truncates. The growing clip keeps its own `src` pinned
    and reveals more of its own source at its tail, so eaten audio is genuinely
    gone (never migrates onto a neighbour). Same rule in both edit modes; on a
    fresh grid it reads as sliding a cut point through continuous source.
  - **Packed reorder** = live reflow: the dragged tile is shuffled through seq.tiles +
    the DOM as you drag (no drop line); the ghost snaps over its live slot; release
    just re-renders + saves. Drop index excludes the dragged tile + counts midpoints
    of ALL entries (clips + gaps) → maps to `seq.tiles`, stable (no oscillation).
  - **Packed vs Gaps** — global Master-bar toggle. Packed = length-conserving
    (no gaps). Gaps = free placement + overwrite; gaps are silent tiles
    `{gap:true,w,src:0}`, edits go through a rasterize→rebuild cell grid (which
    carries `locked` and never overwrites locked cells).
- **Edit / perform layout.** Show/Hide details trades waveform height (`setHeight`,
  280↔96) for tile-row height (`--tile-h`, 7u↔16u). `applyEditMode()` in main.js.
  Newly-added (empty) slicers open in **edit**; restored/duplicated (already
  configured) open in **perform** — `detailsShown = !savedState`.
- **Master transport + BeatGrid (drift-free lock).** Shared AudioContext
  singleton (`audio-context.js`) + shared **BeatGrid** (`beat-grid.js`): one
  beat↔audio-time mapping every Transport schedules against ABSOLUTELY (no
  accumulated seconds). Tempo changes re-anchor the grid once,
  phase-continuously → all tracks correct identically, zero relative drift.
  When the grid moves under a transport it skips tiles wholly in the past and
  starts a partial tile late with an offset INTO its audio (`offsetSec` on
  `scheduleBuffer`). Individual start while others play joins IN PHASE
  (anchors to the current loop boundary, skips in mid-bar). External MIDI is
  PHASE-locked via a per-pulse PLL (midi-clock emits (beat, timestamp); main.js
  slews the grid, snaps on gross error) — verified ~1.6 ms mean phase error
  against a jittered synthetic 116 BPM clock. Debug handle: `window.__jsloop`.
- **Per-slicer actions** — Add / **Duplicate** (clones a slicer via its `getState()`
  snapshot + a copied audio blob, slotted in right after) / Remove.
- **Unified DragControl** everywhere (`drag-control.js`); fluid relative-unit grid
  (see `public/CSS.md`). MIDI clock sync, WSOLA time-stretch + pitch, persistence
  (localStorage settings + IndexedDB audio).

## Open follow-ups (small)

- (Done 2026-07-15: dead `audio-slicer.js` deleted; favicon 404 silenced via
  `<link rel="icon" href="data:,">`; preview self-end now emits 'stopped' via
  `stop()` so the floating transport dismisses — verified headlessly. Slicer
  registry gained a debug-only `_slicer` handle alongside `_transport`.)
- **Manual passes not yet done headlessly:** MIDI device hot-plug with real
  hardware; the external-clock PHASE lock against a real device (verified with
  synthetic pulses through the real handler chain); the gaps-mode START (green)
  handle + pack-mode cascade absorbing a pre-existing gap (both share verified
  code paths).
- **Decision left open:** Vol/Pan/Pitch live in the always-visible header (they
  affect sequenced output), not the details panel. Move if you'd prefer.

## Export WAV — shipped (export-wav.js)

Offline render of the master mix → normalized stereo 16-bit WAV, as designed:
Master-bar button → panel (per-slicer checkboxes, Length default = LCM of the
chosen beat counts) → OfflineAudioContext render through the same
`getTilePlayback` policy as live playback (WSOLA builds prescanned + awaited →
pitch-correct; per-slice fades/reverse included), hard-cut at N beats,
peak-normalized to ~0.99, RIFF/WAV blob download. 24-bit remains a later option.

## Next: bigger bets

- **Tier 2 leftovers** — per-step gain/probability (reverse + fades shipped);
  fade curve shapes beyond linear (drop into `envelope.js`'s FADE_CURVES);
  transient detection + draggable non-uniform slice markers.
- **Richer meter** (future, per discussion) — selectable beat unit (dotted values,
  e.g. 1.5 = dotted quarter) or an odd/compound time-signature editor, building on
  the Beats model.
- **Optional:** restore a per-slicer solo/audition path if master-only transport
  feels too restrictive.

## Known limitations

- Per-track BPM is hidden (master governs); the element still exists in memory so
  `setMidiBpm`'s value/lock writes stay harmless.
- Start-handle GROW is bounded by the source remaining after the clip's window,
  so a slice whose window already ends at the cut can't grow via its start
  handle (a silent-tail "sampler-style" overgrow is a possible follow-up).
