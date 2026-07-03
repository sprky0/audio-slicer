# Where we are / next steps

Working notes for jsloop. Updated 2026-07-02, branch
`feature-slice-drag-sequencer`.

## Checkpoint committed 2026-07-02 (`7438a51`)
Committed the whole stretch: finished DragControl rollout (region sliders + dynamic
MIDI dropdown + dead-knob removal + grid/getValue fixes), master-BPM clamp fix,
Tier 1c shared AudioContext + aligned start, Packed/Gaps sequencer modes, and the
`.slicer-container` / `.sequencer` border+padding tweaks. All verified live.

## "Two mindsets" layout — DONE (2026-07-02, UNCOMMITTED)
Show/Hide details now switches each slicer between an **edit** and a **perform**
mindset, trading vertical space between the waveform and the sequencer tile row.
- Details panel holds the source-editing controls: Source label, Select File +
  file info, Units, Slice, Trim, Reset, region Start/End (numbers + sliders).
- Always-visible header: Vol/Pan/Pitch (kept out of details — they shape the
  audible/sequenced output), Show/Hide details, Remove.
- Details SHOWN (`.editing`, default): waveform tall (JS `WaveformView.setHeight`
  = 280px), tile row short (`--tile-h` 7u). Details HIDDEN: waveform short (96px),
  tile row tall (16u). `applyEditMode()` in main.js drives both; slice hint hidden
  in perform mode.
- Verified live: default edit mode; toggle trades 280↔96 waveform / 60↔132 tiles,
  reversible; controls in the right places; no errors.
- Interpretation to confirm w/ user: Vol/Pan/Pitch left in the header (they affect
  seq playback). If they should live in details, move the 3 appends.
- Default is edit mode (details open) so a fresh user sees the editing controls +
  big waveform; not persisted (resets each load).

## Recently landed (this stretch of work)

- **UI design system** (`50f37c4`) — fluid relative-unit grid driven by one base
  unit + `--ui-scale`; auto-fit toolbars that reflow with no media queries;
  per-item column/row spans via `setSpan()`; semantic button states
  (neutral/blue, primary/green, danger/red with hover/active/focus/disabled);
  `.ui-label` section chips (`makeLabel`).
- **Time-stretch progress overlay** (`e364016`) — per-slicer band on the bottom
  1/8 of the waveform showing WSOLA build progress. Stretch is now incremental
  (`createTimeStretcher`, chunked), sample-accurate, eager-prescanned on
  tempo/pitch change, with bar/percentage parity.
- **Master clock / groovebox transport** (`40f6c2a`) — top-bar Master tempo +
  Play All / Stop All drive every slicer. Master-only transport (per-slicer
  Play/Stop removed). Clips sync as bar multiples/subdivisions of their
  Units/Step split. First clip sets the master tempo. MIDI clock overrides when
  enabled.
- **Unified DragControl** (`7036740`) — one button-shaped control (fill + bright
  leading edge + centered `Label value`) replacing knobs/sliders/dropdowns.
  Drag any direction; smooth (with detent) or stepped. Wraps a number/range
  input or a `<select>`, or runs standalone. Converted: Volume/Pan/Pitch, Master
  BPM, Amount, Units, Step.
- **DragControl click-to-jump** — a click (movement < 4px) jumps to the absolute
  clicked position; click-and-drag stays relative.
- **Toggle-button style** — clear OFF (muted/recessed + hollow pip) vs ON (lit
  green + glow + filled pip), via a reusable `.toggle-btn` class. Applied to
  Loop, Show/Hide details, and the MIDI **Sync** control (was a checkbox).
- **Preview transport overlay** — clicking the waveform auditions (free-run,
  loops); a small floating Play/Pause + Stop fades in over the waveform (no
  layout shift). Stop ends the preview and dismisses it. Only previews show it
  (the sequencer doesn't emit playstatechange); Play All stops any preview. Edge:
  if a preview ends on its own (segments disabled) the controller doesn't emit
  'stopped', so the overlay would linger — minor follow-up.

## Next steps

**Agreed plan (2026-07-02):** verify the current stack live with a real clip,
then finish the DragControl rollout — in that order. Both are committed before
any new features. Tier 1c (shared AudioContext) and the other bigger bets are
explicitly DEFERRED until this unverified pile is closed out; we don't layer a
large refactor on top of unverified UI work.

### 1. Verify live with a real clip — DONE (2026-07-02)
Verified end-to-end by driving headless Chrome over CDP against `public/`
served locally, loading a real clip (`marcus.mp3`, 0:59). All PASS:
- ✅ File-details readout — `marcus.mp3 · 0:59 · 44.1 kHz` on load.
- ✅ DragControl feel — click-to-jump lands exactly (BPM 120), relative drag
  smooth, CLOCK indicator tracks the value in-range.
- ✅ Multi-track master sync — 2 tracks, Play All → both bar-locked (identical
  "Playing slice N", identical playhead); sustained across loops; BPM sweep to
  305 keeps control + CLOCK agreeing. 30 real `AudioBufferSourceNode.start()`
  calls confirmed scheduling.
- ✅ Time-stretch overlay — `.wave-task` fill/label live at opacity 1 during
  stretch (see note below).
- ✅ Preview transport overlay — waveform click fades in ⏸/⏹; Play All dismisses.
- ✅ (bonus) Persistence — 2-slicer session (clips + Loop state) auto-restored
  on reload.

Findings to act on:
- **⚠️ Master BPM control vs actual tempo diverge for out-of-range derived BPM.**
  A 0:59 untrimmed clip → one-bar tempo 240/59 ≈ 4.1 BPM. `setMasterBpm`
  (`main.js:1806-1808`) stores raw 4.06 in `masterClock.bpm` (CLOCK shows
  4.1 BPM, `applyMasterTempo` schedules at 4.06) but `masterBpmControl.setValue`
  clamps *display* to the control floor of 20 → control reads "BPM 20". Two
  readouts disagree. Only bites untrimmed/pathological input. Fix: clamp
  `masterClock.bpm` to the control's [20,400] range (or widen the control min).
  Do before Tier 1c. → being fixed now.
- Time-stretch progress band completes too fast (eager prescan + cache) to be
  perceptible in normal use → the "keep/soften/plain" `mix-blend-mode` question
  is moot; it's not obtrusive because it's barely visible. Leave as-is.
- Preview *self-ending* linger edge NOT exercised (hard to force headlessly);
  Play-All-dismiss works. Documented follow-up stands.
- One 404 = `favicon.ico` (browser default; no ref in index.html). Add a
  favicon to silence the console error.

### 2. Finish the DragControl rollout — DONE (2026-07-02)
- ✅ **Region Start/End sliders** — the two range inputs are now wrapped in
  DragControls (`startSliderCtrl`/`endSliderCtrl`). The number inputs stay as the
  region source-of-truth (read by Slice/Trim/subdivisions); the sliders are the
  visual pair for the waveform markers. `syncFromEl()` is called at all four
  programmatic-set sites: `syncControls()`, the Reset path, the file-load path,
  and the persistence-restore path. Verified live: control drag/click-jump, typed
  number entry, waveform marker drag, Reset, and reload-restore all sync
  bidirectionally + re-slice correctly.
- ✅ **MIDI device dropdown** — added `DragControl.refreshOptions()` (re-reads a
  wrapped `<select>`'s options for hot-plug). `populateMidiDevices()` calls it +
  mirrors the disabled state; explicit device pick calls `syncFromEl()`. Long
  names truncate via `.drag-label` ellipsis. Logic verified via the real module
  (empty → hot-plug 2 → select long-named → unplug); real-hardware hot-plug still
  wants a manual pass with a MIDI device connected.
- ✅ **Dead code removed** — `knob.js` deleted, `.knob*` CSS block + `--knob-size`
  token + stale comments gone. Final load shows 0 knob elements.

Two bugs surfaced + fixed during verification:
- **Details panel clipped onto the waveform.** `.toolbar-cluster` used
  `grid-auto-rows: var(--control-h)`, locking the full-width details panel to one
  32px row; the taller stacked region drag-controls overflowed onto the canvas.
  Fixed → `minmax(var(--control-h), auto)` (normal control rows unchanged; only
  taller full-width content grows).
- **`DragControl.getValue()` crashed on an empty stepped control**
  (`this.options[this._idx].value` when options is `[]`). The initially-empty MIDI
  select is the first empty-select wrapping, so it exposed the landmine. Guarded
  to return `null` when there are no options.

### Shared AudioContext (Tier 1c) — DONE (2026-07-02)
- ✅ **One shared context.** New singleton `audio-context.js` (`getAudioContext()`);
  every `AudioEngine` uses it instead of `new AudioContext()`, keeping its own
  gain/panner → shared destination (per-slicer vol/pan intact). `dispose()` now
  disconnects only its own nodes — never `close()`s the shared context. Verified:
  exactly ONE AudioContext for N slicers; removing a slicer mid-play leaves the
  others' clock running (closeCount 0, surviving track keeps scheduling).
- ✅ **Master-aligned start.** `Transport.start(atTime?)` anchors to an explicit
  shared-clock time; `startAllAligned()` resumes the context once, computes ONE
  `when = currentTime + 0.12`, and (re)starts every track at it. Used by Play All
  AND the MIDI Start/Continue path. Verified: both tracks' first tiles fire at the
  *bit-identical* shared-clock instant (was per-context-approximate before) →
  true sample-lock, no inter-context drift.
- Note: `getRegionBuffer`, time-stretch output, and the clock indicator all read
  `slicer.engine.audioContext`, so they picked up the shared context for free.
- Dead file spotted: `public/js/audio-slicer.js` (old monolithic `AudioSlicer`)
  is not an ES module and never loaded — safe to delete in a cleanup pass.

### Sequencer edit modes: Packed vs Gaps — DONE (2026-07-02)
Global toggle in the Master bar (`seqEditMode` 'pack'|'gaps', persisted). Applies
to every track's tile row.
- **Packed** (default, unchanged): reorder repacks, edge resize conserves Σ w by
  trading units with the neighbour — never any gaps.
- **Gaps** (new, free-placement + overwrite): moving a clip leaves a silent gap
  where it was and overwrites whatever it lands on; edge resize leaves a gap when
  shrinking and overwrites the neighbour when growing.
- Model: gaps are silent tiles `{ gap:true, w, src:0 }` in the shared ordered
  list, so Σ w still equals the bar (U) in both modes. Gaps-mode edits rasterize
  the bar to a SUBSTEP-resolution cell grid, mutate, and rebuild the clip/gap list
  (`rasterizeBar`/`rebuildFromCells`/`paintCells`/`moveTileGaps`/`computeResizeGaps`).
  Split remnants of an overwritten clip keep the clip's colour/source anchor.
- Transport plays a gap as a silent slot (getTilePlayback returns a null buffer;
  onTileVisual clears the playhead + shows "Gap N of M"). `.seq-gap` renders as a
  dashed hatched well; gaps are passive (no handles/drag/merge).
- Verified live (headless CDP + real clip): toggle + default Packed for a fresh
  user; pack-move leaves no gaps; gaps-move leaves a gap + overwrites; gaps
  end-resize shrink→gap, grow→overwrite; Σ w stays 16; gap is silent and the
  transport advances through it; gaps + mode survive reload. NOT drag-tested (logic
  only, symmetric with END): the START (green) handle in gaps mode, and pack-mode
  cascade absorbing a pre-existing gap (the src:0 safety).

### Remaining bigger bets
- **Tier 2 leftovers** — per-step reverse/gain/probability, transient detection
  + draggable non-uniform slice markers, WAV export via OfflineAudioContext.
- **Optional:** restore a solo/audition path (per-slicer preview) if master-only
  transport feels too restrictive.

## Known limitations
- Per-track BPM is hidden (master governs); the element still exists in memory so
  `setMidiBpm`'s value/lock writes stay harmless.
- (Resolved by Tier 1c) Multi-track sync no longer drifts — all tracks share one
  AudioContext and start on one anchor. A track added mid-playback still isn't
  phase-aligned until the next Play All (it self-anchors on individual start).
