#pragma once

#include "IControl.h"
#include "Style.h"

#include <algorithm>
#include <dirent.h>
#include <functional>
#include <string>
#include <vector>

// A modal list of the sample files in one directory: the appliance has no OS
// file dialog, and a finger wants rows a control high. Drag to scroll, tap a
// row to load, tap outside (or the Close row) to dismiss. Desktop builds can
// also drop a file on the waveform.
namespace frogui {

class FileListControl : public IControl {
public:
	using OnPick = std::function<void(const std::string& path)>;
	using OnClose = std::function<void()>;

	// `exts` is a space-separated list of lower-case extensions (".wav .aif");
	// `title` heads the card.
	FileListControl(const IRECT& b, const std::string& dir, OnPick onPick, OnClose onClose, const char* exts = ".wav .wave .aif .aiff", const char* title = "Samples")
	    : IControl(b), mDir(dir), mExts(exts), mTitle(title), mOnPick(std::move(onPick)), mOnClose(std::move(onClose)) {
		mIgnoreMouse = false;
		Scan();
	}

	void Scan() {
		mFiles.clear();
		if (DIR* d = opendir(mDir.c_str())) {
			while (const dirent* e = readdir(d)) {
				std::string name = e->d_name;
				if (name.empty() || name[0] == '.') {
					continue;
				}
				const size_t dot = name.rfind('.');
				std::string ext = dot == std::string::npos ? "" : name.substr(dot);
				std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
				if (!ext.empty() && (" " + mExts + " ").find(" " + ext + " ") != std::string::npos) {
					mFiles.push_back(name);
				}
			}
			closedir(d);
		}
		std::sort(mFiles.begin(), mFiles.end());
		SetDirty(false);
	}

	void OnMouseDown(float x, float y, const IMouseMod& mod) override {
		mDownY = y;
		mMoved = 0.f;
		mScrollStart = mScroll;
	}
	void OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& mod) override {
		mMoved += std::fabs(dY);
		if (mMoved > 4.f) {
			const float maxScroll = std::max(0.f, (float)mFiles.size() * RowH() - ListRect().H());
			mScroll = std::max(0.f, std::min(maxScroll, mScrollStart - (y - mDownY)));
			SetDirty(false);
		}
	}
	void OnMouseUp(float x, float y, const IMouseMod& mod) override {
		if (mMoved > 4.f) {
			return;
		}
		const IRECT card = Card();
		if (!card.Contains(x, y) || CloseRect().Contains(x, y)) {
			if (mOnClose) {
				mOnClose();
			}
			return;
		}
		const IRECT list = ListRect();
		if (list.Contains(x, y)) {
			const int row = (int)((y - list.T + mScroll) / RowH());
			if (row >= 0 && row < (int)mFiles.size() && mOnPick) {
				mOnPick(mDir + "/" + mFiles[(size_t)row]);
			}
		}
	}

	void Draw(IGraphics& g) override {
		g.FillRect(kBg, mRECT, &BLEND_75);
		const IRECT card = Card();
		g.FillRoundRect(kPanel, card, 6.f);
		g.DrawRoundRect(kLine, card, 6.f);
		const IRECT title = card.GetFromTop(RowH());
		g.DrawText(Text(15.f, kLabelText, EAlign::Near), (mTitle + "  " + mDir).c_str(), title.GetPadded(-12.f, 0.f, -12.f, 0.f));
		const IRECT list = ListRect();
		if (mFiles.empty()) {
			g.DrawText(Text(14.f, kTextDim), ("Nothing here yet (" + mExts + ")").c_str(), list);
		} else {
			g.PathClipRegion(list);
			for (size_t i = 0; i < mFiles.size(); i++) {
				const float y = list.T - mScroll + i * RowH();
				if (y + RowH() < list.T || y > list.B) {
					continue;
				}
				const IRECT row(list.L, y, list.R, y + RowH());
				g.FillRoundRect(i % 2 ? kBg : kPanel, row.GetPadded(-2.f), 4.f);
				g.DrawText(Text(15.f, kText, EAlign::Near), mFiles[i].c_str(), row.GetPadded(-12.f, 0.f, -12.f, 0.f));
			}
			g.PathClipRegion(IRECT());
		}
		const IRECT close = CloseRect();
		g.FillRoundRect(kStopDim, close.GetPadded(-2.f), 4.f);
		g.DrawText(Text(15.f, kTextOnFill), "Close", close);
	}

private:
	float RowH() const { return std::max(40.f, mRECT.H() / 12.f); }
	IRECT Card() const { return mRECT.GetPadded(-mRECT.W() * 0.12f, -mRECT.H() * 0.08f, -mRECT.W() * 0.12f, -mRECT.H() * 0.08f); }
	IRECT ListRect() const { return Card().GetReducedFromTop(RowH()).GetReducedFromBottom(RowH()).GetPadded(-8.f, 0.f, -8.f, 0.f); }
	IRECT CloseRect() const { return Card().GetFromBottom(RowH()).GetPadded(-8.f, 0.f, -8.f, 0.f); }

	std::string mDir;
	std::string mExts;
	std::string mTitle;
	std::vector<std::string> mFiles;
	OnPick mOnPick;
	OnClose mOnClose;
	float mScroll = 0.f, mScrollStart = 0.f, mDownY = 0.f, mMoved = 0.f;
};

}  // namespace frogui
