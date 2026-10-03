# Requests to ratfactory-linux-host for Frog

Frog (`frog-appliance`) runs on `libratfactory-linux-host` like Tink. Two
things need edits in the host repo before Frog is a first-class appliance
unit; a third is a confirmation. Frog's side of each is done or waiting
only on the id. Frog's roadmap rows: F3.5, F18.1, F16.4.

Checked against ratfactory-linux-host as of 2026-10-03 (L28 is the last
L-item; `pi-switch-unit.sh` lists two units).

---

## 1. Make `frog-appliance.service` a switchable appliance unit (Frog F3.5)

**What**: `scripts/pi-switch-unit.sh` refuses any unit not in its list, so
`deploy-to-pi.sh --service` from the Frog repo installs the unit but cannot
make it the boot unit.

**Edits**, all in `scripts/pi-switch-unit.sh`:

1. Add the unit to the list (line 26):
   ```sh
   APPLIANCE_UNITS="ratfactory-linux-host.service tink-appliance.service frog-appliance.service"
   ```
   The remote script already iterates `${UNITS}`, so disabling the others
   and the `status` table pick it up with no further change.
2. Add a short name beside `tink`, in the argument loop (line 38) and the
   usage line (line 49) and header comment (lines 9–13):
   ```sh
   frog) CMD="frog-appliance.service"; shift ;;
   ```
   ```
   usage: $0 status|poc|tink|frog|<unit>.service [--host H] [--reboot] [--no-start]
   ```
3. `docs/APPLIANCE_UNIT.md` §"Switching" (around line 117): list
   `frog-appliance.service` with the others and add the line
   `scripts/pi-switch-unit.sh frog --reboot`.

**Also, in the unit files** (one line each, so that starting one appliance
stops the others — Frog's unit already names both of these):

- `tink-vst/platform/linux/systemd/tink-appliance.service` line 35:
  `Conflicts=ratfactory-linux-host.service frog-appliance.service`
- `systemd/ratfactory-linux-host.service`: add a `Conflicts=` naming
  `tink-appliance.service frog-appliance.service` if it has none today.

**Optional, later**: `scripts/build-image.sh` builds an image with one
product unit (lines 174–190 choose Tink or the POC). A `--product frog`
branch would take `FROG_ROOT/platform/linux/build-arm64/bin/frog-appliance`,
`lib/libfrog-editor.so`, `bin/resources/` and
`FROG_ROOT/platform/linux/systemd/frog-appliance.service`, data dir
`/data/ratfactory/frog`. Not needed for development: `deploy-to-pi.sh`
installs over a Tink image.

**Frog's unit for reference**: `platform/linux/systemd/frog-appliance.service`
in the Frog repo, a copy of `ratfactory-linux-host.service` per
`docs/PORTING_A_PLUGIN.md` §4 with `Description`, `ExecStart`
(`/opt/ratfactory/bin/frog-appliance`), `RF_DATA_DIR=/data/ratfactory/frog`
and `Conflicts=` changed.

**Done when**: `scripts/pi-switch-unit.sh frog` on a Pi where
`deploy-to-pi.sh --service` has run makes Frog the boot unit. Please write
the host commit hash into Frog's F3.5 row (or tell the Frog side and we do).

---

## 2. Open an L-item for audio capture (Frog F18.1)

**What**: the host opens ALSA for playback only (`AlsaAudio.cpp` line 86,
`SND_PCM_STREAM_PLAYBACK`) and `Processor::process()` has no input
(`Processor.h` line 239). Frog records live input into a track; the Frog
half is done and tested on the Mac (0.18.0: capture lane, take written to
`samples/`, swapped into the track). On the appliance the Rec button does
nothing until the host has an input path.

**Please add to `LINUX_ROADMAP.md` §5** (next free id is L29) and mirror
into `tink-vst/LINUX_ROADMAP.md` as the other L-rows are:

> | L29 | **Audio capture for recording products** (Frog F18.1, 2026-10-03):
> open the capture PCM on the same ALSA device as playback (full duplex,
> same rate and period, `SND_PCM_STREAM_CAPTURE`), read it in the audio
> thread before `process()` with the same xrun handling as playback, and
> hand it to the plugin through an input path on `rflh::Processor`: either
> `process(const double* const* inputs, int nInputs, double* const* outputs,
> int nChannels, int nFrames)` with the old signature kept as a default
> that forwards with `inputs = nullptr`, or a `processIO()` overload the host
> prefers when `inputChannels() > 0`. `IPlug2HeadlessProcessor` forwards the
> inputs to `ProcessBlock`. `--render-wav` grows `--input FILE` so an
> offline check can feed a WAV. A device without capture (HDMI) reports 0
> input channels and the plugin's Rec stays disabled. Gate: Frog records a
> 4-bar take on the Pi 3B while playing, 0 xruns over 10 min
> (Frog F18.3). Cross-link: Frog ROADMAP F18, F18.1, F18.3 | Planned |

**Notes for whoever builds it**

- Frog already calls `fg_engine_capture_block(inputs, nIn, nFrames)` inside
  `ProcessBlock` when `inputs` is non-null and a track is armed; nothing on
  the audio thread allocates. So the only Frog change once the host has it
  is in `platform/linux/frog-plugin/FrogProcessor.h` (forward the new
  signature), plus enabling Rec on the appliance.
- `Frog::StartRecord` is refused when `NInChansConnected() == 0`; the host
  should report capture channels through the existing channel-count query
  so that stays true on HDMI-only boards.
- Please write L29 (or whatever id) into Frog's F18 master row, F18.1 and
  F18.3, and the Frog ids into the L-row, so both repos read the same
  status. The Frog side can do this once the id exists.

---

## 3. Confirm: product-specific keys in `appliance.conf` (Frog F16.4, F15.5)

**What**: Frog wants three board keys in `/data/ratfactory/frog/appliance.conf`:
`max_tracks=` (4 on the Pi 3), `midi_trigger_base=` (the note that plays
slice 1, default 36) and `clock=internal|midi|host`. These are Frog's, not
the host's, so Frog would read them itself from the same file (same
`key=value` format as `docs/APPLIANCE_CONF.md`).

**Please confirm** `ApplianceConf` keeps unknown keys on write-back
(`ApplianceConf.h` line 105 and `APPLIANCE_CONF.md` line 43 say unknown
keys are logged, skipped and preserved). If so, no host edit is needed and
Frog's F16.4 proceeds. If the host would rather own a generic
"product keys" namespace (e.g. a `plugin.` prefix passed through
`Processor::applySetting`), say so and Frog follows that instead.

---

## Pointers

- Frog repo, branch `native`: `ROADMAP.md` (F3.5, F16.4, F18.1), `platform/linux/README.md`,
  `platform/linux/systemd/frog-appliance.service`, `platform/linux/deploy-to-pi.sh`.
- Host repo: `scripts/pi-switch-unit.sh`, `systemd/`, `host/Processor.h`,
  `host/AlsaAudio.cpp`, `host/iplug2/IPlug2HeadlessProcessor.h`,
  `host/ApplianceConf.h`, `LINUX_ROADMAP.md`, `docs/APPLIANCE_UNIT.md`,
  `docs/PORTING_A_PLUGIN.md`.
