#include "Frog.h"
#include "IPlug_include_in_plug_src.h"
#include "IPlugPaths.h"

#include "engine/render.h"
#include "engine/sample.h"
#include "engine/session.h"

#include <algorithm>
#include <ctime>
#include <dirent.h>
#include <sys/stat.h>

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
	for (int t = 0; t < FG_MAX_TRACKS; t++) {
		mNextColor[t] = fg_unit_count(fg_engine_pattern(mEngine, t));
	}
	// Developer hooks: FROG_AUTOLOAD=<wav> loads into track 1 at start and
	// FROG_AUTOPLAY=1 presses Play once it has loaded (screenshots, benches).
	if (const char* auto1 = getenv("FROG_AUTOLOAD")) {
		RequestLoadSample(0, auto1);
	}

#if IPLUG_EDITOR
	mMakeGraphicsFunc = [&]() {
		return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, GetScaleForScreen(PLUG_WIDTH, PLUG_HEIGHT));
	};

	mLayoutFunc = [&](IGraphics* pGraphics) {
		if (pGraphics->NControls() && mView) {
			mView->Layout(pGraphics);   // a resize: same controls, new rects
			return;
		}
		pGraphics->AttachCornerResizer(EUIResizerMode::Size, false);
		pGraphics->AttachPanelBackground(frogui::kBg);
		pGraphics->LoadFont(frogui::kFont, ROBOTO_FN);
		mView = std::make_unique<frogui::FrogView>(*this, pGraphics);
	};
#endif
}

Frog::~Frog() {
	if (mBounceThread.joinable()) {
		mBounceThread.join();
	}
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

#if IPLUG_EDITOR
void Frog::OnUIClose() {
	mView.reset();   // the controls go with the graphics context
}

const fg_visual* Frog::CurrentNote(int track) const {
	if (track < 0 || track >= FG_MAX_TRACKS || mNoteCount[track] == 0) {
		return nullptr;
	}
	const int64_t now = fg_engine_now(mEngine);
	const fg_visual* best = nullptr;
	for (int k = 0; k < mNoteCount[track]; k++) {
		const fg_visual& v = mNotes[track][(mNoteHead[track] - 1 - k + kNoteRing * 2) % kNoteRing];
		if (v.start <= now && (!best || v.start > best->start)) {
			best = &v;
		}
	}
	return best;
}
#endif

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

std::string Frog::ExportsDir() const {
	if (const char* rf = getenv("RF_DATA_DIR")) {
		return std::string(rf) + "/exports";
	}
	WDL_String dir;
	AppSupportPath(dir, false);
	dir.Append("/Frog/exports");
	return dir.Get();
}

static void EnsureDir(const std::string& path) {
	std::string acc;
	for (size_t i = 1; i <= path.size(); i++) {
		if (i == path.size() || path[i] == '/') {
			acc = path.substr(0, i);
			mkdir(acc.c_str(), 0755);
		}
	}
}

// --- tracks --------------------------------------------------------------

bool Frog::AddTrack() {
	if (mNumTracks >= FG_MAX_TRACKS) {
		return false;
	}
	const int t = mNumTracks++;
	fg_pattern* p = fg_engine_pattern(mEngine, t);
	fg_pattern_init(p);
	fg_pattern_default_tiles(p);
	fg_engine_publish(mEngine, t);
	mNextColor[t] = fg_unit_count(p);
	return true;
}

bool Frog::RemoveTrack() {
	if (mNumTracks <= 1) {
		return false;
	}
	const int t = --mNumTracks;
	fg_pattern* p = fg_engine_pattern(mEngine, t);
	fg_pattern_init(p);
	fg_pattern_default_tiles(p);
	fg_engine_publish(mEngine, t);
	if (fg_sample* old = fg_engine_set_sample(mEngine, t, nullptr)) {
		mRetired.push_back({t, old});
	}
	mTrackNames[t].clear();
	mTrackPaths[t].clear();
#if IPLUG_EDITOR
	mPeaks[t] = frogui::Peaks();
#endif
	GetParam(TrackParam(t, kTrackVolume))->Set(100.);
	GetParam(TrackParam(t, kTrackPan))->Set(0.);
	GetParam(TrackParam(t, kTrackMute))->Set(0.);
	for (int w = 0; w < kNumTrackParams; w++) {
		OnParamChange(TrackParam(t, (ETrackParam)w));
	}
	return true;
}

bool Frog::DuplicateTrack(int from) {
	if (from < 0 || from >= mNumTracks || mNumTracks >= FG_MAX_TRACKS) {
		return false;
	}
	const int t = mNumTracks++;
	*fg_engine_pattern(mEngine, t) = *fg_engine_pattern(mEngine, from);
	fg_engine_publish(mEngine, t);
	mNextColor[t] = mNextColor[from];
	mTrackNames[t] = mTrackNames[from];
	if (!mTrackPaths[from].empty()) {
		RequestLoadSample(t, mTrackPaths[from]);
	}
	GetParam(TrackParam(t, kTrackVolume))->Set(GetParam(TrackParam(from, kTrackVolume))->Value());
	GetParam(TrackParam(t, kTrackPan))->Set(GetParam(TrackParam(from, kTrackPan))->Value());
	GetParam(TrackParam(t, kTrackMute))->Set(GetParam(TrackParam(from, kTrackMute))->Value());
	for (int w = 0; w < kNumTrackParams; w++) {
		OnParamChange(TrackParam(t, (ETrackParam)w));
	}
	return true;
}

// --- sessions ------------------------------------------------------------

std::string Frog::SessionsDir() const {
	if (const char* rf = getenv("RF_DATA_DIR")) {
		return std::string(rf) + "/sessions";
	}
	WDL_String dir;
	AppSupportPath(dir, false);
	dir.Append("/Frog/sessions");
	return dir.Get();
}

void Frog::ScanSessions() {
	mSessions.clear();
	if (DIR* d = opendir(SessionsDir().c_str())) {
		while (const dirent* e = readdir(d)) {
			const std::string n = e->d_name;
			if (n.size() > 5 && n.compare(n.size() - 5, 5, ".json") == 0 && n[0] != '.') {
				mSessions.push_back(n);
			}
		}
		closedir(d);
	}
	std::sort(mSessions.begin(), mSessions.end());
	mSessionIndex = -1;
	for (size_t i = 0; i < mSessions.size(); i++) {
		if (mSessions[i] == mSessionName + ".json") {
			mSessionIndex = (int)i;
		}
	}
}

bool Frog::SaveSession(const std::string& name) {
	const std::string dir = SessionsDir();
	EnsureDir(dir);
	std::string n = name;
	if (n.empty()) {
		char stamp[32];
		const time_t now = time(nullptr);
		strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", localtime(&now));
		n = std::string("session-") + stamp;
	}
	fg_session* s = (fg_session*)calloc(1, sizeof(fg_session));
	SnapshotSession(*s);
	const bool ok = fg_session_save_file((dir + "/" + n + ".json").c_str(), s);
	free(s);
	if (ok) {
		mSessionName = n;
		ScanSessions();
	}
	return ok;
}

void Frog::RequestLoadSession(const std::string& path) {
	mPendingSession = path;
}

bool Frog::StepPreset(int delta) {
	ScanSessions();
	if (mSessions.empty()) {
		return false;
	}
	const int n = (int)mSessions.size();
	int idx = mSessionIndex < 0 ? (delta > 0 ? -1 : 0) : mSessionIndex;
	idx = ((idx + delta) % n + n) % n;
	mPendingProgram.store(idx);
	return true;
}

void Frog::ServiceSession() {
	const int prog = mPendingProgram.exchange(-1);
	if (prog >= 0) {
		if (mSessions.empty()) {
			ScanSessions();
		}
		if (prog < (int)mSessions.size()) {
			mPendingSession = SessionsDir() + "/" + mSessions[(size_t)prog];
		}
	}
	if (mPendingSession.empty()) {
		return;
	}
	const std::string path = mPendingSession;
	mPendingSession.clear();
	fg_session* s = (fg_session*)calloc(1, sizeof(fg_session));
	if (fg_session_load_file(path.c_str(), s)) {
		ApplySession(*s);
		OnParamReset(kPresetRecall);
		const size_t slash = path.rfind('/');
		std::string n = slash == std::string::npos ? path : path.substr(slash + 1);
		if (n.size() > 5) {
			n = n.substr(0, n.size() - 5);
		}
		mSessionName = n;
		ScanSessions();
	}
	free(s);
}

// --- record --------------------------------------------------------------

bool Frog::StartRecord(int track) {
	return fg_engine_record_arm(mEngine, track, 120.0);
}

void Frog::StopRecord() {
	fg_engine_record_stop(mEngine);
}

bool Frog::Recording() const {
	return fg_engine_recording(mEngine);
}

double Frog::RecordSeconds() const {
	return (double)fg_engine_record_frames(mEngine) / mSampleRate;
}

#if IPLUG_EDITOR
void Frog::ToggleRecord(int track) {
	if (Recording()) {
		StopRecord();
	} else {
		StartRecord(track);
	}
}
#endif

void Frog::ServiceRecord() {
	if (!fg_engine_record_done(mEngine)) {
		return;
	}
	int track = -1;
	fg_sample* take = fg_engine_record_take(mEngine, &track);
	if (!take) {
		return;
	}
	// the take goes to disk so the session can find it again
	const std::string dir = SamplesDir();
	EnsureDir(dir);
	char stamp[32];
	const time_t now = time(nullptr);
	strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", localtime(&now));
	const std::string name = std::string("rec-") + stamp + ".wav";
	std::vector<double> l((size_t)take->frames), r((size_t)take->frames);
	for (int64_t i = 0; i < take->frames; i++) {
		l[(size_t)i] = take->ch[0][i];
		r[(size_t)i] = take->ch[1][i];
	}
	const double* ch[2] = {l.data(), r.data()};
	fg_sample_write_wav16((dir + "/" + name).c_str(), ch, 2, take->frames, take->sampleRate);
	snprintf(take->name, sizeof take->name, "%s", name.c_str());
	fg_sample* old = fg_engine_set_sample(mEngine, track, take);
	if (old) {
		mRetired.push_back({track, old});
	}
	mTrackPaths[track] = name;   // relative to the samples dir
	mTrackNames[track] = name;
	fg_pattern* p = fg_engine_pattern(mEngine, track);
	p->start = 0.0;
	p->end = 1.0;
	p->virtualStart = 0.0;
	p->virtualEnd = 1.0;
	if (p->nTiles == 0) {
		fg_pattern_default_tiles(p);
	}
	fg_engine_publish(mEngine, track);
#if IPLUG_EDITOR
	mPeaks[track].Build(take);
	if (mView) {
		mView->SampleChanged(track);
	}
#endif
}

// --- bounce --------------------------------------------------------------

bool Frog::Bounce() {
	if (mBounceRunning.load()) {
		return false;
	}
	if (mBounceThread.joinable()) {
		mBounceThread.join();
	}
	fg_session* s = (fg_session*)calloc(1, sizeof(fg_session));
	SnapshotSession(*s);
	// paths are resolved against the samples dir; absolute ones pass through
	const std::string samplesDir = SamplesDir();
	const std::string exportsDir = ExportsDir();
	EnsureDir(exportsDir);
	char stamp[32];
	const time_t now = time(nullptr);
	strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", localtime(&now));
	mBouncePath = exportsDir + "/frog-" + stamp + ".wav";
	const double sr = mSampleRate;
	mBounceRunning.store(true);
	mBounceDone.store(0);
	mBounceStatus = "Exporting";
	mBounceThread = std::thread([this, s, samplesDir, sr] {
		fg_render_opts o = {0};
		o.sampleRate = sr;
		o.blockSize = 128;
		o.normalize = true;
		o.seed = (uint32_t)time(nullptr);
		o.samplesDir = samplesDir.c_str();
		fg_render_stats st = {0};
		const bool ok = fg_render_session(s, &o, mBouncePath.c_str(), &st);
		free(s);
		mBounceDone.store(ok ? 1 : -1);
		mBounceRunning.store(false);
	});
	return true;
}

void Frog::ServiceBounce() {
	const int done = mBounceDone.exchange(0);
	if (done == 0) {
		return;
	}
	if (mBounceThread.joinable()) {
		mBounceThread.join();
	}
	if (done > 0) {
		mBounceResult = mBouncePath;
		mBounceStatus = "Exported";
	} else {
		mBounceResult = "!" + mBouncePath;
		mBounceStatus = "Export failed";
	}
}

#if IPLUG_EDITOR
const char* Frog::BounceStatus() const {
	return mBounceStatus.c_str();
}
#endif

static std::string ResolvePath(const std::string& dir, const std::string& p) {
	if (p.empty() || p[0] == '/') {
		return p;
	}
	return dir + "/" + p;
}

// --- session snapshot / apply ------------------------------------------

void Frog::SnapshotSession(fg_session& out) const {
	fg_session_init(&out);
	out.nTracks = mNumTracks;
	out.masterBpm = GetParam(kParamBpm)->Value();
	out.masterUserSet = true;
	out.editMode = (uint8_t)mEditMode;
	out.nextTrackId = FG_MAX_TRACKS;
	for (int t = 0; t < mNumTracks; t++) {
		fg_track_state& ts = out.tracks[t];
		memset(&ts, 0, sizeof ts);
		ts.id = t;
		snprintf(ts.fileName, sizeof ts.fileName, "%s", mTrackNames[t].c_str());
		snprintf(ts.samplePath, sizeof ts.samplePath, "%s", mTrackPaths[t].c_str());
		ts.bpm = out.masterBpm;
		ts.mix.volume = GetParam(TrackParam(t, kTrackVolume))->Value() / 100.;
		ts.mix.pan = GetParam(TrackParam(t, kTrackPan))->Value() / 100.;
		ts.mix.muted = GetParam(TrackParam(t, kTrackMute))->Bool();
		ts.pattern = *fg_engine_pattern(const_cast<fg_engine*>(mEngine), t);
	}
}

void Frog::ApplySession(const fg_session& s) {
	const int n = s.nTracks < FG_MAX_TRACKS ? s.nTracks : FG_MAX_TRACKS;
	mEditMode = (fg_edit_mode)s.editMode;
	mNumTracks = n > 0 ? n : 1;
	for (int t = 0; t < n; t++) {
		const fg_track_state& ts = s.tracks[t];
		*fg_engine_pattern(mEngine, t) = ts.pattern;
		fg_engine_publish(mEngine, t);
		mNextColor[t] = fg_unit_count(&ts.pattern);
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
#if IPLUG_EDITOR
	if (mView) {
		for (int t = 0; t < n; t++) {
			mView->PatternChanged(t);
		}
	}
#endif
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
	if (getenv("FROG_AUTOPLAY") && job.track == 0) {
		fg_engine_play_all(mEngine);
	}
	// a new source means a fresh default row when the track had none
	fg_pattern* p = fg_engine_pattern(mEngine, job.track);
	if (p->nTiles == 0) {
		fg_pattern_default_tiles(p);
		fg_engine_publish(mEngine, job.track);
	}
#if IPLUG_EDITOR
	mPeaks[job.track].Build(s);
	if (mView) {
		mView->SampleChanged(job.track);
	}
#endif
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

void Frog::PollVisuals() {
	fg_visual buf[64];
	int n;
	while ((n = fg_engine_poll_visuals(mEngine, buf, 64)) > 0) {
		for (int i = 0; i < n; i++) {
			const int t = buf[i].track;
			if (t < 0 || t >= FG_MAX_TRACKS) {
				continue;
			}
			mNotes[t][mNoteHead[t]] = buf[i];
			mNoteHead[t] = (mNoteHead[t] + 1) % kNoteRing;
			if (mNoteCount[t] < kNoteRing) {
				mNoteCount[t]++;
			}
		}
	}
	fg_flash flashes[32];
	int nf;
	while ((nf = fg_engine_poll_flashes(mEngine, flashes, 32)) > 0) {
		for (int i = 0; i < nf; i++) {
			const int t = flashes[i].track;
			if (t < 0 || t >= FG_MAX_TRACKS) {
				continue;
			}
			mFlashRing[t][mFlashHead[t]] = flashes[i];
			mFlashHead[t] = (mFlashHead[t] + 1) % kFlashRing;
			if (mFlashCount[t] < kFlashRing) {
				mFlashCount[t]++;
			}
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
	if (fg_engine_recording(mEngine)) {
		const int nIn = NInChansConnected();
		fg_engine_capture(mEngine, nIn > 0 ? const_cast<const double* const*>(inputs) : nullptr, nIn, nFrames);
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
	} else if ((status & 0xF0) == 0xC0) {
		mPendingProgram.store(msg.mData1);   // a session, loaded on the idle tick
	} else if (status >= 0x80 && status < 0xF0) {
		fg_engine_midi_msg(mEngine, (uint8_t)status, (uint8_t)msg.mData1, (uint8_t)msg.mData2, msg.mOffset);
	}
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
	ServiceBounce();
	ServiceRecord();
	ServiceSession();
	PollVisuals();
	for (int t = 0; t < FG_MAX_TRACKS; t++) {
		if (fg_engine_take_pattern_change(mEngine, t)) {
#if IPLUG_EDITOR
			if (mView) {
				mView->PatternChanged(t);
			}
#endif
		}
	}
#if IPLUG_EDITOR
	if (mView) {
		mView->Idle();
	}
#endif
}
#endif
