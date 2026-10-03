#include "Frog.h"
#include "IPlug_include_in_plug_src.h"
#include "IPlugPaths.h"

#if IPLUG_EDITOR
#include "IControls.h"
#include "ui/FrogVersion.h"
#endif

#include "engine/sample.h"
#include "engine/session.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

// State chunk layout: magic, then iPlug2's parameter block, then the session
// JSON (the browser version's save object plus `samplePath` per track).
static const char kStateMagic[] = "FROGS001";

Frog::Frog(const InstanceInfo& info)
    : Plugin(info, MakeConfig(kNumParams, kNumPresets)) {
	GetParam(kParamBpm)->InitDouble("BPM", 120., 20., 400., 0.01, "bpm");
	GetParam(kParamMasterGain)->InitGain("Master", 0., -60., 6., 0.1);
	GetParam(kParamClockSource)->InitEnum("Clock", FG_CLOCK_INTERNAL, {"Internal", "MIDI", "Host"});
	for (int t = 0; t < FG_MAX_TRACKS; t++) {
		char name[32];
		snprintf(name, sizeof name, "T%d Vol", t + 1);
		GetParam(TrackParam(t, kTrackVolume))->InitDouble(name, 100., 0., 100., 0.1, "%");
		snprintf(name, sizeof name, "T%d Pan", t + 1);
		GetParam(TrackParam(t, kTrackPan))->InitDouble(name, 0., -100., 100., 1., "%");
		snprintf(name, sizeof name, "T%d Mute", t + 1);
		GetParam(TrackParam(t, kTrackMute))->InitBool(name, false);
	}

	mEngine = fg_engine_create(FG_MAX_TRACKS, 1);

#if IPLUG_EDITOR
	mMakeGraphicsFunc = [&]() {
		return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, GetScaleForScreen(PLUG_WIDTH, PLUG_HEIGHT));
	};

	mLayoutFunc = [&](IGraphics* pGraphics) {
		// Placeholder panel until F10: wordmark + build stamp, nothing else.
		pGraphics->AttachCornerResizer(EUIResizerMode::Scale, false);
		pGraphics->AttachPanelBackground(IColor(255, 24, 26, 30));
		pGraphics->LoadFont("Roboto-Regular", ROBOTO_FN);
		const IRECT b = pGraphics->GetBounds();
		const IText title(48.f, IColor(255, 230, 232, 236), "Roboto-Regular", EAlign::Center, EVAlign::Middle);
		pGraphics->AttachControl(new ITextControl(b.GetCentredInside(400.f, 80.f).GetTranslated(0.f, -20.f), "Frog", title));
		pGraphics->AttachControl(new FrogVersionReadout(b.GetCentredInside(400.f, 24.f).GetTranslated(0.f, 30.f)), kCtrlTagVersion);
	};
#endif
}

Frog::~Frog() {
	// Audio is stopped by the time the plugin is destroyed: every store the
	// engine still holds, and every retired one, is ours to free.
	for (int t = 0; t < FG_MAX_TRACKS && mEngine; t++) {
		fg_sample_free(fg_engine_set_sample(mEngine, t, nullptr));
	}
	for (auto& r : mRetired) {
		fg_sample_free(r.sample);
	}
	fg_engine_destroy(mEngine);
}

// --- paths --------------------------------------------------------------

std::string Frog::SamplesDir() const {
	if (const char* env = getenv("FROG_SAMPLES_DIR")) {
		return env;
	}
	if (const char* rf = getenv("RF_DATA_DIR")) {
		return std::string(rf) + "/samples";
	}
	WDL_String dir;
	AppSupportPath(dir, false);
	dir.Append("/Frog/samples");
	return dir.Get();
}

static std::string ResolvePath(const std::string& dir, const std::string& p) {
	if (p.empty() || p[0] == '/') {
		return p;
	}
	return dir + "/" + p;
}

// --- session snapshot / apply ------------------------------------------

void Frog::SnapshotSession(fg_session& out) const {
	fg_session_init(&out);
	out.nTracks = FG_MAX_TRACKS;
	out.masterBpm = GetParam(kParamBpm)->Value();
	out.masterUserSet = true;
	out.editMode = (uint8_t)mEditMode;
	out.nextTrackId = FG_MAX_TRACKS;
	for (int t = 0; t < FG_MAX_TRACKS; t++) {
		fg_track_state& ts = out.tracks[t];
		memset(&ts, 0, sizeof ts);
		ts.id = t;
		snprintf(ts.fileName, sizeof ts.fileName, "%s", mTrackNames[t].c_str());
		snprintf(ts.samplePath, sizeof ts.samplePath, "%s", mTrackPaths[t].c_str());
		ts.bpm = out.masterBpm;
		ts.mix.volume = GetParam(TrackParam(t, kTrackVolume))->Value() / 100.;
		ts.mix.pan = GetParam(TrackParam(t, kTrackPan))->Value() / 100.;
		ts.mix.muted = GetParam(TrackParam(t, kTrackMute))->Bool();
		const fg_pattern* p = fg_engine_pattern(const_cast<fg_engine*>(mEngine), t);
		ts.pattern = *p;
	}
}

void Frog::ApplySession(const fg_session& s) {
	const int n = s.nTracks < FG_MAX_TRACKS ? s.nTracks : FG_MAX_TRACKS;
	mEditMode = (fg_edit_mode)s.editMode;
	for (int t = 0; t < n; t++) {
		const fg_track_state& ts = s.tracks[t];
		*fg_engine_pattern(mEngine, t) = ts.pattern;
		fg_engine_publish(mEngine, t);
		mTrackNames[t] = ts.fileName;
		mTrackPaths[t] = ts.samplePath[0] ? ts.samplePath : ts.fileName;
		if (!mTrackPaths[t].empty()) {
			RequestLoadSample(t, mTrackPaths[t]);
		}
	}
	// Mix lives in the parameters; the browser session's values seed them.
	for (int t = 0; t < n; t++) {
		const fg_track_state& ts = s.tracks[t];
		GetParam(TrackParam(t, kTrackVolume))->Set(ts.mix.volume * 100.);
		GetParam(TrackParam(t, kTrackPan))->Set(ts.mix.pan * 100.);
		GetParam(TrackParam(t, kTrackMute))->Set(ts.mix.muted ? 1. : 0.);
	}
	if (s.masterBpm > 0.) {
		GetParam(kParamBpm)->Set(s.masterBpm);
	}
}

bool Frog::SerializeState(IByteChunk& chunk) const {
	chunk.PutBytes(kStateMagic, 8);
	if (!SerializeParams(chunk)) {
		return false;
	}
	fg_session* s = (fg_session*)calloc(1, sizeof(fg_session));
	SnapshotSession(*s);
	char* json = fg_session_write(s);
	free(s);
	if (!json) {
		return false;
	}
	const int len = (int)strlen(json);
	chunk.Put(&len);
	chunk.PutBytes(json, len);
	free(json);
	return true;
}

int Frog::UnserializeState(const IByteChunk& chunk, int startPos) {
	char magic[8];
	int pos = chunk.GetBytes(magic, 8, startPos);
	if (pos < 0 || memcmp(magic, kStateMagic, 8) != 0) {
		// not ours (or an older layout): take the parameters only
		return UnserializeParams(chunk, startPos);
	}
	pos = UnserializeParams(chunk, pos);
	int len = 0;
	pos = chunk.Get(&len, pos);
	if (pos < 0 || len <= 0 || len > (16 << 20)) {
		return pos;
	}
	std::string json((size_t)len, '\0');
	pos = chunk.GetBytes(&json[0], len, pos);
	if (pos < 0) {
		return pos;
	}
	fg_session* s = (fg_session*)calloc(1, sizeof(fg_session));
	if (fg_session_parse(json.c_str(), s)) {
		ApplySession(*s);
	}
	free(s);
	OnParamReset(kPresetRecall);
	return pos;
}

// --- samples ------------------------------------------------------------

void Frog::RequestLoadSample(int track, const std::string& path) {
	if (track < 0 || track >= FG_MAX_TRACKS) {
		return;
	}
	for (auto& p : mPendingLoads) {
		if (p.track == track) {
			p.path = path;
			return;
		}
	}
	mPendingLoads.push_back({track, path});
}

void Frog::ServiceLoads() {
	// One file per idle tick: decoding and resampling a long file is the
	// kind of work that must never block a block.
	if (mPendingLoads.empty()) {
		return;
	}
	PendingLoad job = mPendingLoads.front();
	mPendingLoads.erase(mPendingLoads.begin());
	const std::string full = ResolvePath(SamplesDir(), job.path);
	fg_sample* s = fg_sample_load_wav(full.c_str(), mSampleRate, 0.);
	if (!s) {
		DBGMSG("Frog: cannot load %s\n", full.c_str());
		return;
	}
	fg_sample* old = fg_engine_set_sample(mEngine, job.track, s);
	if (old) {
		mRetired.push_back({job.track, old});
	}
	mTrackPaths[job.track] = job.path;
	mTrackNames[job.track] = s->name;
}

void Frog::ServiceRetired() {
	for (size_t i = 0; i < mRetired.size();) {
		if (fg_engine_sample_retired(mEngine, mRetired[i].track, mRetired[i].sample)) {
			fg_sample_free(mRetired[i].sample);
			mRetired.erase(mRetired.begin() + (long)i);
		} else {
			i++;
		}
	}
}

void Frog::PlayAll() {
	fg_engine_play_all(mEngine);
}

void Frog::StopAll() {
	fg_engine_stop_all(mEngine);
}

#if IPLUG_DSP
void Frog::ProcessBlock(sample** inputs, sample** outputs, int nFrames) {
	if (GetParam(kParamClockSource)->Int() == FG_CLOCK_HOST) {
		fg_engine_host_transport(mEngine, mTimeInfo.mTempo, mTimeInfo.mPPQPos, mTimeInfo.mTransportIsRunning);
	}
	const int nCh = NOutChansConnected();
	fg_engine_process(mEngine, outputs, nCh > 2 ? 2 : nCh, nFrames);
	for (int c = 2; c < nCh; c++) {
		memset(outputs[c], 0, sizeof(sample) * (size_t)nFrames);
	}
}

void Frog::ProcessMidiMsg(const IMidiMsg& msg) {
	const int status = msg.mStatus;
	if (status >= 0xF8) {
		fg_engine_midi(mEngine, (uint8_t)status, msg.mOffset);
	}
	// notes and CCs: F16
}

void Frog::OnReset() {
	mSampleRate = GetSampleRate();
	const int block = GetBlockSize() > 4096 ? GetBlockSize() : 4096;
	fg_engine_reset(mEngine, mSampleRate, block);
	// a rate change invalidates every store: reload at the new rate
	for (int t = 0; t < FG_MAX_TRACKS; t++) {
		if (!mTrackPaths[t].empty()) {
			RequestLoadSample(t, mTrackPaths[t]);
		}
	}
	fg_engine_set_tempo(mEngine, GetParam(kParamBpm)->Value());
	fg_engine_set_clock_source(mEngine, GetParam(kParamClockSource)->Int());
	for (int t = 0; t < FG_MAX_TRACKS; t++) {
		fg_engine_set_mix(mEngine, t, GetParam(TrackParam(t, kTrackVolume))->Value() / 100., GetParam(TrackParam(t, kTrackPan))->Value() / 100.);
		fg_engine_set_mute(mEngine, t, GetParam(TrackParam(t, kTrackMute))->Bool());
	}
}

void Frog::OnParamChange(int paramIdx) {
	switch (paramIdx) {
		case kParamBpm:
			if (GetParam(kParamClockSource)->Int() == FG_CLOCK_INTERNAL) {
				fg_engine_set_tempo(mEngine, GetParam(kParamBpm)->Value());
			}
			break;
		case kParamMasterGain:
			fg_engine_set_master_gain(mEngine, std::pow(10., GetParam(kParamMasterGain)->Value() / 20.));
			break;
		case kParamClockSource:
			fg_engine_set_clock_source(mEngine, GetParam(kParamClockSource)->Int());
			if (GetParam(kParamClockSource)->Int() == FG_CLOCK_INTERNAL) {
				fg_engine_set_tempo(mEngine, GetParam(kParamBpm)->Value());
			}
			break;
		default:
			if (paramIdx >= kParamTrackBase) {
				const int t = (paramIdx - kParamTrackBase) / kNumTrackParams;
				const int which = (paramIdx - kParamTrackBase) % kNumTrackParams;
				if (which == kTrackMute) {
					fg_engine_set_mute(mEngine, t, GetParam(paramIdx)->Bool());
				} else {
					fg_engine_set_mix(mEngine, t, GetParam(TrackParam(t, kTrackVolume))->Value() / 100., GetParam(TrackParam(t, kTrackPan))->Value() / 100.);
				}
			}
			break;
	}
}

void Frog::OnIdle() {
	ServiceLoads();
	ServiceRetired();
	for (int t = 0; t < FG_MAX_TRACKS; t++) {
		fg_engine_take_pattern_change(mEngine, t);   // keeps the working copy current (UI reads it, F10)
	}
}
#endif
