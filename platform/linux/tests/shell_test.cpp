// The plugin shell as the appliance drives it (F9): headless Frog behind
// rflh::IPlug2HeadlessProcessor, no idle Timer. A sample requested for a
// track loads on the idle tick, Play All sounds it on the grid, a MIDI
// clock takes over the tempo and transport when the clock source says so,
// and the state chunk carries the pattern, mix and sample path into a
// fresh instance. Built by platform/linux/CMakeLists.txt (target
// shell_test) and run by docker-build-arm64.sh.

#include "Frog.h"
#include "FrogProcessor.h"
#include "engine/midimap.h"
#include "engine/sample.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

int gFail = 0;

void check(bool ok, const char* what, const std::string& detail = "") {
	std::printf("  %-4s %s %s\n", ok ? "PASS" : "FAIL", what, detail.c_str());
	if (!ok) {
		++gFail;
	}
}

// 16 units of 0.125 s at 48 k: an 8-sample burst of (i+1)/16 at each unit
// start. Eight samples, not one: the stretch stage's window zeroes exactly
// the first sample of a slice, so a one-sample impulse would vanish on the
// stretched paths (120 bpm plays raw; 100 and 133 bpm stretch).
const char* writeFixture() {
	static const char* path = "/tmp/frog-shell-test.wav";
	fg_sample* s = fg_sample_create(1, 96000, 48000.0);
	for (int i = 0; i < 16; i++) {
		for (int k = 0; k < 8; k++) {
			s->ch[0][i * 6000 + k] = (float)(i + 1) / 16.f;
		}
	}
	std::vector<double> d(96000);
	for (int i = 0; i < 96000; i++) {
		d[i] = s->ch[0][i];
	}
	const double* ch[1] = {d.data()};
	fg_sample_write_wav16(path, ch, 1, 96000, 48000.0);
	fg_sample_free(s);
	return path;
}

struct Rig {
	iplug::InstanceInfo info;
	Frog plug;
	FrogProcessor proc;
	std::vector<double> l, r;
	double* outs[2];
	std::vector<double> capture;   // left channel of every block processed

	Rig()
	    : info([] { iplug::InstanceInfo i; i.createIdleTimer = false; return i; }()),
	      plug(info), proc(plug, 2), l(128), r(128) {
		outs[0] = l.data();
		outs[1] = r.data();
		proc.reset(48000, 128);
	}
	void blocks(int n) {
		for (int b = 0; b < n; b++) {
			proc.process(outs, 2, 128);
			capture.insert(capture.end(), l.begin(), l.end());
		}
	}
	void midi(unsigned char status, int offset = 0) {
		proc.midi(&status, 1, offset);
	}
	double peakIn(size_t from, size_t to) {
		double p = 0.0;
		for (size_t i = from; i < to && i < capture.size(); i++) {
			p = std::fabs(capture[i]) > p ? std::fabs(capture[i]) : p;
		}
		return p;
	}
};

}  // namespace

int main() {
	setenv("FROG_NO_FACTORY", "1", 1);   // the first-run install is tested on its own below
	const char* wav = writeFixture();

	std::printf("=== load on idle, play on the grid ===\n");
	Rig a;
	a.blocks(4);
	check(a.peakIn(0, a.capture.size()) == 0.0, "silent before anything is loaded");
	a.plug.RequestLoadSample(0, wav);
	a.blocks(2);
	a.plug.PlayAll();
	a.blocks(2);
	check(a.peakIn(0, a.capture.size()) == 0.0, "still silent: the load waits for the idle tick, never the audio thread");
	a.plug.StopAll();
	a.proc.idle();
	a.blocks(1);
	a.capture.clear();
	a.plug.PlayAll();
	a.blocks(750);   // 96000 samples = one bar of 16 × 0.125 s at 120 bpm
	// mono source: the mono pan law puts −3 dB on each side
	const double m = std::sqrt(0.5);
	check(std::fabs(a.capture[0] - m * 1.0 / 16.0) < 1e-6, "unit 0's burst at the first sample, at the mono pan level");
	check(std::fabs(a.capture[6000 * 5] - m * 6.0 / 16.0) < 1e-6, "unit 5 at its grid slot");
	check(std::fabs(a.capture[6000 * 15] - m * 16.0 / 16.0) < 1e-4, "unit 15 at its grid slot (16-bit fixture: 1.0 is 32767/32768)");
	check(a.peakIn(8, 6000) == 0.0, "nothing between the bursts");

	std::printf("=== mix parameters reach the engine ===\n");
	a.plug.GetParam(TrackParam(0, kTrackVolume))->Set(50.);
	a.plug.OnParamChange(TrackParam(0, kTrackVolume));
	a.plug.StopAll();
	a.blocks(1);
	a.capture.clear();
	a.plug.PlayAll();
	a.blocks(2);
	check(std::fabs(a.capture[0] - 0.5 * m * 1.0 / 16.0) < 1e-6, "T1 Vol 50 % halves the level");
	a.plug.GetParam(TrackParam(0, kTrackMute))->Set(1.);
	a.plug.OnParamChange(TrackParam(0, kTrackMute));
	a.plug.StopAll();
	a.blocks(1);
	a.capture.clear();
	a.plug.PlayAll();
	a.blocks(2);
	check(a.peakIn(0, a.capture.size()) == 0.0, "T1 Mute silences the track");
	a.plug.GetParam(TrackParam(0, kTrackMute))->Set(0.);
	a.plug.OnParamChange(TrackParam(0, kTrackMute));
	a.plug.GetParam(TrackParam(0, kTrackVolume))->Set(100.);
	a.plug.OnParamChange(TrackParam(0, kTrackVolume));

	std::printf("=== MIDI clock drives tempo and transport ===\n");
	a.plug.StopAll();
	a.plug.GetParam(kParamClockSource)->Set(FG_CLOCK_MIDI);
	a.plug.OnParamChange(kParamClockSource);
	a.blocks(3);   // the unmute's 5 ms mix ramp settles while stopped
	a.capture.clear();
	a.midi(0xFA);   // Start: beat 0 now, tracks start
	// 100 bpm: 24 pulses per 0.6 s = one pulse every 1200 samples; one per block is 128, so pulses every ~9.4 blocks
	double nextPulse = 0.0;
	for (int b = 0; b < 400; b++) {
		const double blockStart = b * 128.0;
		while (nextPulse < blockStart + 128.0) {
			a.midi(0xF8, (int)(nextPulse - blockStart));   /* sample-accurate, as a plugin host delivers them */
			nextPulse += 1200.0;
		}
		a.blocks(1);
	}
	check(std::fabs(fg_engine_bpm(a.plug.Engine()) - 100.0) < 0.5, "tempo follows the pulses (100 bpm)");
	check(fg_engine_external_running(a.plug.Engine()), "external clock reports running");
	check(a.peakIn(0, 16) > 0.5 * m / 16.0, "Start played unit 0 at once");
	check(a.peakIn(16, 6900) == 0.0, "silence until the next slot");
	// The first slots fall early while the tempo is still being acquired
	// (the grid runs at the previous tempo until two pulses are in); by unit
	// 4 the grid sits on the 100 bpm spacing of 7200 samples, within the
	// PLL's 2 ms dead band.
	check(a.peakIn(4 * 7200 - 100, 4 * 7200 + 20) > 0.5 * m * 5.0 / 16.0, "unit 4 lands on the 100 bpm grid (within the PLL dead band)");
	check(a.peakIn(6 * 7200 - 100, 6 * 7200 + 20) > 0.5 * m * 7.0 / 16.0, "unit 6 too");
	check(a.peakIn(5 * 7200 + 20, 6 * 7200 - 100) == 0.0, "and silence between them");
	a.midi(0xFC);
	a.blocks(2);
	check(!fg_engine_external_running(a.plug.Engine()), "Stop halts the external clock");
	a.plug.GetParam(kParamClockSource)->Set(FG_CLOCK_INTERNAL);
	a.plug.OnParamChange(kParamClockSource);
	a.blocks(1);
	check(std::fabs(fg_engine_bpm(a.plug.Engine()) - 120.0) < 1e-9, "back to the internal clock: the BPM parameter rules again");

	std::printf("=== state chunk round trip ===\n");
	fg_pattern* p = fg_engine_pattern(a.plug.Engine(), 0);
	p->tiles[1].muted = true;
	p->tiles[2].reversed = true;
	p->nMods = 1;
	p->mods[0] = fg_mod{3, FG_MOD_RATCHET, FG_FIRE_PROB, FG_RATCHET_EVEN, 100, 0, 2, 2, 0, 1};
	fg_engine_publish(a.plug.Engine(), 0);
	a.plug.GetParam(kParamBpm)->Set(133.);
	a.plug.GetParam(TrackParam(0, kTrackPan))->Set(-25.);
	iplug::IByteChunk chunk;
	check(a.plug.SerializeState(chunk), "SerializeState succeeds");
	check(chunk.Size() > 1000, "the chunk carries the session JSON");

	Rig b;
	const int pos = b.plug.UnserializeState(chunk, 0);
	check(pos == chunk.Size(), "UnserializeState consumes the whole chunk");
	check(b.plug.PendingLoadCount() == 1 && b.plug.TrackPath(0) == wav, "the sample path is restored and queued for the idle tick");
	const fg_pattern* q = fg_engine_pattern(b.plug.Engine(), 0);
	check(q->tiles[1].muted && q->tiles[2].reversed && q->nMods == 1 && q->mods[0].action == FG_MOD_RATCHET, "pattern restored (mute, reverse, ratchet modifier)");
	check(std::fabs(b.plug.GetParam(kParamBpm)->Value() - 133.) < 1e-9, "BPM parameter restored");
	check(std::fabs(b.plug.GetParam(TrackParam(0, kTrackPan))->Value() + 25.) < 1e-9, "pan parameter restored");
	b.proc.idle();   // the restored sample path loads here
	check(b.plug.PendingLoadCount() == 0 && fg_engine_has_sample(b.plug.Engine(), 0), "loaded on the idle tick");
	b.blocks(1);
	b.capture.clear();
	b.plug.PlayAll();
	b.blocks(140);   // 17920 samples: through unit 3 at 133 bpm
	// 133 bpm: a step is 5413.5 samples, stretched 0.9x
	check(b.peakIn(0, 16) > 0.5 * m / 16.0, "the restored instance plays the sample again (at 133 bpm)");
	check(b.peakIn(5300, 10800) == 0.0, "the restored mute on unit 1 holds");
	check(b.peakIn(16241, 16260) > 0.5 * m * 4.0 / 16.0, "unit 3 follows at the restored tempo (unit 2 is the reversed one)");

	std::printf("=== a MIDI note plays a slice ===\n");
	b.plug.StopAll();
	b.blocks(2);
	b.capture.clear();
	{
		const unsigned char note[3] = {0x90, 36 + 3, 127};
		b.proc.midi(note, 3, 0);
	}
	b.blocks(2);
	// the restored pan is −25 %: the mono law's left gain is cos(0.375 · π / 2)
	const double gl = std::cos(0.375 * 3.14159265358979 / 2.0);
	check(std::fabs(b.capture[0] - gl * 4.0 / 16.0) < 1e-4, "note 39 on channel 1 plays unit 4 (index 3) of track 1 at once");

	std::printf("=== bounce from the plugin ===\n");
	setenv("RF_DATA_DIR", "/tmp/frog-shell-data", 1);
	check(b.plug.Bounce(), "Bounce() starts a worker");
	check(!b.plug.Bounce(), "a second one is refused while it runs");
	for (int i = 0; i < 2000 && b.plug.Bouncing(); i++) {
		usleep(5000);
	}
	b.proc.idle();
	check(!b.plug.Bouncing() && !b.plug.LastBounce().empty() && b.plug.LastBounce()[0] != '!', "the bounce finished", b.plug.LastBounce());
	{
		fg_sample* out = fg_sample_load_wav(b.plug.LastBounce().c_str(), 0.0, 0.0);
		check(out != nullptr && out->frames > 0, "the WAV reads back");
		if (out) {
			double peak = 0.0;
			for (int64_t i = 0; i < out->frames; i++) {
				peak = std::max(peak, (double)std::fabs(out->ch[0][i]));
			}
			check(peak > 0.9, "normalised to 0.99", std::to_string(peak));
			// 4 beats at 133 bpm (the restored tempo) at 48 k
			check(std::llabs(out->frames - (int64_t)std::llround(4.0 * 60.0 / 133.0 * 48000.0)) <= 1, "one bar long at the session tempo", std::to_string(out->frames));
			fg_sample_free(out);
		}
	}

	std::printf("=== export options: loops, focused, stems ===\n");
	{
		auto bounceAndWait = [&](const Frog::ExportOpts& o) {
			if (!b.plug.Bounce(o)) {
				return false;
			}
			for (int i = 0; i < 4000 && b.plug.Bouncing(); i++) {
				usleep(5000);
			}
			b.proc.idle();
			return !b.plug.Bouncing() && !b.plug.LastBounce().empty() && b.plug.LastBounce()[0] != '!';
		};
		auto frames = [](const std::string& path) {
			fg_sample* s = fg_sample_load_wav(path.c_str(), 0.0, 0.0);
			const int64_t n = s ? s->frames : -1;
			fg_sample_free(s);
			return n;
		};
		const int64_t bar = (int64_t)std::llround(4.0 * 60.0 / 133.0 * 48000.0);
		Frog::ExportOpts o;
		o.loops = 2;
		check(bounceAndWait(o), "two loops render", b.plug.LastBounce());
		check(std::llabs(frames(b.plug.LastBounce()) - 2 * bar) <= 1, "twice the bar long", std::to_string(frames(b.plug.LastBounce())));
		check(b.plug.DuplicateTrack(0), "a second track for stems");
		b.proc.idle();   // its sample loads
		o.loops = 1;
		o.subset = Frog::kExportFocused;
		check(bounceAndWait(o), "the focused track alone", b.plug.LastBounce());
		check(b.plug.LastBounce().size() > 7 && b.plug.LastBounce().substr(b.plug.LastBounce().size() - 7) == "-t1.wav", "named after the track", b.plug.LastBounce());
		o.subset = Frog::kExportStems;
		check(bounceAndWait(o), "stems", b.plug.LastBounce());
		check(frames(b.plug.LastBounce() + "-t1.wav") == bar && frames(b.plug.LastBounce() + "-t2.wav") == bar, "one WAV per track, a bar each");
		check(b.plug.RemoveTrack(), "back to one track");
		b.proc.idle();
	}

	std::printf("=== sessions as presets ===\n");
	check(!b.proc.stepPreset(1), "no sessions yet: stepPreset says so");
	check(b.plug.SaveSession("alpha") && b.plug.SaveSession("beta"), "two sessions saved");
	check(b.proc.currentPreset().name == "beta" && b.proc.currentPreset().program == 1, "the saved one is current", b.proc.currentPreset().name);
	check(b.proc.stepPreset(-1), "previous");
	b.proc.idle();
	check(b.proc.currentPreset().name == "alpha" && b.proc.currentPreset().program == 0, "alpha loaded on the idle tick", b.proc.currentPreset().name);
	check(b.proc.stepPreset(-1), "wraps");
	b.proc.idle();
	check(b.proc.currentPreset().name == "beta", "back to beta", b.proc.currentPreset().name);
	{
		const unsigned char pc[2] = {0xC0, 0};
		b.proc.midi(pc, 2, 0);
	}
	b.blocks(1);
	check(b.proc.currentPreset().name == "beta", "a Program Change waits for the idle tick");
	b.proc.idle();
	check(b.proc.currentPreset().name == "alpha", "then loads program 0", b.proc.currentPreset().name);
	check(fg_engine_pattern(b.plug.Engine(), 0)->tiles[1].muted, "the restored pattern came with it");

	std::printf("=== CC map: defaults, learn, file, state ===\n");
	remove("/tmp/frog-shell-data/midimap.json");
	auto cc = [&](Rig& r, int channel, int num, int value) {
		const unsigned char m[3] = {(unsigned char)(0xB0 | (channel - 1)), (unsigned char)num, (unsigned char)value};
		r.proc.midi(m, 3, 0);
		r.blocks(1);
	};
	cc(b, 5, 7, 64);
	check(b.plug.GetParam(TrackParam(0, kTrackVolume))->Value() == 100., "a CC waits for the idle tick");
	b.proc.idle();
	check(std::fabs(b.plug.GetParam(TrackParam(0, kTrackVolume))->Value() - 100. * 64. / 127.) < 1e-6, "CC 7 on channel 5 (omni) sets the focused track's volume");
	cc(b, 1, 20, 127);
	b.proc.idle();
	check(b.plug.GetParam(TrackParam(0, kTrackMute))->Bool(), "CC 20 press toggles mute on");
	cc(b, 1, 20, 0);
	b.proc.idle();
	check(b.plug.GetParam(TrackParam(0, kTrackMute))->Bool(), "its release is ignored");
	cc(b, 1, 20, 100);
	b.proc.idle();
	check(!b.plug.GetParam(TrackParam(0, kTrackMute))->Bool(), "the next press toggles it off");
	b.plug.StopAll();
	b.blocks(1);
	cc(b, 1, 28, 127);
	b.proc.idle();
	b.blocks(1);   // the Play command lands on the next block
	check(fg_engine_grid_running(b.plug.Engine()), "CC 28 is Play");
	cc(b, 1, 29, 127);
	b.proc.idle();
	b.blocks(1);
	check(!fg_engine_grid_running(b.plug.Engine()), "CC 29 is Stop");
	// learn: arm for a hook, the next CC binds it (any channel) and the map is saved
	const double panBefore = b.plug.GetParam(TrackParam(0, kTrackPan))->Value();
	b.plug.ArmLearn("track.pan");
	check(std::string(b.plug.LearnArmed()) == "track.pan", "learn armed");
	cc(b, 3, 33, 5);
	b.proc.idle();
	check(b.plug.LearnArmed()[0] == 0, "the CC disarmed learn");
	const fg_binding* bd = fg_midimap_find(&b.plug.MidiMap(), 1, 33);
	check(bd && std::string(bd->target) == "track.pan" && bd->channel == 0, "CC 33 is now track.pan on any channel");
	check(fg_midimap_find(&b.plug.MidiMap(), 1, 10) == nullptr, "the old CC 10 binding is gone");
	check(b.plug.GetParam(TrackParam(0, kTrackPan))->Value() == panBefore, "the learning CC itself was not applied");
	cc(b, 1, 33, 127);
	b.proc.idle();
	check(std::fabs(b.plug.GetParam(TrackParam(0, kTrackPan))->Value() - 100.) < 1e-6, "CC 33 drives the pan");
	{
		fg_midimap onDisk;
		check(fg_midimap_load_file("/tmp/frog-shell-data/midimap.json", &onDisk), "midimap.json written under RF_DATA_DIR");
		const fg_binding* d = fg_midimap_find(&onDisk, 1, 33);
		check(d && std::string(d->target) == "track.pan", "with the learned binding");
	}
	Rig c;
	bd = fg_midimap_find(&c.plug.MidiMap(), 1, 33);
	check(bd && std::string(bd->target) == "track.pan", "a new instance starts from the saved file");
	c.plug.ClearBinding("track.pan");
	check(!c.plug.HookBound("track.pan"), "ClearBinding unbinds");
	{
		fg_midimap onDisk;
		fg_midimap_load_file("/tmp/frog-shell-data/midimap.json", &onDisk);
		check(!fg_midimap_is_bound(&onDisk, "track.pan"), "and saves");
	}
	{
		iplug::IByteChunk ch;
		check(c.plug.SerializeState(ch), "state v2 serialises (session + map)");
		Rig d;
		check(d.plug.UnserializeState(ch, 0) == ch.Size(), "and a fresh instance consumes the whole chunk");
	}
	// an unbound CC does nothing; a bound hook that needs the editor (slice.*) is simply ignored headless
	cc(c, 1, 99, 127);
	cc(c, 1, 70, 127);
	c.proc.idle();
	check(true, "unbound and editor-only hooks are ignored without the editor");

	std::printf("=== first run: the factory session ===\n");
	unsetenv("FROG_NO_FACTORY");
	system("rm -rf /tmp/frog-shell-fresh");
	setenv("FROG_DATA_DIR", "/tmp/frog-shell-fresh", 1);
	{
		Rig f;
		f.proc.idle();
		check(f.plug.TrackName(0)[0] == 0, "nothing happens in the first 300 ms (a host may still restore state)");
		usleep(350000);
		f.proc.idle();
		check(std::string(f.plug.TrackName(0)) == "amen-variation.wav", "then the factory session is installed and loaded", f.plug.TrackName(0));
		check(std::string(f.plug.GetCurrentPresetName()) == "factory", "as the current preset", f.plug.GetCurrentPresetName());
		check(fg_engine_pattern(f.plug.Engine(), 0)->nTiles == 32 && fg_engine_pattern(f.plug.Engine(), 0)->beats == 16, "four bars of eighths");
		check(std::fabs(f.plug.GetParam(kParamBpm)->Value() - 136.) < 1e-9, "at 136 bpm");
		f.proc.idle();
		check(fg_engine_has_sample(f.plug.Engine(), 0), "the clip loads on the next tick");
		f.plug.PlayAll();
		f.blocks(4);
		check(f.peakIn(0, f.capture.size()) > 0.1, "and sounds");
		struct stat st;
		check(stat("/tmp/frog-shell-fresh/samples/amen-variation.wav", &st) == 0 && stat("/tmp/frog-shell-fresh/sessions/factory.json", &st) == 0, "clip and session written under the data dir");
	}
	{
		Rig g;   // second run: the files exist, the factory session loads again, nothing is rewritten
		usleep(350000);
		g.proc.idle();
		check(std::string(g.plug.TrackName(0)) == "amen-variation.wav", "a second run boots into the factory session");
	}
	{
		iplug::IByteChunk ch;
		Rig h;
		Rig src;
		src.plug.SerializeState(ch);
		h.plug.UnserializeState(ch, 0);
		usleep(350000);
		h.proc.idle();
		check(h.plug.TrackName(0)[0] == 0, "restored state (even an empty one) suppresses the factory load");
	}
	setenv("FROG_NO_FACTORY", "1", 1);

	std::printf("%s\n", gFail == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
	return gFail == 0 ? 0 : 1;
}
