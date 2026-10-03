#pragma once

#include "IControl.h"
#include "IControls.h"

#include "DragControl.h"
#include "FileListControl.h"
#include "FrogVersion.h"
#include "ModLaneControl.h"
#include "Peaks.h"
#include "Style.h"
#include "TileRowControl.h"
#include "WaveformControl.h"

#include "engine/edit.h"
#include "engine/engine.h"
#include "engine/pattern.h"

#include <cmath>
#include <cstring>
#include <functional>
#include <string>

// The MVP panel (F10): one focus track. Pinned bands (transport, track header,
// slice toolbar) and filling bands (waveform, tile row) laid out from one
// unit, re-laid on every resize and on the edit / perform switch. Every edit
// mutates the engine's working pattern and publishes it; playback feedback
// arrives through the visual records the engine posts.
//
// The view talks to the plugin through this interface so the UI headers do
// not depend on Frog.h (and the appliance editor module stays decoupled).
namespace frogui {

class Host {
public:
	virtual ~Host() = default;
	virtual fg_engine* Engine() = 0;
	virtual void Publish(int track) = 0;
	virtual const Peaks& TrackPeaks(int track) const = 0;
	virtual const char* TrackName(int track) const = 0;
	virtual void LoadSample(int track, const std::string& path) = 0;
	virtual std::string SamplesDir() const = 0;
	virtual void PlayAll() = 0;
	virtual void StopAll() = 0;
	virtual int& NextColor(int track) = 0;
	virtual fg_edit_mode& EditMode() = 0;
	// the latest sounding slot for the track, or nullptr
	virtual const fg_visual* CurrentNote(int track) const = 0;
	// recent modifier flashes (ring, oldest first is not guaranteed); count returned
	virtual int RecentFlashes(int track, const fg_flash** out) const = 0;
	// parameter indices the view binds to
	virtual int ParamBpm() const = 0;
	virtual int ParamClock() const = 0;
	virtual int ParamTrackVolume(int track) const = 0;
	virtual int ParamTrackPan(int track) const = 0;
	virtual int ParamTrackMute(int track) const = 0;
};

class FrogView {
public:
	FrogView(Host& host, IGraphics* g)
	    : mHost(host) {
		Build(g);
		Layout(g);
	}

	// Called on every resize (the layout function re-runs) and on mode changes.
	void Layout(IGraphics* g) {
		const IRECT b = g->GetBounds();
		const float u = Unit(b.W(), b.H());
		const float ch = kControlU * u, gap = kGapU * u;
		IRECT rest = b.GetPadded(-gap);

		const IRECT transport = rest.ReduceFromTop(ch);
		rest.ReduceFromTop(gap);
		const IRECT header = rest.ReduceFromTop(ch);
		rest.ReduceFromTop(gap);
		const IRECT toolbar2 = rest.GetFromBottom(ch);
		rest = rest.GetReducedFromBottom(ch + gap);
		const IRECT toolbar1 = rest.GetFromBottom(ch);
		rest = rest.GetReducedFromBottom(ch + gap);
		const IRECT modLane = rest.GetFromBottom(4.f * u);
		rest = rest.GetReducedFromBottom(4.f * u + gap);
		// waveform and tiles share the rest: edit favours the waveform, perform the tiles
		const float waveW = mPerform ? 1.f : 3.f, tileW = mPerform ? 4.f : 2.f;
		const float flex = rest.H() - gap;
		const float waveH = std::max(8.f * u, flex * waveW / (waveW + tileW));
		const IRECT wave = rest.ReduceFromTop(waveH);
		rest.ReduceFromTop(gap);
		const IRECT tiles = rest;

		Row(transport, gap, mTransport);
		Row(header, gap, mHeader);
		mWaveform->SetTargetAndDrawRECTs(wave);
		mTiles->SetTargetAndDrawRECTs(tiles);
		mModLane->SetTargetAndDrawRECTs(modLane);
		Row(toolbar1, gap, mToolbar1);
		Row(toolbar2, gap, mToolbar2);
		Row(toolbar1, gap, mModRow1);
		Row(toolbar2, gap, mModRow2);
		if (mFileList) {
			mFileList->SetTargetAndDrawRECTs(b);
		}
		g->SetAllControlsDirty();
	}

	// Per idle tick from the plugin (the UI thread): playback feedback.
	void Idle() {
		const fg_visual* v = mHost.CurrentNote(mTrack);
		const int64_t now = fg_engine_now(mHost.Engine());
		const double sr = 48000.0;   // flash timing only; a 180 ms window
		// modifier flashes: the newest within 180 ms of now
		const fg_flash* fl = nullptr;
		const int nf = mHost.RecentFlashes(mTrack, &fl);
		int flashStep = -1;
		int64_t best = -1;
		for (int k = 0; k < nf; k++) {
			if (fl[k].at <= now && now - fl[k].at < (int64_t)(0.18 * sr) && fl[k].at > best) {
				best = fl[k].at;
				flashStep = fl[k].step;
			}
		}
		if (!v || v->silent || now >= v->stop) {
			mTiles->SetPlaying(v && now < v->stop ? v->tileIndex : -1);
			mWaveform->SetPlayhead(-1.0);
			mModLane->SetLive(flashStep, -1, -1, -1);
			return;
		}
		mTiles->SetPlaying(v->tileIndex);
		const fg_pattern* p = Pat();
		const int U = fg_unit_count(p);
		double frac = (double)(now - v->start) / (double)std::max<int64_t>(1, v->stop - v->start);
		double regionFrac = 1.0;   // how much of the tile's region the current hit sweeps
		double wTile = v->w;
		if (v->ratchet && v->hits > 0) {
			// per-hit restart: locate the hit by its boundaries (ramp hits are not uniform)
			double steps[FG_MAX_HITS];
			const int hits = fg_ratchet_hit_steps(&v->rt, v->spanSteps, steps, FG_MAX_HITS);
			int h = 0;
			while (h + 1 < hits && frac >= steps[h + 1] / v->w) {
				h++;
			}
			const double b0 = steps[h] / v->w;
			const double b1 = h + 1 < hits ? steps[h + 1] / v->w : std::min(1.0, v->spanSteps / v->w);
			frac = b1 > b0 ? std::min(1.0, (frac - b0) / (b1 - b0)) : 0.0;
			wTile = v->wTile;
			regionFrac = std::min(1.0, (v->spanSteps / hits) / v->wTile);
			const int supFrom = (int)std::ceil(v->stepInBar + v->wTile - FG_EPS);
			const int supTo = (int)std::floor(v->stepInBar + v->spanSteps + FG_EPS);
			mModLane->SetLive(flashStep, v->rt.step, supFrom, supTo);
		} else {
			mModLane->SetLive(flashStep, -1, -1, -1);
		}
		double unitPos;
		if (v->tileIndex >= 0 && v->tileIndex < p->nTiles && p->tiles[v->tileIndex].reversed) {
			unitPos = v->src + (1.0 - frac * regionFrac) * wTile;
		} else {
			unitPos = v->src + frac * regionFrac * wTile;
		}
		const double vspan = p->virtualEnd - p->virtualStart;
		const double f = p->virtualStart + (p->start + (p->end - p->start) * (unitPos / U)) * vspan;
		mWaveform->SetPlayhead(f);
	}

	// A sample finished loading (main thread): point the controls at its peaks.
	void SampleChanged(int track) {
		if (track != mTrack) {
			return;
		}
		const Peaks& pk = mHost.TrackPeaks(track);
		mWaveform->SetPeaks(pk.Empty() ? nullptr : &pk);
		mTiles->SetPeaks(pk.Empty() ? nullptr : &pk);
		mFileChip->SetLocalValue(0.);
		mTiles->Refresh();
		RefreshToolbar();
	}

	// The working pattern changed under us (a pattern action on the audio thread).
	void PatternChanged(int track) {
		if (track == mTrack) {
			mTiles->Refresh();
			mModLane->Refresh();
			RefreshToolbar();
		}
	}

private:
	fg_pattern* Pat() { return fg_engine_pattern(mHost.Engine(), mTrack); }
	void Publish() { mHost.Publish(mTrack); }

	// Lay a row of controls out as equal columns with a weighted flex item.
	void Row(const IRECT& r, float gap, std::vector<std::pair<IControl*, float>>& items) {
		float total = 0.f;
		for (auto& it : items) {
			total += it.second;
		}
		const float w = (r.W() - gap * (items.size() - 1)) / std::max(1.f, total);
		float x = r.L;
		for (auto& it : items) {
			const float cw = w * it.second;
			it.first->SetTargetAndDrawRECTs(IRECT(x, r.T, x + cw, r.B));
			x += cw + gap;
		}
	}

	void Build(IGraphics* g) {
		const IRECT z(0, 0, 10, 10);
		auto add = [&](IControl* c, std::vector<std::pair<IControl*, float>>& row, float weight) {
			g->AttachControl(c);
			row.push_back({c, weight});
			return c;
		};
		auto drag = [](IControl* c) { return static_cast<DragControl*>(c); };

		// --- transport -----------------------------------------------------
		add(new DragControl(z, "FROG", DragControl::Mode::Button, 0, 1, 1, NAN, Intent::Label), mTransport, 1.f);
		add(DragControl::Button(z, "Play", Intent::Go, [this](double) { mHost.PlayAll(); }), mTransport, 1.f);
		add(DragControl::Button(z, "Stop", Intent::Stop, [this](double) { mHost.StopAll(); }), mTransport, 1.f);
		add(new DragControl(z, "BPM", mHost.ParamBpm()), mTransport, 2.f);
		add(new DragControl(z, "Clock", mHost.ParamClock()), mTransport, 2.f);
		mPerformBtn = drag(add(DragControl::Toggle(z, "Perform", mPerform, Intent::Neutral, [this, g](double v) {
			mPerform = v >= 0.5;
			Layout(g);
		}), mTransport, 1.f));
		g->AttachControl(mVersion = new FrogVersionReadout(z));
		mTransport.push_back({mVersion, 2.f});

		// --- track header --------------------------------------------------
		add(DragControl::Button(z, "Load", Intent::Neutral, [this, g](double) { OpenFileList(g); }), mHeader, 1.f);
		mFileChip = drag(add(new DragControl(z, "", DragControl::Mode::Button, 0, 1, 1, NAN, Intent::Label), mHeader, 3.f));
		mFileChip->WithFormat([this](double) { return std::string(mHost.TrackName(mTrack)); });
		mBeats = drag(add(DragControl::Enum(z, "Beats", {"1", "2", "3", "4", "6", "8", "12", "16"}, 3, [this](double v) {
			static const int beats[8] = {1, 2, 3, 4, 6, 8, 12, 16};
			fg_edit_set_grid(Pat(), beats[(int)v], Pat()->denom, &mHost.NextColor(mTrack));
			Publish();
			mTiles->Select(-1);
			mTiles->Refresh();
		}), mHeader, 1.5f));
		mStep = drag(add(DragControl::Enum(z, "Step", {"1/2", "1/4", "1/8", "1/16", "1/32"}, 3, [this](double v) {
			static const int denoms[5] = {2, 4, 8, 16, 32};
			fg_edit_set_grid(Pat(), Pat()->beats, denoms[(int)v], &mHost.NextColor(mTrack));
			Publish();
			mTiles->Select(-1);
			mTiles->Refresh();
		}), mHeader, 1.5f));
		mPitch = drag(add(new DragControl(z, "Pitch", DragControl::Mode::Value, -12, 12, 1, 0, Intent::Neutral), mHeader, 1.5f));
		mPitch->WithFormat([](double v) { char b[8]; snprintf(b, sizeof b, "%+d", (int)std::lround(v)); return std::string(b); })
		    ->WithOnChange([this](double v) {
			    Pat()->masterPitch = (int)std::lround(v);
			    Publish();
		    });
		add(new DragControl(z, "Vol", mHost.ParamTrackVolume(mTrack)), mHeader, 1.5f);
		add(new DragControl(z, "Pan", mHost.ParamTrackPan(mTrack)), mHeader, 1.5f);
		add(new DragControl(z, "Mute", mHost.ParamTrackMute(mTrack), Intent::Stop), mHeader, 1.f);
		mLoop = drag(add(DragControl::Toggle(z, "Loop", true, Intent::Go, [this](double v) {
			Pat()->loop = v >= 0.5;
			Publish();
		}), mHeader, 1.f));

		// --- waveform, tiles, modifier lane --------------------------------
		mWaveform = new WaveformControl(z);
		mWaveform->SetPattern(Pat());
		mWaveform->SetOnRegion([this](double s, double e) {
			Pat()->start = s;
			Pat()->end = e;
			Publish();
			mTiles->Refresh();
		});
		mWaveform->SetOnDrop([this](const std::string& path) { mHost.LoadSample(mTrack, path); });
		g->AttachControl(mWaveform);
		mTiles = new TileRowControl(z);
		mTiles->Bind(Pat(), &mHost.EditMode(), &mHost.NextColor(mTrack), [this] { Publish(); }, [this](int) { RefreshToolbar(); });
		g->AttachControl(mTiles);
		mModLane = new ModLaneControl(z);
		mModLane->Bind(Pat(), [this] { Publish(); }, [this](int i) { ShowModPanel(i >= 0); });
		g->AttachControl(mModLane);

		// --- slice toolbar (row 1: the selected slice) ---------------------
		mMute = drag(add(DragControl::Toggle(z, "Mute", false, Intent::Stop, [this](double v) { SetSel([&](fg_tile& t) { t.muted = v >= 0.5; }); }), mToolbar1, 1.f));
		mLock = drag(add(DragControl::Toggle(z, "Lock", false, Intent::Neutral, [this](double v) { SetSel([&](fg_tile& t) { t.locked = v >= 0.5; }); }), mToolbar1, 1.f));
		mRev = drag(add(DragControl::Toggle(z, "Rev", false, Intent::Neutral, [this](double v) { SetSel([&](fg_tile& t) { t.reversed = v >= 0.5; }); }), mToolbar1, 1.f));
		mGain = drag(add(new DragControl(z, "Gain", DragControl::Mode::Value, 0, 200, 1, 100), mToolbar1, 1.5f));
		mGain->WithFormat(Pct)->WithOnChange([this](double v) { SetSel([&](fg_tile& t) { t.gain = (float)(v / 100.); }); });
		mFadeIn = drag(add(new DragControl(z, "F.In", DragControl::Mode::Value, 0, 100, 1, 0), mToolbar1, 1.5f));
		mFadeIn->WithFormat(Pct)->WithOnChange([this](double v) { SetSel([&](fg_tile& t) { t.fadeIn = (float)(v / 100.); }); });
		mCurveIn = drag(add(DragControl::Enum(z, "", {"Lin", "Exp", "Log", "S"}, 0, [this](double v) { SetSel([&](fg_tile& t) { t.curveIn = (uint8_t)v; }); }), mToolbar1, 1.f));
		mFadeOut = drag(add(new DragControl(z, "F.Out", DragControl::Mode::Value, 0, 100, 1, 0), mToolbar1, 1.5f));
		mFadeOut->WithFormat(Pct)->WithOnChange([this](double v) { SetSel([&](fg_tile& t) { t.fadeOut = (float)(v / 100.); }); });
		mCurveOut = drag(add(DragControl::Enum(z, "", {"Lin", "Exp", "Log", "S"}, 0, [this](double v) { SetSel([&](fg_tile& t) { t.curveOut = (uint8_t)v; }); }), mToolbar1, 1.f));
		mOffset = drag(add(new DragControl(z, "Pitch", DragControl::Mode::Value, -12, 12, 1, 0), mToolbar1, 1.5f));
		mOffset->WithFormat([](double v) { char b[8]; snprintf(b, sizeof b, "%+d", (int)std::lround(v)); return std::string(b); })
		    ->WithOnChange([this](double v) { SetSel([&](fg_tile& t) { t.offset = (int8_t)std::lround(v); }); });
		add(DragControl::Button(z, "Split", Intent::Neutral, [this](double) {
			const int i = mTiles->Selected();
			if (i >= 0 && fg_edit_split(Pat(), i, mHost.NextColor(mTrack)++)) {
				Publish();
				mTiles->Refresh();
			}
		}), mToolbar1, 1.f);
		add(DragControl::Button(z, "Merge", Intent::Neutral, [this](double) {
			const int i = mTiles->Selected();
			if (i >= 0 && fg_edit_merge(Pat(), i)) {
				mTiles->Select(std::max(0, i - 1));
				Publish();
				mTiles->Refresh();
			}
		}), mToolbar1, 1.f);

		// --- pattern toolbar (row 2) ---------------------------------------
		add(DragControl::Button(z, "Dup <", Intent::Neutral, [this](double) { Dup(-1); }), mToolbar2, 1.f);
		add(DragControl::Button(z, "Dup >", Intent::Neutral, [this](double) { Dup(+1); }), mToolbar2, 1.f);
		add(DragControl::Button(z, "Refill", Intent::Neutral, [this](double) {
			const int i = mTiles->Selected();
			if (i >= 0 && fg_edit_refill(Pat(), i)) {
				Publish();
				mTiles->Refresh();
				RefreshToolbar();
			}
		}), mToolbar2, 1.f);
		add(DragControl::Button(z, "Randomize", Intent::Neutral, [this](double) {
			if (fg_edit_randomize(Pat(), Pat()->randLevel / 100.0, &mRng)) {
				Publish();
				mTiles->Refresh();
			}
		}), mToolbar2, 1.5f);
		mAmt = drag(add(new DragControl(z, "Amt", DragControl::Mode::Value, 0, 100, 1, 50), mToolbar2, 1.5f));
		mAmt->WithFormat(Pct)->WithOnChange([this](double v) {
			Pat()->randLevel = (int)std::lround(v);
			Publish();
		});
		add(DragControl::Button(z, "Reset Order", Intent::Neutral, [this](double) {
			fg_edit_reset_order(Pat());
			Publish();
			mTiles->Refresh();
		}), mToolbar2, 1.5f);
		add(DragControl::Button(z, "Reset All", Intent::Stop, [this](double) {
			fg_edit_reset_all(Pat());
			Publish();
			mTiles->Select(-1);
			mTiles->Refresh();
		}), mToolbar2, 1.5f);
		mModeBtn = drag(add(DragControl::Enum(z, "", {"Packed", "Gaps"}, (int)mHost.EditMode(), [this](double v) {
			mHost.EditMode() = v >= 0.5 ? FG_EDIT_GAPS : FG_EDIT_PACK;
			mTiles->Refresh();
		}), mToolbar2, 1.f));

		// --- modifier settings (shown in place of the toolbars while a chip is selected) ---
		mModAction = drag(add(DragControl::Enum(z, "", {"Mute", "Rev", "Gain", "Rand", "Reset", "Ratchet"}, 0, [this](double v) { SetMod([&](fg_mod& m) { m.action = (uint8_t)v; }); }), mModRow1, 1.5f));
		mModFire = drag(add(DragControl::Enum(z, "Fire", {"Prob", "Every"}, 0, [this](double v) {
			SetMod([&](fg_mod& m) {
				m.fireMode = (uint8_t)v;
				m.fireValue = (int16_t)(m.fireMode == FG_FIRE_EVERY ? 2 : 100);
			});
		}), mModRow1, 1.5f));
		mModValue = drag(add(new DragControl(z, "Chance", DragControl::Mode::Value, 0, 100, 1, 100), mModRow1, 1.5f));
		mModValue->WithFormat([this](double v) {
			char b[16];
			const fg_mod* m = SelMod();
			if (m && m->fireMode == FG_FIRE_EVERY) {
				snprintf(b, sizeof b, "1 in %d", (int)std::lround(v));
			} else {
				snprintf(b, sizeof b, "%d%%", (int)std::lround(v));
			}
			return std::string(b);
		})->WithOnChange([this](double v) { SetMod([&](fg_mod& m) { m.fireValue = (int16_t)std::lround(v); }); });
		mModLevel = drag(add(new DragControl(z, "Level", DragControl::Mode::Value, 0, 200, 1, 100), mModRow1, 1.5f));
		mModLevel->WithFormat(Pct)->WithOnChange([this](double v) { SetMod([&](fg_mod& m) { m.gainAmt = (int16_t)std::lround(v); }); });
		mModMode = drag(add(DragControl::Enum(z, "", {"Even", "Ramp", "Pitch"}, 0, [this](double v) { SetMod([&](fg_mod& m) { m.mode = (uint8_t)v; }); }), mModRow1, 1.5f));
		mModHits = drag(add(new DragControl(z, "Hits", DragControl::Mode::Value, 1, 8, 1, 2), mModRow2, 1.f));
		mModHits->WithOnChange([this](double v) { SetMod([&](fg_mod& m) { m.subdiv = (int8_t)std::lround(v); }); });
		mModTo = drag(add(new DragControl(z, "To", DragControl::Mode::Value, 1, 8, 1, 4), mModRow2, 1.f));
		mModTo->WithOnChange([this](double v) { SetMod([&](fg_mod& m) { m.subdivTo = (int8_t)std::lround(v); }); });
		mModPitch = drag(add(new DragControl(z, "Pitch", DragControl::Mode::Value, -12, 12, 1, 0), mModRow2, 1.f));
		mModPitch->WithFormat([](double v) { char b[8]; snprintf(b, sizeof b, "%+d", (int)std::lround(v)); return std::string(b); })
		    ->WithOnChange([this](double v) { SetMod([&](fg_mod& m) { m.pitchStep = (int8_t)std::lround(v); }); });
		mModLen = drag(add(new DragControl(z, "Len", DragControl::Mode::Value, 1, 16, 1, 1), mModRow2, 1.f));
		mModLen->WithOnChange([this](double v) { SetMod([&](fg_mod& m) { m.lenSteps = (int8_t)std::lround(v); }); });
		add(DragControl::Button(z, "Remove", Intent::Stop, [this](double) {
			const int i = mModLane->Selected();
			fg_pattern* p = Pat();
			if (i >= 0 && i < p->nMods) {
				memmove(&p->mods[i], &p->mods[i + 1], sizeof(fg_mod) * (size_t)(p->nMods - i - 1));
				p->nMods--;
				Publish();
			}
			mModLane->Select(-1);
			ShowModPanel(false);
		}), mModRow2, 1.f);
		add(DragControl::Button(z, "Done", Intent::Go, [this](double) {
			mModLane->Select(-1);
			ShowModPanel(false);
		}), mModRow2, 1.f);
		ShowModPanel(false);

		fg_rng_seed(&mRng, 0xC0FFEE);
		SyncFromPattern();
		SampleChanged(mTrack);
		g->SetLayoutOnResize(true);
	}

	static std::string Pct(double v) {
		char b[16];
		snprintf(b, sizeof b, "%d%%", (int)std::lround(v));
		return b;
	}

	fg_mod* SelMod() {
		const int i = mModLane ? mModLane->Selected() : -1;
		fg_pattern* p = Pat();
		return (i >= 0 && i < p->nMods) ? &p->mods[i] : nullptr;
	}

	void SetMod(const std::function<void(fg_mod&)>& fn) {
		fg_mod* m = SelMod();
		if (!m) {
			return;
		}
		fn(*m);
		fg_pattern_validate(Pat());
		Publish();
		mModLane->Refresh();
		RefreshModPanel();
	}

	// the toolbars and the modifier panel share the two bottom rows
	void ShowModPanel(bool on) {
		for (auto& it : mToolbar1) {
			it.first->Hide(on);
		}
		for (auto& it : mToolbar2) {
			it.first->Hide(on);
		}
		for (auto& it : mModRow1) {
			it.first->Hide(!on);
		}
		for (auto& it : mModRow2) {
			it.first->Hide(!on);
		}
		if (on) {
			RefreshModPanel();
		}
	}

	void RefreshModPanel() {
		const fg_mod* m = SelMod();
		if (!m) {
			return;
		}
		mModAction->SetLocalValue(m->action);
		mModFire->SetLocalValue(m->fireMode);
		mModValue->SetLocalValue(m->fireValue);
		mModLevel->SetLocalValue(m->gainAmt);
		mModMode->SetLocalValue(m->mode);
		mModHits->SetLocalValue(m->subdiv);
		mModTo->SetLocalValue(m->subdivTo);
		mModPitch->SetLocalValue(m->pitchStep);
		mModLen->SetLocalValue(m->lenSteps);
		const bool ratchet = m->action == FG_MOD_RATCHET;
		mModLevel->WithEnabled(m->action == FG_MOD_GAIN);
		mModMode->WithEnabled(ratchet);
		mModHits->WithEnabled(ratchet);
		mModTo->WithEnabled(ratchet && m->mode == FG_RATCHET_RAMP);
		mModPitch->WithEnabled(ratchet && m->mode == FG_RATCHET_PITCH);
		mModLen->WithEnabled(ratchet);
		DragControl* all[] = {mModAction, mModFire, mModValue, mModLevel, mModMode, mModHits, mModTo, mModPitch, mModLen};
		for (auto* c : all) {
			c->SetDirty(false);
		}
	}

	// apply a setter to the selected clip, publish, redraw
	void SetSel(const std::function<void(fg_tile&)>& fn) {
		const int i = mTiles->Selected();
		fg_pattern* p = Pat();
		if (i < 0 || i >= p->nTiles || p->tiles[i].gap) {
			return;
		}
		fn(p->tiles[i]);
		Publish();
		mTiles->Refresh();
	}

	void Dup(int dir) {
		const int i = mTiles->Selected();
		if (i < 0) {
			return;
		}
		const int c = fg_edit_dup(Pat(), i, dir);
		if (c >= 0) {
			mTiles->Select(c);
			Publish();
			mTiles->Refresh();
		}
	}

	// header controls follow the pattern (after a load / restore)
	void SyncFromPattern() {
		const fg_pattern* p = Pat();
		static const int beats[8] = {1, 2, 3, 4, 6, 8, 12, 16};
		for (int k = 0; k < 8; k++) {
			if (beats[k] == p->beats) {
				mBeats->SetLocalValue(k);
			}
		}
		static const int denoms[5] = {2, 4, 8, 16, 32};
		for (int k = 0; k < 5; k++) {
			if (denoms[k] == p->denom) {
				mStep->SetLocalValue(k);
			}
		}
		mPitch->SetLocalValue(p->masterPitch);
		mLoop->SetLocalValue(p->loop ? 1. : 0.);
		mAmt->SetLocalValue(p->randLevel);
		mModeBtn->SetLocalValue((double)mHost.EditMode());
		RefreshToolbar();
	}

	// toolbar follows the selection
	void RefreshToolbar() {
		const int i = mTiles->Selected();
		const fg_pattern* p = Pat();
		const bool clip = i >= 0 && i < p->nTiles && !p->tiles[i].gap;
		DragControl* sliceControls[] = {mMute, mLock, mRev, mGain, mFadeIn, mCurveIn, mFadeOut, mCurveOut, mOffset};
		for (auto* c : sliceControls) {
			c->WithEnabled(clip);
			c->SetDirty(false);
		}
		if (!clip) {
			return;
		}
		const fg_tile& t = p->tiles[i];
		mMute->SetLocalValue(t.muted);
		mLock->SetLocalValue(t.locked);
		mRev->SetLocalValue(t.reversed);
		mGain->SetLocalValue(t.gain * 100.);
		mFadeIn->SetLocalValue(t.fadeIn * 100.);
		mCurveIn->SetLocalValue(t.curveIn);
		mFadeOut->SetLocalValue(t.fadeOut * 100.);
		mCurveOut->SetLocalValue(t.curveOut);
		mOffset->SetLocalValue(t.offset);
	}

	void OpenFileList(IGraphics* g) {
		if (mFileList) {
			return;
		}
		mFileList = new FileListControl(g->GetBounds(), mHost.SamplesDir(),
		                                [this, g](const std::string& path) {
			                                mHost.LoadSample(mTrack, path);
			                                CloseFileList(g);
		                                },
		                                [this, g] { CloseFileList(g); });
		g->AttachControl(mFileList);
	}

	void CloseFileList(IGraphics* g) {
		if (mFileList) {
			g->RemoveControl(mFileList);
			mFileList = nullptr;
			g->SetAllControlsDirty();
		}
	}

	Host& mHost;
	int mTrack = 0;
	bool mPerform = false;
	fg_rng mRng;

	std::vector<std::pair<IControl*, float>> mTransport, mHeader, mToolbar1, mToolbar2, mModRow1, mModRow2;
	WaveformControl* mWaveform = nullptr;
	TileRowControl* mTiles = nullptr;
	ModLaneControl* mModLane = nullptr;
	DragControl *mModAction = nullptr, *mModFire = nullptr, *mModValue = nullptr, *mModLevel = nullptr, *mModMode = nullptr;
	DragControl *mModHits = nullptr, *mModTo = nullptr, *mModPitch = nullptr, *mModLen = nullptr;
	FrogVersionReadout* mVersion = nullptr;
	FileListControl* mFileList = nullptr;
	DragControl *mPerformBtn = nullptr, *mFileChip = nullptr, *mBeats = nullptr, *mStep = nullptr, *mPitch = nullptr, *mLoop = nullptr;
	DragControl *mMute = nullptr, *mLock = nullptr, *mRev = nullptr, *mGain = nullptr, *mFadeIn = nullptr, *mCurveIn = nullptr, *mFadeOut = nullptr, *mCurveOut = nullptr, *mOffset = nullptr;
	DragControl *mAmt = nullptr, *mModeBtn = nullptr;
};

}  // namespace frogui
