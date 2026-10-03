#pragma once

#include "IControl.h"
#include "Style.h"

#include <functional>
#include <string>

// The track strip: one tab per track (name, lit while it sounds, dimmed when
// muted; the focused one framed) plus Add, Dup and Remove. One row, so any
// track count costs the same height: the focused track owns the editor
// bands below, a tap on another tab brings it there.
namespace frogui {

class TrackStripControl : public IControl {
public:
	struct Info {
		std::string name;
		bool playing = false;
		bool muted = false;
		bool hasSample = false;
	};
	using InfoFn = std::function<Info(int track)>;
	using IntFn = std::function<void(int)>;
	using VoidFn = std::function<void()>;

	TrackStripControl(const IRECT& b)
	    : IControl(b) {
		mIgnoreMouse = false;
	}

	void Bind(int* numTracks, int* focus, InfoFn info, IntFn onFocus, VoidFn onAdd, VoidFn onDup, VoidFn onRemove) {
		mNum = numTracks;
		mFocus = focus;
		mInfo = std::move(info);
		mOnFocus = std::move(onFocus);
		mOnAdd = std::move(onAdd);
		mOnDup = std::move(onDup);
		mOnRemove = std::move(onRemove);
	}

	// per idle tick: redraw only when a tab's live state changed
	void Refresh() {
		unsigned key = 0;
		for (int t = 0; t < *mNum; t++) {
			const Info i = mInfo(t);
			key = key * 8u + (i.playing ? 1u : 0u) + (i.muted ? 2u : 0u) + (i.hasSample ? 4u : 0u);
		}
		key = key * 16u + (unsigned)*mFocus;
		if (key != mKey) {
			mKey = key;
			SetDirty(false);
		}
	}

	void OnMouseUp(float x, float y, const IMouseMod& mod) override {
		const int n = *mNum;
		const float bw = ButtonW();
		if (x >= mRECT.R - 3.f * bw) {
			const int which = (int)((x - (mRECT.R - 3.f * bw)) / bw);
			if (which == 0 && mOnAdd) {
				mOnAdd();
			} else if (which == 1 && mOnDup) {
				mOnDup();
			} else if (which == 2 && mOnRemove) {
				mOnRemove();
			}
			SetDirty(false);
			return;
		}
		const float tw = TabW();
		const int t = (int)((x - mRECT.L) / tw);
		if (t >= 0 && t < n && mOnFocus) {
			mOnFocus(t);
			SetDirty(false);
		}
	}

	void Draw(IGraphics& g) override {
		const int n = *mNum;
		const float tw = TabW();
		const float fs = std::max(11.f, std::min(15.f, mRECT.H() * 0.32f));
		for (int t = 0; t < n; t++) {
			const Info i = mInfo(t);
			const IRECT r(mRECT.L + t * tw, mRECT.T, mRECT.L + (t + 1) * tw, mRECT.B);
			const IRECT tab = r.GetPadded(-1.f);
			g.FillRoundRect(kPanel, tab, 4.f);
			if (i.playing) {
				g.FillRoundRect(SliceColor(t * 2), tab, 4.f, &BLEND_25);
			}
			if (i.muted) {
				g.FillRoundRect(kBg, tab, 4.f, &BLEND_50);
			}
			if (t == *mFocus) {
				g.DrawRoundRect(kText, tab, 4.f, 0, 2.f);
			}
			char num[8];
			snprintf(num, sizeof num, "%d", t + 1);
			g.DrawText(Text(fs, kLabelText, EAlign::Near), num, tab.GetPadded(-8.f, 0.f, 0.f, 0.f));
			const std::string name = i.name.empty() ? (i.hasSample ? "sample" : "empty") : i.name;
			g.DrawText(Text(fs, i.hasSample ? kText : kTextDim), name.c_str(), tab.GetPadded(-24.f, 0.f, -4.f, 0.f));
		}
		const float bw = ButtonW();
		const char* labels[3] = {"+ Track", "Dup", "- Track"};
		const IColor cols[3] = {kGoDim, kNeutralDim, kStopDim};
		for (int k = 0; k < 3; k++) {
			const IRECT r(mRECT.R - (3 - k) * bw, mRECT.T, mRECT.R - (2 - k) * bw, mRECT.B);
			const IRECT b = r.GetPadded(-1.f);
			g.FillRoundRect(cols[k], b, 4.f, &BLEND_75);
			g.DrawText(Text(fs, kTextOnFill), labels[k], b);
		}
	}

private:
	float ButtonW() const { return std::max(60.f, mRECT.W() * 0.09f); }
	float TabW() const {
		const int n = std::max(1, *mNum);
		return (mRECT.W() - 3.f * ButtonW()) / n;
	}

	int* mNum = nullptr;
	int* mFocus = nullptr;
	InfoFn mInfo;
	IntFn mOnFocus;
	VoidFn mOnAdd, mOnDup, mOnRemove;
	unsigned mKey = 0xFFFFFFFFu;
};

}  // namespace frogui
