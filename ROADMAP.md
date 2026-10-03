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
most recent effort, PATCH increments per landed piece inside it. Versions
only go up: when a lower-numbered effort lands after a higher one has
already set MINOR, it takes the next PATCH instead (0.17.1 = F15).
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
| F9 | Plugin shell — iPlug2 params (BPM, clock source, per-track vol / pan / mute), state chunk with the session JSON, `OnIdle` loading + decode + resample, MIDI clock PLL, Start / Continue / Stop, host transport sync in DAWs | Done 0.9.0 |
| F10 | MVP UI — owned controls: DragControl, transport bar, WaveformControl (region handles, playhead), TileRowControl (select, drag, edge-resize, mini waveforms), slice toolbar, FileList; band layout; edit / perform layouts; Pi 3 render budget | Done 0.10.0 (F10.6 budget and F10.7 IDs open) |
| F11 | MVP release — one track end-to-end on the Mac (appliance: in the container; on the board pending), docs/ARCHITECTURE.md, README, tag `v0.11-mvp` | Done 0.11.0 (on-device gate open, with F3.6) |
| F12 | Live-performance pass — critique the UI with the device in hand: permanent big actions, pad / trigger mode, what moves to MIDI | Planned |
| F13 | Modifier lane — one modifier per step, prob / every-N, mute / rev / gain actions, rand / reset pattern actions via `beforeTile`, pinned settings panel, chip flash / firing / superseded visuals | Done 0.13.0 |
| F14 | Ratchet + pattern tools — even / ramp / pitch hit layouts, span absorption, randomize with lock buckets, Reset Order / Reset All, lock, dup, refill, split / merge, Packed vs Gaps (rasterize → rebuild), grid-change inheritance, the All broadcast, zoom (Trim / Full) | Done 0.14.0 |
| F15 | Multi-track — up to `FG_MAX_TRACKS`, a track tab strip with the focused track in the editor, add / duplicate / remove, in-phase join, master mix | Done 0.17.1 (`max_tracks` conf key open) |
| F16 | Performance MIDI — note-triggered slices, CC map (learn), program change = session, `appliance.conf` keys | In progress 0.18.1 (notes and sessions-as-presets done; CC map and conf keys open) |
| F17 | Bounce — offline render of the master mix to `RF_DATA_DIR/exports`, one bar (LCM of the tracks' beats), normalised | Done 0.17.0 |
| F18 | Record — two halves with one status: **host capture** (ALSA capture PCM + an input path on `Processor`), an L-item to open in ratfactory-linux-host and link here once numbered; **Frog side** (record into a track, re-slice live), developed on the Mac first. Both rows move together, same commit | In progress 0.18.0 (Frog side done; host half awaiting the L-item) |
| F19 | Transient markers — detection + draggable non-uniform slice points (needs the model change noted in the JS NEXT.md) | Planned |

### F1 — Plan

| ID | Subtask | Status |
|----|---------|--------|
| F1.1 | Survey tink-vst conventions (roadmap, version, fork, style, RT rules) and ratfactory-linux-host constraints (audio, MIDI, display, hosting, storage) | Done |
| F1.2 | Inventory the JS app: data model, grid math, edit ops, scheduling policy, UI structure | Done |
| F1.3 | Write docs/PORT_PLAN.md: architecture, stretch strategy, clock sources, caps, UI adaptation, milestones, tests, risks | Done |
| F1.4 | Branches: `js` preserves the browser app at its last commit; `native` carries the port | Done |
| F1.5 | Decisions (2026-10-03, **0.1.1**): name **Frog**; record (F18) runs in parallel with the MVP with both halves tracked in sync; float32 sample storage accepted, all shared math stays `double` | Done |

### F16 — Performance MIDI

| ID | Subtask | Status |
|----|---------|--------|
| F16.1 | Note triggers: Note On on channel n plays one unit of track n − 1 (note 36 = unit 0, chromatic) at its natural rate, velocity as level, one-shot, ringing out, on top of whatever the sequencer plays; a visual record with tileIndex −1 sweeps the waveform. `fg_engine_midi_msg` carries channel messages into the per-block event list; `trigger_test`; the appliance shell test sends a note through the adapter | Done |
| F16.2 | CC map with learn (volume, pan, mute, randomize, Amt, master), stored in the session | Planned |
| F16.3 | Sessions as presets (0.18.1): `<data dir>/sessions/*.json`, sorted; **Save** writes the current state (named after the loaded session or a stamp), **Sessions** lists them to load; `StepPreset` and MIDI Program Change pick one and the load lands on the idle tick (never the audio thread); `GetCurrentPresetName / Program` feed the appliance panel's status line. `shell_test` saves two, steps, wraps, program-changes | Done |
| F16.4 | `appliance.conf` keys: `max_tracks=`, `midi_trigger_base=`, `clock=` | Planned |

### F15 — Multi-track

PORT_PLAN.md §4 proposed stacked summary rows for the other tracks; one
tab strip costs the same height for any track count and keeps the editor
bands tall on 600 px, so that is what landed.

| ID | Subtask | Status |
|----|---------|--------|
| F15.1 | `Frog`: tracks in use (`NumTracks`, 1..8) with `AddTrack` (fresh default row), `RemoveTrack` (last one: row reset, sample retired, mix parameters reset), `DuplicateTrack` (pattern copied, sample reloaded by path, mix copied); the session carries the count | Done |
| F15.2 | `ui/TrackStripControl.h`: one tab per track (number, name, lit while it sounds, dimmed when muted, the focused one framed) plus + Track / Dup / − Track | Done |
| F15.3 | Focus switching rebinds the editor bands: waveform, tile row, modifier lane, the param-linked Vol / Pan / Mute (`SetParamIdx`), header values; selection and modifier panel reset | Done |
| F15.4 | `editor_shot_test`: duplicate track 1, tap the second tab, render (`docs/F15_evidence/`) | Done |
| F15.5 | Appliance cap from `appliance.conf` (`max_tracks=`, 4 on the Pi 3) through the host's board-settings path; today the plugin allows all eight everywhere | Planned |

### F17 — Bounce

| ID | Subtask | Status |
|----|---------|--------|
| F17.1 | `Frog::Bounce()`: snapshot the session, render it on a worker thread through `fg_render_session` (its own engine; samples reloaded from their paths) to `<data dir>/exports/frog-<stamp>.wav`, normalised, LCM-of-beats long at the current tempo; one at a time; results collected on the idle tick | Done |
| F17.2 | Export button in the transport bar whose text follows the status (Export / Exporting / Exported / Export failed) | Done |
| F17.3 | `shell_test`: a bounce from the restored instance finishes, reads back, peaks at 0.99 and is one bar long at the session tempo | Done |
| F17.4 | Length in beats and a track subset from the UI (the browser's export panel) | Planned |

### F14 — Ratchet + pattern tools

Most of this list landed inside earlier efforts: hit layouts and span
absorption (F6), randomize / reset / lock / dup / refill / split / merge /
gaps raster / grid-change inheritance (F10.9). What remained:

| ID | Subtask | Status |
|----|---------|--------|
| F14.1 | **All** broadcast toggle in the slice toolbar: while lit, Mute / Rev / Gain / fades / curves / Pitch apply to every clip (toggles read "on" only when every clip is on, so a tap from a mixed state turns all on, from all-on all off); Refill with All refills every unlocked entry; Lock and Dup stay per slice | Done |
| F14.2 | Zoom: **Trim** makes the selection the view (`virtualStart/End`, the selection becomes 0..1 of it), **Full** shows the whole sample again with the audible region kept; the waveform draws the virtual window and maps the playhead into it; tile waveforms already read through it | Done |
| F14.3 | Per-slice pitch is the toolbar's Pitch control (the browser's mouse wheel) | Done |

### F13 — Modifier lane

The engine side (resolution at schedule time, ratchet spans, pattern
actions, flash events) landed with F6; this is the UI and the plumbing.

| ID | Subtask | Status |
|----|---------|--------|
| F13.1 | `ui/ModLaneControl.h`: one cell per step pinned to the grid; tap an empty cell places a Mute modifier, tap a chip selects it, drag moves it (occupied steps skipped); glyph + intent colour per action; a brace from a ratchet's chip to the end of its span | Done |
| F13.2 | Settings panel in place of the two toolbar rows while a chip is selected: Action, Fire (Prob / Every) with Chance or "1 in N", Level (gain), ratchet Mode / Hits / To / Pitch / Len enabled per action and mode, Remove, Done; edits validate and publish | Done |
| F13.3 | Live feedback from the engine's rings: chips flash for 180 ms at their audible moment; a ratchet's chip and brace stay lit for the span; chips inside the span past the covered tile grey out; the waveform playhead restarts per hit (ramp hits located by their boundaries, sweeping only the material a hit consumes). `fg_visual` carries the slot's grid step for this | Done |
| F13.4 | `editor_shot_test` places a 2-step ramp ratchet and a mute, taps the chip; `docs/F13_evidence/` has the panel and the ratchet firing | Done |

### F11 — MVP release

| ID | Subtask | Status |
|----|---------|--------|
| F11.1 | `docs/ARCHITECTURE.md` (as built) and the README rewritten for Frog (what it is, building both targets, running, layout, conventions) | Done |
| F11.2 | Mac: APP / VST3 / AU build Release, auval passes, the app runs with the UI and a sample via the developer hooks | Done |
| F11.3 | Appliance: `docker-build-arm64.sh` green — `frog-appliance` + `libfrog-editor.so`, `shell_test` (29 checks), `editor_shot_test` (17 checks, five renders) | Done |
| F11.4 | On the board: deploy, boot to sound, touch the panel through one edit session, 0 xruns; recorded with F3.6 and F10.6 when the Pi is at hand | Planned — needs the board |
| F11.5 | Factory session: none shipped yet — no licensed demo sample. A short generated loop (the parity fixture's bursts) can be embedded if a first-boot sound is wanted | Planned |
| F11.6 | Tag `v0.11-mvp` | Done |

### F18 — Record

Two halves, one status. Neither moves without the other's row being updated
in the same commit, and every status change gets a line in the Log.

| ID | Subtask | Status |
|----|---------|--------|
| F18.1 | **Host capture — cross-repo action, pending.** Open an L-item in `ratfactory-linux-host/LINUX_ROADMAP.md` (mirrored into `tink-vst`): ALSA capture PCM on the same device as playback (full duplex, same period / rate), an input path on `rflh::Processor` (an `inputs` pointer on `process()` or a `processIO()` overload), `IPlug2HeadlessProcessor` forwarding, `--render-wav --input FILE` for offline checks. Once numbered, write its L-id into this row and F18's master row, and the F18 ids into that L-row. This repo is read-only on the host repo; the owner opens it | Planned — awaiting the L-item |
| F18.2 | Frog side (0.18.0): `fg_engine_record_arm / capture / stop / take` — the UI arms a capture buffer (allocated off the audio thread), the audio thread appends the block's inputs while armed, a stop handshake ends writes before the UI takes the trimmed capture (`record_test`). `Frog::StartRecord / StopRecord`: the take is written to the samples dir as `rec-<stamp>.wav`, swapped into the track by the F6 pointer swap, region reset, row kept; a **Rec** toggle in the header shows the seconds. Works wherever the format has inputs (Mac APP, VST3 / AU with `2-2`); on the appliance the lane idles until F18.1 | Done |
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
| F2.7 | Brand assets stored (**0.2.1**): `resources/img/frog-wordmark.svg` — FROG set in Cheltenham Bold, baked to outlines (no font needed at render time), fill `#E0332E`; `resources/img/rat-factory.svg` — the family maker mark, unmodified from tink-vst. The Cheltenham Bold OTF itself is **not** committed: the supplied file is Bitstream's (`CheltenhamBT-Bold`, "Confidential" in its name table) from a free-font site, so its redistribution licence is unknown; the outlines are all the UI needs. Decided 0.18.3: the OTF stays with the owner outside the repo; `resources/img/README.md` says what it is and where, `tools/wordmark/make-wordmark.py` regenerates the SVG from it. Use in the panel is deferred to F10 | Done |

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

### F9 — Plugin shell

| ID | Subtask | Status |
|----|---------|--------|
| F9.1 | `Frog.h/.cpp` owns an `fg_engine`: `ProcessBlock` → `fg_engine_process` (double, D15), `OnReset` → `fg_engine_reset` at the host rate with a 4096-frame floor, parameters BPM / Master / Clock source / per-track Vol, Pan, Mute for all `FG_MAX_TRACKS` (stable list; appliance caps come from `appliance.conf`) | Done |
| F9.2 | State chunk `FROGS001`: iPlug2 parameter block + the session JSON (patterns, mix, `samplePath` per track); `UnserializeState` restores patterns, seeds the mix parameters from the browser-format values and queues the sample loads | Done |
| F9.3 | Sample loading on the idle tick only (one file per tick: decode, down-mix, resample to the engine rate), atomic swap into the engine, retired stores freed once the audio thread has moved on; paths resolve against `FROG_SAMPLES_DIR`, `RF_DATA_DIR/samples` or the app-support folder | Done |
| F9.4 | Clock sources in the engine: **internal** (BPM parameter), **MIDI** (`midiclock.c`: 24 PPQN window average, Start / Continue / Stop, per-pulse PLL with 2 ms dead band, 10 % slew, 80 ms snap; `midiclock_test` locks a ±2 ms-jittered 116 bpm clock to 0.76 ms mean phase error), **host** (tempo + PPQ as one observation per block, running edges start the tracks in phase). The first pulse after Start is the downbeat per the MIDI spec (the browser code counted it as 1/24) | Done |
| F9.5 | `engine.h` is opaque for C++ (struct bodies with C11 atomics moved to `engine_internal.h`); GCC in C++ mode rejected `_Atomic`, clang had let it through | Done |
| F9.6 | Appliance: the shared `IPlug2HeadlessProcessor` drops MIDI realtime bytes, so `platform/linux/frog-plugin/FrogProcessor.h` wraps it and forwards 0xF8–0xFC itself (shrinks to a using-declaration if the host adapter learns to pass them: candidate host change, not blocking) | Done |
| F9.7 | `platform/linux/tests/shell_test.cpp` (built + run by `docker-build-arm64.sh`): load on idle never on the audio thread, Play All on the grid, Vol / Mute parameters reach the engine, a MIDI clock at 100 bpm takes tempo and transport (tempo acquisition over the first slots, then on the grid within the dead band), the state chunk round-trips pattern + parameters + sample path into a fresh instance that plays again | Done |
| F9.8 | WSOLA lag search: lag 0 is the baseline and a candidate must beat it by a margin, so silence or a featureless reference no longer drags an earlier frame in (an echo the browser algorithm also has) | Done |

### F10 — MVP UI

| ID | Subtask | Status |
|----|---------|--------|
| F10.1 | `ui/Style.h` tokens (unit = shorter side / 75 clamped 6–12 px, controls 6 u, intent colours, the 16 slice hues) and the band layout in `FrogView::Layout`: pinned transport / header / two toolbar rows / modifier lane, waveform and tiles share the rest (edit 3:2, perform 1:4), re-laid on resize and the Perform toggle (the panel background too, 0.18.2; window minimum 1024 × 600, 0.18.4). Rendered at 1024 × 600 and 1280 × 720 (`docs/F10_evidence/`) | Done |
| F10.2 | `ui/DragControl.h`: value / enum / toggle / button modes, drag any direction (right or up increases, 150 px = full range, 20 px per step), tap to value or step, double-tap to the detent, wheel; param-linked or local; a formatter turns a button into a live chip | Done |
| F10.3 | `ui/WaveformControl.h` over `ui/Peaks.h` (4096 min / max columns built at load): selection shade, green / red handles with finger-sized hit zones, unit grid, 2 px dirty-rect playhead; drop a file to load. Zoom / trim deferred to F14 with the virtual window | Done |
| F10.4 | `ui/TileRowControl.h`: tiles proportional to `w` with the slice's own waveform (mirrored when reversed, muted dimmed, locked ringed), badges (unit, reverse, pitch, gain); tap selects, drag reorders (packed live reflow / gaps ghost + drop), edge drag on the selected tile resizes from a snapshot through `edit.h`, double-tap splits | Done |
| F10.5 | Transport (Play / Stop / BPM / Clock / Perform / build stamp), header (Load / file chip / Beats / Step / Pitch / Vol / Pan / Mute / Loop), slice toolbar (Mute / Lock / Rev / Gain / fades + curves / Pitch / Split / Merge), pattern toolbar (Dup / Refill / Randomize / Amt / Reset Order / Reset All / Packed-Gaps); `ui/FileListControl.h` over the samples dir (POSIX dirent: the Mac target is 10.13) | Done |
| F10.6 | Pi 3 budget: at rest ≤ 3 % of a core, drag ≤ 20 %, frame cap 30 fps, 0 xruns over 10 min with the UI driven | Planned — needs the board |
| F10.7 | Stable dotted control IDs + Debug `--dump-layout`; `layout.json` hot reload if steering by numbers is slow | Planned |
| F10.8 | `platform/linux/tests/editor_shot_test.cpp`: the editor through the module, surfaceless on the container's Mesa: loads a sample, renders edit / playing (playhead + lit tile) / perform / selected at 1024 × 600 and the panel at 1280 × 720, taps Perform and a tile through IGraphics; images in `build-arm64/shots/`, kept under `docs/F10_evidence/` | Done |
| F10.9 | `edit.c` grew the interactive set the UI needs: packed move and boundary-eating resize from a snapshot, split, merge, refill, dup, gaps-mode move and resize over a raster, grid change with positional inheritance of per-step settings, locked tiles kept, mods rescaled (`edit_test`) | Done |

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
- **0.18.4 — F10.1 window minimum.** Decided: the Mac window re-lays out
  freely at any aspect (Size mode, not Tink's uniform Scale) but cannot
  shrink below 1024 × 600; `PLUG_MIN_WIDTH / HEIGHT` in config.h, max 8192.
- **0.18.3 — F2.7 font decision.** The Cheltenham Bold OTF is kept out of
  the repository for good (licence unknown); `resources/img/README.md`
  records what the file is and that the owner holds it, and
  `tools/wordmark/make-wordmark.py` bakes the wordmark from it (paths
  identical to the stored SVG; the viewBox is now the ink box).
- **0.18.2 — F10.1 background on resize.** The panel background is an
  `IPanelControl` sized once at attach; in Size mode the view's `Layout`
  re-ran on every resize but never touched it, so a grown window showed the
  dark block at the old size over the surface's uninitialised clear (pink).
  `FrogView::Layout` now covers the new bounds with it first.
- **F16.3 sessions as presets (0.18.1).** Save / Sessions in the transport
  bar; the appliance's preset buttons and Program Change move through the
  sessions folder with loads deferred to the idle tick.
- **F18.2 record, Frog side (0.18.0).** Arm, capture, stop and take through
  a lock-free lane; takes land in the samples dir and replace the track's
  store without a dropout. The host capture half (F18.1) is still the
  owner's cross-repo item; on the appliance the Rec button does nothing
  until it exists.
- **F16.1 note triggers (0.17.2).** A MIDI note plays a slice one-shot on
  the channel's track, through the adapter on the appliance too. CC map and
  program-change sessions stay open.
- **F15 multi-track (0.17.1).** Track tab strip with add / dup / remove and
  focus switching that rebinds the editor bands; verified in the container
  with a duplicated track. The Pi cap via `appliance.conf` stays open.
- **F17 bounce (0.17.0).** Export from the transport bar renders the
  session off the UI thread into the data dir; the headless test exercises
  it on arm64. Length / subset choices stay open (F17.4).
- **F14 complete (0.14.0).** The All broadcast and the Trim / Full zoom
  close the pattern-tool list; everything else in F14 had landed with F6
  and F10. Next: F17 (bounce from the UI) and F15 (multi-track), then F16.
- **F13 complete (0.13.0).** The modifier lane is in the panel with its
  settings replacing the toolbars while a chip is selected, and the live
  flash / firing / superseded states driven from the engine's rings.
  Verified offscreen in the container (docs/F13_evidence); Mac APP / AU /
  VST3 build, auval passes. F12 (the live-performance pass) stays queued
  for when the device is in hand; F14 (remaining pattern tools: the virtual
  zoom / trim window, the All broadcast) is next.
- **F11: MVP (0.11.0), tag `v0.11-mvp`.** Milestone M3 on the Mac and in
  the appliance's cross-build; the board gates (F3.6, F10.6, F11.4) wait for
  hardware. docs/ARCHITECTURE.md describes what was built; the README is
  Frog's. Next: F13 (modifier lane) and F14 (the remaining pattern tools),
  then F12's live-performance pass once the device is in hand.
- **F10 complete bar the board items (0.10.0).** The MVP panel renders and
  works on both targets: one focus track with transport, header, waveform,
  tile row, modifier-lane placeholder and two toolbar rows, all from the
  unified drag control. Verified offscreen through the appliance's editor
  module (Mesa in the container, PNGs under docs/F10_evidence) because this
  Mac session cannot capture screens; the Mac app, VST3 and AU build and
  auval passes. Developer hooks `FROG_AUTOLOAD` / `FROG_AUTOPLAY` load and
  play a file at start. F10.6 (Pi render budget) waits for the board; F10.7
  (control IDs / layout dump) stays open.
- **F9 complete (0.9.0).** The engine lives inside the iPlug2 plugin:
  parameters, state chunk with the session, idle-thread sample loading, and
  three clock sources (internal, MIDI with the PLL, host transport). APP /
  VST3 / AU build and auval passes; the arm64 appliance builds and its new
  headless shell test passes in the container. Two findings: GCC needs the
  engine header free of C11 atomics (now opaque), and the host's iPlug2
  adapter drops MIDI realtime bytes (wrapped locally in `FrogProcessor.h`).
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
