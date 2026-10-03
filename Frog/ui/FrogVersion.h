#pragma once

#include "IControl.h"

// Build stamp. scripts/stamp-version.sh regenerates version.h before every
// build; it is gitignored, so a fresh clone compiles against the fallbacks
// below and reads "dev build" until the script has run once.

// version.h is generated one level up in Frog/.
#if defined(__has_include)
#if __has_include("../version.h")
#include "../version.h"
#elif __has_include("version.h")
#include "version.h"
#endif
#endif

#ifndef FROG_VERSION_SHORT
#define FROG_VERSION_STR "0.0.0"
#define FROG_BUILD_NUMBER 0
#define FROG_GIT_COMMITS 0
#define FROG_GIT_SHA "dev"
#define FROG_GIT_DIRTY 0
#define FROG_GIT_BRANCH "dev"
#define FROG_BUILD_DATE "unstamped"
#define FROG_VERSION_SHORT "dev build"
#define FROG_VERSION_FULL "Frog — unstamped developer build (run scripts/stamp-version.sh)"
#endif

using namespace iplug;
using namespace igraphics;

// One dim line with the exact build; tap shows the full stamp. A dirty tree
// tints the line amber so an uncommitted build is obvious.
class FrogVersionReadout : public IControl {
public:
	explicit FrogVersionReadout(const IRECT& bounds)
	    : IControl(bounds) {
		mIgnoreMouse = false;
		SetTooltip(FROG_VERSION_FULL);
	}

	void Draw(IGraphics& g) override {
#if FROG_GIT_DIRTY
		const IColor col(255, 235, 170, 60);
#else
		const IColor col(255, 140, 146, 156);
#endif
		const IText txt(14.f, col, "Roboto-Regular", EAlign::Center, EVAlign::Middle);
		g.DrawText(txt, FROG_VERSION_SHORT, mRECT, &mBlend);
	}

	void OnMouseDown(float x, float y, const IMouseMod& mod) override {
		GetUI()->ShowBubbleControl(this, mRECT.MW(), mRECT.T, FROG_VERSION_FULL,
		                           EDirection::Horizontal, IRECT(0.f, 0.f, 360.f, 28.f));
	}
};
