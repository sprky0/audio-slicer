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

- **Samples**: `FROG_SAMPLES_DIR`, else `$RF_DATA_DIR/samples` on the
  appliance, else `~/Library/Application Support/Frog/samples`. The Load
  button lists that folder; on the Mac a file can also be dropped on the
  waveform. Recordings land there as `rec-<stamp>.wav`; bounces go to
  `exports/`, sessions to `sessions/` beside it.
- **MIDI**: clock (0xF8 / Start / Continue / Stop) when Clock is set to
  MIDI; Note On on channel n plays a slice of track n (note 36 = slice 1);
  Program Change loads a session by index.
- **Developer hooks**: `FROG_AUTOLOAD=<wav>` loads a file into track 1 at
  start, `FROG_AUTOPLAY=1` presses Play once it has loaded.
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
