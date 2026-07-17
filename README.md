# jsloop

A browser-based audio slicer and step sequencer, built with vanilla JavaScript and the Web Audio API. No build step, no dependencies — just static files.

Load an audio file, chop it into slices on a waveform, and sequence those slices into loops. Each slicer is independent, so you can run several at once, all locked to one master clock.

## Features

- **Waveform slicing** — load a file, set the start/end region (drag the green/red handles or the sliders), and let the app cut it into an evenly spaced grid.
- **Beats + Step** — declare how many quarter-note **beats** the loop is (this sets the tempo: `BPM = 60 × beats / selectionDuration`) and the **Step** subdivision (1/2…1/32). The slice grid is `beats × step` — e.g. 4 beats · 1/16 = 16 cells.
- **Zoom & trim** — zoom into a selection and "Trim" to make it the new working view.
- **Tile sequencer** — arrange slices into a per-slicer loop of variable-width tiles: drag to reorder, drag the edges to resize (sub-step precision), double-click to split, shift-click to merge — all live while the loop plays.
  - **Packed vs Gaps** — a global mode toggle: *Packed* keeps tiles contiguous (resize borrows from the neighbour); *Gaps* is free placement — moving/shrinking a clip leaves silence, dropping/growing overwrites.
  - **Reset Order / Reset All** — put the slices back in native play order (each keeps its fades/reverse/pitch; locks and gaps stay anchored), or rebuild the pristine default pattern.
- **Slice settings** — per-slice mute, lock, reverse, fade in/out envelope, duplicate, and refill, with an **All** broadcast toggle; per-slice pitch offset via mouse-wheel.
- **Modifier lane** — one optional step modifier per grid cell, firing by probability or every-N-loops: **Mute**, **Reverse**, **Randomize** (virtual shuffle press), **Reset** (restore native order, settings kept), and **Ratchet** (retrigger the step 1–8× per step across a 1–16-step length; the span absorbs the tiles it covers). Live feedback: the chip and its span brace light while a ratchet sounds, the waveform playhead restarts on every hit, and modifiers swallowed by the span grey out.
- **WAV export** — offline render of the master mix (any subset of slicers, any length in beats) to a normalized 16-bit stereo WAV, bit-faithful to live playback — time-stretch, fades, and step modifiers included.
- **Edit / perform layout** — Show/Hide details trades vertical space between a big waveform (edit) and a big tile row (perform).
- **Master transport** — one Master tempo + Play All / Stop All drive every slicer. All slicers share a single AudioContext and start on one clock instant, so multi-track loops are sample-locked.
- **MIDI clock sync** — sync to an external MIDI clock (Web MIDI): the incoming clock sets the master tempo, and MIDI Start/Continue/Stop drive the sequencers. A clock indicator shows the active source, tempo, and a 4-beat pulse.
- **Time-stretch & pitch-shift** — WSOLA time-stretching keeps pitch constant across tempos; slices can also be pitch-shifted (master + per-step offsets).
- **Randomize**, **color-coded slices**, **multiple slicers**, and **persistence** (settings in `localStorage`, audio in IndexedDB) so your session survives a reload.

The whole UI is built from one unified drag-control (button-shaped, drag any direction; replaces knobs/sliders/dropdowns) on a fluid relative-unit grid — see [public/CSS.md](public/CSS.md).

## Running

Everything lives under `public/` and runs as static files — no build or install. Serve that directory with any static web server, for example:

```sh
cd public
python3 -m http.server 8000
```

Then open <http://localhost:8000> in a browser. (A server is needed rather than opening the file directly because the app loads ES modules.)

## Project layout

```
public/
  index.html
  css/styles.css              design system (tokens + components) — see CSS.md
  js/
    main.js                     entry point — slicers, tile sequencer, transport, persistence
    audio-slicer-controller.js  connects the engine and the waveform view
    audio-engine.js             audio loading, slicing, playback (no DOM)
    audio-context.js            the shared AudioContext singleton (Tier 1c)
    beat-grid.js                shared beat↔time mapping (drift-free master clock)
    waveform-view.js            canvas drawing and interaction
    transport.js                lookahead sequencer scheduler
    drag-control.js             the unified button-shaped control
    midi-clock.js               Web MIDI clock receiver → master tempo + transport
    timestretch.js              WSOLA time-stretch DSP
    envelope.js                 per-voice fade envelope (shared by live + export)
    export-wav.js               offline mix render → 16-bit RIFF/WAV download
    palette.js                  shared slice colors
    storage.js                  localStorage + IndexedDB persistence
```

## Usage

TBD.

## Code style

See [CLINE_STYLE.md](CLINE_STYLE.md) — tabs for indentation, OTBS braces, spaces for alignment.
