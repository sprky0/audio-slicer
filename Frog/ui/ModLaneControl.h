#pragma once

#include "IControl.h"
#include "Style.h"
#include "engine/frog_types.h"
#include "engine/pattern.h"

#include <cmath>
#include <functional>
#include <string>

// The modifier lane under the tile row: one cell per grid step, pinned to the
// grid (tiles reorder underneath). Tap an empty cell to place a Mute
// modifier, tap a chip to select it (the toolbar becomes its settings),
// drag a chip to another step. A ratchet longer than one step draws a brace
// to the end of its span. Chips flash at their audible moment; a ratchet's
// chip stays lit for its whole span, and the chips its span swallowed grey
// out while it sounds.
namespace frogui {

class ModLaneControl : public IControl {
public:
	using Publish = std::function<void()>;
	using OnSelect = std::function<void(int modIndex)>;

	ModLaneControl(const IRECT& b)
	    : IControl(b) {
		mIgnoreMouse = false;
	}

	void Bind(fg_pattern* pattern, Publish publish, OnSelect onSelect) {
		mPattern = pattern;
		mPublish = std::move(publish);
		mOnSelect = std::move(onSelect);
	}

	int Selected() const { return mSelected; }
	void Select(int i) {
		if (mSelected != i) {
			mSelected = i;
			SetDirty(false);
			if (mOnSelect) {
				mOnSelect(i);
			}
		}
	}
	void Refresh() { SetDirty(false); }

	// Live state from the view's idle tick: `flashStep` lit briefly (−1 none),
	// `firingStep` the ratchet sounding now with its span, `supersededFrom/To`
	// the steps inside the span past the covered tile. Redraws on change.
	void SetLive(int flashStep, int firingStep, int supersededFrom, int supersededTo) {
		if (flashStep != mFlashStep || firingStep != mFiringStep || supersededFrom != mSupFrom || supersededTo != mSupTo) {
			mFlashStep = flashStep;
			mFiringStep = firingStep;
			mSupFrom = supersededFrom;
			mSupTo = supersededTo;
			SetDirty(false);
		}
	}

	void OnMouseDown(float x, float y, const IMouseMod& mod) override {
		if (!mPattern) {
			return;
		}
		const int step = StepAt(x);
		mDownX = x;
		mMoved = false;
		mDragMod = ModAt(step);
		if (mDragMod >= 0) {
			Select(mDragMod);
		}
	}

	void OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& mod) override {
		if (!mPattern || mDragMod < 0) {
			return;
		}
		if (std::fabs(x - mDownX) > 5.f) {
			mMoved = true;
		}
		if (!mMoved) {
			return;
		}
		const int step = StepAt(x);
		if (step < 0 || step == mPattern->mods[mDragMod].step || ModAt(step) >= 0) {
			return;   /* occupied steps are skipped */
		}
		mPattern->mods[mDragMod].step = (int16_t)step;
		mPublish();
		SetDirty(false);
	}

	void OnMouseUp(float x, float y, const IMouseMod& mod) override {
		if (!mPattern) {
			return;
		}
		if (mDragMod < 0 && !mMoved) {
			// a tap on an empty cell places a Mute modifier
			const int step = StepAt(x);
			if (step >= 0 && ModAt(step) < 0 && mPattern->nMods < FG_MAX_MODS) {
				fg_mod m = {0};
				m.step = (int16_t)step;
				m.action = FG_MOD_MUTE;
				m.fireMode = FG_FIRE_PROB;
				m.fireValue = 100;
				m.gainAmt = 50;
				m.subdiv = 2;
				m.subdivTo = 4;
				m.pitchStep = 0;
				m.lenSteps = 1;
				m.pitchAmt = 12;
				mPattern->mods[mPattern->nMods++] = m;
				mPublish();
				Select(mPattern->nMods - 1);
			} else if (step >= 0 && ModAt(step) < 0) {
				Select(-1);
			}
		}
		mDragMod = -1;
		SetDirty(false);
	}

	void Draw(IGraphics& g) override {
		g.FillRect(kWell, mRECT);
		if (!mPattern) {
			return;
		}
		const int U = fg_unit_count(mPattern);
		const float cw = mRECT.W() / U;
		for (int s = 1; s < U; s++) {
			g.DrawVerticalLine(kLine, mRECT.L + s * cw, mRECT.T, mRECT.B, &BLEND_50, 1.f);
		}
		// braces first, chips on top
		for (int i = 0; i < mPattern->nMods; i++) {
			const fg_mod& m = mPattern->mods[i];
			if (m.action == FG_MOD_RATCHET && m.lenSteps > 1) {
				const int end = std::min(U, m.step + m.lenSteps);
				const float x0 = mRECT.L + (m.step + 1) * cw, x1 = mRECT.L + end * cw;
				const bool firing = m.step == mFiringStep;
				const IColor col = firing ? kText : kTextDim;
				g.DrawHorizontalLine(col, mRECT.B - 3.f, x0, x1, 0, 2.f);
				g.DrawVerticalLine(col, x1 - 1.f, mRECT.T + 4.f, mRECT.B - 3.f, 0, 2.f);
			}
		}
		for (int i = 0; i < mPattern->nMods; i++) {
			const fg_mod& m = mPattern->mods[i];
			const IRECT cell(mRECT.L + m.step * cw, mRECT.T, mRECT.L + (m.step + 1) * cw, mRECT.B);
			const IRECT chip = cell.GetPadded(-2.f);
			const bool sel = i == mSelected;
			const bool flash = m.step == mFlashStep;
			const bool firing = m.step == mFiringStep;
			const bool superseded = mSupFrom >= 0 && m.step >= mSupFrom && m.step < mSupTo;
			IColor fill = ChipColor(m);
			if (superseded) {
				g.DrawDottedRect(kTextDim, chip);
				fill = kLabelChip;
			}
			g.FillRoundRect(fill, chip, 3.f, superseded ? &BLEND_50 : 0);
			if (flash || firing) {
				g.FillRoundRect(kText, chip, 3.f, &BLEND_50);
			}
			if (sel) {
				g.DrawRoundRect(kText, chip, 3.f, 0, 2.f);
			}
			const float fs = std::max(10.f, std::min(14.f, chip.H() * 0.5f));
			g.DrawText(Text(fs, kTextOnFill), Glyph(m), chip);
		}
	}

	static const char* ActionName(int action) {
		switch (action) {
			case FG_MOD_MUTE:
				return "Mute";
			case FG_MOD_REV:
				return "Rev";
			case FG_MOD_GAIN:
				return "Gain";
			case FG_MOD_RAND:
				return "Rand";
			case FG_MOD_RESET:
				return "Reset";
			case FG_MOD_RATCHET:
				return "Ratchet";
			case FG_MOD_PITCH:
				return "Pitch";
			default:
				return "?";
		}
	}

private:
	int StepAt(float x) const {
		const int U = fg_unit_count(mPattern);
		const int s = (int)((x - mRECT.L) / mRECT.W() * U);
		return s < 0 || s >= U ? -1 : s;
	}
	int ModAt(int step) const {
		for (int i = 0; i < mPattern->nMods; i++) {
			if (mPattern->mods[i].step == step) {
				return i;
			}
		}
		return -1;
	}
	static const char* Glyph(const fg_mod& m) {
		switch (m.action) {
			case FG_MOD_MUTE:
				return "M";
			case FG_MOD_REV:
				return "<";
			case FG_MOD_GAIN:
				return "G";
			case FG_MOD_RAND:
				return "?";
			case FG_MOD_RESET:
				return "R";
			case FG_MOD_RATCHET:
				return "=";
			case FG_MOD_PITCH:
				return "P";
			default:
				return "";
		}
	}
	static IColor ChipColor(const fg_mod& m) {
		switch (m.action) {
			case FG_MOD_MUTE:
				return kStop;
			case FG_MOD_RAND:
			case FG_MOD_RESET:
				return kLabelText;
			case FG_MOD_RATCHET:
				return kGo;
			default:
				return kNeutral;
		}
	}

	fg_pattern* mPattern = nullptr;
	Publish mPublish;
	OnSelect mOnSelect;
	int mSelected = -1;
	int mDragMod = -1;
	float mDownX = 0.f;
	bool mMoved = false;
	int mFlashStep = -1, mFiringStep = -1, mSupFrom = -1, mSupTo = -1;
};

}  // namespace frogui
