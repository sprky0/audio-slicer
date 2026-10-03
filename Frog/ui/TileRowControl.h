#pragma once

#include "IControl.h"
#include "Peaks.h"
#include "Style.h"
#include "engine/edit.h"
#include "engine/frog_types.h"
#include "engine/pattern.h"

#include <cmath>
#include <functional>
#include <string>

// The loop as a row of variable-width tiles over the grid, each showing the
// slice of source it plays (mirrored when reversed) in its colour, with
// badges for mute / lock / reverse / gain / pitch. One finger: tap selects;
// drag a tile to reorder (packed: live reflow; gaps: a ghost, dropped on
// release); drag the edge of the selected tile to resize (the boundary eats
// what it crosses); double-tap splits. Every edit goes through edit.h on the
// working pattern, then `publish` hands it to the audio thread.
namespace frogui {

class TileRowControl : public IControl {
public:
	using Publish = std::function<void()>;
	using OnSelect = std::function<void(int)>;

	TileRowControl(const IRECT& b)
	    : IControl(b) {
		mIgnoreMouse = false;
	}

	void Bind(fg_pattern* pattern, fg_edit_mode* mode, int* nextColor, Publish publish, OnSelect onSelect) {
		mPattern = pattern;
		mMode = mode;
		mNextColor = nextColor;
		mPublish = std::move(publish);
		mOnSelect = std::move(onSelect);
	}
	void SetPeaks(const Peaks* p) {
		mPeaks = p;
		SetDirty(false);
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
	// the sounding tile (−1 none); redraws only on change
	void SetPlaying(int tileIndex) {
		if (tileIndex != mPlaying) {
			mPlaying = tileIndex;
			SetDirty(false);
		}
	}
	void Refresh() { SetDirty(false); }

	// --- interaction -----------------------------------------------------------

	void OnMouseDown(float x, float y, const IMouseMod& mod) override {
		if (!mPattern || mPattern->nTiles == 0) {
			return;
		}
		const int i = TileAt(x);
		if (i < 0) {
			return;
		}
		mDownX = x;
		mDrag = Drag::None;
		mGrabUnits = (x - mRECT.L) / PxPerUnit() - fg_edit_entry_start(mPattern, i);
		if (i == mSelected) {
			IRECT tr = TileRect(i);
			const float edge = std::max(12.f, std::min(24.f, tr.W() * 0.25f));
			if (x - tr.L <= edge && (i > 0 || *mMode == FG_EDIT_GAPS) && !mPattern->tiles[i].gap) {
				mDrag = Drag::ResizeStart;
			} else if (tr.R - x <= edge && (i < mPattern->nTiles - 1 || *mMode == FG_EDIT_GAPS) && !mPattern->tiles[i].gap) {
				mDrag = Drag::ResizeEnd;
			}
			if (mDrag != Drag::None) {
				mSnap = *mPattern;
				mDragIndex = i;
				return;
			}
		}
		Select(i);
		mDragIndex = i;
		mDrag = Drag::MaybeMove;
	}

	void OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& mod) override {
		if (!mPattern || mDragIndex < 0) {
			return;
		}
		const double dU = (x - mDownX) / PxPerUnit();
		switch (mDrag) {
			case Drag::ResizeStart:
			case Drag::ResizeEnd: {
				const bool end = mDrag == Drag::ResizeEnd;
				if (*mMode == FG_EDIT_GAPS) {
					fg_edit_resize_gaps(&mSnap, mDragIndex, end, dU, mPattern);
				} else if (end) {
					fg_edit_resize_end(&mSnap, mDragIndex, dU, mPattern);
				} else {
					fg_edit_resize_start(&mSnap, mDragIndex, dU, mPattern);
				}
				// keep the grown / shrunk tile selected: the one reading the snapshot
				// tile's source head (end drag) or sharing its colour (start drag)
				mSelected = FindLike(mSnap.tiles[mDragIndex], end);
				mPublish();
				SetDirty(false);
				break;
			}
			case Drag::MaybeMove:
				if (std::fabs(x - mDownX) < 6.f) {
					return;
				}
				mDrag = Drag::Move;
				/* fallthrough */
			case Drag::Move: {
				if (*mMode == FG_EDIT_GAPS) {
					mGhostUnit = (x - mRECT.L) / PxPerUnit() - mGrabUnits;
					SetDirty(false);
					return;
				}
				// packed: live reflow by midpoints, the dragged width discounted
				const int cur = mSelected;
				if (cur < 0) {
					return;
				}
				const float ppu = PxPerUnit();
				const float dw = (float)mPattern->tiles[cur].w * ppu;
				int insert = 0;
				float pos = mRECT.L;
				for (int k = 0; k < mPattern->nTiles; k++) {
					const float w = (float)mPattern->tiles[k].w * ppu;
					if (k != cur) {
						const float midRaw = pos + w * 0.5f;
						const float mid = k > cur ? midRaw - dw : midRaw;
						if (x > mid) {
							insert++;
						}
					}
					pos += w;
				}
				if (insert != cur) {
					fg_edit_move(mPattern, cur, insert);
					mSelected = insert;
					mPublish();
					SetDirty(false);
				}
				break;
			}
			default:
				break;
		}
	}

	void OnMouseUp(float x, float y, const IMouseMod& mod) override {
		if (mPattern && mDrag == Drag::Move && *mMode == FG_EDIT_GAPS && mSelected >= 0) {
			const fg_tile moved = mPattern->tiles[mSelected];
			if (fg_edit_move_gaps(mPattern, mSelected, mGhostUnit)) {
				mSelected = FindLike(moved, true);
				mPublish();
			}
		}
		mDrag = Drag::None;
		mDragIndex = -1;
		mGhostUnit = -1.0;
		SetDirty(false);
	}

	void OnMouseDblClick(float x, float y, const IMouseMod& mod) override {
		if (!mPattern) {
			return;
		}
		const int i = TileAt(x);
		if (i >= 0 && fg_edit_split(mPattern, i, mNextColor ? (*mNextColor)++ : 0)) {
			mSelected = i;
			mPublish();
			SetDirty(false);
		}
	}

	// --- drawing ---------------------------------------------------------------

	void Draw(IGraphics& g) override {
		g.FillRect(kWell, mRECT);
		if (!mPattern || mPattern->nTiles == 0) {
			g.DrawText(Text(14.f, kTextDim), "No slices yet", mRECT);
			return;
		}
		const float ppu = PxPerUnit();
		float x = mRECT.L;
		for (int i = 0; i < mPattern->nTiles; i++) {
			const fg_tile& t = mPattern->tiles[i];
			const float w = (float)t.w * ppu;
			const IRECT r(x, mRECT.T, x + w, mRECT.B);
			DrawTile(g, r.GetPadded(-1.f), t, i);
			x += w;
		}
		if (mDrag == Drag::Move && *mMode == FG_EDIT_GAPS && mSelected >= 0 && mGhostUnit >= 0.0) {
			const float gx = mRECT.L + (float)mGhostUnit * ppu;
			const float gw = (float)mPattern->tiles[mSelected].w * ppu;
			g.DrawRect(kText, IRECT(gx, mRECT.T + 1.f, gx + gw, mRECT.B - 1.f), 0, 2.f);
		}
	}

private:
	enum class Drag { None, MaybeMove, Move, ResizeStart, ResizeEnd };

	float PxPerUnit() const {
		const int U = mPattern ? fg_unit_count(mPattern) : 16;
		return mRECT.W() / (float)U;
	}

	int TileAt(float x) const {
		const float ppu = PxPerUnit();
		float pos = mRECT.L;
		for (int i = 0; i < mPattern->nTiles; i++) {
			const float w = (float)mPattern->tiles[i].w * ppu;
			if (x >= pos && x < pos + w) {
				return i;
			}
			pos += w;
		}
		return x >= pos ? mPattern->nTiles - 1 : -1;
	}

	IRECT TileRect(int i) const {
		const float ppu = PxPerUnit();
		const float x = mRECT.L + (float)fg_edit_entry_start(mPattern, i) * ppu;
		return IRECT(x, mRECT.T, x + (float)mPattern->tiles[i].w * ppu, mRECT.B);
	}

	int FindLike(const fg_tile& like, bool bySrc) const {
		for (int k = 0; k < mPattern->nTiles; k++) {
			const fg_tile& t = mPattern->tiles[k];
			if (t.gap || like.gap) {
				continue;
			}
			if (t.colorIdx == like.colorIdx && (bySrc ? std::fabs(t.src - like.src) < 1e-9 : true)) {
				return k;
			}
		}
		for (int k = 0; k < mPattern->nTiles; k++) {
			if (!mPattern->tiles[k].gap && mPattern->tiles[k].colorIdx == like.colorIdx) {
				return k;
			}
		}
		return -1;
	}

	void DrawTile(IGraphics& g, const IRECT& r, const fg_tile& t, int i) {
		const bool sel = i == mSelected;
		const bool playing = i == mPlaying;
		if (t.gap) {
			g.FillRect(kBg, r);
			g.DrawDottedRect(kLine, r.GetPadded(-2.f));
			if (sel) {
				g.FillRect(kText, r, &BLEND_10);
			}
			return;
		}
		IColor col = SliceColor(t.colorIdx);
		g.FillRect(kPanel, r);
		if (playing) {
			g.FillRect(col, r, &BLEND_25);
		}
		if (sel) {
			g.FillRect(kText, r, &BLEND_10);
		}
		// the slice's own waveform: region [src, src + w) of the selection
		if (mPeaks && !mPeaks->Empty() && r.W() > 2.f) {
			const int U = fg_unit_count(mPattern);
			const double vspan = mPattern->virtualEnd - mPattern->virtualStart;
			const double span = (mPattern->end - mPattern->start) * vspan;
			const double selS = mPattern->virtualStart + mPattern->start * vspan;
			const double u0 = std::max(0.0, std::min((double)U, t.src));
			const double u1 = std::max(0.0, std::min((double)U, t.src + t.w));
			const double fA = selS + (u0 / U) * span, fB = selS + (u1 / U) * span;
			const int W = (int)r.W();
			const float mid = r.MH();
			const float half = (r.H() * 0.5f - 3.f) * std::min(2.f, t.gain > 0.f ? t.gain : 1.f);
			const IColor wave = sel ? kText : col;
			for (int px = 0; px < W; px++) {
				double q0 = (double)px / W, q1 = (double)(px + 1) / W;
				if (t.reversed) {
					const double a = 1.0 - q1, b = 1.0 - q0;
					q0 = a;
					q1 = b;
				}
				float lo, hi;
				mPeaks->Range(0, fA + q0 * (fB - fA), fA + q1 * (fB - fA), lo, hi);
				const float y0 = std::max(r.T + 1.f, mid - hi * half), y1 = std::min(r.B - 1.f, mid - lo * half);
				g.DrawVerticalLine(wave, r.L + px + 0.5f, std::min(y0, y1), std::max(y0, y1) + 1.f, t.muted ? &BLEND_25 : 0, 1.f);
			}
			// fade guides
			if (t.fadeIn > 0.f || t.fadeOut > 0.f) {
				const float top = r.T + 2.f, bot = r.B - 2.f;
				if (t.fadeIn > 0.f) {
					const float xf = r.L + r.W() * t.fadeIn;
					g.DrawLine(kText, r.L, bot, xf, top, &BLEND_50, 1.f);
				}
				if (t.fadeOut > 0.f) {
					const float xf = r.R - r.W() * t.fadeOut;
					g.DrawLine(kText, xf, top, r.R, bot, &BLEND_50, 1.f);
				}
			}
		}
		if (t.muted) {
			g.FillRect(kBg, r, &BLEND_50);
		}
		if (t.locked) {
			g.DrawRect(kLock, r.GetPadded(-1.f), 0, 2.f);
		}
		// badges: source unit + width, flags
		const float fs = std::max(10.f, std::min(13.f, r.H() * 0.2f));
		char lbl[32];
		if (std::fabs(t.w - std::round(t.w)) < 1e-9) {
			snprintf(lbl, sizeof lbl, "%d", (int)std::lround(t.src) + 1);
		} else {
			snprintf(lbl, sizeof lbl, "%.2g", t.src + 1.0);
		}
		if (r.W() >= 18.f) {
			g.DrawText(Text(fs, kTextOnFill, EAlign::Near, EVAlign::Top), lbl, r.GetPadded(-3.f));
		}
		std::string flags;
		if (t.reversed) {
			flags += "<";
		}
		if (t.offset != 0) {
			char b[8];
			snprintf(b, sizeof b, "%+d", t.offset);
			flags += b;
		}
		if (std::fabs(t.gain - 1.f) > 1e-3f) {
			char b[8];
			snprintf(b, sizeof b, "%d%%", (int)std::lround(t.gain * 100.f));
			flags += (flags.empty() ? "" : " ") + std::string(b);
		}
		if (!flags.empty() && r.W() >= 30.f) {
			g.DrawText(Text(fs, kTextOnFill, EAlign::Far, EVAlign::Bottom), flags.c_str(), r.GetPadded(-3.f));
		}
		if (sel) {
			// resize handles on the selected clip
			const float hw = 4.f;
			if (i > 0 || *mMode == FG_EDIT_GAPS) {
				g.FillRect(kSelStart, IRECT(r.L, r.T, r.L + hw, r.B));
			}
			if (i < mPattern->nTiles - 1 || *mMode == FG_EDIT_GAPS) {
				g.FillRect(kSelEnd, IRECT(r.R - hw, r.T, r.R, r.B));
			}
		}
	}

	fg_pattern* mPattern = nullptr;
	fg_edit_mode* mMode = nullptr;
	int* mNextColor = nullptr;
	Publish mPublish;
	OnSelect mOnSelect;
	const Peaks* mPeaks = nullptr;
	int mSelected = -1;
	int mPlaying = -1;
	Drag mDrag = Drag::None;
	int mDragIndex = -1;
	float mDownX = 0.f;
	double mGrabUnits = 0.0;
	double mGhostUnit = -1.0;
	fg_pattern mSnap;
};

}  // namespace frogui
