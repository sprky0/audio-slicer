# CSS / design system

All styles live in **`css/styles.css`** (one file: tokens in `:root`, then
components). It's a dark, relative-unit system — no hard-coded layout pixels, no
media queries. `example.html` + `css/example.css` are a standalone demo page, not
part of the app.

## Design tokens (`:root`)

**Colors** — dark surfaces + semantic action colors, each with resting/hover/active
shades (hover lightens, press darkens):
- Surfaces: `--color-bg-primary` `#282c34`, `--color-bg-secondary` `#181818`,
  `--color-input-bg`; text `--color-text-primary`; borders `--color-border`,
  `--color-border-hover`.
- Neutral / default = **blue** (`--color-accent`, `-hover`, `-active`).
- Positive / "go" / on = **green** (`--color-positive*`, dark text `--color-on-positive`).
- Danger / stop / destructive = **red** (`--color-danger*`, `--color-marker`).
- Section-label chip: `--color-label-bg` (grey) + `--color-label-text` (orange).
- `--focus-ring`, `--focus-ring-accent`, `--shadow-btn`.

**Spacing / radius / misc:** `--space-xs…xxxl` (6→32px), `--radius-sm/md/lg`,
`--transition-fast` / `-press`, `--opacity-disabled`.

**Layout grid — the important part.** One relative base unit drives every height,
gap and column width, so the whole UI scales with the root font size:
- `--ui-scale` (default 1) — global size knob; bump it (on `:root` or a single
  `.slicer-container`) for a roomier/accessible mode and everything grows together.
- `--grid-unit` = `0.5rem * --ui-scale` (~8px). Derived: `--gap` (1u),
  `--control-h` (4u, one interface row ~32px), `--col-min` (12u, min column),
  `--tile-h` (sequencer tile-row height; overridden per edit/perform mode on
  `.slicer-container`).

## Layout conventions

- **Toolbars are fluid auto-fit grids.** `.toolbar-cluster` / `.seq-toolbar` use
  `grid-template-columns: repeat(auto-fit, minmax(var(--col-min), 1fr))` +
  `grid-auto-rows: minmax(var(--control-h), auto)`, so they reflow at any width
  with no media queries. Every control is one `--control-h` row tall.
- **Per-item span** via `setSpan(el, cols, rows)` (helper in `main.js`): sets
  `--col-span` / `--row-span` custom props the CSS reads. `cols='full'` →
  `.span-full` (whole row). Retune a toolbar by changing those numbers, not the CSS.

## Components

- **`.drag-control`** — the unified control (replaces knobs/sliders/dropdowns): a
  button-shaped cell with a translucent fill from origin→value, a bright leading
  edge, and a centered "Label value" readout. Driven by `js/drag-control.js`.
- **`.toggle-btn`** — on/off toggle (recessed hollow-pip OFF vs lit-green filled-pip
  ON). Used by Loop, Show/Hide details, Sync, the Packed/Gaps mode toggle.
- **Buttons** carry semantic intent by class (blue default / green primary / red
  danger), each with hover/active/focus-visible/disabled states.
- **`.ui-label`** — section-title chip (`makeLabel('…')` in main.js); e.g. the
  "Source" / "Sequencer" titles.
- **`.seq-tile`** / **`.seq-gap`** — sequencer tiles (waveform-backed) and silent
  gaps (dashed well, leave-gaps mode).

When adding a control, reuse these tokens/classes so it stays cohesive; assign its
grid footprint with `setSpan` rather than writing new width CSS.
