#pragma once

#include "IControl.h"
#include "Peaks.h"
#include "Style.h"
#include "engine/frog_types.h"
#include "engine/pattern.h"

#include <functional>
#include <string>

// The source waveform with the selection (green start / red end handles),
// the unit grid inside it, and the playhead sweeping the source a slot
// reads. Peaks come pre-reduced; the playhead is a dirty-rect update. Drop a
// file on it to load (desktop).
namespace frogui {

class WaveformControl : public IControl {
public:
	using OnRegion = std::function<void(double start, double end)>;   // fractions of the sample
	using DropFn = std::function<void(const std::string& path)>;

	WaveformControl(const IRECT& b)
	    : IControl(b) {
		mIgnoreMouse = false;
	}

	void SetPeaks(const Peaks* p) {
		mPeaks = p;
		SetDirty(false);
	}
	void SetPattern(const fg_pattern* p) { mPattern = p; }
	void SetOnRegion(OnRegion fn) { mOnRegion = std::move(fn); }
	void SetOnDrop(DropFn fn) { mOnDrop = std::move(fn); }
	void SetEmptyText(const char* s) { mEmptyText = s; }

	// playhead as a fraction of the whole sample (< 0 hides it), mapped into
	// the view's virtual window; redraws only on a visible change
	void SetPlayhead(double frac) {
		if (frac >= 0.0 && mPattern) {
			const double vs = mPattern->virtualStart, ve = mPattern->virtualEnd;
			frac = ve > vs ? (frac - vs) / (ve - vs) : frac;
			if (frac < 0.0 || frac > 1.0) {
				frac = -1.0;
			}
		}
		const float px = frac < 0.0 ? -1.f : std::round((float)frac * mRECT.W());
		if (px != mPlayheadPx) {
			mPlayheadPx = px;
			SetDirty(false);
		}
	}

	void OnMouseDown(float x, float y, const IMouseMod& mod) override {
		mDrag = Hit(x);
		mDownX = x;
		if (mDrag == Drag::None && mPattern && mPeaks && !mPeaks->Empty()) {
			// a tap inside the region moves the nearer handle? no: a tap does nothing
			// (audition arrives with the pad mode, F12). Keep the finger safe.
		}
	}

	void OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& mod) override {
		if (mDrag == Drag::None || !mPattern || !mOnRegion) {
			return;
		}
		const double f = std::max(0.0, std::min(1.0, (double)((x - mRECT.L) / std::max(1.f, mRECT.W()))));
		double s = mPattern->start, e = mPattern->end;
		if (mDrag == Drag::Start) {
			s = std::min(f, e - 0.001);
		} else {
			e = std::max(f, s + 0.001);
		}
		mOnRegion(s, e);
		SetDirty(false);
	}

	void OnMouseUp(float x, float y, const IMouseMod& mod) override {
		mDrag = Drag::None;
	}

	void OnDrop(const char* str) override {
		if (mOnDrop && str) {
			mOnDrop(str);
		}
	}

	void Draw(IGraphics& g) override {
		g.FillRect(kWell, mRECT);
		if (!mPeaks || mPeaks->Empty()) {
			g.DrawText(Text(14.f, kTextDim), mEmptyText.c_str(), mRECT);
			g.DrawDottedRect(kLine, mRECT.GetPadded(-4.f));
			return;
		}
		const IRECT r = mRECT;
		const double selS = mPattern ? mPattern->start : 0.0;
		const double selE = mPattern ? mPattern->end : 1.0;
		// selection shade
		const IRECT sel(r.L + (float)selS * r.W(), r.T, r.L + (float)selE * r.W(), r.B);
		g.FillRect(kPanel, sel);
		// peaks: both channels overlaid, dim outside the selection
		const int W = (int)r.W();
		const float mid = r.MH();
		const float half = r.H() * 0.5f - 2.f;
		const double vs = mPattern ? mPattern->virtualStart : 0.0;
		const double vspan = mPattern ? (mPattern->virtualEnd - vs) : 1.0;
		for (int px = 0; px < W; px++) {
			const double f0 = (double)px / W, f1 = (double)(px + 1) / W;
			const bool inside = f1 > selS && f0 < selE;
			for (int c = 0; c < mPeaks->nCh; c++) {
				float lo, hi;
				mPeaks->Range(c, vs + f0 * vspan, vs + f1 * vspan, lo, hi);
				const float y0 = mid - hi * half, y1 = mid - lo * half;
				const IColor col = inside ? (c == 0 ? kWave : kWaveDim) : kWaveDim;
				g.DrawVerticalLine(col, r.L + px + 0.5f, std::min(y0, y1), std::max(y0, y1) + 1.f, c == 0 ? 0 : &BLEND_50, 1.f);
			}
		}
		// unit grid
		if (mPattern) {
			const int U = fg_unit_count(mPattern);
			for (int u = 1; u < U; u++) {
				const float x = sel.L + sel.W() * (float)u / U;
				g.DrawVerticalLine(kLine, x, r.T, r.B, &BLEND_50, 1.f);
			}
		}
		// handles
		DrawHandle(g, sel.L, kSelStart, true);
		DrawHandle(g, sel.R, kSelEnd, false);
		// playhead
		if (mPlayheadPx >= 0.f) {
			g.DrawVerticalLine(kPlayhead, r.L + mPlayheadPx, r.T, r.B, 0, 2.f);
		}
	}

private:
	enum class Drag { None, Start, End };

	Drag Hit(float x) const {
		if (!mPattern) {
			return Drag::None;
		}
		const float tol = std::max(14.f, mRECT.W() * 0.012f);
		const float xs = mRECT.L + (float)mPattern->start * mRECT.W();
		const float xe = mRECT.L + (float)mPattern->end * mRECT.W();
		const float ds = std::fabs(x - xs), de = std::fabs(x - xe);
		if (ds <= tol && ds <= de) {
			return Drag::Start;
		}
		if (de <= tol) {
			return Drag::End;
		}
		return Drag::None;
	}

	void DrawHandle(IGraphics& g, float x, const IColor& col, bool start) {
		g.DrawVerticalLine(col, x, mRECT.T, mRECT.B, 0, 3.f);
		const float w = 10.f, h = 14.f;
		if (start) {
			g.FillTriangle(col, x, mRECT.T, x + w, mRECT.T, x, mRECT.T + h);
			g.FillTriangle(col, x, mRECT.B, x + w, mRECT.B, x, mRECT.B - h);
		} else {
			g.FillTriangle(col, x, mRECT.T, x - w, mRECT.T, x, mRECT.T + h);
			g.FillTriangle(col, x, mRECT.B, x - w, mRECT.B, x, mRECT.B - h);
		}
	}

	const Peaks* mPeaks = nullptr;
	const fg_pattern* mPattern = nullptr;
	OnRegion mOnRegion;
	DropFn mOnDrop;
	std::string mEmptyText = "Load a sample";
	Drag mDrag = Drag::None;
	float mDownX = 0.f;
	float mPlayheadPx = -1.f;
};

}  // namespace frogui
