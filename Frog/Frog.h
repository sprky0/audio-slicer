#pragma once

#include "IPlug_include_in_plug_hdr.h"

#include "engine/engine.h"
#include "engine/frog_types.h"

#if IPLUG_EDITOR
#include "ui/FrogView.h"
#include "ui/Peaks.h"
#endif

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// Frog — sample slicer and loop performer. The plugin class is the shell
// around the C engine (engine/): parameters, state, MIDI, the host clock,
// sample loading on the idle thread, and the UI (ui/).

const int kNumPresets = 1;

// Tracks are a fixed set so the parameter list is stable (COMPATIBILITY.md:
// parameters are added, never removed). Appliance caps come from
// appliance.conf; the plugin always exposes FG_MAX_TRACKS.
enum ETrackParam {
	kTrackVolume = 0,
	kTrackPan,
	kTrackMute,
	kNumTrackParams
};

enum EParams {
	kParamBpm = 0,      // master tempo, internal clock
	kParamMasterGain,   // dB
	kParamClockSource,  // fg_clock_source
	kParamTrackBase,    // then FG_MAX_TRACKS × kNumTrackParams
	kNumParams = kParamTrackBase + FG_MAX_TRACKS * kNumTrackParams
};

inline int TrackParam(int track, ETrackParam which) {
	return kParamTrackBase + track * kNumTrackParams + which;
}

enum ECtrlTags {
	kCtrlTagVersion = 0,
	kNumCtrlTags
};

using namespace iplug;
using namespace igraphics;

class Frog final : public Plugin
#if IPLUG_EDITOR
    , public frogui::Host
#endif
{
public:
	Frog(const InstanceInfo& info);
	~Frog();

	// --- state ---------------------------------------------------------
	bool SerializeState(IByteChunk& chunk) const override;
	int UnserializeState(const IByteChunk& chunk, int startPos) override;

	// --- the engine, for the UI and tests (main thread) ------------------
	fg_engine* Engine() { return mEngine; }
	// Queue a sample file for a track; loaded on the idle thread.
	void RequestLoadSample(int track, const std::string& path);
	// Where relative sample paths resolve (RF_DATA_DIR/samples on the
	// appliance, the app-support folder elsewhere).
	std::string SamplesDir() const;
	void PlayAll();
	void StopAll();
	int PendingLoadCount() const { return (int)mPendingLoads.size(); }
	// Bounce the session to <data dir>/exports/frog-<stamp>.wav on a worker
	// thread (its own engine, samples reloaded from their paths). One at a time.
	// tracks in use (1..FG_MAX_TRACKS); the engine always runs FG_MAX_TRACKS
	int NumTracks() const { return mNumTracks; }
	bool AddTrack();
	bool RemoveTrack();              // the last one: pattern reset, sample unloaded
	bool DuplicateTrack(int from);   // appended; pattern copied, sample reloaded by path
	bool Bounce();
	bool Bouncing() const { return mBounceRunning.load(); }
	const std::string& LastBounce() const { return mBounceResult; }   // path, or an error after "!"
	std::string ExportsDir() const;
	const std::string& TrackPath(int track) const { return mTrackPaths[track]; }
	const char* TrackName(int track) const { return mTrackNames[track].c_str(); }

#if IPLUG_EDITOR
	// frogui::Host
	void Publish(int track) override { fg_engine_publish(mEngine, track); }
	void StartBounce() override { Bounce(); }
	int TrackCount() const override { return mNumTracks; }
	int& TrackCountRef() override { return mNumTracks; }
	bool AddTrackUI() override { return AddTrack(); }
	bool RemoveTrackUI() override { return RemoveTrack(); }
	bool DuplicateTrackUI(int from) override { return DuplicateTrack(from); }
	bool TrackHasSample(int track) const override { return fg_engine_has_sample(mEngine, track); }
	bool TrackMuted(int track) const override { return GetParam(TrackParam(track, kTrackMute))->Bool(); }
	const char* BounceStatus() const override;
	const frogui::Peaks& TrackPeaks(int track) const override { return mPeaks[track]; }
	void LoadSample(int track, const std::string& path) override { RequestLoadSample(track, path); }
	int& NextColor(int track) override { return mNextColor[track]; }
	fg_edit_mode& EditMode() override { return mEditMode; }
	const fg_visual* CurrentNote(int track) const override;
	int RecentFlashes(int track, const fg_flash** out) const override {
		*out = mFlashRing[track];
		return mFlashCount[track];
	}
	int ParamBpm() const override { return kParamBpm; }
	int ParamClock() const override { return kParamClockSource; }
	int ParamTrackVolume(int track) const override { return TrackParam(track, kTrackVolume); }
	int ParamTrackPan(int track) const override { return TrackParam(track, kTrackPan); }
	int ParamTrackMute(int track) const override { return TrackParam(track, kTrackMute); }
	void OnUIClose() override;
#endif

#if IPLUG_DSP
	void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
	void ProcessMidiMsg(const IMidiMsg& msg) override;
	void OnReset() override;
	void OnParamChange(int paramIdx) override;
	void OnIdle() override;
#endif

private:
	struct PendingLoad {
		int track;
		std::string path;
	};
	struct Retired {
		int track;
		fg_sample* sample;
	};

	void SnapshotSession(fg_session& out) const;
	void ApplySession(const fg_session& s);
	void ServiceLoads();
	void ServiceRetired();
	void PollVisuals();
	void ServiceBounce();

	fg_engine* mEngine = nullptr;
	std::vector<PendingLoad> mPendingLoads;   // main thread only
	std::vector<Retired> mRetired;            // main thread only
	std::string mTrackNames[FG_MAX_TRACKS];   // display names of loaded samples
	std::string mTrackPaths[FG_MAX_TRACKS];   // as stored in the session
	int mNextColor[FG_MAX_TRACKS] = {0};
	fg_edit_mode mEditMode = FG_EDIT_PACK;
	int mNumTracks = 1;
	double mSampleRate = 48000.;
	std::thread mBounceThread;
	std::atomic<bool> mBounceRunning{false};
	std::atomic<int> mBounceDone{0};   // 1 ok, -1 failed, 0 none
	std::string mBouncePath;           // written by the worker before mBounceDone
	std::string mBounceResult;
	std::string mBounceStatus = "Export";
	// the recent slots per track, from the engine's visual ring (main thread)
	static constexpr int kNoteRing = 32;
	fg_visual mNotes[FG_MAX_TRACKS][kNoteRing];
	int mNoteHead[FG_MAX_TRACKS] = {0};
	int mNoteCount[FG_MAX_TRACKS] = {0};
	static constexpr int kFlashRing = 16;
	fg_flash mFlashRing[FG_MAX_TRACKS][kFlashRing];
	int mFlashHead[FG_MAX_TRACKS] = {0};
	int mFlashCount[FG_MAX_TRACKS] = {0};
#if IPLUG_EDITOR
	frogui::Peaks mPeaks[FG_MAX_TRACKS];
	std::unique_ptr<frogui::FrogView> mView;
#endif
};
