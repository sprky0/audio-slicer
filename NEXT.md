# Where we are / next steps

Working notes for jsloop. Updated 2026-06-30, branch
`feature-slice-drag-sequencer`.

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

## Next steps

### Finish the DragControl rollout (the two deferred, delicate controls)
- **Region Start/End sliders** (in the Show-details panel) — wrap the two range
  inputs. They pair with the waveform markers and are updated programmatically
  by `syncControls()` / marker drags, so each programmatic set needs a
  `ctrl.syncFromEl()` (no change-loop). Verify region selection + re-slice still
  behave.
- **MIDI device dropdown** — dynamic option list (hot-plug). Needs a
  `refreshOptions()` on DragControl, called from `populateMidiDevices()`. Device
  names are long → confirm truncation reads OK in a cell.
- Remove dead `knob.js` and the `.knob*` CSS once nothing references them.

### Verify live with a real clip (not yet done headlessly)
- File-details readout (name · duration · sample rate) on load + restore.
- DragControl drag feel (sensitivity, detents, stepping, vertical drag).
- Multi-track master sync: load 2 clips → Play All → confirm they lock; sweep
  master BPM and confirm both follow, bar-aligned.
- Time-stretch overlay appearing/filling on BPM/pitch change; judge the
  `mix-blend-mode: difference` look on a real waveform (keep / soften / plain).

### Bigger bets (roadmap — see memory `jsloop-tier-roadmap`)
- **Shared AudioContext (Tier 1c)** — the master sync is currently
  start-together + shared-tempo, not sample-locked, because each slicer has its
  own AudioContext. A shared context would give tight sample-lock and is the
  foundation for a real groovebox. Largest item; unblocks true sync.
- **Tier 2 leftovers** — per-step reverse/gain/probability, transient detection
  + draggable non-uniform slice markers, WAV export via OfflineAudioContext.
- **Optional:** restore a solo/audition path (per-slicer preview) if master-only
  transport feels too restrictive.

## Known limitations
- Multi-track sync drifts over long runs (independent AudioContexts); Play All
  re-aligns. A track added mid-playback isn't phase-aligned until the next
  Play All.
- Per-track BPM is hidden (master governs); the element still exists in memory so
  `setMidiBpm`'s value/lock writes stay harmless.
