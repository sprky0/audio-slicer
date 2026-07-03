# Where we are / next steps

Working notes for jsloop. Updated 2026-07-03, branch
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
- **Tile sequencer.** Ordered variable-width tiles over the grid; reorder / edge-
  resize / split / merge / per-step pitch / mute, live while playing.
  - **Packed vs Gaps** — global Master-bar toggle. Packed = length-conserving
    (no gaps). Gaps = free placement + overwrite; gaps are silent tiles
    `{gap:true,w,src:0}`, edits go through a rasterize→rebuild cell grid.
- **Edit / perform layout.** Show/Hide details trades waveform height (`setHeight`,
  280↔96) for tile-row height (`--tile-h`, 7u↔16u). `applyEditMode()` in main.js.
- **Master transport (Tier 1c done).** Shared AudioContext singleton
  (`audio-context.js`); every engine shares it (own gain/panner → shared
  destination; `dispose()` disconnects, never closes). `Transport.start(atTime)` +
  `startAllAligned()` anchor all tracks to one clock instant → sample-locked.
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

## Next: bigger bets

- **Tier 2 leftovers** — per-step reverse/gain/probability; transient detection +
  draggable non-uniform slice markers; WAV export via OfflineAudioContext.
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
