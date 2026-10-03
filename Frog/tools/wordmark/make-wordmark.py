#!/usr/bin/env python3
"""Bake a word to SVG outlines from an OTF / TTF so the UI needs no font at
render time. Used for resources/img/frog-wordmark.svg (FROG in Cheltenham
Bold, fill #E0332E). The font file is not in the repository: see
resources/img/README.md for where it is kept.

    make-wordmark.py <font.otf> [TEXT] [#fill] > out.svg

Requires fontTools (pip install fonttools)."""
import sys

from fontTools.pens.boundsPen import BoundsPen
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont


def main():
	if len(sys.argv) < 2:
		sys.exit(__doc__)
	path = sys.argv[1]
	text = sys.argv[2] if len(sys.argv) > 2 else "FROG"
	fill = sys.argv[3] if len(sys.argv) > 3 else "#E0332E"
	font = TTFont(path)
	glyphs = font.getGlyphSet()
	cmap = font.getBestCmap()
	hmtx = font["hmtx"]
	kern = {}
	if "kern" in font:
		for table in font["kern"].kernTables:
			kern.update(table.kernTable)
	family = font["name"].getDebugName(4) or "unknown"

	# glyphs laid out on the advance, y up (font units); the viewBox flips it
	paths = []
	bounds = BoundsPen(glyphs)
	x = 0
	prev = None
	for ch in text:
		name = cmap[ord(ch)]
		if prev is not None:
			x += kern.get((prev, name), 0)
		pen = SVGPathPen(glyphs)
		glyphs[name].draw(TransformPen(pen, (1, 0, 0, -1, x, 0)))
		glyphs[name].draw(TransformPen(bounds, (1, 0, 0, -1, x, 0)))
		paths.append(pen.getCommands())
		x += hmtx[name][0]
		prev = name
	# a tight box with a 1-unit margin, so the mark scales to its ink
	l, t, r, b = (round(v) for v in bounds.bounds)
	print(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{l - 1} {t - 1} {r - l + 2} {b - t + 2}" role="img" aria-label="{text}">')
	print(f"\t<!-- {text} wordmark. {family}, set as outlines so no font is needed at render time. -->")
	print(f'\t<path fill="{fill}" d="{" ".join(paths)}"/>')
	print("</svg>")


if __name__ == "__main__":
	main()
