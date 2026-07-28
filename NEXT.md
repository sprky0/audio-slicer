# Where we are / next steps

Working notes for jsloop. Updated 2026-07-27, branch `develop`. (Completed work
lives in git history; this file is the current shape + what's next.)

## Current shape

Vanilla-JS, no-build browser audio slicer + tile sequencer. Load a clip, set a
region, and it's cut into a grid you arrange into a loop. Multi-instance, all
locked to one master clock.

- **Timing model — Beats + Step.** The selection is `beats` quarter-notes long
  (**Beats** control, {1,2,3,4,6,8,12,16}, default 4) → `BPM = 60·beats/selDur`.
  **Step** (1/2…1/32, default 1/16) is an independent subdivision. The slice grid
  is derived: `cells = beats × stepDenom/4` (`cellCount()` in main.js). Fixed
  quarter-note beat unit for now.
  - **Edits survive a Beats/Step change.** Per-step settings (mute, reverse,
    pitch offset, fades + curves, gain) re-apply positionally onto the new grid
    (`inheritStepSettingsFromPrevGrid`): new step p inherits old step
    `floor(p/r)` — an upsize copies a step's edits to all its new subdivisions;
    a downsize keeps the surviving positions' edits and drops the in-between
    ones. Arrangement (moves/resizes/gaps) resets to native audio — except
    LOCKED slices, which keep audio + loop position via the (now locked-only)
    `preserveMarkedTilesFromPrevGrid`, painted on top. Modifier-lane steps were
    already rescaled. (Mute-only tiles used to run-preserve with their audio;
    they now travel as positional settings like everything else.)
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
  - **Gain** — per-slice level (0–200%, dbl-click = 100%), the ceiling its fades
    rise to / fall from. Per-voice gain automation, never baked into buffers
    (caches stay level-agnostic); tile waveform amplitude tracks it (boosts clip
    at the rails) + a bottom-right badge when ≠100% (click resets).
    `tile.gain` (linear 0..2), persisted, export-faithful; ratchet hits inherit it.
  - **F.In / F.Out** — per-slice fade envelope, stored as 0..1 fractions of the
    slice's length, applied as per-voice gain automation at schedule time
    (`envelope.js` — live engine + WAV export share the code). Each fade has a
    **curve picker** (↗/↘): Lin / Exp (slow start) / Log (fast start) / S
    (cosine) — pure shapes in `FADE_SHAPES`, scheduled via setValueCurveAtTime,
    and the tile's envelope guides trace the actual shape. `tile.fadeIn/fadeOut`
    + `tile.fadeInCurve/fadeOutCurve`, persisted.
  - **Dup ◀ / Dup ▶** (clip only) — stamp a clone before/after, overwriting under it.
  - **Refill** (anything non-pristine) — set the entry's `src` to its grid
    position + default settings (fills a gap; resets a moved slice; clears
    offset/mute/rev/fades). With All: restores everything except locked slices.
  - (Removed: the old per-tile mute dot and the redundant details-panel "Slice" button.)
  - **Modifier lane** (below the tile row) — one optional modifier per STEP,
    pinned to the grid (tiles reorder/resize underneath). Click an empty cell to
    place one, click a chip for its settings popover, drag to move it. Each mod
    has an action — **Mute** / **Rev** / **Gain** (one-shot per-voice overrides
    at schedule time; rev TOGGLES an already-reversed slice; gain multiplies the
    slice's own level by `gainAmt`% — duck below 100, accent above; multiple
    gain mods under one wide tile multiply), **Rand** / **Reset** (virtual
    Randomize press at the Amt level / order-only reset — native play order,
    slices keep their settings; both respect locks), or **Ratchet** (see below) —
    and a fire mode: **Prob** (0–100%, rolled per pass) or **Every N** (1st of
    every N loops, counted from play start). A modifier applies to the whole tile
    covering its step, decided when that tile is scheduled; pattern actions fire
    via Transport.beforeTile so a rand on step 0 reshapes the loop INCLUDING its
    first slot. Chips flash at their audible moment. Persisted (`seq.mods`),
    rescaled on grid changes, applied in WAV export too (rand/reset against the
    render's working copy — a bounce evolves like a take; prob is unseeded by
    design). Prescan covers reversed stretch variants for tiles under rev mods
    (all tiles when a pattern mod could shuffle them).
  - **Ratchet modifier** — retrigger across `lenSteps` steps (1–16, clamped to
    the bar end), with three hit-layout modes (`mode` on the mod, Even/Ramp/
    Pitch toggle row in the popover): **even** — `subdiv` uniform hits per step
    (×1–8); **ramp** — spacing morphs from `subdiv` to `subdivTo` hits/step
    across the span (accelerando/ritardando; instantaneous density lerped by
    span position); **pitch** — even spacing, hit k shifted k·`pitchStep`
    semitones (−12…+12/hit) as VARISPEED (playbackRate multiplier, cumulative
    clamp ±48 st — pitch and hit-material speed move together, sampler-style).
    Hit layout lives in pure shared helpers `ratchetHitSteps`/`ratchetHitRate`
    (named exports of transport.js) used by BOTH the live scheduler and the WAV
    export, so bounces stay bit-faithful. Resolved once per scheduled slot
    (`Transport.resolveRatchet` → `_scheduleRatchet`): each hit replays the
    covered tile from its top at grid-locked times, cut at the next hit (always
    declicked); per-slice fades scale to ONE (average) HIT (`ov.envDurSec`
    through getTilePlayback). Beyond the tile's own width the span ABSORBS the
    tiles that start inside it (their slots are consumed); if Len is shorter
    than the tile, the rest of its slot is silent. Gaps/muted tiles don't
    ratchet. Mirrored exactly in the WAV export (`modHooks.resolveRatchet` in
    export-wav.js). Live visuals, all per-frame from the transport's rAF loop
    (no timers — survives grid slews and DOM rebuilds): the covered tile stays
    `.playing` for the whole span; chip + span brace hold a `.firing` glow; the
    waveform playhead restarts each hit — per-hit boundary fractions
    (`hitBounds` on the note) so ramped, non-uniform hits track correctly —
    sweeping only the source one hit consumes; modifiers in the ABSORBED part
    of the span (which never resolve) show `.superseded` (grey/dashed) while it
    sounds. A static `.mod-brace` (bottom+right border = duration + endpoint)
    runs from the end of the mod's step to the end of the covered span whenever
    Len > 1.
  - **Reset Order / Reset All** (toolbar) — Reset Order stable-sorts clips back
    to native (ascending `src`) play order, every tile KEEPING its settings;
    locks and gaps stay anchored (runs between anchors sort internally, so
    anchor positions are exact). Reset All rebuilds the pristine
    one-unit-per-tile default. The modifier-lane Reset action = Reset Order;
    the full wipe survives only behind Refill.
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
- **Channel mute** — a Mute toggle in the header (next to Vol/Pan/Pitch):
  `engine.setMuted` zeroes the output gain while `engine.volume` remembers the
  set level (Vol edits while muted stick, output stays 0). Sequencer keeps
  running. Persisted (`muted` in getState); Export panel default-unchecks muted
  channels (checking one still exports at full level — export has its own graph).
- **Loop default** — new lanes start with Loop ON (`seq.loop: true` + the button
  pre-lit); restored lanes keep their saved value.
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

- **Tier 2 leftovers** — transient detection + draggable non-uniform slice
  markers (the last one standing: gain, probability, fades + curve shapes,
  reverse have all shipped). A model-change design discussion first: the whole
  tile system assumes U equal units.
- **Modifier lane follow-ups** — true per-step chop (mute/reverse just the
  step's slice of a wider tile — needs mid-voice splitting); more actions
  (pitch nudge — gain shipped; retrigger shipped as Ratchet, now with
  Even/Ramp/Pitch modes); a ratchet option to let the tile's tail play through when Len < tile
  width; pitch-preserving ratchet pitch ramps (per-hit WSOLA variants) if
  varispeed ever feels wrong; seeded export probability if reproducible
  bounces are ever wanted.
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
