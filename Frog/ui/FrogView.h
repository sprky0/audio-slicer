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
#include "TrackStripControl.h"
#include "WaveformControl.h"

#include "engine/edit.h"
#include "engine/engine.h"
#include "engine/pattern.h"

#include <cmath>
#include <cstring>
#include <functional>
#include <map>
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
	virtual void ToggleRecord(int track) = 0;
	virtual bool IsRecording() const = 0;
	virtual double RecordedSeconds() const = 0;
	virtual void StartBounce() = 0;
	virtual const char* BounceStatus() const = 0;   // "Export", "Exporting", "Exported", ...
	// export options: loops of the bar, subset (0 all, 1 focused, 2 stems), peak normalise
	virtual int& ExportLoops() = 0;
	virtual int& ExportSubset() = 0;
	virtual bool& ExportNormalize() = 0;
	virtual void SaveSessionUI() = 0;
	virtual void LoadSessionUI(const std::string& path) = 0;
	virtual std::string SessionsDirUI() const = 0;
	virtual const char* SessionName() const = 0;
	virtual int TrackCount() const = 0;
	virtual int& TrackCountRef() = 0;
	virtual bool AddTrackUI() = 0;
	virtual bool RemoveTrackUI() = 0;
	virtual bool DuplicateTrackUI(int from) = 0;
	virtual bool TrackHasSample(int track) const = 0;
	virtual bool TrackMuted(int track) const = 0;
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
	// MIDI CC map (F16.2): learn is armed for one hook at a time, bindings
	// live in the plugin, the revision ticks when the map changes.
	virtual void ArmLearn(const char* hook) = 0;
	virtual const char* LearnArmed() const = 0;   // "" when idle
	virtual void ClearBinding(const char* hook) = 0;
	virtual bool HookBound(const char* hook) const = 0;
	virtual int MapRevision() const = 0;
	// the focused track outlives the editor (plugin-side hooks use it)
	virtual void FocusChanged(int track) = 0;
	virtual int FocusTrack() const = 0;
};

class FrogView {
public:
	FrogView(Host& host, IGraphics* g)
	    : mHost(host), mTrack(std::max(0, std::min(host.FocusTrack(), host.TrackCount() - 1))) {
		Build(g);
		Layout(g);
	}

	// A CC for a hook: special targets first (selection and focus are not
	// controls), then the control carrying that name. False when nothing
	// here answers to it (the plugin then tries its own fallbacks).
	bool ApplyHook(const char* name, double norm, bool press) {
		if (strcmp(name, "slice.select") == 0) {
			const fg_pattern* p = Pat();
			if (p->nTiles > 0) {
				mTiles->Select(std::min(p->nTiles - 1, (int)std::lround(norm * (p->nTiles - 1))));
				mTiles->Refresh();
				RefreshToolbar();
			}
			return true;
		}
		if (strcmp(name, "track.focus") == 0) {
			Focus(std::min(mHost.TrackCount() - 1, (int)std::lround(norm * (mHost.TrackCount() - 1))));
			return true;
		}
		auto it = mHooks.find(name);
		if (it == mHooks.end()) {
			return false;
		}
		it->second->Drive(norm, press);
		return true;
	}

	// Called on every resize (the layout function re-runs) and on mode changes.
	void Layout(IGraphics* g) {
		const IRECT b = g->GetBounds();
		// the panel background is a control at index 0 sized once at attach;
		// Size-mode resizes re-run this layout, so cover the new bounds too
		if (IControl* bg = g->GetBackgroundControl()) {
			bg->SetTargetAndDrawRECTs(b);
		}
		const float u = Unit(b.W(), b.H());
		const float ch = kControlU * u, gap = kGapU * u;
		IRECT rest = b.GetPadded(-gap);

		const IRECT transport = rest.ReduceFromTop(ch);
		rest.ReduceFromTop(gap);
		const IRECT strip = rest.ReduceFromTop(5.f * u);
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
		mLearnChip->SetTargetAndDrawRECTs(mVersion->GetRECT());
		mStrip->SetTargetAndDrawRECTs(strip);
		Row(header, gap, mHeader);
		mWaveform->SetTargetAndDrawRECTs(wave);
		mTiles->SetTargetAndDrawRECTs(tiles);
		mModLane->SetTargetAndDrawRECTs(modLane);
		Row(toolbar1, gap, mToolbar1);
		Row(toolbar2, gap, mToolbar2);
		Row(toolbar1, gap, mModRow1);
		Row(toolbar2, gap, mModRow2);
		Row(toolbar1, gap, mExportRow1);
		Row(toolbar2, gap, mExportRow2);
		if (mFileList) {
			mFileList->SetTargetAndDrawRECTs(b);
		}
		g->SetAllControlsDirty();
	}

	// Per idle tick from the plugin (the UI thread): playback feedback.
	void Idle() {
		const std::string armed = mHost.LearnArmed();
		if (armed != mLearnShown) {
			mLearnShown = armed;
			mVersion->Hide(!armed.empty());
			mLearnChip->Hide(armed.empty());
			mLearnChip->SetDirty(false);
		}
		if (mHost.MapRevision() != mMapRevision) {
			mMapRevision = mHost.MapRevision();
			for (auto& h : mHooks) {
				h.second->SetBound(mHost.HookBound(h.first.c_str()));
			}
		}
		if (mExportStatus != mHost.BounceStatus()) {
			mExportStatus = mHost.BounceStatus();
			mExportBtn->SetDirty(false);
			mExportStatusChip->SetDirty(false);
		}
		if (mSessionShown != mHost.SessionName()) {
			mSessionShown = mHost.SessionName();
			mSessionsBtn->SetDirty(false);
		}
		const bool rec = mHost.IsRecording();
		if (rec || mRec->On()) {
			mRec->SetLocalValue(rec ? 1. : 0.);
			if (rec) {
				mRec->SetDirty(false);   /* the seconds tick */
			}
		}
		mStrip->Refresh();
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
		mStrip->SetDirty(false);
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
		// name a control as a MIDI hook: a CC drives it, a long press arms learn for it
		auto hk = [this](const char* name, IControl* c) {
			auto* d = static_cast<DragControl*>(c);
			const std::string n = name;
			d->WithHook(name);
			d->SetOnLongPress([this, n] { mHost.ArmLearn(n.c_str()); });
			mHooks[n] = d;
			return d;
		};

		// --- transport -----------------------------------------------------
		add(new DragControl(z, "FROG", DragControl::Mode::Button, 0, 1, 1, NAN, Intent::Label), mTransport, 1.f);
		hk("transport.play", add(DragControl::Button(z, "Play", Intent::Go, [this](double) { mHost.PlayAll(); }), mTransport, 1.f));
		hk("transport.stop", add(DragControl::Button(z, "Stop", Intent::Stop, [this](double) { mHost.StopAll(); }), mTransport, 1.f));
		hk("transport.bpm", add(new DragControl(z, "BPM", mHost.ParamBpm()), mTransport, 2.f));
		hk("clock.source", add(new DragControl(z, "Clock", mHost.ParamClock()), mTransport, 2.f));
		mPerformBtn = hk("transport.perform", add(DragControl::Toggle(z, "Perform", mPerform, Intent::Neutral, [this, g](double v) {
			mPerform = v >= 0.5;
			Layout(g);
		}), mTransport, 1.f));
		mExportBtn = hk("export.open", add(DragControl::Button(z, "Export", Intent::Neutral, [this](double) { ShowExportPanel(!mExportOpen); }), mTransport, 1.f));
		hk("session.save", add(DragControl::Button(z, "Save", Intent::Neutral, [this](double) { mHost.SaveSessionUI(); }), mTransport, 1.f));
		mSessionsBtn = drag(add(DragControl::Button(z, "Sessions", Intent::Label, [this, g](double) { OpenSessionList(g); }), mTransport, 1.5f));
		mSessionsBtn->WithFormat([this](double) {
			const std::string n = mHost.SessionName();
			return n.empty() ? std::string("Sessions") : n;
		});
		mExportBtn->WithFormat([this](double) { return std::string(mHost.BounceStatus()); });
		g->AttachControl(mVersion = new FrogVersionReadout(z));
		mTransport.push_back({mVersion, 2.f});
		// while learn is armed this replaces the build stamp; a tap clears the hook's binding
		mLearnChip = DragControl::Button(z, "Learn", Intent::Label, [this](double) { mHost.ClearBinding(mHost.LearnArmed()); });
		mLearnChip->WithFormat([this](double) {
			const char* armed = mHost.LearnArmed();
			auto it = mHooks.find(armed);
			const std::string what = it != mHooks.end() && !it->second->Label().empty() ? it->second->Label() : std::string(armed);
			return "Learn " + what + "  (tap: unbind)";
		});
		mLearnChip->Hide(true);
		g->AttachControl(mLearnChip);

		// --- track header --------------------------------------------------
		hk("track.load", add(DragControl::Button(z, "Load", Intent::Neutral, [this, g](double) { OpenFileList(g); }), mHeader, 1.f));
		mFileChip = drag(add(new DragControl(z, "", DragControl::Mode::Button, 0, 1, 1, NAN, Intent::Label), mHeader, 3.f));
		mFileChip->WithFormat([this](double) { return std::string(mHost.TrackName(mTrack)); });
		mBeats = hk("track.beats", add(DragControl::Enum(z, "Beats", {"1", "2", "3", "4", "6", "8", "12", "16"}, 3, [this](double v) {
			static const int beats[8] = {1, 2, 3, 4, 6, 8, 12, 16};
			fg_edit_set_grid(Pat(), beats[(int)v], Pat()->denom, &mHost.NextColor(mTrack));
			Publish();
			mTiles->Select(-1);
			mTiles->Refresh();
		}), mHeader, 1.5f));
		mStep = hk("track.step", add(DragControl::Enum(z, "Step", {"1/2", "1/4", "1/8", "1/16", "1/32"}, 3, [this](double v) {
			static const int denoms[5] = {2, 4, 8, 16, 32};
			fg_edit_set_grid(Pat(), Pat()->beats, denoms[(int)v], &mHost.NextColor(mTrack));
			Publish();
			mTiles->Select(-1);
			mTiles->Refresh();
		}), mHeader, 1.5f));
		mPitch = hk("track.pitch", add(new DragControl(z, "Pitch", DragControl::Mode::Value, -12, 12, 1, 0, Intent::Neutral), mHeader, 1.5f));
		mPitch->WithFormat([](double v) { char b[8]; snprintf(b, sizeof b, "%+d", (int)std::lround(v)); return std::string(b); })
		    ->WithOnChange([this](double v) {
			    Pat()->masterPitch = (int)std::lround(v);
			    Publish();
		    });
		mVol = hk("track.vol", add(new DragControl(z, "Vol", mHost.ParamTrackVolume(mTrack)), mHeader, 1.5f));
		mPan = hk("track.pan", add(new DragControl(z, "Pan", mHost.ParamTrackPan(mTrack)), mHeader, 1.5f));
		mTrackMute = hk("track.mute", add(new DragControl(z, "Mute", mHost.ParamTrackMute(mTrack), Intent::Stop), mHeader, 1.f));
		mLoop = hk("track.loop", add(DragControl::Toggle(z, "Loop", true, Intent::Go, [this](double v) {
			Pat()->loop = v >= 0.5;
			Publish();
		}), mHeader, 1.f));
		mRec = hk("record.toggle", add(DragControl::Toggle(z, "Rec", false, Intent::Stop, [this](double) { mHost.ToggleRecord(mTrack); }), mHeader, 1.f));
		mRec->WithFormat([this](double) {
			if (!mHost.IsRecording()) {
				return std::string("Rec");
			}
			char b[24];
			snprintf(b, sizeof b, "Rec %.1fs", mHost.RecordedSeconds());
			return std::string(b);
		});

		// --- track strip -----------------------------------------------------
		mStrip = new TrackStripControl(z);
		mStrip->Bind(&mHost.TrackCountRef(), &mTrack,
		             [this](int t) {
			             TrackStripControl::Info i;
			             i.name = mHost.TrackName(t);
			             i.hasSample = mHost.TrackHasSample(t);
			             i.muted = mHost.TrackMuted(t);
			             const fg_visual* v = mHost.CurrentNote(t);
			             i.playing = v && !v->silent && fg_engine_now(mHost.Engine()) < v->stop;
			             return i;
		             },
		             [this](int t) { Focus(t); },
		             [this] {
			             if (mHost.AddTrackUI()) {
				             Focus(mHost.TrackCount() - 1);
			             }
		             },
		             [this] {
			             if (mHost.DuplicateTrackUI(mTrack)) {
				             Focus(mHost.TrackCount() - 1);
			             }
		             },
		             [this] {
			             if (mHost.RemoveTrackUI()) {
				             Focus(std::min(mTrack, mHost.TrackCount() - 1));
			             }
		             });
		g->AttachControl(mStrip);

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

		// --- slice toolbar (row 1: the selected slice, or every slice with All) ---
		mAll = hk("slice.all", add(DragControl::Toggle(z, "All", false, Intent::Label, [this](double) { RefreshToolbar(); }), mToolbar1, 1.f));
		mMute = hk("slice.mute", add(DragControl::Toggle(z, "Mute", false, Intent::Stop, [this](double v) { SetSel([&](fg_tile& t) { t.muted = v >= 0.5; }); }), mToolbar1, 1.f));
		mLock = hk("slice.lock", add(DragControl::Toggle(z, "Lock", false, Intent::Neutral, [this](double v) { SetSel([&](fg_tile& t) { t.locked = v >= 0.5; }); }), mToolbar1, 1.f));
		mRev = hk("slice.rev", add(DragControl::Toggle(z, "Rev", false, Intent::Neutral, [this](double v) { SetSel([&](fg_tile& t) { t.reversed = v >= 0.5; }); }), mToolbar1, 1.f));
		mGain = hk("slice.gain", add(new DragControl(z, "Gain", DragControl::Mode::Value, 0, 200, 1, 100), mToolbar1, 1.5f));
		mGain->WithFormat(Pct)->WithOnChange([this](double v) { SetSel([&](fg_tile& t) { t.gain = (float)(v / 100.); }); });
		mFadeIn = hk("slice.fadeIn", add(new DragControl(z, "F.In", DragControl::Mode::Value, 0, 100, 1, 0), mToolbar1, 1.5f));
		mFadeIn->WithFormat(Pct)->WithOnChange([this](double v) { SetSel([&](fg_tile& t) { t.fadeIn = (float)(v / 100.); }); });
		mCurveIn = hk("slice.curveIn", add(DragControl::Enum(z, "", {"Lin", "Exp", "Log", "S"}, 0, [this](double v) { SetSel([&](fg_tile& t) { t.curveIn = (uint8_t)v; }); }), mToolbar1, 1.f));
		mFadeOut = hk("slice.fadeOut", add(new DragControl(z, "F.Out", DragControl::Mode::Value, 0, 100, 1, 0), mToolbar1, 1.5f));
		mFadeOut->WithFormat(Pct)->WithOnChange([this](double v) { SetSel([&](fg_tile& t) { t.fadeOut = (float)(v / 100.); }); });
		mCurveOut = hk("slice.curveOut", add(DragControl::Enum(z, "", {"Lin", "Exp", "Log", "S"}, 0, [this](double v) { SetSel([&](fg_tile& t) { t.curveOut = (uint8_t)v; }); }), mToolbar1, 1.f));
		mOffset = hk("slice.pitch", add(new DragControl(z, "Pitch", DragControl::Mode::Value, -12, 12, 1, 0), mToolbar1, 1.5f));
		mOffset->WithFormat([](double v) { char b[8]; snprintf(b, sizeof b, "%+d", (int)std::lround(v)); return std::string(b); })
		    ->WithOnChange([this](double v) { SetSel([&](fg_tile& t) { t.offset = (int8_t)std::lround(v); }); });
		hk("slice.split", add(DragControl::Button(z, "Split", Intent::Neutral, [this](double) {
			const int i = mTiles->Selected();
			if (i >= 0 && fg_edit_split(Pat(), i, mHost.NextColor(mTrack)++)) {
				Publish();
				mTiles->Refresh();
			}
		}), mToolbar1, 1.f));
		hk("slice.merge", add(DragControl::Button(z, "Merge", Intent::Neutral, [this](double) {
			const int i = mTiles->Selected();
			if (i >= 0 && fg_edit_merge(Pat(), i)) {
				mTiles->Select(std::max(0, i - 1));
				Publish();
				mTiles->Refresh();
			}
		}), mToolbar1, 1.f));

		// --- pattern toolbar (row 2) ---------------------------------------
		hk("pattern.dupPrev", add(DragControl::Button(z, "Dup <", Intent::Neutral, [this](double) { Dup(-1); }), mToolbar2, 1.f));
		hk("pattern.dupNext", add(DragControl::Button(z, "Dup >", Intent::Neutral, [this](double) { Dup(+1); }), mToolbar2, 1.f));
		hk("pattern.refill", add(DragControl::Button(z, "Refill", Intent::Neutral, [this](double) {
			const int i = mTiles->Selected();
			if (mAll->On()) {
				fg_edit_refill_all(Pat());
			} else if (i < 0 || !fg_edit_refill(Pat(), i)) {
				return;
			}
			Publish();
			mTiles->Refresh();
			RefreshToolbar();
		}), mToolbar2, 1.f));
		hk("pattern.randomize", add(DragControl::Button(z, "Randomize", Intent::Neutral, [this](double) {
			if (fg_edit_randomize(Pat(), Pat()->randLevel / 100.0, &mRng)) {
				Publish();
				mTiles->Refresh();
			}
		}), mToolbar2, 1.5f));
		mAmt = hk("pattern.amt", add(new DragControl(z, "Amt", DragControl::Mode::Value, 0, 100, 1, 50), mToolbar2, 1.5f));
		mAmt->WithFormat(Pct)->WithOnChange([this](double v) {
			Pat()->randLevel = (int)std::lround(v);
			Publish();
		});
		hk("pattern.resetOrder", add(DragControl::Button(z, "Reset Order", Intent::Neutral, [this](double) {
			fg_edit_reset_order(Pat());
			Publish();
			mTiles->Refresh();
		}), mToolbar2, 1.5f));
		hk("pattern.resetAll", add(DragControl::Button(z, "Reset All", Intent::Stop, [this](double) {
			fg_edit_reset_all(Pat());
			Publish();
			mTiles->Select(-1);
			mTiles->Refresh();
		}), mToolbar2, 1.5f));
		mModeBtn = hk("pattern.mode", add(DragControl::Enum(z, "", {"Packed", "Gaps"}, (int)mHost.EditMode(), [this](double v) {
			mHost.EditMode() = v >= 0.5 ? FG_EDIT_GAPS : FG_EDIT_PACK;
			mTiles->Refresh();
		}), mToolbar2, 1.f));
		// zoom: Trim makes the selection the view; Full shows the whole sample again
		hk("view.trim", add(DragControl::Button(z, "Trim", Intent::Neutral, [this](double) {
			fg_pattern* p = Pat();
			const double vs = p->virtualStart, vspan = p->virtualEnd - vs;
			const double ns = vs + p->start * vspan, ne = vs + p->end * vspan;
			if (ne - ns < 0.001) {
				return;
			}
			p->virtualStart = ns;
			p->virtualEnd = ne;
			p->start = 0.0;
			p->end = 1.0;
			Publish();
			mWaveform->SetDirty(false);
			mTiles->Refresh();
		}), mToolbar2, 1.f));
		hk("view.full", add(DragControl::Button(z, "Full", Intent::Neutral, [this](double) {
			fg_pattern* p = Pat();
			const double vs = p->virtualStart, vspan = p->virtualEnd - vs;
			p->start = vs + p->start * vspan;   /* keep the audible region where it is */
			p->end = vs + p->end * vspan;
			p->virtualStart = 0.0;
			p->virtualEnd = 1.0;
			Publish();
			mWaveform->SetDirty(false);
			mTiles->Refresh();
		}), mToolbar2, 1.f));

		// --- modifier settings (shown in place of the toolbars while a chip is selected) ---
		mModAction = hk("mod.action", add(DragControl::Enum(z, "", {"Mute", "Rev", "Gain", "Rand", "Reset", "Ratchet"}, 0, [this](double v) { SetMod([&](fg_mod& m) { m.action = (uint8_t)v; }); }), mModRow1, 1.5f));
		mModFire = hk("mod.fire", add(DragControl::Enum(z, "Fire", {"Prob", "Every"}, 0, [this](double v) {
			SetMod([&](fg_mod& m) {
				m.fireMode = (uint8_t)v;
				m.fireValue = (int16_t)(m.fireMode == FG_FIRE_EVERY ? 2 : 100);
			});
		}), mModRow1, 1.5f));
		mModValue = hk("mod.chance", add(new DragControl(z, "Chance", DragControl::Mode::Value, 0, 100, 1, 100), mModRow1, 1.5f));
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
		mModLevel = hk("mod.level", add(new DragControl(z, "Level", DragControl::Mode::Value, 0, 200, 1, 100), mModRow1, 1.5f));
		mModLevel->WithFormat(Pct)->WithOnChange([this](double v) { SetMod([&](fg_mod& m) { m.gainAmt = (int16_t)std::lround(v); }); });
		mModMode = hk("mod.mode", add(DragControl::Enum(z, "", {"Even", "Ramp", "Pitch"}, 0, [this](double v) { SetMod([&](fg_mod& m) { m.mode = (uint8_t)v; }); }), mModRow1, 1.5f));
		mModHits = hk("mod.hits", add(new DragControl(z, "Hits", DragControl::Mode::Value, 1, 8, 1, 2), mModRow2, 1.f));
		mModHits->WithOnChange([this](double v) { SetMod([&](fg_mod& m) { m.subdiv = (int8_t)std::lround(v); }); });
		mModTo = hk("mod.to", add(new DragControl(z, "To", DragControl::Mode::Value, 1, 8, 1, 4), mModRow2, 1.f));
		mModTo->WithOnChange([this](double v) { SetMod([&](fg_mod& m) { m.subdivTo = (int8_t)std::lround(v); }); });
		mModPitch = hk("mod.pitch", add(new DragControl(z, "Pitch", DragControl::Mode::Value, -12, 12, 1, 0), mModRow2, 1.f));
		mModPitch->WithFormat([](double v) { char b[8]; snprintf(b, sizeof b, "%+d", (int)std::lround(v)); return std::string(b); })
		    ->WithOnChange([this](double v) { SetMod([&](fg_mod& m) { m.pitchStep = (int8_t)std::lround(v); }); });
		mModLen = hk("mod.len", add(new DragControl(z, "Len", DragControl::Mode::Value, 1, 16, 1, 1), mModRow2, 1.f));
		mModLen->WithOnChange([this](double v) { SetMod([&](fg_mod& m) { m.lenSteps = (int8_t)std::lround(v); }); });
		hk("mod.remove", add(DragControl::Button(z, "Remove", Intent::Stop, [this](double) {
			const int i = mModLane->Selected();
			fg_pattern* p = Pat();
			if (i >= 0 && i < p->nMods) {
				memmove(&p->mods[i], &p->mods[i + 1], sizeof(fg_mod) * (size_t)(p->nMods - i - 1));
				p->nMods--;
				Publish();
			}
			mModLane->Select(-1);
			ShowModPanel(false);
		}), mModRow2, 1.f));
		hk("mod.done", add(DragControl::Button(z, "Done", Intent::Go, [this](double) {
			mModLane->Select(-1);
			ShowModPanel(false);
		}), mModRow2, 1.f));
		ShowModPanel(false);

		// --- export options (shown in place of the toolbars while Export is open) ---
		static const int loopChoices[4] = {1, 2, 4, 8};
		int loopIdx = 0;
		for (int k = 0; k < 4; k++) {
			if (loopChoices[k] == mHost.ExportLoops()) {
				loopIdx = k;
			}
		}
		hk("export.loops", add(DragControl::Enum(z, "Length", {"1 loop", "2 loops", "4 loops", "8 loops"}, loopIdx, [this](double v) {
			mHost.ExportLoops() = loopChoices[(int)v];
		}), mExportRow1, 1.5f));
		hk("export.tracks", add(DragControl::Enum(z, "Tracks", {"All", "Focused", "Stems"}, mHost.ExportSubset(), [this](double v) {
			mHost.ExportSubset() = (int)v;
		}), mExportRow1, 1.5f));
		hk("export.normalize", add(DragControl::Toggle(z, "Normalize", mHost.ExportNormalize(), Intent::Neutral, [this](double v) {
			mHost.ExportNormalize() = v >= 0.5;
		}), mExportRow1, 1.f));
		mExportNote = drag(add(new DragControl(z, "", DragControl::Mode::Button, 0, 1, 1, NAN, Intent::Label), mExportRow1, 3.f));
		mExportNote->WithFormat([this](double) {
			switch (mHost.ExportSubset()) {
				case 1: return std::string("the focused track alone, to exports/");
				case 2: return std::string("one WAV per track, levels kept, to exports/");
				default: return std::string("the mix, to exports/");
			}
		});
		hk("export.go", add(DragControl::Button(z, "Export now", Intent::Go, [this](double) {
			mHost.StartBounce();
			ShowExportPanel(false);
		}), mExportRow2, 2.f));
		mExportStatusChip = drag(add(new DragControl(z, "", DragControl::Mode::Button, 0, 1, 1, NAN, Intent::Label), mExportRow2, 3.f));
		mExportStatusChip->WithFormat([this](double) { return std::string(mHost.BounceStatus()); });
		hk("export.cancel", add(DragControl::Button(z, "Close", Intent::Neutral, [this](double) { ShowExportPanel(false); }), mExportRow2, 1.f));
		ShowExportPanel(false);

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

	// bring another track into the editor bands
	void Focus(int t) {
		if (t < 0 || t >= mHost.TrackCount()) {
			return;
		}
		mTrack = t;
		mHost.FocusChanged(t);
		fg_pattern* p = Pat();
		mWaveform->SetPattern(p);
		mTiles->Bind(p, &mHost.EditMode(), &mHost.NextColor(mTrack), [this] { Publish(); }, [this](int) { RefreshToolbar(); });
		mTiles->Select(-1);
		mModLane->Bind(p, [this] { Publish(); }, [this](int i) { ShowModPanel(i >= 0); });
		mModLane->Select(-1);
		ShowModPanel(false);
		mVol->SetParamIdx(mHost.ParamTrackVolume(mTrack));
		mPan->SetParamIdx(mHost.ParamTrackPan(mTrack));
		mTrackMute->SetParamIdx(mHost.ParamTrackMute(mTrack));
		SyncFromPattern();
		SampleChanged(mTrack);
		mStrip->SetDirty(false);
		mWaveform->SetDirty(false);
		mTiles->Refresh();
		mModLane->Refresh();
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

	// the toolbars, the modifier panel and the export panel share the two bottom rows
	void ShowModPanel(bool on) {
		if (on) {
			mExportOpen = false;
		}
		mModOpen = on;
		ShowRows();
		if (on) {
			RefreshModPanel();
		}
	}

	void ShowExportPanel(bool on) {
		if (on && mModOpen) {
			mModLane->Select(-1);
			mModOpen = false;
		}
		mExportOpen = on;
		ShowRows();
	}

	void ShowRows() {
		const bool tools = !mModOpen && !mExportOpen;
		for (auto& it : mToolbar1) {
			it.first->Hide(!tools);
		}
		for (auto& it : mToolbar2) {
			it.first->Hide(!tools);
		}
		for (auto& it : mModRow1) {
			it.first->Hide(!mModOpen);
		}
		for (auto& it : mModRow2) {
			it.first->Hide(!mModOpen);
		}
		for (auto& it : mExportRow1) {
			it.first->Hide(!mExportOpen);
		}
		for (auto& it : mExportRow2) {
			it.first->Hide(!mExportOpen);
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

	// apply a setter to the selected clip (or, with All lit, to every clip),
	// publish, redraw. Toggles follow fill-up semantics through the control's
	// own value: the control shows "on" only when every clip is on, so a tap
	// from a mixed state turns everything on, and from all-on everything off.
	void SetSel(const std::function<void(fg_tile&)>& fn) {
		fg_pattern* p = Pat();
		if (mAll && mAll->On()) {
			for (int k = 0; k < p->nTiles; k++) {
				if (!p->tiles[k].gap) {
					fn(p->tiles[k]);
				}
			}
			Publish();
			mTiles->Refresh();
			return;
		}
		const int i = mTiles->Selected();
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

	// toolbar follows the selection (or, with All, the whole row)
	void RefreshToolbar() {
		const int i = mTiles->Selected();
		const fg_pattern* p = Pat();
		const bool all = mAll && mAll->On();
		const bool clip = all ? p->nTiles > 0 : (i >= 0 && i < p->nTiles && !p->tiles[i].gap);
		DragControl* sliceControls[] = {mMute, mLock, mRev, mGain, mFadeIn, mCurveIn, mFadeOut, mCurveOut, mOffset};
		for (auto* c : sliceControls) {
			c->WithEnabled(clip);
			c->SetDirty(false);
		}
		if (all) {
			mLock->WithEnabled(false);   /* Lock and Dup stay per slice */
		}
		if (!clip) {
			return;
		}
		if (all) {
			// the row's state: toggles read "on" only when every clip is on
			bool allMuted = true, allRev = true;
			for (int k = 0; k < p->nTiles; k++) {
				if (p->tiles[k].gap) {
					continue;
				}
				allMuted = allMuted && p->tiles[k].muted;
				allRev = allRev && p->tiles[k].reversed;
			}
			mMute->SetLocalValue(allMuted);
			mRev->SetLocalValue(allRev);
			const int k = i >= 0 && i < p->nTiles ? i : 0;
			const fg_tile& t = p->tiles[k];
			mGain->SetLocalValue(t.gain * 100.);
			mFadeIn->SetLocalValue(t.fadeIn * 100.);
			mCurveIn->SetLocalValue(t.curveIn);
			mFadeOut->SetLocalValue(t.fadeOut * 100.);
			mCurveOut->SetLocalValue(t.curveOut);
			mOffset->SetLocalValue(t.offset);
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

	void OpenSessionList(IGraphics* g) {
		if (mFileList) {
			return;
		}
		mFileList = new FileListControl(g->GetBounds(), mHost.SessionsDirUI(),
		                                [this, g](const std::string& path) {
			                                mHost.LoadSessionUI(path);
			                                CloseFileList(g);
		                                },
		                                [this, g] { CloseFileList(g); }, ".json", "Sessions");
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

	std::vector<std::pair<IControl*, float>> mTransport, mHeader, mToolbar1, mToolbar2, mModRow1, mModRow2, mExportRow1, mExportRow2;
	bool mModOpen = false, mExportOpen = false;
	DragControl *mExportNote = nullptr, *mExportStatusChip = nullptr;
	WaveformControl* mWaveform = nullptr;
	TileRowControl* mTiles = nullptr;
	ModLaneControl* mModLane = nullptr;
	TrackStripControl* mStrip = nullptr;
	DragControl *mVol = nullptr, *mPan = nullptr, *mTrackMute = nullptr;
	DragControl *mModAction = nullptr, *mModFire = nullptr, *mModValue = nullptr, *mModLevel = nullptr, *mModMode = nullptr;
	DragControl *mModHits = nullptr, *mModTo = nullptr, *mModPitch = nullptr, *mModLen = nullptr;
	FrogVersionReadout* mVersion = nullptr;
	DragControl* mLearnChip = nullptr;
	std::string mLearnShown;
	int mMapRevision = -1;
	std::map<std::string, DragControl*> mHooks;   // hook name → the control a CC drives
	FileListControl* mFileList = nullptr;
	DragControl* mExportBtn = nullptr;
	DragControl* mSessionsBtn = nullptr;
	std::string mSessionShown;
	DragControl* mRec = nullptr;
	std::string mExportStatus;
	DragControl *mPerformBtn = nullptr, *mFileChip = nullptr, *mBeats = nullptr, *mStep = nullptr, *mPitch = nullptr, *mLoop = nullptr;
	DragControl* mAll = nullptr;
	DragControl *mMute = nullptr, *mLock = nullptr, *mRev = nullptr, *mGain = nullptr, *mFadeIn = nullptr, *mCurveIn = nullptr, *mFadeOut = nullptr, *mCurveOut = nullptr, *mOffset = nullptr;
	DragControl *mAmt = nullptr, *mModeBtn = nullptr;
};

}  // namespace frogui
