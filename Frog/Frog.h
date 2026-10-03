#pragma once

#include "IPlug_include_in_plug_hdr.h"

#include "engine/engine.h"
#include "engine/frog_types.h"

#include <string>
#include <vector>

// Frog — sample slicer and loop performer. The plugin class is the shell
// around the C engine (engine/): parameters, state, MIDI, the host clock,
// sample loading on the idle thread, and (F10) the UI.

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

class Frog final : public Plugin {
public:
	Frog(const InstanceInfo& info);
	~Frog();

	// --- state ---------------------------------------------------------
	bool SerializeState(IByteChunk& chunk) const override;
	int UnserializeState(const IByteChunk& chunk, int startPos) override;

	// --- the engine, for the UI (main thread) ---------------------------
	fg_engine* Engine() { return mEngine; }
	// Queue a sample file for a track; loaded on the idle thread.
	void RequestLoadSample(int track, const std::string& path);
	// Where relative sample paths resolve (RF_DATA_DIR/samples on the
	// appliance, the app-support folder elsewhere).
	std::string SamplesDir() const;
	void PlayAll();
	void StopAll();
	int PendingLoadCount() const { return (int)mPendingLoads.size(); }
	const std::string& TrackPath(int track) const { return mTrackPaths[track]; }
	const std::string& TrackName(int track) const { return mTrackNames[track]; }

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

	fg_engine* mEngine = nullptr;
	std::vector<PendingLoad> mPendingLoads;   // main thread only
	std::vector<Retired> mRetired;            // main thread only
	std::string mTrackNames[FG_MAX_TRACKS];   // display names of loaded samples
	std::string mTrackPaths[FG_MAX_TRACKS];   // as stored in the session
	fg_edit_mode mEditMode = FG_EDIT_PACK;
	double mSampleRate = 48000.;
};
