# Frog — architecture (as built, 0.18)

Frog is a sample slicer and loop performer: load a clip, declare how many
beats it spans, and it is cut into a grid of slices you rearrange, resize,
reverse, fade and modulate into a loop that stays locked to one clock. This
document describes the native implementation. The reasoning behind the
design is in [PORT_PLAN.md](PORT_PLAN.md); progress and status in
[../ROADMAP.md](../ROADMAP.md).

## 1. Layers

```
Frog/engine/      C11, no iPlug2: everything that makes sound and the data model
Frog/Frog.*       C++17 iPlug2 plugin shell: parameters, state, MIDI, host clock, loading
Frog/ui/          header-only IGraphics controls and the view that wires them to the engine
platform/linux/   the appliance binary on libratfactory-linux-host, editor as a module
Frog/tools/       frog-render (offline bounce), parity (the browser version as oracle)
```

The engine is the product; the shell and the UI are adapters around it. The
same `engine/*.c` files are linked into the Mac formats (Xcode and CMake),
the appliance binary (`frog_engine` static library), the render tool and the
tests.

## 2. Data model (`engine/frog_types.h`)

- **Pattern** — what the sequencer reads at schedule time: grid (`beats`,
  `denom`), selection (`virtualStart/End`, `start/end` fractions), master
  pitch, randomize level, loop flag, the tile row and the modifier lane.
  Fixed capacity (512 tiles, 128 modifiers), so a pattern is a POD copied
  between threads without allocation.
- **Tile** — reads `w` units of source from unit `src` and occupies `w`
  steps; positions on a ¼-unit lattice; mute, lock, reverse, pitch offset,
  fades with curve shapes, gain, colour. Gap tiles are silent slots.
- **Modifier** — at most one per step: action (mute, rev, gain, rand, reset,
  ratchet), fire mode (probability or every N loops), ratchet layout (even,
  ramp, pitch), span.
- **Session** — tracks (pattern + mix + sample path) and master settings, as
  JSON. Format v1 *is* the browser version's `version: 3` save object plus
  `format` / `formatVersion` / `samplePath`, so old sessions import.

Invariant: Σw over a row equals the unit count U = beats × denom / 4. Every
edit operation in `edit.c` preserves it; `fg_pattern_validate` rebuilds a
row that breaks it.

## 3. Time

- **Grid** (`grid.c`) — one linear beat ↔ sample mapping (`bpm`, origin
  sample, origin beat). A tempo change re-anchors once, phase-continuously;
  `nudge` slides the origin for the PLL; `sync_phase` hard-anchors a beat to
  a sample (MIDI Start, host transport start).
- **Sequencer** (`sequencer.c`) — per track, per block: walks the row in
  absolute grid time. Slots already past are skipped; a slot in progress
  starts late with an offset into its material; slots starting inside the
  block start voices at their sample. Pattern actions (rand / reset) fire
  before their covering slot is scheduled; voice modifiers (mute / rev /
  gain) resolve per slot; a ratchet replaces the slot with hits drained
  block by block (so the voice pool stays bounded) and absorbs the tiles it
  covers. `fg_resolve_slot` is the browser's `getTilePlayback` policy: the
  region, the stretch factor (fill × pitch), the envelope.
- **Clock sources** (`engine.c`, `midiclock.c`) — internal (BPM parameter),
  MIDI clock (24 PPQN averaged over 49 pulses for tempo; each pulse a phase
  observation into a PLL with a 2 ms dead band, 10 % slew and an 80 ms snap;
  Start / Continue / Stop drive the tracks; the first pulse after Start is
  the downbeat), host transport (tempo + PPQ per block as one observation,
  running edges start the tracks in phase).
- **Note triggers** — a Note On on channel n plays one unit of track n − 1
  (note 36 = unit 0) one-shot at its natural rate, velocity as level, on
  top of whatever the sequencer plays; its visual record has tileIndex −1.

## 4. Sound

- **Sample store** (`sample.c`) — float32 channels at the engine rate
  (decoded with dr_wav, down-mixed to two channels, Kaiser-windowed-sinc
  resampled once at load). Storage is float32 for memory on a 1 GB board;
  every computation on it is `double` (family decision D15).
- **Voice** (`voice.c`) — region reader (reverse = backwards index, no
  copies) → optional streaming WSOLA stage → 4-point Hermite reader at
  pitch × varispeed → per-sample envelope (four fade shapes, gain ceiling,
  proportional scaling of overlong fades) → 5 ms declick at a cut.
- **WSOLA** (`wsola.c`) — frame 1024, hop 256, Hann, COLA-normalised into a
  ring; lag search ±256 coarse-to-fine with lag 0 as the baseline; output on
  demand so a voice pulls exactly a block's worth; bypassed at factor 1.
  Replaces the browser's pre-rendered cache: no memory growth, no first-pass
  fallback, instant response to tempo.
- **Mix** (`engine.c`) — per-track volume × mute smoothed over 5 ms (snapped
  while silent), equal-power pan as the Web Audio StereoPannerNode (the mono
  law for mono sources), master gain.

## 5. Threads and lanes

```
UI / main thread                       audio thread
edit the working pattern ──publish──►  triple buffer ──► active copy (per block)
                          ◄──outbox──  pattern actions that changed the row
load / decode / resample ──swap──────►  atomic sample pointer; old store freed once seen
commands (play, stop, tempo, mix) ───►  SPSC ring, applied at block start
                          ◄──rings───  visual records (slot, times) and modifier flashes
MIDI (clock bytes, notes, offsets) ──►  per-block event list, applied in offset order
arm / stop recording ────────────────►  capture lane: inputs appended while armed, stop handshake, take
```

Rules (as `host/Processor.h` and Tink's ARCHITECTURE.md): nothing on the
audio thread allocates, locks or logs; `fg_engine_process` only reads
atomics, copies POD and renders. The race test (`pattern_race_test`) drives
every lane under ThreadSanitizer.

## 6. The plugin shell (`Frog.h/.cpp`)

- Parameters: BPM, Master, Clock source, and Vol / Pan / Mute for all eight
  tracks (a fixed list: parameters are added, never removed).
- State chunk `FROGS001`: iPlug2 parameter block, then the session JSON.
  Restoring publishes the patterns, seeds the mix parameters and queues the
  sample loads.
- `OnIdle` (main thread, ~20 ms on the appliance): one sample load per
  tick, retirement of replaced stores, draining the visual rings, taking
  pattern changes posted by the audio thread, refreshing the view.
- Paths: `FROG_SAMPLES_DIR`, else `RF_DATA_DIR/samples` (appliance), else
  the app-support folder; `exports/` and `sessions/` beside it. Developer
  hooks `FROG_AUTOLOAD=<wav>` and `FROG_AUTOPLAY=1`.
- Tracks in use (1..8) with add / duplicate / remove; the engine always runs
  all eight sequencers.
- Bounce: a worker thread renders a snapshot of the session through its own
  engine into `exports/`; one at a time, status collected on the idle tick.
- Record: arm a capture buffer for a track; the audio thread appends the
  block's inputs; on stop the take is written to `samples/rec-<stamp>.wav`
  and swapped into the track (desktop formats with inputs; the appliance
  waits for host capture).
- Sessions as presets: `sessions/*.json`; Save, a list to load, `StepPreset`
  and MIDI Program Change pick one and the load lands on the idle tick;
  `GetCurrentPresetName / Program` feed the appliance panel.

## 7. The UI (`ui/`)

Bands from one unit (8 px at 1024 × 600, scaling with the shorter side):
transport (Play / Stop / BPM / Clock / Perform / Export / Save / Sessions /
build stamp), the track strip (one tab per track, the focused one owns the
bands below; + Track / Dup / − Track), track header (Load / name / Beats /
Step / Pitch / Vol / Pan / Mute / Loop / Rec), waveform, tile row, modifier
lane, and two rows that are either the slice + pattern toolbars or, while
a modifier chip is selected, its settings. Edit mode favours the waveform,
Perform the tiles. Controls are owned `IControl`s: the unified
`DragControl` (value / enum / toggle / button; drag any direction, tap,
double-tap, wheel; a formatter makes a live chip), `WaveformControl` over
pre-reduced peaks, `TileRowControl` (select, reorder, edge resize, split),
`ModLaneControl`, `TrackStripControl`, `FileListControl`. Playback feedback
is a dirty-rect playhead (per hit inside a ratchet), a lit tile, flashing
and firing chips, all from the visual rings; nothing animates at rest.

## 8. The appliance (`platform/linux/`)

`frog-appliance` links `rflh_host` and `frog_plugin_ui`; the editor's GPU
side (IGraphicsKMS, NanoVG on GLES2, Mesa) is `libfrog-editor.so`, loaded
after the first audio callback. `FrogProcessor` wraps the shared adapter to
pass MIDI realtime bytes through. `docker-build-arm64.sh` cross-builds from
the Mac and runs `shell_test` (headless plugin) and `editor_shot_test`
(the editor rendered offscreen to PNG) on every build.

## 9. Testing

`Frog/engine/tests/run-tests.sh` — grid, pattern, session, envelope, edit,
sequencer (impulse-coded source, sample-exact slot timing), wsola, midiclock
(phase lock against a jittered clock), parity (the browser version's own
bounces: sample-exact on unstretched paths, level and pitch on stretched
ones), and the TSan race test. `tools/parity/README.md` has the oracle
method and the browser quirks it works around.
