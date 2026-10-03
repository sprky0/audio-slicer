# Brand assets

- `frog-wordmark.svg` — FROG set in **Cheltenham Bold**, baked to outlines
  (fill `#E0332E`, `kBrand` in `ui/Style.h`). No font is needed at render
  time. Regenerate with `Frog/tools/wordmark/make-wordmark.py <font.otf>`.
- `rat-factory.svg` — the family maker mark, unmodified from tink-vst.

## The font file

The Cheltenham Bold OTF is **not in the repository**. The copy used is
Bitstream's `CheltenhamBT-Bold` (name table says "Confidential"), supplied
as `cheltenham-bold.zip` containing `Cheltenham Bold.otf`; its
redistribution licence is unknown, so it stays with the repository owner,
outside any checkout. Anyone regenerating the wordmark needs that file (or
another Cheltenham Bold) and passes its path to the script. The SVG in the
tree is the deliverable; nothing in the build reads the font.
