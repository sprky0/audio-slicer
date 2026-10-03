#pragma once

#include "IPlug_include_in_plug_hdr.h"

#include "engine/engine.h"
#include "engine/frog_types.h"
#include "engine/midimap.h"
#include "engine/pattern.h"

#include "IPlugQueue.h"

#if IPLUG_EDITOR
#include "ui/FrogView.h"
#include "ui/Peaks.h"
#endif

#include <atomic>
#include <chrono>
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

// methods the view reaches through frogui::Host; plain methods in editor-less builds
#if IPLUG_EDITOR
#define FROG_HOST override
#else
#define FROG_HOST
#endif

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
	// Record the live inputs into a track (desktop formats with inputs; the
	// appliance has none until its host grows capture). Stop writes the take
	// to the samples dir as rec-<stamp>.wav and loads it into the track.
	bool StartRecord(int track);
	void StopRecord();
	bool Recording() const;
	double RecordSeconds() const;
	// Sessions on disk (<data dir>/sessions/*.json) double as the appliance's
	// presets: StepPreset / Program Change move through them, loads land on
	// the idle tick. Save writes the current state under `name` (or a stamp).
	std::string SessionsDir() const;
	bool SaveSession(const std::string& name);          // "" → session-<stamp>
	void RequestLoadSession(const std::string& path);   // loaded on the idle tick
	bool StepPreset(int delta);
	std::string GetCurrentPresetName() const { return mSessionName.empty() ? "unsaved" : mSessionName; }
	int GetCurrentPresetBank() const { return 0; }
	int GetCurrentPresetProgram() const { return mSessionIndex; }
	bool Bounce();
	bool Bouncing() const { return mBounceRunning.load(); }
	const std::string& LastBounce() const { return mBounceResult; }   // path, or an error after "!"
	std::string ExportsDir() const;
	const std::string& TrackPath(int track) const { return mTrackPaths[track]; }
	const char* TrackName(int track) const { return mTrackNames[track].c_str(); }
	// <data dir>: FROG_DATA_DIR, else RF_DATA_DIR (appliance), else app support.
	// samples/, exports/, sessions/ and midimap.json live under it.
	std::string DataDir() const;
	// --- MIDI CC map (F16.2) --------------------------------------------------
	// CCs queue from ProcessMidiMsg and are applied on the idle tick: a learn
	// armed for a hook binds the next CC (any channel) and saves the map;
	// otherwise the binding's hook is driven (the view's control when the
	// editor is open, the plugin's own fallbacks for mix / transport /
	// pattern hooks when it is not). Track hooks act on the focused track.
	std::string MidiMapPath() const;
	const fg_midimap& MidiMap() const { return mMap; }
	void ArmLearn(const char* hook) FROG_HOST;
	const char* LearnArmed() const FROG_HOST { return mLearnHook.c_str(); }
	void ClearBinding(const char* hook) FROG_HOST;
	bool HookBound(const char* hook) const FROG_HOST { return fg_midimap_is_bound(&mMap, hook); }
	int MapRevision() const FROG_HOST { return mMapRevision; }
	void FocusChanged(int track) FROG_HOST { mFocusTrack = track; }
	int FocusTrack() const FROG_HOST { return mFocusTrack; }

#if IPLUG_EDITOR
	// frogui::Host
	void Publish(int track) override { fg_engine_publish(mEngine, track); }
	void StartBounce() override { Bounce(); }
	void SaveSessionUI() override { SaveSession(mSessionName); }
	void LoadSessionUI(const std::string& path) override { RequestLoadSession(path); }
	std::string SessionsDirUI() const override { return SessionsDir(); }
	const char* SessionName() const override { return mSessionName.c_str(); }
	void ToggleRecord(int track) override;
	bool IsRecording() const override { return Recording(); }
	double RecordedSeconds() const override { return RecordSeconds(); }
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
	void ServiceRecord();
	void ServiceSession();
	void ScanSessions();
	void ServiceMidiMap();
	bool ApplyHook(const char* name, double norm, bool press);
	void SetParamFromMidi(int idx, double norm);
	void LoadMidiMap();
	void SaveMidiMap();

	fg_engine* mEngine = nullptr;
	std::vector<PendingLoad> mPendingLoads;   // main thread only
	std::vector<Retired> mRetired;            // main thread only
	std::string mTrackNames[FG_MAX_TRACKS];   // display names of loaded samples
	std::string mTrackPaths[FG_MAX_TRACKS];   // as stored in the session
	int mNextColor[FG_MAX_TRACKS] = {0};
	fg_edit_mode mEditMode = FG_EDIT_PACK;
	int mNumTracks = 1;
	double mSampleRate = 48000.;
	std::vector<std::string> mSessions;   // file names in SessionsDir(), sorted
	int mSessionIndex = -1;
	std::string mSessionName;             // the loaded / saved session's name (no extension)
	std::string mPendingSession;          // path queued for the idle tick
	std::atomic<int> mPendingProgram{-1}; // from MIDI Program Change
	IPlugQueue<IMidiMsg> mCcQueue{256};   // CCs, ProcessMidiMsg → idle tick
	fg_midimap mMap;
	int mMapRevision = 0;
	std::string mLearnHook;               // the hook learn is armed for, or ""
	std::chrono::steady_clock::time_point mLearnSince;
	int mFocusTrack = 0;
	fg_rng mRng;
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
