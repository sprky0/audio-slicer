# Frog — feature table (0.24.0)

Every user-facing behaviour of the browser version (`public/`, the oracle)
beside what Frog does today, as the input to the UI discussion: now that
the audio side is close to complete, which of these should be in front of
a performer, how, and what is missing for creativity and flow.

Legend: **=** same behaviour · **+** Frog goes further · **~** present with a
difference · **✗** not in Frog · **—** not in the browser.

## 1. Slice (tile) properties

| Feature | Browser | Frog | Notes |
|---|---|---|---|
| Source position, width on a ¼-unit lattice (`src`, `w`, min ¼) | yes | = | Σw = unit count, every edit preserves it |
| Mute | toggle, All-broadcast | = | silent slot keeps its time |
| Lock | per slice only | + | pinned in **both** edit modes (F20); orange ring + badge |
| Reverse | toggle, All-broadcast | = | backwards reader, no copies |
| Pitch offset per slice | mouse wheel, ±5, no control | ~ | toolbar Pitch control, ±12 (master ±12, total clamped) |
| Gain 0–200 %, fades scale to it | yes | = | badge when ≠ 100 % |
| Fade in / out 0–100 % of the slice | yes | = | shapes Lin / Exp / Log / S, proportional scaling when they overlap, 5 ms declick clearance |
| Colour: 16 hues, stable through resizes | yes | = | |
| Gaps (silent slots) | Gaps mode | = | |
| Audition / preview from a point in the waveform | click in the selection | ✗ | raw equal segments, ignores the arrangement; a UI question |

## 2. Row edits

| Feature | Browser | Frog | Notes |
|---|---|---|---|
| Select a slice or gap (tap) | yes | = | drives the toolbar |
| Reorder by drag (packed: live reflow) | yes | = | locked slices never move or get crossed (F20) |
| Edge resize, boundary eats what it moves over, cascade | yes | = | a lock is a wall (F20) |
| Split (double-tap) | yes | = | Split button too |
| Merge (shift-click) | yes | ~ | Merge button (no modifier keys on a touch panel) |
| Dup ◀ / ▶ (stamp a clone, blocked by locks) | yes | = | |
| Refill (restore a slot's native slice; All skips locks) | yes | = | |
| Reset Order (native order, settings kept, locks and gaps anchor) | yes | = | |
| Reset All (pristine row, wipes locks) | yes | = | |
| Randomize at Amt (0–100, order only, locks pinned) | yes | = | identical algorithm (buckets between locks, Fisher–Yates per level) |
| Packed / Gaps edit modes | global toggle | = | per plugin instance |
| All broadcast toggle (Mute / Rev / Gain / fades / curves / Pitch) | yes | + | Pitch broadcast too |
| Beats {1…16} × Step {1/2…1/32} grid with positional inheritance | yes | = | same-count change keeps the row (F20) |
| Locked runs survive a grid change in place | yes | = | |
| Selection region (start / end), Trim (zoom in), Full | handles + sliders + Trim | ~ | handles with finger-sized zones; Trim and Full buttons; no sliders |
| Track "Reset" (vol, pan, window, region, beats, step) | details panel | ✗ | partially covered by Reset All; UI question |

## 3. Step modifiers (the lane)

| Feature | Browser | Frog | Notes |
|---|---|---|---|
| One modifier per step, pinned to the grid, drag to move | yes | = | |
| Fire: Prob 0–100 % (rolled per pass) / Every N loops (1st of N) | yes | = | |
| Mute, Rev (toggles), Gain (× level, multiply) | yes | = | |
| Rand (virtual Randomize at Amt), Reset (order) — fire before the covering slot | yes | = | the audio thread edits the active pattern and posts it back |
| Ratchet: Even / Ramp / Pitch, Hits 1–8, To, Pitch per hit, Len 1–16, absorbs covered tiles | yes | = | hits drained block by block |
| Ratchet tail-through (Len < tile) | planned | + (F21) | |
| Ratchet pitch hits at constant speed (keep pitch) | planned | + (F21) | varispeed stays the default |
| **Pitch** action (transposition of the covered pass, ±12) | planned ("pitch nudge") | + (F21) | the "auto transposition" |
| Voice modifiers apply to ratchet hits | yes | = (F21) | |
| Chips flash at their moment; ratchet chip + brace lit; absorbed chips grey | yes | = | |
| True per-step chop (mute / rev one step of a wider tile) | planned | ✗ | needs mid-voice splitting; candidate |
| Modifier defaults (gain 50, 2 hits, Len 1, whole tone per hit) | yes | = (F21) | |

## 4. Time, pitch, sound

| Feature | Browser | Frog | Notes |
|---|---|---|---|
| Master BPM 20–400, first clip sets it until the user does | yes | = | a parameter |
| Drift-free beat grid; tempo change re-anchors phase-continuously | yes | = | |
| Fill policy: raw at factor ≈ 1, else stretch (WSOLA) × pitch | yes | ~ | streaming WSOLA per voice: no cache, no first-pass fallback, instant tempo response |
| Master pitch per track | ±5 | + | ±12 |
| Loop on / off (one pass, tail rings out) | yes | = | |
| Late join in phase, skip-forward into the bar | yes | = | |
| Per-track play / stop | code only (hidden) | — | engine supports it; no UI |
| Per-track BPM | hidden | — | master governs |
| Volume, Pan (Web Audio laws), channel Mute | yes | = | parameters, host-automatable |
| Solo | planned ("audition path") | + (F22) | |
| Preview overlay transport | yes | ✗ | tied to the audition question |

## 5. Clock and MIDI

| Feature | Browser | Frog | Notes |
|---|---|---|---|
| MIDI clock 24 PPQN, 49-pulse tempo window, PLL phase lock | yes | = | first pulse after Start is the downbeat (spec; the browser counted it 1/24) |
| Start / Continue / Stop | yes | = | |
| Host transport sync | — | + | plugin formats |
| Clock source choice Internal / MIDI / Host | Sync toggle | + | |
| Note triggers (channel n → track n, note 36 = slice 1, velocity) | — | + | one-shot, rings out |
| Program Change → session | — | + | |
| CC map with learn, hand-editable file, omni, focused track | — | + | every control hookable (F16.2) |
| Clock indicator (source, BPM, beat dots) | yes | ✗ | the transport shows BPM only; UI question |

## 6. Sessions, files, export

| Feature | Browser | Frog | Notes |
|---|---|---|---|
| Persistence (localStorage + IndexedDB), restore validates | yes | ~ | sessions as files; plugin state chunk; samples by path |
| Save / load named sessions, step through them | — | + | sessions double as presets (F16.3) |
| Factory session on first run | — | + | generated break (F11.5) |
| Add / Duplicate / Remove track | yes | = | tab strip |
| Load a file (picker, drop) | yes | = | file list from the samples dir; drop on the Mac |
| Record live input into a track | — | + | desktop formats with inputs (F18.2); appliance awaits host capture |
| Export: per-track checkboxes, length in beats, normalised 16-bit | yes | ~+ | All / Focused / Stems, 1–8 loops, Normalize, 16 / 24-bit, fixed seed (F17.4, F23) |
| Clear saved | yes | — | delete files |

## 7. Layout and interaction

| Feature | Browser | Frog | Notes |
|---|---|---|---|
| Unified drag control (any direction, tap, double-tap, wheel) | yes | = | plus long-press = MIDI learn |
| Edit / perform layout (waveform vs tile height) | Show / Hide details | = | Perform toggle |
| Relative-unit fluid grid | CSS | = | unit from the shorter side, 6–12 px |
| Keyboard: Escape closes popovers | yes | — | no keyboard on the panel |
| Tooltips | yes | ~ | version bubble only |
| Multi-track view | stacked slicers | ~ | one focus track + tab strip |

## 8. Not in either — candidates for the discussion

These came up while porting or in the browser's notes. None is built; each
is a question of whether it serves a performer.

- **Audition / preview** of the source from a tapped point, before slicing
  (browser had it; Frog does not).
- **Per-step chop**: a modifier that affects one step of a wider tile.
- **Swing / shuffle** on the step grid (never in the browser).
- **Per-slice micro-timing nudge** (ahead / behind the grid).
- **Choke / legato between slices** (let a slice ring into the next instead
  of cutting; the ratchet tail is a first taste).
- **Richer meter**: dotted beat unit, odd or compound signatures.
- **Scale / interval vocabulary** for the Pitch action and ratchet pitch
  instead of raw semitones.
- **Pattern memory / scenes**: store and recall a track's row and modifiers
  in a tap (sessions are whole-rig; this would be per track, instant).
- **Pads / trigger mode**: play slices from the panel or a controller
  without the sequencer (note triggers exist over MIDI; nothing on screen).
- **Transient markers** (F19): non-uniform slices from detected onsets.
- **Boot into the last session** on the appliance (F11.6).
- **Per-track CC bindings** for multi-channel rigs (F16.5).

## 9. UI questions this table raises

1. The modifier settings panel now carries 14 controls; which belong on
   the chip itself, which in a panel, which to MIDI?
2. Audition before slicing: worth a gesture on the waveform, or does the
   factory session and instant slicing make it moot?
3. The transport bar: BPM alone, or a clock indicator with beat dots and
   source, as the browser had?
4. Per-track play / stop and solo: the engine has both; what does a
   performer want on the tab strip?
5. Which of §8 would change how a set feels, rather than add a feature?
