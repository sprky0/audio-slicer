# Native port — analysis and plan

*Working notes for taking the browser slicer / loop performer (the `js`
branch) to native code on iPlug2, for the Rat Factory plugin family and the
Raspberry Pi appliance. Written 2026-10-03 on the `native` branch. Tracking
lives in [ROADMAP.md](../ROADMAP.md); this file holds the reasoning behind
the F-items there and is updated when a decision changes.*

Product: **Frog** (knitting: to rip back and rework; decided 2026-10-03). Owner: Rat Factory.

## 0. The shape in one paragraph

A small **C11 engine library** (`engine/`) owns everything that makes sound:
sample store, beat grid, pattern model (tiles + step modifiers), the
absolute-time sequencer, voices with streaming WSOLA time-stretch, envelopes,
and the per-track mix. It has no iPlug2 dependency, allocates nothing on the
audio path after setup, and is driven by a plain `process(out, nFrames)`
call. A thin **C++17 iPlug2 plugin** wraps it: parameters, state, MIDI,
host transport, and an IGraphics (NanoVG) UI built from a handful of owned
controls (waveform, tile row, modifier lane, the unified drag control,
transport bar). On the Mac it builds as APP / VST3 / AU from the Rat Factory
iPlug2 fork; on the appliance it builds exactly as Tink does: a static
`frog-appliance` binary on `libratfactory-linux-host` with the editor in a
runtime-loaded module. The browser version stays intact on the `js` branch
and doubles as the golden reference for engine parity tests.

## 1. What we are porting (inventory of the JS app)

Sizes are the JS line counts; they say where the complexity is.

| JS module | lines | what it is | native home |
|---|---|---|---|
| `beat-grid.js` | 69 | one beat↔time mapping, re-anchored on tempo change, nudged by the PLL | `engine/grid.c` (times in samples) |
| `transport.js` | 425 | per-track lookahead scheduler: absolute grid times, skip-past / late-join with offset, ratchet hit layout, visual note queue | `engine/sequencer.c` (per block instead of per 25 ms timer) |
| `timestretch.js` | 160 | WSOLA, frame 1024 / hop 256 / ±256 lag search, stereo lag from ch 0 | `engine/wsola.c`, streaming per voice |
| `envelope.js` | 78 | per-voice fades (lin / exp / log / s) scaled to the audible span, gain ceiling | `engine/envelope.c` |
| `audio-engine.js` | 291 | buffer source + per-voice gain + declick + vol / pan / mute | `engine/voice.c`, `engine/track.c` |
| `midi-clock.js` | 159 | 24 PPQN averaging, Start / Continue / Stop, per-pulse phase events | `engine/midiclock.c` + the PLL from `main.js:4128-4156` |
| `main.js` (policy part) | ~600 | `getTilePlayback` fill / pitch policy, region + stretch caches, prescan, modifier resolution (`resolveVoiceMods`, `resolveRatchet`, `firePatternMods`) | `engine/pattern.c`, `engine/sequencer.c` |
| `main.js` (edit ops) | ~1500 | packed / gaps reorder, boundary-eats resize, split, merge, dup, refill, randomize (lock-aware buckets), reset order / all, rasterize→rebuild, grid-change inheritance | `engine/edit.c` — pure functions on the pattern struct, UI-thread only |
| `main.js` (UI + persistence) | ~2000 | DOM, popovers, export panel, localStorage / IndexedDB | `ui/`, iPlug2 state, session JSON |
| `waveform-view.js` | 639 | canvas waveform, handles, selection, playhead | `ui/WaveformControl` |
| `drag-control.js` | 273 | the one button-shaped control | `ui/DragControl` |
| `export-wav.js` | 234 | offline render through the same policy | `engine/render.c` + `tools/render` CLI |
| `storage.js` | 113 | localStorage + IndexedDB | session JSON under `RF_DATA_DIR` |

**Data model to keep** (it is good and it is already serialised):

- Track: `virtualStart/End`, `start/end` (0..1 fractions), `beats`
  {1,2,3,4,6,8,12,16}, `divisionDenom` {2..32}, `volume`, `pan`, `muted`,
  `masterPitch` (±5 st), `randLevel`, `seq.loop`.
- Grid: `U = beats × denom / 4` units; a tile `{src, w}` reads `w` units of
  source from unit `src` and occupies `w` steps; Σw = U always; widths and
  positions on a ¼-unit lattice (`SUBSTEP 4`, `MIN_W 0.25`).
- Tile: `src, w, offset(st), muted, locked, reversed, fadeIn/Out (0..1 of
  own length), fadeInCurve/OutCurve, gain (0..2), colorIdx`; gap tiles
  `{gap, w}`.
- Modifier (≤ 1 per step): `step, action {mute, rev, gain, rand, reset,
  ratchet}, fireMode {prob, every}, fireValue, gainAmt, mode {even, ramp,
  pitch}, subdiv, subdivTo, pitchStep, lenSteps`.
- Master: `bpm`, `seqEditMode {pack, gaps}`, MIDI sync enable + input.

The JS save object (`version: 3`) becomes the native **session format v1**
verbatim (plus a `format` field). Native reads JS sessions; the parity tests
in §6 depend on it, and nobody has to re-author work.

## 2. Platform constraints (from ../ratfactory-linux-host, ../tink-vst)

These are facts of the host and the fork as of 2026-10-03, with the
consequence for this project after each.

- **Pi 3B is the reference board**: 4 × Cortex-A53 at 1.2 GHz, 1 GB RAM
  (~905 MB usable), `ondemand` governor, stock `PREEMPT` kernel (RT only
  planned, L13). → budget one core for audio, one for UI; never assume RT.
- **Audio: ALSA playback only, 48 kHz, period 128 × 2, SCHED_FIFO 80,
  samples `double` end to end (family decision D15).** `process()` gets one
  128-frame block per period; `midi()` arrives on RtMidi's thread with
  `sampleOffset 0`; **there is no capture path**. → the engine is
  block-driven and sample-clocked; MIDI clock pulses must be timestamped on
  arrival; recording (stretch goal) needs host work first (§8).
- **MIDI clock 0xF8 is delivered**, Start / Continue / Stop too; no MIDI
  out, no clock master, no host transport. → the slicer is a clock
  *follower* on the appliance and a *master* only via its own BPM.
- **Static linking, one product per binary** (D16). `platform/linux/` with
  `frog_plugin` + `frog-appliance`, `rflh::IPlug2HeadlessProcessor<T>`,
  `HEADLESS_API IPLUG_DSP=1 SAMPLE_TYPE_DOUBLE -fsigned-char`. Editor is a
  separate `libfrog-editor.so` loaded after the first audio callback. →
  copy Tink's folder; no new host API needed for MVP.
- **Display: 7" 1024 × 600 HDMI panel, single-contact evdev touch, DRM/KMS +
  GLES2 NanoVG via `IGraphicsKMS`.** A full-panel redraw on the Pi 3 is
  ~30 ms CPU + ~35 ms GPU (15 fps); at rest it must cost ~2 %; partial
  redraws work around a vc4 stall with a frame cap. → no continuous
  full-panel animation; playhead and tile highlights are small dirty rects;
  waveforms are cached, not re-tessellated per frame; one finger only (no
  shift-click, no wheel, no pinch).
- **Touch targets 7–10 mm** (Tink's scaled knobs were 1.6–5 mm: too small).
  At 6.65 px/mm on the 7" panel that is 48–64 px. → control height 48 px
  minimum on the appliance.
- **Storage**: read-only root + tmpfs overlay; the only writable tree is
  `/data/ratfactory/frog/` (`RF_DATA_DIR`), writes via
  `rflh::atomicWrite()`. **No convention yet for samples**; USB storage is
  planned (L25). → sessions as JSON in `RF_DATA_DIR`; samples under
  `RF_DATA_DIR/samples/`; a file-list control instead of an OS dialog.
- **Threading rules** (`host/Processor.h`): no locks, allocation or logging
  in `process()`; `midi()` lock-free; heavy work in `idle()` (~20 ms, main
  thread, which is also the UI thread).
- **Versioning / compatibility** (`COMPATIBILITY.md`): saved state loads
  across every minor within a major; parameters are added, never removed or
  repurposed.
- **Family identity**: `PLUG_MFR "Rat Factory"`, `PLUG_MFR_ID 'RatF'`,
  `BUNDLE_MFR "ratfactory"`, maker mark `rat-factory.svg`. This repo stays
  neutral: no personal names, URLs or e-mail in `config.h`, docs or commits.

## 3. Target architecture

### 3.1 Threads and data flow

```
 UI / main thread                     audio thread (one block per period)
 ────────────────                     ──────────────────────────────────
 edit ops on a working Pattern  ──►  publish(Pattern snapshot)  ──►  sequencer reads snapshot
 file load / decode / resample  ──►  publish(SampleStore ptr)   ──►  voices read samples
 param changes (iPlug2)         ──►  atomics / smoothers        ──►  mix
 MIDI thread: clock pulses      ──►  SPSC ring (byte, t_mono)   ──►  PLL nudges grid
                                ◄──  SPSC ring: NoteVisual {track, tile, hit, t0, t1}
                                ◄──  atomics: block sample counter + CLOCK_MONOTONIC stamp
```

- **Pattern snapshot.** The pattern (tiles + mods + grid settings) is a
  fixed-size POD struct (caps in §3.4), so publishing is a copy into a
  triple buffer with an atomic index (the hazard-pointer shape Tink uses for
  its mod matrix). The audio thread never sees a half-edited pattern, and
  every edit lands at the next block boundary: **editing never interrupts
  playback.** Pattern actions fired *by the sequencer* (rand / reset
  modifiers) mutate the audio thread's own copy and post the result back
  through a ring so the UI shows what is sounding.
- **Sample store.** Decoded in `idle()` / a worker, resampled to the engine
  rate once, stored as **float32** (memory; the mix bus and voice math are
  `double` per D15), published by atomic pointer swap; the previous store is
  freed on the main thread once the audio thread's generation counter has
  moved past it.
- **Sequencer per block.** For the block `[s0, s1)` on the sample clock,
  walk the tile list exactly as `transport.js` does but with the lookahead
  window = this block: tiles whose slot ended before `s0` are skipped, a
  tile already in progress starts late with an offset into its material, a
  tile starting inside the block starts at its sample offset. Grid moves
  (tempo change, PLL nudge) are picked up next block, as they are picked up
  next 25 ms tick today.
- **Voice.** `{region(src,w,reversed), stretchFactor, pitchRatio, env,
  startSample, stopSample, declick}`. Reads the region directly from the
  sample store (reverse = backwards index; no copies), through a streaming
  WSOLA stage when `stretchFactor ≠ 1`, then a 4-point interpolating reader
  at `pitchRatio × hitRate` (varispeed for ratchet pitch mode), then the
  per-sample envelope (fade shapes from `envelope.js`, gain ceiling, 5 ms
  declick at a cut). Fixed per-voice work buffers; no allocation.
- **Track.** Voice pool (cap §3.4), volume × mute, pan, mix into the
  stereo bus. **Master**: sum of tracks (keep headroom; no limiter in MVP).

### 3.2 Time-stretch: streaming WSOLA, not a pre-render cache

The JS app pre-renders every stretched slice into an LRU cache (128
entries, 2 %-quantised factors), with a varispeed fallback on a miss and a
progress overlay. That was the right call for Web Audio; on a Pi it is the
wrong one: unbounded memory, a one-pass pitch wobble on every miss, and a
tempo sweep rebuilds everything.

Cost of running WSOLA inside the voice instead (per output sample, per
stretched voice, parameters as in the JS):

| part | per hop of 256 out | per sample |
|---|---|---|
| lag search, 513 lags × 256-sample NCC | ~131 k MAC | ~513 |
| overlap-add, 1024 × 2 ch | ~2 k | ~8 |

≈ 25 M MAC/s per voice at 48 kHz: roughly **4 % of an A53 core scalar,
~1 % with NEON**, and a coarse-to-fine lag search (step 4, then ±3) cuts the
search ~3.8× again. Eight simultaneous stretched voices stay well inside
one core even unvectorised; **at natural tempo the stretch stage is bypassed
entirely** (as it is today). Instant response to tempo / pitch changes, zero
cache, deterministic. F7.4 measures this on the board before anything else
depends on it; the pre-render cache is the recorded fallback if the number
is wrong.

Parity note: the streaming build and the JS build will not be bit-identical
when stretching (different search schedule, factor not quantised). They are
expected to be identical, within float rounding, for everything else (§6).

### 3.3 Clock sources

| source | where | how |
|---|---|---|
| internal | all builds | BPM param → `grid_set_tempo(bpm, atSample)` phase-continuous |
| MIDI clock (appliance) | `midi()` on RtMidi's thread | stamp `CLOCK_MONOTONIC` on arrival, push `(0xF8, t)` to a ring; the audio thread publishes `(blockStartSample, t_mono)` each block; the PLL maps pulse time → sample position and runs the JS loop unchanged: EMA 0.8/0.2, dead band 2 ms, slew 10 %, snap at 80 ms. Start → `grid_sync_phase(0, now)`; Stop → stop all tracks; Continue → keep phase |
| MIDI clock (plugin) | `ProcessMidiMsg` with sample offsets | same PLL fed exact offsets |
| host transport (plugin) | `GetTransportInfo` (PPQ, tempo, playing) | drive the grid from the host's PPQ when playing; parameter chooses host vs internal |

### 3.4 Caps (fixed-size, no heap on the audio path)

| cap | value | why |
|---|---|---|
| tracks | 4 on Pi 3 (`max_tracks=` in `appliance.conf`), 8 desktop | memory and CPU; Tink's `max_voices` pattern |
| units `U` | 128 | 16 beats × 1/32 |
| tiles per track | 512 | gaps-mode raster is `U × 4` cells |
| modifiers per track | 128 | one per step |
| voices per track | 8 | one sounding + ring-out tails + ratchet hit overlap (hits cut at the next hit) |
| ratchet hits per span | 128 | as JS `MAX_HITS` |
| source length per track | 60 s default (`max_sample_seconds=`) | 48 k stereo float32 = 23 MB/min |

### 3.5 Engine API sketch (C11)

```c
/* engine/slicer.h — the whole engine behind one opaque handle. */
typedef struct sl_engine sl_engine;

sl_engine* sl_create(const sl_config* cfg);           /* main thread, allocates once */
void       sl_destroy(sl_engine*);
void       sl_reset(sl_engine*, double sampleRate, int maxBlock);

/* audio thread */
void sl_process(sl_engine*, double* const* out, int nCh, int nFrames);
void sl_midi(sl_engine*, const uint8_t* msg, int n, int sampleOffset, int64_t tMonoNs);

/* main thread: edits go through a working copy, then publish */
sl_pattern* sl_pattern_edit(sl_engine*, int track);   /* working copy */
void        sl_pattern_publish(sl_engine*, int track);
int         sl_load_sample(sl_engine*, int track, const float* const* ch, int nCh, int frames, double sr);

/* main thread: drain visuals for the UI */
int sl_poll_visuals(sl_engine*, sl_visual* out, int max);
```

`engine/edit.h` holds the pure edit operations (`sl_edit_move`,
`sl_edit_resize`, `sl_edit_split`, `sl_edit_randomize`, …) on `sl_pattern`,
ported function-for-function from `main.js` so each gets a unit test and
the parity tests (§6) can replay recorded edit sequences.

Why C for the engine: the request, portability (no iPlug2 in the hot path;
the same `.c` files can be driven by the appliance host directly, by a
test binary, or compiled with Emscripten back into the web UI later), and a
hard line against C++ conveniences that allocate. The plugin and UI are
C++17 because iPlug2 is.

### 3.6 Repo layout (target)

```
Frog/                       the plugin, like tink-vst/Tink/
  config.h                  PLUG_* — version source of truth (PLUG_VERSION_STR / _HEX)
  Frog.h/.cpp               iPlug2 plugin class: params, state, MIDI, OnIdle, layout
  engine/                   C11 engine (slicer.h, grid.c, pattern.c, edit.c, sequencer.c,
                            voice.c, wsola.c, envelope.c, midiclock.c, session.c, render.c)
  engine/tests/             *_test.c, "ALL CHECKS PASSED" convention, run-tests.sh
  ui/                       owned IControls (DragControl, WaveformControl, TileRowControl,
                            ModLaneControl, TransportBar, FileList), Style.h tokens, Layout.h
  resources/                fonts, img/rat-factory.svg, factory sessions
  tools/render/             CLI: session JSON + samples → WAV (parity + CPU ratio)
  projects/ config/ scripts/  Xcode / xcconfig / stamp-version.sh, as Tink
  CMakeLists.txt
platform/linux/             frog-plugin, frog-appliance, systemd, iplug2-patches, scripts
docs/                       PORT_PLAN.md (this), ARCHITECTURE.md (once code exists), evidence
ROADMAP.md                  F-items; LINUX_ROADMAP.md is NOT duplicated here (it lives in
                            tink-vst / ratfactory-linux-host; cross-link L-items from F rows)
public/                     the JS app, kept on this branch until F8 parity lands, then
                            removed here (the `js` branch keeps it)
```

iPlug2 comes in as the submodule `Rat-Factory/iPlug2`, branch
`ratfactory-linux`, pinned to the same commit as tink-vst so one fork serves
the family.

## 4. UI: from browser to a 7" single-touch panel

Keep what the JS UI got right and change what a finger and a Pi 3 cannot do.

**Keep**

- **The one DragControl.** Button-shaped, drag any direction (`dx − dy`),
  tap jumps to the tapped value, double-tap resets to the detent. Already
  touch-native; it is the family-UI question jsloop raised
  (`FAMILY_UI_LIBRARY.md` Q "one control shape") answered in native code.
- **Intent colours** (blue neutral, green go / on, red stop / danger, grey
  chips with orange labels) and the relative-unit grid; the first
  implementation of a shared token table (`ui/Style.h` now, `tokens.json`
  codegen when the family spec lands).
- **Edit / perform layouts** trading waveform height against tile height.
- **Absolute grid timing, late-join, PLL** — all of it, unchanged.

**Change**

- **Gestures → one finger.** Double-click split stays (double-tap);
  shift-click merge → toolbar `Merge`; wheel pitch → toolbar `Pitch`
  DragControl on the selection; right-click / hover → long-press opens the
  modifier popover; tile drag-reorder and edge-resize stay as drags on a
  selected tile (select first, then drag edges: avoids accidental resizes
  under a finger).
- **Sizes.** `unit = clamp(shortSide / 75, 6, 12)` px; control height 6 u
  (48 px at 1024 × 600); tile row ≥ 20 u in perform mode. Fonts from the
  family set.
- **Rendering budget.** Waveforms pre-reduced to min/max peak columns at
  load and cached per zoom; redrawn only when region / zoom / size change.
  Playhead is a 3-px dirty rect; the sounding tile dirties itself; modifier
  flashes dirty one chip. Frame cap 30 fps; at rest no redraw. Verified on
  the board with the host's load meter (F10.6).
- **Multi-track on 600 px.** One **focus track** gets the full editor
  (header, waveform, tiles, mod lane, slice toolbar); other tracks collapse
  to 6 u summary rows (name, play, mute, mini tile strip) that tap to focus.
  Two tracks fit uncollapsed in perform mode; more scroll the summary stack.
- **Files.** No OS dialog on the appliance: a `FileList` control over
  `RF_DATA_DIR/samples/` (and USB when L25 lands); on the desktop also the
  native dialog and drag-drop.
- **Popovers → panels.** The modifier popover becomes a pinned bottom
  panel while a modifier is selected (bigger targets, no dismissal
  surprises); the export panel becomes a page.
- **Performance view** (the host's panel, logo / preset / load) stays as
  Tink's `SimpleView`; `EDITOR` toggles into ours.

**Responsive layout.** Design logical size 1024 × 600; `PLUG_HOST_RESIZE`
on; a declarative band layout (`ui/Layout.h`): rows with `fixed(u)` or
`flex(weight, min)`, columns likewise, resolved in `OnResize` and whenever
the edit / perform mode or focus track changes. Pinned: transport bar (top),
slice toolbar (bottom), track headers. Filling: waveform and tile row. On an
aspect mismatch nothing is letterboxed — the flex rows absorb it; uniform
scaling only kicks in below 800 × 480 (min size) or above 1.5× the design
unit. Every control gets a stable dotted ID (`track.0.tiles`,
`transport.bpm`) and a Debug `--dump-layout` JSON, per
`UI_LAYOUT_RECOMMENDATIONS.md` R1/R4; a `layout.json` override with hot
reload is F10.7 if steering by numbers proves slow.

Live-performance pass (F12) revisits all of this with the device in hand:
which actions deserve a permanent big button (Randomize, Reset Order,
Reverse-all, per-track mute), whether tiles should be tappable to *trigger*
(a pad mode), and a MIDI note / CC map.

## 5. Milestones and gates

| milestone | contents | gate |
|---|---|---|
| **M0 scaffold** (F2–F4) | branch split, iPlug2 submodule, `config.h`, CMake + Xcode, Mac APP/VST3/AU of an empty plugin, `platform/linux` boots on the Pi 3 with silence, `.clang-format`, tests runner | `--render-wav` runs on the Pi; `auval` passes |
| **M1 engine** (F5–F8) | C engine: grid, pattern, sequencer, voices, WSOLA, envelopes, session import, render CLI, parity vs JS | parity suite green; Pi 3: 1 track / 4 stretched voices ≤ 25 % of a core |
| **M2 plugin shell** (F9) | params, state, MIDI clock PLL, host transport, OnIdle loading | sync verified against a hardware clock; 0 xruns 10 min |
| **M3 MVP UI** (F10–F11) | transport, waveform, tiles, slice toolbar, file list, layouts, Pi render budget | ships as the first appliance unit; one track end-to-end by touch |
| **M4 modifiers** (F13–F14) | mod lane, popover-panel, ratchet modes, randomize / reset / locks, gaps mode | parity suite covers mods |
| **M5 multi-track** (F15) | 2–4 tracks, focus / summary rows, mixer, duplicate | 4 tracks on Pi 3 within budget |
| **M6 performance MIDI** (F16) | note-triggered slices, CC map, program = session | |
| **M7 bounce** (F17) | offline render to `RF_DATA_DIR/exports` from the UI | |
| **M8 record** (F18) | needs host capture (new L-item); record to a track, re-slice live | stretch goal |

MVP = M0–M3: load a sample, set a region and Beats / Step, play a looped
tile pattern locked to MIDI clock, move / resize / mute / reverse / fade /
gain slices live, on the appliance and on the Mac.

## 6. Testing

- **Unit tests** in C: grid math, hit layout (`ratchetHitSteps` /
  `ratchetHitRate`), envelope scaling, every edit op (ported invariants:
  Σw = U, lattice, lock protection, boundary-eats semantics), PLL
  convergence against a jittered synthetic clock (the JS verified ~1.6 ms
  mean phase error at 116 BPM; same fixture, same bar).
- **Parity suite**: sessions exported from the JS app (`getState()` JSON +
  the source WAV + the JS WAV bounce) live in `tests/fixtures/`. The
  `render` tool bounces the same session natively. Unstretched paths
  (natural tempo, fades, gain, reverse, mute, ratchet even / pitch) must
  match within −80 dBFS; stretched paths match per-step RMS envelopes and
  onset positions within one hop.
- **Pi measurements** recorded in `docs/` the way `L3_*` and
  `PHASE_*_LOG.md` are: `--render-wav` realtime ratio, load-meter peaks, UI
  frame cost, xruns over 10 minutes.
- **Race test**: the pattern triple buffer and sample swap under TSan
  (Tink's `race` target shape).

## 7. Conventions adopted from Tink

- **ROADMAP.md**: F-ids never renumbered, `Planned · In progress · Done ·
  Dropped`, dotted subtasks, `## Log` by date, cross-links to L-items.
- **Version**: `MAJOR.MINOR.PATCH`, **MINOR = F-number of the current or
  most recent effort**, PATCH per landed piece inside it; `config.h` is the
  source of truth once it exists (until then the number sits in
  ROADMAP.md). `0.x` until MVP ships, then `1.x`. **Tags on milestones**:
  `v<MAJOR>.<MINOR>-<milestone>` (e.g. `v0.8-engine`, `v0.11-mvp`), unlike
  Tink's informal ones.
- **Commits**: `<version>: <what landed, F-ids, evidence>` for bumps;
  `docs:`, `linux:`, `build:`, `tests:` prefixes otherwise; no attribution
  trailers; nothing personal in the message body.
- **Style**: `.clang-format` as tink-vst (tabs indent, spaces align, OTBS,
  braces always); C files follow the same rules; short comments.
- **Threading rules** as `ARCHITECTURE.md` "Threading & realtime rules".

## 8. Risks and open questions

1. **Record needs the host.** `AudioOut` is playback-only; recording means
   a capture PCM (same device, full-duplex) and a `Processor::processIO`
   or input pointer. That is an L-item in ratfactory-linux-host, not
   ours. **Decided 2026-10-03: run it in parallel with the MVP.** The host
   side is opened as an L-item in ratfactory-linux-host and F18 here links
   to it; both rows carry the same status and are updated in the same
   commit (the mirrored-sync rule). The Frog side (record into a track,
   re-slice live) develops on the Mac first, where RtAudio has inputs, so
   the two halves can land independently. Status of each half is reported
   in the ROADMAP log whenever either moves.
2. **WSOLA budget** is estimated, not measured (§3.2); F7.4 measures before
   M1 closes. Fallback: pre-render cache in a worker thread.
3. **`double` vs float32 storage.** D15 says double end to end; storing
   samples as float32 halves memory and bandwidth on a 1 GB board while
   keeping all math in double. **Accepted 2026-10-03** as a deviation at
   the storage layer only: everything the family might share (grid math,
   envelopes, the mix bus, parameter values, file formats) stays `double`
   so results are comparable across members; only the decoded sample
   arrays are float32.
4. **MIDI jitter on the appliance.** Pulses are stamped on RtMidi's thread
   and mapped through the block clock; the JS PLL tolerated multi-ms
   jitter, so this should hold, but it is the first thing to verify with
   hardware (F9.4).
5. **Name: Frog** (decided 2026-10-03; knitting: to rip back and rework).
   Identifiers: `Frog` class / dir, `frog_plugin`, `frog-appliance`,
   `libfrog-editor.so`, `frog-appliance.service`, `RF_DATA_DIR=/data/ratfactory/frog`,
   `PLUG_UNIQUE_ID 'Frog'`, bundle `com.ratfactory.*.Frog`.
6. **Multi-track UI on 600 px** (§4) is a proposal; it needs the device in
   hand before M5 commits to it.
7. **Pi 4 / 5** are placeholders in the host; caps come from
   `appliance.conf`, so no code changes expected.
8. **Family UI spec** (`FAMILY_UI_LIBRARY.md` option 3) has no home yet.
   This project will carry `ui/Style.h` tokens and stable control IDs so it
   can adopt the spec when it exists; it should not block on it.
