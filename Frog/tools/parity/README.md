# tools/parity — the browser version as the engine's oracle

The `js` branch's app (kept in `public/` on this branch) renders the same
sessions through its own `renderMix`; the engine must agree. Fixtures live in
`Frog/engine/tests/fixtures/parity/` and `engine/tests/parity_test.c` compares
them on every test run.

```
gen-fixtures.py     writes parity-source.wav (1.6 s stereo 16-bit 44.1 k, one sine burst per unit)
                    and the five session files: natural, edits, mods (exact), stretch, pitch (loose)
js-bounce.mjs       headless Chrome (playwright-core, system browser): seeds each session into
                    localStorage + IndexedDB the way the app saves them, loads the app, drives
                    the slicer registry's export hooks through renderMix, writes <case>.js.wav
                    (32-bit float, pre-encode) and <case>.js.json (what the app actually restored)
```

Regenerate after changing a session (the bounces are committed, so a normal
test run needs no browser):

```sh
python3 Frog/tools/parity/gen-fixtures.py
python3 -m http.server 8931 --bind 127.0.0.1 &          # repo root
cd Frog/tools/parity && npm i playwright-core && node js-bounce.mjs
cd ../.. && ./engine/tests/run-tests.sh
```

## What "exact" means

Exact cases match sample for sample to within 1e-2 peak / −80 dBFS RMS: the
engine writes 16-bit on disk, the browser applies curved fades as 65-point
piecewise-linear approximations, and its export ends in a 5 ms declick ramp
it never intended (`stopAt < when + effDur` compares 1.6 against
1.6000000000000001 on the final slot; the last 5 ms are skipped). Loose
cases (time-stretch, pitch) differ by implementation and are compared per
unit by level (1.5 dB) and pitch (4 %).

## Browser quirks the fixtures work around

- A saved `beats` other than 4 is not honoured on restore (the row is
  regenerated at the default grid), so fixtures use 4 beats of 1/16 and a
  1.6 s source at 150 bpm.
- Σw = U is not enforced on restore; a short row loops early. The engine
  rebuilds such a row, so fixture rows must add up.
- Voices may start on fractional samples (interpolated), so exact cases keep
  every event on an integer sample (0.1 s units = 4410 frames; half-unit
  hits = 2205). Quarter-unit hits stay in the loose case.
- The app flushes its own state on `beforeunload`: seed storage from a blank
  same-origin page, never by reloading the app page.
