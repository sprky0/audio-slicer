# jsloop

A browser-based audio slicer and step sequencer, built with vanilla JavaScript and the Web Audio API. No build step, no dependencies — just static files.

Load an audio file, chop it into slices on a waveform, and sequence those slices into loops. Each slicer is independent, so you can run several at once.

## Features

- **Waveform slicing** — load a file, set start/end markers, and subdivide the selection into evenly spaced slices.
- **Zoom & cut** — zoom into a selection and "cut" to make it the new working view.
- **Step sequencer** — arrange slices into a per-slicer loop with adjustable BPM and sample-accurate scheduling.
- **Time-stretch & pitch-shift** — WSOLA time-stretching keeps pitch constant across tempos; slices can also be pitch-shifted.
- **Color-coded slices** — each slice has a consistent color across the waveform and the sequencer.
- **Multiple slicers** — add as many independent slicers as you like.
- **Persistence** — settings are saved to `localStorage` and audio files to IndexedDB, so your session survives a reload.

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
  css/
  js/
    main.js                     entry point — wires up slicers, sequencer, persistence
    audio-slicer-controller.js  connects the engine and the waveform view
    audio-engine.js             audio loading, slicing, playback (no DOM)
    waveform-view.js            canvas drawing and interaction
    transport.js                lookahead sequencer scheduler
    timestretch.js              WSOLA time-stretch DSP
    knob.js                     custom rotary control
    palette.js                  shared slice colors
    storage.js                  localStorage + IndexedDB persistence
```

## Usage

TBD.

## Code style

See [CLINE_STYLE.md](CLINE_STYLE.md) — tabs for indentation, OTBS braces, spaces for alignment.
