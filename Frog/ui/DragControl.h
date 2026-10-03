#pragma once

#include "IControl.h"
#include "Style.h"

#include <cmath>
#include <functional>
#include <string>
#include <vector>

// The one control shape, ported from the browser version: a button-sized cell
// with a fill from the origin (0, or the detent) to the value, a leading-edge
// line, and a centred "Label value" readout. Drag in any direction (right or
// up increases); a tap jumps a value control to the tapped position, flips a
// toggle, fires a button, steps an enum; double-tap resets to the detent;
// the wheel nudges one step. Touch-native: one finger, no hover needed.
//
// Four modes:
//   Value   continuous or stepped number in [min, max]; onChange(value)
//   Enum    one of N labels; onChange(index)
//   Toggle  on / off; onChange(0 | 1)
//   Button  momentary; onChange(1) on release inside
// A param-linked control (paramIdx != kNoParameter) reads and writes the
// plugin parameter instead of a local value; the mode follows the parameter.
namespace frogui {

class DragControl : public IControl {
public:
	enum class Mode { Value, Enum, Toggle, Button };
	using Formatter = std::function<std::string(double)>;
	using OnChange = std::function<void(double)>;

	// local value control
	DragControl(const IRECT& b, const char* label, Mode mode, double min, double max, double step, double detent, Intent intent = Intent::Neutral)
	    : IControl(b), mLabel(label), mMode(mode), mMin(min), mMax(max), mStep(step), mDetent(detent), mHasDetent(!std::isnan(detent)), mIntent(intent) {
		mValue = mHasDetent ? detent : min;
		mIgnoreMouse = false;
	}
	// param-linked
	DragControl(const IRECT& b, const char* label, int paramIdx, Intent intent = Intent::Neutral)
	    : IControl(b, paramIdx), mLabel(label), mMode(Mode::Value), mIntent(intent) {
		mIgnoreMouse = false;
	}

	static DragControl* Toggle(const IRECT& b, const char* label, bool on, Intent intent, OnChange fn) {
		auto* c = new DragControl(b, label, Mode::Toggle, 0., 1., 1., NAN, intent);
		c->mValue = on ? 1. : 0.;
		c->mOnChange = std::move(fn);
		return c;
	}
	static DragControl* Button(const IRECT& b, const char* label, Intent intent, OnChange fn) {
		auto* c = new DragControl(b, label, Mode::Button, 0., 1., 1., NAN, intent);
		c->mOnChange = std::move(fn);
		return c;
	}
	static DragControl* Enum(const IRECT& b, const char* label, std::vector<std::string> options, int idx, OnChange fn) {
		auto* c = new DragControl(b, label, Mode::Enum, 0., (double)options.size() - 1, 1., NAN, Intent::Neutral);
		c->mOptions = std::move(options);
		c->mValue = idx;
		c->mOnChange = std::move(fn);
		return c;
	}

	DragControl* WithFormat(Formatter f) {
		mFormat = std::move(f);
		return this;
	}
	DragControl* WithOnChange(OnChange f) {
		mOnChange = std::move(f);
		return this;
	}
	DragControl* WithEnabled(bool on) {
		SetDisabled(!on);
		return this;
	}

	void SetLocalValue(double v, bool notify = false) {
		mValue = Clamp(v);
		SetDirty(false);
		if (notify && mOnChange) {
			mOnChange(mValue);
		}
	}
	double LocalValue() const { return mValue; }
	const std::string& Label() const { return mLabel; }
	bool On() const { return mValue >= 0.5; }

	// --- interaction ---------------------------------------------------------

	void OnMouseDown(float x, float y, const IMouseMod& mod) override {
		mDownX = x;
		mDownY = y;
		mLastX = x;
		mLastY = y;
		mMoved = 0.f;
		mDragging = false;
		mPressed = true;
		mStartValue = Current();
		mAccum = 0.f;
		SetDirty(false);
	}

	void OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& mod) override {
		mMoved += std::fabs(dX) + std::fabs(dY);
		mLastX = x;
		mLastY = y;
		if (mMoved < 4.f) {
			return;
		}
		mDragging = true;
		if (mMode == Mode::Button || mMode == Mode::Toggle) {
			return;
		}
		mAccum += dX - dY;   // right or up increases
		if (IsStepped()) {
			const int steps = (int)std::trunc(mAccum / 20.f);
			Apply(Clamp(mStartValue + steps * StepSize()));
		} else {
			const double range = Max() - Min();
			double v = mStartValue + (mAccum / 150.f) * range;
			Apply(Snap(v));
		}
	}

	void OnMouseUp(float x, float y, const IMouseMod& mod) override {
		const bool inside = mRECT.Contains(x, y);
		mPressed = false;
		if (!mDragging) {
			// a tap
			switch (mMode) {
				case Mode::Button:
					if (inside && mOnChange) {
						mOnChange(1.);
					}
					break;
				case Mode::Toggle:
					if (inside) {
						Apply(mValue >= 0.5 ? 0. : 1.);
					}
					break;
				case Mode::Enum:
					if (inside) {
						const int n = (int)mOptions.size();
						const int dir = x < mRECT.MW() ? -1 : 1;
						Apply((double)(((int)Current() + dir + n) % n));
					}
					break;
				case Mode::Value: {
					if (IsStepped()) {
						const int dir = x < mRECT.MW() ? -1 : 1;
						Apply(Clamp(Current() + dir * StepSize()));
					} else {
						const double f = (x - mRECT.L) / std::max(1.f, mRECT.W());
						Apply(Snap(Min() + f * (Max() - Min())));
					}
					break;
				}
			}
		}
		mDragging = false;
		SetDirty(false);
	}

	void OnMouseDblClick(float x, float y, const IMouseMod& mod) override {
		if (mMode == Mode::Value && HasDetent()) {
			Apply(Detent());
		}
	}

	void OnMouseWheel(float x, float y, const IMouseMod& mod, float d) override {
		if (mMode == Mode::Button) {
			return;
		}
		const double step = IsStepped() ? StepSize() : (Max() - Min()) * 0.01;
		Apply(Clamp(Current() + (d > 0 ? step : -step)));
	}

	// --- drawing -----------------------------------------------------------

	void Draw(IGraphics& g) override {
		const IRECT r = mRECT.GetPadded(-1.f);
		const float radius = 4.f;
		const bool disabled = IsDisabled();
		const IColor fill = FillFor(mIntent);
		const IColor dim = DimFor(mIntent);
		g.FillRoundRect(kPanel, r, radius);
		std::string text;
		switch (mMode) {
			case Mode::Toggle: {
				const bool on = mValue >= 0.5;
				g.FillRoundRect(on ? fill : dim, r, radius, &BLEND_75);
				text = mLabel;
				break;
			}
			case Mode::Button: {
				g.FillRoundRect(mPressed ? fill : dim, r, radius, &BLEND_75);
				text = mFormat ? mFormat(0.) : mLabel;   /* a formatter makes it a live chip */
				break;
			}
			case Mode::Enum: {
				g.FillRoundRect(dim, r, radius, &BLEND_75);
				const int idx = (int)std::lround(Current());
				text = mLabel.empty() ? "" : mLabel + "  ";
				text += (idx >= 0 && idx < (int)mOptions.size()) ? mOptions[idx] : "";
				break;
			}
			case Mode::Value: {
				const double lo = Min(), hi = Max();
				const double v = Current();
				const double origin = HasDetent() ? Detent() : lo;
				const float fx0 = r.L + (float)((origin - lo) / (hi - lo)) * r.W();
				const float fx1 = r.L + (float)((v - lo) / (hi - lo)) * r.W();
				const IRECT fillR(std::min(fx0, fx1), r.T, std::max(fx0, fx1), r.B);
				g.FillRoundRect(dim, r, radius, &BLEND_75);
				if (fillR.W() > 0.5f) {
					g.FillRect(fill, fillR, &BLEND_75);
				}
				g.DrawVerticalLine(kTextOnFill, fx1, r.T + 2.f, r.B - 2.f, 0, 1.f);
				text = mLabel.empty() ? Format(v) : mLabel + "  " + Format(v);
				break;
			}
		}
		if (mPressed && !mDragging && mMode != Mode::Button) {
			g.DrawRoundRect(kText, r, radius, &BLEND_50);
		}
		const float size = std::max(11.f, std::min(16.f, r.H() * 0.36f));
		const IColor col = disabled ? kTextDim : (mIntent == Intent::Label ? kLabelText : kTextOnFill);
		g.DrawText(Text(size, col), text.c_str(), r, &mBlend);
		if (disabled) {
			g.FillRoundRect(kBg, r, radius, &BLEND_50);
		}
	}

private:
	bool Linked() const { return GetParamIdx() != kNoParameter; }
	const IParam* P() const { return GetParam(); }
	double Min() const { return Linked() ? P()->GetMin() : mMin; }
	double Max() const { return Linked() ? P()->GetMax() : mMax; }
	double StepSize() const { return Linked() ? std::max(P()->GetStep(), 1e-9) : std::max(mStep, 1e-9); }
	bool IsStepped() const {
		if (Linked()) {
			const auto t = P()->Type();
			return t == IParam::kTypeEnum || t == IParam::kTypeBool || t == IParam::kTypeInt;
		}
		return mMode == Mode::Enum || (mMode == Mode::Value && mStep > 0. && (Max() - Min()) / mStep <= 64.);
	}
	bool HasDetent() const { return Linked() ? true : mHasDetent; }
	double Detent() const { return Linked() ? P()->GetDefault() : mDetent; }
	double Current() const { return Linked() ? P()->FromNormalized(GetValue()) : mValue; }
	double Clamp(double v) const { return std::max(Min(), std::min(Max(), v)); }
	double Snap(double v) const {
		v = Clamp(v);
		if (!Linked() && mStep > 0.) {
			v = Min() + std::round((v - Min()) / mStep) * mStep;
		}
		if (HasDetent()) {
			const double range = Max() - Min();
			if (std::fabs(v - Detent()) <= 0.04 * range) {
				v = Detent();
			}
		}
		return Clamp(v);
	}
	void Apply(double v) {
		if (Linked()) {
			SetValueFromUserInput(P()->ToNormalized(v));
		} else {
			mValue = v;
			SetDirty(false);
		}
		if (mOnChange) {
			mOnChange(v);
		}
	}
	std::string Format(double v) const {
		if (mFormat) {
			return mFormat(v);
		}
		if (Linked()) {
			WDL_String s;
			P()->GetDisplay(s);
			std::string out = s.Get();
			const char* lbl = P()->GetLabel();
			if (lbl && lbl[0]) {
				out += " ";
				out += lbl;
			}
			return out;
		}
		char buf[32];
		if (IsStepped() || std::fabs(v - std::round(v)) < 1e-9) {
			snprintf(buf, sizeof buf, "%d", (int)std::lround(v));
		} else {
			snprintf(buf, sizeof buf, "%.2f", v);
		}
		return buf;
	}

	std::string mLabel;
	Mode mMode;
	double mMin = 0., mMax = 1., mStep = 0., mDetent = NAN;
	bool mHasDetent = false;
	Intent mIntent = Intent::Neutral;
	std::vector<std::string> mOptions;
	Formatter mFormat;
	OnChange mOnChange;
	double mValue = 0.;
	// drag state
	float mDownX = 0.f, mDownY = 0.f, mLastX = 0.f, mLastY = 0.f, mMoved = 0.f, mAccum = 0.f;
	double mStartValue = 0.;
	bool mDragging = false, mPressed = false;
};

}  // namespace frogui
