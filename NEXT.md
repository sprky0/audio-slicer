# Where we are / next steps

Working notes for jsloop. Updated 2026-07-04, branch
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
  live while playing. **Click a tile to select** it — the selection highlights via
  background tint + white waveform (no border/movement) and shows its length
  handles. A **"Slice" settings** group (right of Step, disabled when nothing is
  selected) acts on the selected slice: **Mute**, **Dup ◀ / Dup ▶** (stamp a clone
  before/after, overwriting under it). (The old per-tile mute dot and the redundant
  "Slice" action button in the details panel were removed.)
  - Reorder drop index counts ALL entries (clips + gaps) so it maps to `seq.tiles`;
    computed by excluding the dragged tile + midpoints (symmetric fwd/back); the
    ghost overlaps the tile under the cursor.
  - **Packed vs Gaps** — global Master-bar toggle. Packed = length-conserving
    (no gaps). Gaps = free placement + overwrite; gaps are silent tiles
    `{gap:true,w,src:0}`, edits go through a rasterize→rebuild cell grid.
- **Edit / perform layout.** Show/Hide details trades waveform height (`setHeight`,
  280↔96) for tile-row height (`--tile-h`, 7u↔16u). `applyEditMode()` in main.js.
  Newly-added (empty) slicers open in **edit**; restored/duplicated (already
  configured) open in **perform** — `detailsShown = !savedState`.
- **Master transport (Tier 1c done).** Shared AudioContext singleton
  (`audio-context.js`); every engine shares it (own gain/panner → shared
  destination; `dispose()` disconnects, never closes). `Transport.start(atTime)` +
  `startAllAligned()` anchor all tracks to one clock instant → sample-locked.
- **Per-slicer actions** — Add / **Duplicate** (clones a slicer via its `getState()`
  snapshot + a copied audio blob, slotted in right after) / Remove.
- **Unified DragControl** everywhere (`drag-control.js`); fluid relative-unit grid
  (see `public/CSS.md`). MIDI clock sync, WSOLA time-stretch + pitch, persistence
  (localStorage settings + IndexedDB audio).

## Open follow-ups (small)

- **Dead file:** `public/js/audio-slicer.js` (old monolithic `AudioSlicer`) is not
  an ES module and never loaded — safe to delete.
- **favicon 404:** browser requests `/favicon.ico` (none served) → one console
  404. Add a favicon (even empty) to silence it.
- **Preview self-ending linger:** if a waveform preview ends on its own (segments
  disabled) the controller doesn't emit 'stopped', so the floating transport could
  linger. Play-All-dismiss works; the self-end path is unexercised.
- **Manual passes not yet done headlessly:** MIDI device hot-plug with real
  hardware; the gaps-mode START (green) handle + pack-mode cascade absorbing a
  pre-existing gap (both share verified code paths).
- **Decision left open:** Vol/Pan/Pitch live in the always-visible header (they
  affect sequenced output), not the details panel. Move if you'd prefer.

## Export WAV — agreed design (2026-07-04, not yet built)

Render the mix offline to a downloadable stereo WAV. No new deps
(OfflineAudioContext + hand-rolled WAV header + Blob download).

**UI:** an "Export WAV" button in the top Master bar opens a small panel:
- a checkbox per slicer (label + filename), default all enabled — chooses which
  slicers render into the mix;
- a **Length (beats)** control, default = **LCM of the enabled slicers' beat
  counts** (so every loop lands on the boundary), freely overridable longer OR
  shorter;
- an **Export** button.

**Render:**
- `OfflineAudioContext(2, frames, sampleRate)`, sampleRate = shared-ctx rate,
  `frames = round(N · (60/masterBPM) · sampleRate)`. masterBPM = the effective
  master tempo (what plays). Output = exactly N beats.
- Per enabled slicer: build a gain(→panner)→destination graph on the offline ctx
  mirroring the engine (apply the slicer's Vol/Pan). Walk its tiles accumulating
  the master `stepSec`; resolve each tile via `getTilePlayback(tile, stepSec)` and
  schedule its buffer at the cumulative time through the slicer's gain, at its
  `playbackRate`. Loop the tile list (advancing time) until N beats; skip
  gaps/muted (silent slots).
- **Hard-cut at N beats:** don't schedule any voice start ≥ endTime; `source.stop`
  at endTime; truncate the rendered buffer to exactly `frames` → seamless loop.
- **Peak-normalize:** after render, scale all samples so the max abs peak hits
  ~0.99 (never clips; per-slicer Vol still sets the balance).
- Encode: interleave 2 channels → 16-bit PCM → RIFF/WAV `Blob` →
  `URL.createObjectURL` → `<a download="jsloop-export.wav">`.

**Implementation notes / risks:**
- Buffers created on the online ctx are reused in the offline ctx (same rate) — OK.
- Time-stretch readiness: `getTilePlayback` returns a null/repitch-fallback buffer
  on a stretch-cache miss. For a pitch-preserving export, prescan + await the WSOLA
  builds before rendering (else accept the repitch fallback). Decide at build time.
- Needs each slicer to expose its `beats` (for the LCM) — `getState().beats` or a
  small accessor on the `slicers[]` entry.
- 16-bit for now; 24-bit is a possible later option.

## Next: bigger bets

- **Tier 2 leftovers** — per-step reverse/gain/probability; transient detection +
  draggable non-uniform slice markers.
- **Richer meter** (future, per discussion) — selectable beat unit (dotted values,
  e.g. 1.5 = dotted quarter) or an odd/compound time-signature editor, building on
  the Beats model.
- **Optional:** restore a per-slicer solo/audition path if master-only transport
  feels too restrictive.

## Known limitations

- Per-track BPM is hidden (master governs); the element still exists in memory so
  `setMidiBpm`'s value/lock writes stay harmless.
- A track added mid-playback isn't phase-aligned until the next Play All (it
  self-anchors on individual start; Play All re-aligns everything).
