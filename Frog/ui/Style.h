#pragma once

#include "IGraphicsStructs.h"

#include <algorithm>
#include <cmath>

// Frog's UI tokens. One relative unit drives every size (the browser version's
// --grid-unit): 8 px at the 1024 × 600 panel, scaling with the shorter side so
// the same layout fits 800 × 480 and 1280 × 720. Colour carries intent: blue is
// neutral, green is go / on, red is stop / danger, grey chips with orange text
// are labels. Touch targets are a full control height (6 u = 48 px at the
// panel, about 7 mm on the 7" screen).
namespace frogui {

using namespace iplug;
using namespace igraphics;

inline float Unit(float w, float h) {
	const float u = std::min(w / 128.f, h / 75.f);
	return std::max(6.f, std::min(12.f, u));
}

constexpr float kControlU = 6.f;   // control height in units
constexpr float kGapU = 1.f;       // spacing between controls

constexpr const char* kFont = "Roboto-Regular";

// surfaces
const IColor kBg(255, 24, 26, 30);
const IColor kPanel(255, 34, 37, 43);
const IColor kWell(255, 16, 17, 20);
const IColor kLine(255, 58, 62, 70);
const IColor kText(255, 230, 232, 236);
const IColor kTextDim(255, 140, 146, 156);
const IColor kTextOnFill(255, 245, 246, 248);

// intents
const IColor kNeutral(255, 58, 123, 213);
const IColor kNeutralDim(255, 40, 70, 110);
const IColor kGo(255, 46, 158, 91);
const IColor kGoDim(255, 30, 80, 52);
const IColor kStop(255, 217, 70, 60);
const IColor kStopDim(255, 110, 40, 36);
const IColor kLabelChip(255, 48, 51, 58);
const IColor kLabelText(255, 224, 138, 46);
const IColor kBrand(255, 224, 51, 46);   // the wordmark red

// waveform / tiles
const IColor kWave(255, 120, 190, 255);
const IColor kWaveDim(255, 70, 95, 120);
const IColor kSelStart(255, 46, 158, 91);
const IColor kSelEnd(255, 217, 70, 60);
const IColor kPlayhead(255, 255, 255, 255);
const IColor kLock(255, 255, 160, 40);

enum class Intent { Neutral, Go, Stop, Label };

inline IColor FillFor(Intent i) {
	switch (i) {
		case Intent::Go:
			return kGo;
		case Intent::Stop:
			return kStop;
		case Intent::Label:
			return kLabelChip;
		default:
			return kNeutral;
	}
}

inline IColor DimFor(Intent i) {
	switch (i) {
		case Intent::Go:
			return kGoDim;
		case Intent::Stop:
			return kStopDim;
		case Intent::Label:
			return kLabelChip;
		default:
			return kNeutralDim;
	}
}

// The 16 slice colours: hsl(i × 22.5°, 70 %, 48 %), as the browser version.
inline IColor SliceColor(int idx) {
	const double h = (idx % 16) * 22.5;
	const double s = 0.70, l = 0.48;
	const double c = (1.0 - std::fabs(2.0 * l - 1.0)) * s;
	const double hp = h / 60.0;
	const double x = c * (1.0 - std::fabs(std::fmod(hp, 2.0) - 1.0));
	double r = 0, g = 0, b = 0;
	if (hp < 1) {
		r = c; g = x;
	} else if (hp < 2) {
		r = x; g = c;
	} else if (hp < 3) {
		g = c; b = x;
	} else if (hp < 4) {
		g = x; b = c;
	} else if (hp < 5) {
		r = x; b = c;
	} else {
		r = c; b = x;
	}
	const double m = l - c / 2.0;
	return IColor(255, (int)std::lround((r + m) * 255), (int)std::lround((g + m) * 255), (int)std::lround((b + m) * 255));
}

inline IText Text(float size, const IColor& col, EAlign align = EAlign::Center, EVAlign valign = EVAlign::Middle) {
	return IText(size, col, kFont, align, valign);
}

}  // namespace frogui
