#!/usr/bin/env python3
"""Generate the parity fixtures' source WAV and session files.

	python3 Frog/tools/parity/gen-fixtures.py    # writes into Frog/engine/tests/fixtures/parity/

The source is 1.6 s stereo 16-bit at 44 100 Hz (headless Chrome's AudioContext
rate): sixteen 0.1 s units, each a decaying sine burst at its own frequency
(left) and a quieter burst at 1.5x that frequency (right), so reordering,
reversal, pan law and pitch are all measurable. Sessions use the browser
version's default grid, 4 beats of 1/16, at 150 bpm (natural: 4 beats /
1.6 s) so every slot boundary, half-step ratchet hit and 50 % fade lands on
an integer sample, which keeps the "exact" cases free of sub-sample start
interpolation in the browser. (The browser version does not honour a saved
`beats` other than 4 on restore, so the fixtures stay on the default.)
"""
import json
import math
import os
import struct
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, '..', '..', 'engine', 'tests', 'fixtures', 'parity'))
SR = 44100
UNITS = 16
UNIT_SEC = 0.1
FRAMES = int(SR * UNITS * UNIT_SEC)


def unit_freq(i):
	return 220.0 * 2 ** (i / 8.0)


def write_source(path):
	buf = bytearray()
	for n in range(FRAMES):
		t = n / SR
		i = min(UNITS - 1, int(t / UNIT_SEC))
		pos = t - i * UNIT_SEC
		env = math.exp(-pos / 0.06)
		left = 0.8 * env * math.sin(2 * math.pi * unit_freq(i) * pos)
		right = 0.5 * env * math.sin(2 * math.pi * unit_freq(i) * 1.5 * pos)
		buf += struct.pack('<hh', int(round(left * 32767)), int(round(right * 32767)))
	with wave.open(path, 'wb') as w:
		w.setnchannels(2)
		w.setsampwidth(2)
		w.setframerate(SR)
		w.writeframes(bytes(buf))


def tile(src, w=1, **kw):
	t = {'src': src, 'w': w, 'offset': 0, 'muted': False, 'colorIdx': src}
	t.update(kw)
	return t


def gap(w):
	return {'gap': True, 'w': w}


def session(bpm, tiles, mods=(), volume=1.0, pan=0.0):
	return {
		'version': 3, 'nextSlicerId': 1,
		'slicers': [{
			'id': 0, 'fileName': 'parity-source.wav', 'virtualStart': 0, 'virtualEnd': 1, 'start': 0, 'end': 1,
			'beats': 4, 'bpm': 150, 'divisionDenom': 16, 'bpmManual': False,
			'volume': volume, 'pan': pan, 'muted': False, 'masterPitch': 0, 'randLevel': 50,
			'seq': {'tiles': tiles, 'mods': list(mods), 'loop': True},
		}],
		'midi': {'enabled': False, 'inputId': None},
		'master': {'bpm': bpm, 'userSet': True},
		'seqEditMode': 'pack',
	}


def natural_row():
	return [tile(i) for i in range(UNITS)]


CASES = {
	# exact: every event on an integer sample, no stretch, no pitch
	'natural': session(150, natural_row()),
	'edits': session(150, [
		tile(1), tile(0),                                   # swapped
		tile(2, muted=True),
		gap(1),
		tile(4, reversed=True),
		tile(5, gain=1.5),
		tile(6, fadeIn=0.5, fadeInCurve='exp', fadeOut=0.25, fadeOutCurve='s'),
		tile(7, fadeIn=0.5, fadeInCurve='linear'),
		tile(8, w=2), tile(10, fadeOut=0.5, fadeOutCurve='log'),
		tile(11, reversed=True, gain=0.5),
		tile(13, w=0.5), tile(12, w=0.5), tile(12.5, w=0.5), tile(13.5, w=0.5),   # quarter-lattice widths, Σw stays 16
		tile(14), tile(15, muted=True),
	], volume=0.8, pan=-0.3),
	'mods': session(150, [tile(1), tile(0)] + [tile(i) for i in range(2, UNITS)], mods=[
		{'step': 0, 'action': 'reset', 'fireMode': 'every', 'fireValue': 1},
		{'step': 1, 'action': 'mute', 'fireMode': 'prob', 'fireValue': 100},
		{'step': 2, 'action': 'gain', 'fireMode': 'prob', 'fireValue': 100, 'gainAmt': 30},
		{'step': 3, 'action': 'rev', 'fireMode': 'prob', 'fireValue': 100},
		{'step': 4, 'action': 'ratchet', 'fireMode': 'prob', 'fireValue': 100, 'mode': 'even', 'subdiv': 2, 'subdivTo': 2, 'pitchStep': 0, 'lenSteps': 1},
		{'step': 6, 'action': 'ratchet', 'fireMode': 'prob', 'fireValue': 100, 'mode': 'even', 'subdiv': 1, 'subdivTo': 1, 'pitchStep': 0, 'lenSteps': 2},
		{'step': 9, 'action': 'ratchet', 'fireMode': 'prob', 'fireValue': 100, 'mode': 'even', 'subdiv': 2, 'subdivTo': 2, 'pitchStep': 0, 'lenSteps': 1},
		{'step': 12, 'action': 'mute', 'fireMode': 'every', 'fireValue': 2},
		{'step': 14, 'action': 'gain', 'fireMode': 'prob', 'fireValue': 100, 'gainAmt': 150},
	], pan=0.4),
	# loose: time-stretch (fill 1.2) and pitch paths differ in implementation
	'stretch': session(125, natural_row()),
	'pitch': session(150, [
		tile(0, offset=12), tile(1, offset=-5), tile(2, offset=7), tile(3),
		tile(4), tile(5), tile(6), tile(7),
	] + [tile(i) for i in range(8, UNITS)], mods=[
		{'step': 5, 'action': 'ratchet', 'fireMode': 'prob', 'fireValue': 100, 'mode': 'ramp', 'subdiv': 1, 'subdivTo': 4, 'pitchStep': 0, 'lenSteps': 2},
		{'step': 7, 'action': 'ratchet', 'fireMode': 'prob', 'fireValue': 100, 'mode': 'pitch', 'subdiv': 2, 'subdivTo': 2, 'pitchStep': 12, 'lenSteps': 1},
		# x4 hits start on quarter samples: the browser interpolates sub-sample starts, so this stays loose
		{'step': 9, 'action': 'ratchet', 'fireMode': 'prob', 'fireValue': 100, 'mode': 'even', 'subdiv': 4, 'subdivTo': 4, 'pitchStep': 0, 'lenSteps': 1},
	]),
}


def main():
	os.makedirs(OUT, exist_ok=True)
	write_source(os.path.join(OUT, 'parity-source.wav'))
	for name, s in CASES.items():
		with open(os.path.join(OUT, f'{name}.json'), 'w') as f:
			json.dump(s, f, indent='\t')
			f.write('\n')
	print(f'wrote {len(CASES)} sessions + parity-source.wav ({FRAMES} frames) to {OUT}')


if __name__ == '__main__':
	main()
