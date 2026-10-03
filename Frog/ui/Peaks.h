#pragma once

#include "engine/sample.h"

#include <algorithm>
#include <cmath>
#include <vector>

// Min / max peak columns of a sample, computed once at load on the main
// thread, so the waveform and tile controls never touch the sample on a
// redraw (a few thousand columns instead of a few million samples; the Pi 3
// budget).
namespace frogui {

struct Peaks {
	int cols = 0;
	int nCh = 0;
	int64_t frames = 0;
	double sampleRate = 0.;
	std::vector<float> mn[2], mx[2];

	bool Empty() const { return cols == 0; }

	void Build(const fg_sample* s, int columns = 4096) {
		*this = Peaks();
		if (!s || s->frames <= 0) {
			return;
		}
		cols = columns;
		nCh = s->nCh;
		frames = s->frames;
		sampleRate = s->sampleRate;
		for (int c = 0; c < nCh; c++) {
			mn[c].assign((size_t)cols, 0.f);
			mx[c].assign((size_t)cols, 0.f);
			for (int k = 0; k < cols; k++) {
				const int64_t a = (int64_t)((double)k / cols * frames);
				int64_t b = (int64_t)((double)(k + 1) / cols * frames);
				if (b <= a) {
					b = a + 1;
				}
				float lo = 1.f, hi = -1.f;
				for (int64_t i = a; i < b && i < frames; i++) {
					const float v = s->ch[c][i];
					lo = v < lo ? v : lo;
					hi = v > hi ? v : hi;
				}
				mn[c][(size_t)k] = lo <= hi ? lo : 0.f;
				mx[c][(size_t)k] = lo <= hi ? hi : 0.f;
			}
		}
	}

	// peak envelope over the sample fraction [f0, f1), channel c
	void Range(int c, double f0, double f1, float& lo, float& hi) const {
		lo = 0.f;
		hi = 0.f;
		if (Empty() || c >= nCh) {
			return;
		}
		int a = (int)std::floor(f0 * cols), b = (int)std::ceil(f1 * cols);
		a = std::max(0, std::min(cols - 1, a));
		b = std::max(a + 1, std::min(cols, b));
		lo = 1.f;
		hi = -1.f;
		for (int k = a; k < b; k++) {
			lo = std::min(lo, mn[c][(size_t)k]);
			hi = std::max(hi, mx[c][(size_t)k]);
		}
		if (lo > hi) {
			lo = hi = 0.f;
		}
	}
};

}  // namespace frogui
