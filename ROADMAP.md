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
most recent effort, PATCH increments per landed piece inside it.
`Frog/config.h` (`PLUG_VERSION_STR` / `PLUG_VERSION_HEX`) is the source of
truth; `Frog/scripts/stamp-version.sh` turns it into the build stamp the UI
shows. Tags are cut at milestones: `v<MAJOR>.<MINOR>-<milestone>`.

**The browser version** is preserved on the `js` branch. `public/` also
stays on this branch: it is the parity oracle (`Frog/tools/parity`) and the
UI reference for F10–F12; its removal is decided at F11.

**Priority.** Plugin and standalone builds on the Mac, with proof of build
at every step, come first; the appliance target is scaffolded early (F3) so
nothing in the engine or UI drifts away from it, but its on-device gates
are not blockers for the engine and UI work (F4–F11).

Design and reasoning: [docs/PORT_PLAN.md](docs/PORT_PLAN.md).

## Features

| ID | Feature | Status |
|----|---------|--------|
| F1 | Plan — survey tink-vst / ratfactory-linux-host, inventory the JS app, write PORT_PLAN.md + this roadmap, split `js` / `native` branches, settle name / record / storage decisions | Done 0.1.0, 0.1.1 |
| F2 | Scaffold — iPlug2 fork submodule (`Rat-Factory/iPlug2` @ ratfactory-linux, same pin as tink-vst), `Frog/config.h`, CMake + Xcode, empty plugin builds APP / VST3 / AU on the Mac, `.clang-format`, version stamp | Done 0.2.0, 0.2.1 |
| F3 | Appliance target scaffold — `platform/linux/` in tink-vst's shape (plugin lib, appliance binary, systemd unit, Docker arm64 build, deploy script); the arm64 cross-build must succeed from the Mac. On-device gates (boots on the Pi 3B with silence, `--render-wav`) are recorded when the board is at hand and do not block F4–F11 (↔ L5, L15) | Done 0.3.0 (cross-build); on-device gate open |
| F4 | Test harness — `engine/tests/run-tests.sh` (no framework, "ALL CHECKS PASSED"), `tools/render` CLI, fixtures dir, TSan race target | Done 0.5.0, 0.6.0 |
| F5 | Engine: model + grid — C11 `fg_pattern` (tiles, gaps, mods, caps), beat grid in samples (tempo re-anchor, sync phase, nudge), session JSON v1 = JS `version: 3` import / export | Done 0.5.0 |
| F6 | Engine: sequencer + voices — per-block absolute scheduling (skip-past, late-join offset), voice pool, raw / reversed region reader, envelopes (lin / exp / log / s, gain ceiling), declick, track vol / pan / mute, stereo mix | Done 0.6.0 |
| F7 | Engine: time-stretch + pitch — streaming WSOLA per voice (coarse-to-fine lag search), 4-point interpolating pitch / varispeed reader, bypass at factor 1, Pi 3 cost measured | Done 0.7.0 (F7.4 board measurement open) |
| F8 | Engine: parity — bounce JS fixtures natively, unstretched paths within −80 dBFS, stretched paths by per-step RMS + onset | Done 0.8.0 (`public/` kept, see above) |
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

### F2 — Scaffold

Generated from the fork's `IPlugInstrument` template with `duplicate.py`,
then cut down to the Tink shape (PORT_PLAN.md §3.6). The VST3 SDK is not
part of the fork: run `iPlug2/Dependencies/IPlug/download-vst3-sdk.sh` once
after cloning (it lands gitignored inside the submodule).

| ID | Subtask | Status |
|----|---------|--------|
| F2.1 | Submodule `iPlug2` → `Rat-Factory/iPlug2` branch `ratfactory-linux`, pinned to `d1cd40c48` (tink-vst's pin) | Done |
| F2.2 | `Frog/config.h`: Rat Factory identity (`'Frog'` / `'RatF'`, `com.ratfactory.*.Frog`), 0.2.0, 1024 × 600 with host resize, channel IO `0-2 2-2` (inputs reserved for F18), state chunks, no personal strings | Done |
| F2.3 | `Frog.h/.cpp`: silent `ProcessBlock`, BPM + Master params, placeholder panel (wordmark + build stamp). Template DSP, RPP, VS solution and workspace files removed | Done |
| F2.4 | Version stamp: `scripts/stamp-version.sh` → gitignored `version.h` (`FROG_*`), `ui/FrogVersion.h` readout with `__has_include` fallback; Run Script phase on all nine Xcode targets; `FrogVersionStamp` CMake target wired to every `Frog-<fmt>` target | Done |
| F2.5 | Builds: `xcodebuild -target APP / VST3 / AU -configuration Release` all succeed; APP launches and quits cleanly; `auval -v aumu Frog RatF` → AU VALIDATION SUCCEEDED; `cmake -S Frog -B build` configures | Done |
| F2.6 | Root `.gitignore` in Tink's shape (build dirs, stamp outputs, Linux cross-build output) | Done |
| F2.7 | Brand assets stored (**0.2.1**): `resources/img/frog-wordmark.svg` — FROG set in Cheltenham Bold, baked to outlines (no font needed at render time), fill `#E0332E`; `resources/img/rat-factory.svg` — the family maker mark, unmodified from tink-vst. The Cheltenham Bold OTF itself is **not** committed: the supplied file is Bitstream's (`CheltenhamBT-Bold`, "Confidential" in its name table) from a free-font site, so its redistribution licence is unknown; the outlines are all the UI needs. Use in the panel is deferred to F10 | Done |

### F3 — Appliance target scaffold

`platform/linux/` is a copy-and-edit of tink-vst's folder (the host repo's
`docs/PORTING_A_PLUGIN.md` says every port after the first two is). No
`iplug2-patches/` copy here: the fork is the submodule, and tink-vst's patch
README documents the series.

| ID | Subtask | Status |
|----|---------|--------|
| F3.1 | `platform/linux/CMakeLists.txt` + `frog-plugin/` (`frog_plugin` editor-less on `HEADLESS_API`; `frog_plugin_ui` + `libfrog-editor.so` with IGraphicsKMS / NanoVG GLES2 behind `FROG_APPLIANCE_EDITOR`) + `frog-appliance/main.cpp` (`rflh::IPlug2HeadlessProcessor<Frog>`, `rflh::runAppliance()`, data dirs `sessions/` and `samples/` under `RF_DATA_DIR`) | Done |
| F3.2 | `systemd/frog-appliance.service` from the host's template: `RF_DATA_DIR=/data/ratfactory/frog`, same sandbox and device policy, `Conflicts=` the other appliance units | Done |
| F3.3 | `docker-build-arm64.sh` (host build image, cmake, `frog-appliance`, then `--render-wav` 2 s as proof of build) and `deploy-to-pi.sh` (Tink's, re-pathed) | Done |
| F3.4 | Cross-build from the Mac: `frog-appliance` 1.5 MB arm64, `readelf -d` NEEDED = libasound, libstdc++, libm, libgcc_s, libc only; `libfrog-editor.so` carries libdrm / gbm / EGL / GLESv2 with `RPATH $ORIGIN`; resources copied beside the binary; render check writes 96000 silent frames | Done |
| F3.5 | **Cross-repo action, pending:** `ratfactory-linux-host/scripts/pi-switch-unit.sh` lists the appliance units it will switch between (`APPLIANCE_UNITS`); `frog-appliance.service` must be added there before `deploy-to-pi.sh --service` can make Frog the boot unit. Owner edit in the host repo; link the L-item or commit here when done | Planned — awaiting the host edit |
| F3.6 | On-device gate: deploy to the Pi 3B, boots to silence with 0 xruns, `--render-wav` on the board. Recorded when the board is at hand | Planned — needs the board |

### F4 — Test harness

| ID | Subtask | Status |
|----|---------|--------|
| F4.1 | `Frog/engine/tests/run-tests.sh` — compiles every `*_test.c` against `engine/*.c` with plain `cc -std=c11`, passes on "ALL CHECKS PASSED"; `check.h` holds the two macros; `FROG_TESTS_OUT / BUILD_ONLY / PREBUILT` for cross-running on the Pi | Done |
| F4.2 | `Frog/tools/render/main.c` — loads a session and prints its summary; `--seconds / --bars / --rate / --samples / --seed` parsed, the bounce itself lands with F6. Built by `frog_add_render_tool()` (engine.cmake) from Frog/CMakeLists.txt | Done |
| F4.3 | `engine/tests/fixtures/` — `js-session-basic.json`, a browser-version save exercising every tile and modifier field | Done |
| F4.4 | TSan race target: `pattern_race_test.c` publishes 3.4 M patterns, swaps 64 samples, pushes commands and drains rings against a processing audio thread; `run-tests.sh race` fails on any ThreadSanitizer report; quiet (0.6.0) | Done |

### F5 — Engine: model + grid

Namespace prefix is `fg_` (PORT_PLAN.md sketched `sl_` before the name was
chosen). Vendored: cJSON 1.7.18 (MIT) and dr_wav 0.14 (MIT-0 / public
domain) under `engine/third_party/`.

| ID | Subtask | Status |
|----|---------|--------|
| F5.1 | `frog_types.h` — caps (8 tracks, 128 units, 512 tiles, 128 mods, 8 voices, 128 hits), `fg_tile`, `fg_mod`, `fg_pattern` (grid, region, pitch, rand level, loop, tiles, mods), `fg_track_state`, `fg_session`; all fixed-size | Done |
| F5.2 | `grid.c` — beat ↔ sample mapping, restart, phase-continuous `set_tempo`, `sync_phase`, `nudge`; `grid_test` | Done |
| F5.3 | `pattern.c` — unit count, step beats, lattice snap, default row, validation (clamps, duplicate / out-of-range mods dropped, broken rows rebuilt), `mods_in_span`, `bar_pos`, `mod_fires` on a seedable xorshift, ratchet hit layout + per-hit rate; `pattern_test` checks the layouts against the browser version's formulas | Done |
| F5.4 | `session.c` — cJSON read / write of the browser save object (`slicers`, `seq.tiles`, `seq.mods`, `master`, `midi`, `seqEditMode`) plus `format` / `formatVersion` / `samplePath`; round trip is byte-equal on the pattern; `session_test` | Done |
| F5.5 | `engine/engine.cmake` — `frog_engine` static library + `frog-render`, included by Frog/CMakeLists.txt and linked into every format target; the Xcode project gains the engine when the plugin shell consumes it (F9) | Done |

### F6 — Engine: sequencer + voices

| ID | Subtask | Status |
|----|---------|--------|
| F6.1 | `sample.c` — float32 store, dr_wav load (down-mix to 2 ch, Kaiser-windowed-sinc resample to the engine rate, length cap), 16-bit WAV write | Done |
| F6.2 | `envelope.c` — the four shapes, proportional scaling of overlong fades, 5 ms clearance before a cut, gain ceiling; `envelope_test` | Done |
| F6.3 | `voice.c` — region reader (reverse = backwards index, no copies), optional WSOLA stage, 4-point Hermite reader at pitch × varispeed, envelope, declick ramp at a cut, natural-end detection, late-join skip | Done |
| F6.4 | `sequencer.c` — per-block port of `transport.js`: absolute grid times, skip-past, late start with offset, `beforeTile` pattern actions, voice modifiers (mute / rev toggle / gain multiply), ratchet spans with hits drained block by block (bounded voices), absorption of covered tiles, visual records + flash events; `fg_resolve_slot` = `getTilePlayback` | Done |
| F6.5 | `edit.c` — `reset_order` and `randomize` (bucket swaps + Fisher-Yates, locks anchored) allocation-free for the audio thread; `edit_test` | Done |
| F6.6 | `engine.c` — tracks, voice pools, grid, sample clock; UI ↔ audio lanes: pattern triple buffers both ways, SPSC command ring (play / stop / tempo / sync / nudge / mix), visual + flash rings, atomic sample swap with retire check; equal-power pan as the Web Audio panner, 5 ms mix smoothing | Done |
| F6.7 | `render.c` + `frog-render` — bounce a session through the live engine (LCM length default, normalise to 0.99 as the browser export), realtime ratio in the output; the 44.1 k click fixture renders with onsets on the grid at 120 and at 90 bpm | Done |
| F6.8 | `sequencer_test` — impulse-coded source: slot timing to the sample across 128-frame blocks, loop, reorder, mute, gap, reverse, gain, fade-in, late join in phase, ratchet hits, mute / gain modifiers, a reset action posting its change back, sample retirement, loop-off; 138 checks | Done |

### F7 — Time-stretch + pitch

Streaming WSOLA inside the voice replaces the JS pre-render cache (reasoning
in PORT_PLAN.md §3.2). F7.4 is the gate: if a stretched voice costs more
than ~5 % of a Pi 3 core unvectorised, fall back to a worker-thread
pre-render cache and record the measurement.

| ID | Subtask | Status |
|----|---------|--------|
| F7.1 | `wsola.c`: streaming port of `createTimeStretcher` — frame 1024, hop 256, Hann, COLA normalisation into an 8192-sample ring, output on demand (`fg_wsola_get`), late join at any output offset, reversed input read backwards without a copy; `wsola_test` (identity at 1×, length + pitch + level at 2× and 0.5×, reverse, late join) | Done |
| F7.2 | Coarse-to-fine lag search (every 4th lag over ±256, then ±3), stereo lag from channel 0 | Done |
| F7.3 | Voice reader: 4-point Hermite at pitch ratio × ratchet hit rate; through the engine a +12 st tile keeps its slot at 880 Hz, −12 st at 220 Hz, a pitch-mode ratchet plays hit 1 an octave up, and an 80 bpm fill stretches 1.5× at 440 Hz with no gap before the cut (`wsola_test`) | Done |
| F7.4 | Measure. `engine/tests/bench_stretch.c` (every slot stretched + pitched, stereo). **Apple M1 Pro, 48 k / 128:** 1 track 0.90 % of a core (worst block 2.8 % of the period), 4 tracks 3.68 % (0.92 % per track); at natural tempo the stage bypasses (4 tracks 0.50 %). The suite and the bench also build and pass under aarch64 GCC 12 in the host's build container (QEMU, so no timing). **Pi 3B figures: pending the board** — expected an order of magnitude above the M1 per core, inside the 25 % budget | In progress — needs the board |

### F8 — Engine: parity

`Frog/tools/parity/README.md` has the method and the browser quirks found
on the way. Bounces are committed, so `run-tests.sh` needs no browser.

| ID | Subtask | Status |
|----|---------|--------|
| F8.1 | `gen-fixtures.py`: 1.6 s stereo source (one sine burst per unit, distinct pitch per unit and channel) + five sessions on the browser's default grid at 150 bpm, every exact-case event on an integer sample | Done |
| F8.2 | `js-bounce.mjs`: headless Chrome seeds the session into localStorage + IndexedDB, loads the app, drives `renderMix` through the slicer registry's export hooks, saves float bounces and the restored state | Done |
| F8.3 | `parity_test.c`: **natural** and **mods** (mute / gain / rev / even ratchets / every-N / reset) match at −87 dBFS peak, −99 to −101 dBFS RMS; **edits** (reorder, mute, gap, reverse, gain, four fade curves, w = 2 and ½-unit tiles, vol 0.8, pan −0.3) at −41 dBFS peak / −81 dBFS RMS (the peak is the browser's 65-point fade curve); **stretch** (125 bpm) and **pitch** (±12 / +7 st, ramp and pitch-mode ratchets, ×4 hits) within 0.4 dB per unit with identical measured pitch | Done |
| F8.4 | Engine fixes found by parity: mono sources take the Web Audio mono pan law (−3 dB at centre); a mix change while a track is silent snaps instead of smoothing, so a loaded level is exact from the first sample | Done |

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
- **F2 complete (0.2.0).** Frog scaffold on the Rat Factory iPlug2 fork:
  config.h, silent plugin shell with a build-stamp panel, version stamp in
  both build systems, root gitignore. APP / VST3 / AU build Release with
  Xcode 26.6; auval passes; CMake configures. The VST3 SDK must be fetched
  once with the fork's `download-vst3-sdk.sh`.
- **F8 complete (0.8.0) — milestone M1, tag `v0.8-engine`.** The engine
  matches the browser version's own bounces: sample-exact on the unstretched
  paths, within 0.4 dB and identical pitch on the stretched and pitched
  ones. Two engine fixes came out of it (mono pan law, exact initial mix
  level). `public/` stays as the oracle and UI reference.
- **F7 complete bar the board measurement (0.7.0).** Streaming WSOLA
  under test on its own and through the engine (pitch shift, varispeed
  ratchet, slow-tempo fill). `M_PI` replaced by `FG_PI`: the engine is
  strict C11 and now builds and passes under aarch64 GCC in the host's
  container as well as clang on the Mac. Benchmark harness recorded with
  M1 Pro numbers; the Pi 3B run waits for the hardware.
- **F6 complete, F4 complete (0.6.0).** The engine plays: sequencer, voices,
  envelopes, mix, and the lock-free lanes between threads, all under test
  (sequencer_test 138 checks, TSan quiet). `frog-render` bounces sessions
  through the live engine; the click fixture lands on the grid at both the
  natural tempo and a stretched one. F7's streaming WSOLA landed with the
  voice code; its own tests and the Pi measurement are next.
- **F5 complete, F4 in progress (0.5.0).** The engine's model, grid and
  session format exist in C11 with 108 checks across three tests; the
  browser fixture imports field for field and round-trips. The render tool
  loads sessions; it bounces once F6 lands.
- **F3 scaffold complete (0.3.0).** `platform/linux/` in tink-vst's shape;
  the arm64 cross-build runs from the Mac in the host repo's Docker image and
  ends in a 2 s offline render. Binary links no GPU library; the editor is a
  module. Two items stay open: the host's `pi-switch-unit.sh` must learn
  `frog-appliance.service` (F3.5, owner edit), and the on-device gate (F3.6)
  waits for the board.
- **F2.7 brand assets (0.2.1).** FROG wordmark baked to SVG outlines from
  Cheltenham Bold (red), plus the family maker mark; the font file stays out
  of the repo pending its licence. Roadmap priority stated: Mac plugin /
  standalone builds and proof of build first; the appliance target is
  scaffolded early but its on-device gates do not block engine and UI work.
- **F18 split into F18.1–F18.3.** F18.1 (host capture) is a pending
  cross-repo action: the L-item in ratfactory-linux-host is to be opened by
  the owner, then cross-linked here. Nothing on F18 proceeds on the
  appliance until it exists; F18.2 can start on the Mac any time after F6.
