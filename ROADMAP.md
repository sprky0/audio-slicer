# Frog — Roadmap & Progress Log

Features are tracked with stable IDs (**F1**, **F2**, …). IDs only increment —
they are never renumbered or reused, even if a feature is dropped (mark it
**Dropped** instead). Subtasks use dotted IDs (**F1.1**, **F1.2**, …).

Statuses: `Planned` · `In progress` · `Done` · `Dropped`

**Parallel tracks.** The Raspberry Pi appliance platform is tracked in
`LINUX_ROADMAP.md` (**L1**, **L2**, …), which lives in `tink-vst` and
`ratfactory-linux-host`, not here. Rows below that depend on an L-item name
it; when such an item changes there, update the row here in the same
commit. Prefixes across roadmap files must not clash.

**Version.** `MAJOR.MINOR.PATCH`: MINOR is the F-number of the current or
most recent effort, PATCH increments per landed piece inside it. `config.h`
(`PLUG_VERSION_STR` / `PLUG_VERSION_HEX`) is the source of truth once F2
lands; until then the current version is recorded here: **0.1.1**. Tags are
cut at milestones: `v<MAJOR>.<MINOR>-<milestone>`.

**The browser version** is preserved on the `js` branch. `public/` remains
on this branch as the parity reference until F8 closes, then is removed here.

Design and reasoning: [docs/PORT_PLAN.md](docs/PORT_PLAN.md).

## Features

| ID | Feature | Status |
|----|---------|--------|
| F1 | Plan — survey tink-vst / ratfactory-linux-host, inventory the JS app, write PORT_PLAN.md + this roadmap, split `js` / `native` branches, settle name / record / storage decisions | Done 0.1.0, 0.1.1 |
| F2 | Scaffold — iPlug2 fork submodule (`Rat-Factory/iPlug2` @ ratfactory-linux, same pin as tink-vst), `Frog/config.h`, CMake + Xcode, empty plugin builds APP / VST3 / AU on the Mac, `.clang-format`, version stamp | Planned |
| F3 | Appliance target — `platform/linux/` copied from tink-vst (plugin lib, appliance binary, systemd unit, docker build, deploy), boots on the Pi 3B with silence, `--render-wav` works (↔ L5, L15) | Planned |
| F4 | Test harness — `engine/tests/run-tests.sh` (no framework, "ALL CHECKS PASSED"), `tools/render` CLI, fixtures dir, TSan race target | Planned |
| F5 | Engine: model + grid — C11 `sl_pattern` (tiles, gaps, mods, caps), beat grid in samples (tempo re-anchor, sync phase, nudge), session JSON v1 = JS `version: 3` import / export | Planned |
| F6 | Engine: sequencer + voices — per-block absolute scheduling (skip-past, late-join offset), voice pool, raw / reversed region reader, envelopes (lin / exp / log / s, gain ceiling), declick, track vol / pan / mute, stereo mix | Planned |
| F7 | Engine: time-stretch + pitch — streaming WSOLA per voice (coarse-to-fine lag search), 4-point interpolating pitch / varispeed reader, bypass at factor 1, Pi 3 cost measured | Planned |
| F8 | Engine: parity — bounce JS fixtures natively, unstretched paths within −80 dBFS, stretched paths by per-step RMS + onset; remove `public/` from this branch | Planned |
| F9 | Plugin shell — iPlug2 params (BPM, per-track vol / pan / pitch / mute, edit mode), state chunks with version, `OnIdle` loading + decode + resample, MIDI clock PLL (appliance stamps + plugin offsets), Start / Continue / Stop, host transport sync in DAWs | Planned |
| F10 | MVP UI — owned controls: DragControl, TransportBar, WaveformControl (region handles, zoom / trim, playhead), TileRowControl (select, drag, edge-resize, mini waveforms), slice toolbar, FileList; band layout engine with stable IDs; edit / perform layouts; Pi 3 render budget | Planned |
| F11 | MVP release — one track end-to-end on the appliance and the Mac, factory session, docs/ARCHITECTURE.md, tag `v0.11-mvp` | Planned |
| F12 | Live-performance pass — critique the UI with the device in hand: permanent big actions, pad / trigger mode, what moves to MIDI | Planned |
| F13 | Modifier lane — one modifier per step, prob / every-N, mute / rev / gain actions, rand / reset pattern actions via `beforeTile`, pinned settings panel, chip flash / firing / superseded visuals | Planned |
| F14 | Ratchet + pattern tools — even / ramp / pitch hit layouts, span absorption, randomize with lock buckets, Reset Order / Reset All, lock, dup, refill, split / merge, Packed vs Gaps (rasterize → rebuild), grid-change inheritance | Planned |
| F15 | Multi-track — up to `max_tracks` (4 on Pi 3), focus track + summary rows, add / duplicate / remove, in-phase join, master mix | Planned |
| F16 | Performance MIDI — note-triggered slices, CC map (learn), program change = session, `appliance.conf` keys | Planned |
| F17 | Bounce — offline render of the master mix to `RF_DATA_DIR/exports`, length in beats, normalise | Planned |
| F18 | Record — runs in parallel with the MVP (decided 2026-10-03). Two halves with one status: **host capture** (ALSA capture PCM + an input path on `Processor`), an L-item to open in ratfactory-linux-host and link here once numbered; **Frog side** (record into a track, re-slice live), developed on the Mac first. Both rows move together, same commit | Planned |
| F19 | Transient markers — detection + draggable non-uniform slice points (needs the model change noted in the JS NEXT.md) | Planned |

### F1 — Plan

| ID | Subtask | Status |
|----|---------|--------|
| F1.1 | Survey tink-vst conventions (roadmap, version, fork, style, RT rules) and ratfactory-linux-host constraints (audio, MIDI, display, hosting, storage) | Done |
| F1.2 | Inventory the JS app: data model, grid math, edit ops, scheduling policy, UI structure | Done |
| F1.3 | Write docs/PORT_PLAN.md: architecture, stretch strategy, clock sources, caps, UI adaptation, milestones, tests, risks | Done |
| F1.4 | Branches: `js` preserves the browser app at its last commit; `native` carries the port | Done |
| F1.5 | Decisions (2026-10-03, **0.1.1**): name **Frog**; record (F18) runs in parallel with the MVP with both halves tracked in sync; float32 sample storage accepted, all shared math stays `double` | Done |

### F18 — Record

Two halves, one status. Neither moves without the other's row being updated
in the same commit, and every status change gets a line in the Log.

| ID | Subtask | Status |
|----|---------|--------|
| F18.1 | **Host capture — cross-repo action, pending.** Open an L-item in `ratfactory-linux-host/LINUX_ROADMAP.md` (mirrored into `tink-vst`): ALSA capture PCM on the same device as playback (full duplex, same period / rate), an input path on `rflh::Processor` (an `inputs` pointer on `process()` or a `processIO()` overload), `IPlug2HeadlessProcessor` forwarding, `--render-wav --input FILE` for offline checks. Once numbered, write its L-id into this row and F18's master row, and the F18 ids into that L-row. This repo is read-only on the host repo; the owner opens it | Planned — awaiting the L-item |
| F18.2 | Frog side on the Mac first (RtAudio inputs): arm / record into a track's sample store, auto-region on stop, re-slice live; publish the new store by the F6 pointer swap so playback is never interrupted | Planned |
| F18.3 | Join the halves on the appliance: record through the host capture path; 0 xruns over 10 min while recording and playing on the Pi 3B | Planned — after F18.1 |

### F7 — Time-stretch + pitch

Streaming WSOLA inside the voice replaces the JS pre-render cache (reasoning
in PORT_PLAN.md §3.2). F7.4 is the gate: if a stretched voice costs more
than ~5 % of a Pi 3 core unvectorised, fall back to a worker-thread
pre-render cache and record the measurement.

| ID | Subtask | Status |
|----|---------|--------|
| F7.1 | Port `createTimeStretcher` as a streaming stage with fixed work buffers (frame 1024, hop 256, Hann, COLA normalisation) | Planned |
| F7.2 | Coarse-to-fine lag search; stereo lag from channel 0 | Planned |
| F7.3 | 4-point interpolating reader after the stretch stage: pitch ratio × ratchet hit rate (varispeed), cumulative clamp ±48 st | Planned |
| F7.4 | Measure on the Pi 3B: `--render-wav` realtime ratio and load-meter peaks for 1 / 4 / 8 stretched voices; record in docs/ | Planned |

### F10 — MVP UI

| ID | Subtask | Status |
|----|---------|--------|
| F10.1 | `ui/Style.h` tokens (unit, control height 6 u, intent colours) and `ui/Layout.h` band layout (fixed / flex rows and columns, min sizes), resolved on resize and mode change | Planned |
| F10.2 | DragControl: drag any direction, tap-to-value, double-tap detent, enum and value modes | Planned |
| F10.3 | WaveformControl: cached min / max peaks, region handles, zoom / trim, dirty-rect playhead | Planned |
| F10.4 | TileRowControl: variable-width tiles with mini waveforms and badges, select → drag / edge-resize, double-tap split | Planned |
| F10.5 | TransportBar + slice toolbar + FileList over `RF_DATA_DIR/samples` | Planned |
| F10.6 | Pi 3 budget: at rest ≤ 3 % of a core, drag ≤ 20 %, frame cap 30 fps, 0 xruns over 10 min with the UI driven | Planned |
| F10.7 | Stable dotted control IDs + Debug `--dump-layout`; `layout.json` hot reload if steering by numbers is slow | Planned |

## Log

### 2026-10-03
- **F1 complete.** Surveyed the sibling repos read-only, inventoried the JS
  app (data model, grid math, edit ops, scheduling policy, UI), and wrote
  docs/PORT_PLAN.md: C11 engine + C++17 iPlug2 shell, streaming WSOLA
  instead of a pre-render cache, pattern snapshots by triple buffer so edits
  never interrupt playback, MIDI clock PLL carried over unchanged, session
  format = the JS save object, single-touch UI adaptation for the 1024 × 600
  panel, milestones M0–M8 with gates. Branch `js` preserves the browser app;
  `native` carries the port. Version 0.1.0.
- **F1.5 decisions (0.1.1).** The product is **Frog**. Record (F18) is
  scheduled in parallel with the MVP: the host capture half becomes an
  L-item in ratfactory-linux-host, the Frog half develops on the Mac first,
  and the two rows carry one status updated in the same commit. Float32
  sample storage is accepted with all shared math kept in `double` for
  cross-family comparability.
- **F18 split into F18.1–F18.3.** F18.1 (host capture) is a pending
  cross-repo action: the L-item in ratfactory-linux-host is to be opened by
  the owner, then cross-linked here. Nothing on F18 proceeds on the
  appliance until it exists; F18.2 can start on the Mac any time after F6.
