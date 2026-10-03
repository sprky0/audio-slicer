# Frog

A sample slicer and loop performer for the Rat Factory plugin family: load a
clip, say how many beats it is, and it becomes a grid of slices you
rearrange, resize, reverse, fade and modulate into a loop that stays locked
to one clock — internal, MIDI, or the host's transport. Edits land live,
without interrupting playback.

> **Frog** (v.): to rip knitting back and rework it.

Frog is the native successor to a browser prototype (preserved on the `js`
branch and kept in `public/` here as the parity oracle and UI reference). It
targets the Mac (standalone, VST3, AU) and the Raspberry Pi appliance of the
family, where it runs on a 7" 1024 × 600 touch panel.

## What it is

- **A C11 engine** (`Frog/engine/`): the data model, a drift-free beat grid,
  a per-block sequencer with step modifiers and ratchets, voices with
  streaming WSOLA time-stretch and pitch shift, envelopes, and the mix. No
  allocation on the audio thread; lock-free lanes to the UI.
- **An iPlug2 shell** (`Frog/`): parameters, a state chunk carrying the
  session, sample loading on the idle thread, MIDI clock with a PLL, host
  transport sync.
- **A touch-first UI** (`Frog/ui/`): one control shape (drag any direction,
  tap, double-tap), waveform with region handles, a tile row you reorder
  and resize with a finger, edit and perform layouts from one relative unit.
- **The appliance target** (`platform/linux/`): a static binary on
  `libratfactory-linux-host` with the editor as a runtime-loaded module.

Status: past the MVP (0.18): multi-track, the modifier lane, bounce, note
triggers, recording (where the format has inputs) and sessions as presets
are in. The appliance build passes its headless and offscreen-editor tests
in the cross-build container; the on-device gates and the host's capture
path wait for the board and the host repo. See [ROADMAP.md](ROADMAP.md) for per-feature
status and [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the design.

## Building (Mac)

```sh
git submodule update --init                       # the Rat Factory iPlug2 fork
iPlug2/Dependencies/IPlug/download-vst3-sdk.sh    # once; VST3 target only
cd Frog
xcodebuild -project projects/Frog-macOS.xcodeproj -target APP -configuration Release build
```

Targets: `APP` `VST3` `AU` (also `AUv3` `CLAP` `AAX`). Products install to
`~/Applications/Frog.app` and the user plug-in folders. `cmake -S Frog -B
build` configures the same plugin for CMake-driven builds and also builds
`frog-render`.

Tests: `cd Frog && ./engine/tests/run-tests.sh [all]` (plain `cc`, no
framework; `all` adds the ThreadSanitizer race test).

## Building (appliance)

Three checkouts side by side: this repo, `ratfactory-linux-host`, and the
`iPlug2` submodule. Then `platform/linux/docker-build-arm64.sh` cross-builds
`frog-appliance` for linux/arm64 in the host repo's container, runs the
headless shell test and renders the editor offscreen as a check. See
[platform/linux/README.md](platform/linux/README.md).

## Running

- **Data dir**: `FROG_DATA_DIR`, else `$RF_DATA_DIR` on the appliance,
  else `~/Library/Application Support/Frog`. Under it: `samples/` (or
  `FROG_SAMPLES_DIR`), `exports/`, `sessions/`, `midimap.json`. The Load
  button lists the samples folder; on the Mac a file can also be dropped
  on the waveform. Recordings land there as `rec-<stamp>.wav`.
- **First run**: with no sessions on disk and no state from a host, Frog
  generates a four-bar drum break (`samples/amen-variation.wav`, synthesised,
  licence-free) and `sessions/factory.json`, and loads it; later runs
  without host state boot into `factory.json` while it exists. Delete or
  rename it to boot blank. `FROG_NO_FACTORY=1` skips all of this.
- **MIDI**: clock (0xF8 / Start / Continue / Stop) when Clock is set to
  MIDI; Note On on channel n plays a slice of track n (note 36 = slice 1);
  Program Change loads a session by index; CCs drive the panel through the
  map below.
- **CC map**: every control is a named *hook*; `<data dir>/midimap.json`
  says which CC drives which. **Learn**: hold a control still for 0.6 s,
  then move the controller; the chip at the top right names the hook while
  armed, a tap on it unbinds, and 10 s without a CC cancels. A dot marks a
  bound control. Learned bindings answer on any channel; set `"channel"` to
  1–16 in the file for a specific one (an explicit channel outranks `any`).
  Track hooks act on the focused track and slice hooks on the selected
  slice, so one single-channel controller follows whatever is in front.
  Values and enums follow the CC position, toggles flip and buttons fire on
  a value ≥ 64. The file is the rig's single source for the app and the
  appliance; a plugin instance also keeps its map in the project.

  Defaults (all `any` channel):

  | CC | hook | CC | hook | CC | hook |
  |---|---|---|---|---|---|
  | 7 | track.vol | 20 | track.mute | 70 | slice.fadeIn |
  | 10 | track.pan | 21 | track.pitch | 71 | slice.fadeOut |
  | 14 | master.gain | 22 | pattern.amt | 72 | slice.gain |
  | 27 | transport.perform | 23 | track.loop | 73 | slice.pitch |
  | 28 | transport.play | 24 | pattern.randomize | 74 | slice.mute |
  | 29 | transport.stop | 25 | pattern.resetOrder | 75 | slice.rev |
  | 30 | session.prev | 26 | pattern.resetAll | 76 | slice.lock |
  | 31 | session.next | 79 | slice.select | 77 | slice.curveIn |
  | | | 80 | track.focus | 78 | slice.curveOut |

  Also hookable, no default: `transport.bpm`, `clock.source`, `export.start`,
  `session.save`, `record.toggle`, `track.load`, `track.beats`, `track.step`,
  `slice.all`, `slice.split`, `slice.merge`, `pattern.dupPrev`,
  `pattern.dupNext`, `pattern.refill`, `pattern.mode`, `view.trim`,
  `view.full`, and the modifier panel's `mod.action`, `mod.fire`,
  `mod.chance`, `mod.level`, `mod.mode`, `mod.hits`, `mod.to`, `mod.pitch`,
  `mod.len`, `mod.remove`, `mod.done`. With the editor closed (a plugin
  with its window shut) the mix, transport, pattern and session hooks still
  respond; slice and modifier hooks need the panel.
- **Developer hooks**: `FROG_AUTOLOAD=<wav>` loads a file into track 1 at
  start (and skips the factory session), `FROG_AUTOPLAY=1` presses Play
  once it has loaded, `FROG_NO_FACTORY=1` boots blank.
- **Offline**: `frog-render session.json out.wav [--beats N] [--rate HZ]`
  bounces a session through the same engine.

## Repository layout

```
Frog/               the plugin (config.h is the version's source of truth)
  engine/           C11 engine + tests
  ui/               IGraphics controls and the view
  tools/            frog-render, parity tooling
platform/linux/     appliance build, systemd unit, tests
docs/               PORT_PLAN.md, ARCHITECTURE.md, evidence images
public/             the browser version (reference and parity oracle)
ROADMAP.md          features, status, log
```

## Conventions

Version `MAJOR.MINOR.PATCH` with MINOR = the F-number of the current effort
(`Frog/config.h`); tags at milestones (`v0.8-engine`, `v0.11-mvp`). Tabs
for indentation, spaces for alignment, braces always (`.clang-format`).
Short comments. The repository is neutral: no personal attribution in code,
commits or docs.

## License

TBD.
