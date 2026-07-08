---
name: verify
description: Build/launch/drive recipe for verifying jsloop changes end-to-end in a headless browser.
---

# Verifying jsloop

Static site — no build. Everything lives in `public/` (ES modules, `index.html` loads `js/main.js`).

## Launch

```bash
cd public && python3 -m http.server 8931 --bind 127.0.0.1 &
```

## Drive (headless Chrome via playwright-core)

`npm i playwright-core` in a scratch dir and launch with the system browser —
no browser download needed:

```js
chromium.launch({ channel: 'chrome', headless: true,
                  args: ['--autoplay-policy=no-user-gesture-required'] })
```

Key facts that save time:

- The app auto-creates **one empty slicer on load** (when localStorage is empty).
  Don't click `#addSlicerBtn` unless you want a second one.
- Load audio via the hidden per-slicer `input[type=file]` with `setInputFiles`
  (a 2s WAV → 4 beats @ 120 BPM → 16 tiles at the default Beats 4 / Step 1/16).
- Generate the test WAV yourself: put a constant tone in some steps (good for
  measuring fades/gain) and a short burst at each step START in others (good for
  detecting reversal/reordering — the burst position inside the step window is
  the tell).
- Tiles are `.seq-tile`; click selects (drives the Slice settings row). Wheel
  over a tile = per-tile pitch (forces the time-stretch path at natural BPM).
  DragControls (`.drag-control`) jump on click: click at x-fraction = value.
- Persistence: settings in `localStorage['jsloop.state.v1']` (400ms debounce +
  flush on beforeunload); audio blobs in IndexedDB. `page.reload()` exercises
  the full save/restore path.

## Observe audio at sample level

Live output is hard to capture headless — use the app's own **Export WAV**
(master toolbar → panel → Export button, catch Playwright's `download` event).
It renders through the same `getTilePlayback` policy as live playback, so it's
faithful evidence. Parse the WAV (16-bit stereo, 44.1k) and compare RMS windows
per 0.125s step.

## Gotchas

- `favicon.ico` 404s in the console are pre-existing noise.
- Export waits for WSOLA stretch builds itself; no need to poll the overlay.
