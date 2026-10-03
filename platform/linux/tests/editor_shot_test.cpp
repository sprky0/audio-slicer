// The editor on the appliance's panel, offscreen (F10): Frog's UI through the
// host's adapter and the editor module, drawn surfaceless on the container's
// Mesa (on the Pi the same code draws on DRM/KMS). Loads a sample, opens the
// editor at the 7" panel's 1024 x 600, writes DIR/edit.png, presses Play and
// writes DIR/playing.png with the playhead and the sounding tile lit, taps
// Perform for DIR/perform.png, then reopens at 1280 x 720 for DIR/wide.png.
// Built by platform/linux/CMakeLists.txt (editor_shot_test) and run by
// docker-build-arm64.sh, which keeps the images in build-arm64/shots/.

#include "Frog.h"
#include "FrogProcessor.h"
#include "IGraphicsKMS.h"
#include "engine/sample.h"
#include "ui/DragControl.h"
#include "ui/ModLaneControl.h"
#include "ui/TileRowControl.h"
#include "ui/TrackStripControl.h"

#include "EditorModule.h"
#include "iplug2/IGraphicsKMSEditorModule.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace iplug;
using namespace igraphics;

namespace {

int gFail = 0;

void check(bool ok, const char* what, const std::string& detail = "") {
	std::printf("  %-4s %-66s %s\n", ok ? "PASS" : "FAIL", what, detail.c_str());
	if (!ok) {
		++gFail;
	}
}

// 1.6 s: sixteen decaying bursts, one per 1/16 at 150 bpm, distinct pitches
const char* writeFixture() {
	static const char* path = "/tmp/frog-shot.wav";
	const double sr = 48000.0;
	const int64_t n = (int64_t)(1.6 * sr);
	std::vector<double> l(n), r(n);
	for (int64_t i = 0; i < n; i++) {
		const double t = i / sr;
		const int u = std::min(15, (int)(t / 0.1));
		const double pos = t - u * 0.1;
		const double env = std::exp(-pos / 0.05);
		l[i] = 0.8 * env * std::sin(2 * 3.14159265 * 220.0 * std::pow(2.0, u / 8.0) * pos);
		r[i] = 0.5 * env * std::sin(2 * 3.14159265 * 330.0 * std::pow(2.0, u / 8.0) * pos);
	}
	const double* ch[2] = {l.data(), r.data()};
	fg_sample_write_wav16(path, ch, 2, n, sr);
	return path;
}

frogui::DragControl* buttonLabelled(IGraphics& g, const char* label) {
	for (int i = 0; i < g.NControls(); ++i) {
		auto* d = dynamic_cast<frogui::DragControl*>(g.GetControl(i));
		if (d && d->Label() == label) {
			return d;
		}
	}
	return nullptr;
}

void tap(IGraphics& g, float x, float y) {
	IMouseInfo info;
	info.x = x;
	info.y = y;
	info.dX = 0.f;
	info.dY = 0.f;
	info.ms = IMouseMod(true);
	g.OnMouseDown(std::vector<IMouseInfo>{info});
	g.OnMouseUp(std::vector<IMouseInfo>{info});
}

}  // namespace

int main(int argc, char** argv) {
	setenv("FROG_NO_FACTORY", "1", 1);   // renders start from a blank track
	const std::string shots = argc > 1 ? argv[1] : "";
	const char* wav = writeFixture();

	InstanceInfo info;
	info.createIdleTimer = false;
	Frog plug(info);
	FrogProcessor proc(plug, 2);
	proc.reset(48000., 128);
	plug.RequestLoadSample(0, wav);
	proc.idle();
	check(fg_engine_has_sample(plug.Engine(), 0), "the sample loaded on the idle tick");
	// two modifiers for the lane: a 2-step ratchet on step 4, a mute on step 9
	{
		fg_pattern* p = fg_engine_pattern(plug.Engine(), 0);
		p->nMods = 2;
		p->mods[0] = fg_mod{4, FG_MOD_RATCHET, FG_FIRE_PROB, FG_RATCHET_RAMP, 100, 0, 1, 4, 0, 2};
		p->mods[1] = fg_mod{9, FG_MOD_MUTE, FG_FIRE_PROB, 0, 100, 0, 1, 1, 0, 1};
		fg_engine_publish(plug.Engine(), 0);
	}

	std::printf("=== the editor module ===\n");
	check(!proc.editorModuleBuildId().empty(), "the build's editor is in a module", proc.editorModuleBuildId());
	rflh::EditorModule module;
	std::string why;
	const std::string path = rflh::editorModulePath("libfrog-editor.so");
	const bool loaded = rflh::loadEditorModule(path, proc.editorModuleBuildId(), module, why);
	check(loaded, "the module loads (build id matches)", loaded ? module.entry->name : why);
	if (!loaded) {
		return 1;
	}
	const auto* table = static_cast<const rflh::iplug2::KmsEditorTable*>(module.entry->impl);
	IGraphicsKMS::Config& kc = table->settings();
	kc.surfaceless = true;
	kc.offscreenW = 1024;
	kc.offscreenH = 600;
	rflh::Processor::EditorOptions eo;
	eo.touchDevice = "";
	eo.module = module.entry->impl;

	std::printf("=== 1024 x 600: edit, playing, perform ===\n");
	why.clear();
	const bool opened = proc.openEditor(eo, why);
	check(opened, "openEditor()", why);
	if (!opened) {
		return 1;
	}
	check(proc.setEditorVisible(true), "shown");
	proc.idle();
	proc.editorTurn();
	IGraphics* ui = plug.GetUI();
	check(ui != nullptr, "the UI exists");
	if (!shots.empty()) {
		check(proc.editorScreenshot(shots + "/edit.png"), "edit.png written");
	}

	// press Play, run a third of a bar, let the view pick up the visuals
	std::vector<double> l(128), r(128);
	double* outs[2] = {l.data(), r.data()};
	plug.PlayAll();
	for (int b = 0; b < 200; b++) {   // 25600 samples ≈ 0.53 s ≈ unit 5
		proc.process(outs, 2, 128);
	}
	proc.idle();
	proc.editorTurn();
	double peak = 0.0;
	for (int i = 0; i < 128; i++) {
		peak = std::max(peak, std::fabs(l[i]));
	}
	check(peak > 0.01, "audio is playing", std::to_string(peak));
	if (!shots.empty()) {
		check(proc.editorScreenshot(shots + "/playing.png"), "playing.png written");
	}

	if (ui) {
		frogui::DragControl* perform = buttonLabelled(*ui, "Perform");
		check(perform != nullptr, "the Perform toggle is in the transport bar");
		if (perform) {
			const IRECT pr = perform->GetRECT();
			tap(*ui, pr.MW(), pr.MH());
			check(perform->On(), "a tap lights it");
			proc.idle();
			proc.editorTurn();
			if (!shots.empty()) {
				check(proc.editorScreenshot(shots + "/perform.png"), "perform.png written");
			}
		}
		// the first tile: a tap selects it, the slice toolbar follows
		for (int i = 0; i < ui->NControls(); ++i) {
			if (auto* tiles = dynamic_cast<frogui::TileRowControl*>(ui->GetControl(i))) {
				const IRECT tr = tiles->GetRECT();
				tap(*ui, tr.L + tr.W() / 32.f, tr.MH());
				check(tiles->Selected() == 0, "a tap on the first tile selects it");
				break;
			}
		}
		proc.editorTurn();
		if (!shots.empty()) {
			check(proc.editorScreenshot(shots + "/selected.png"), "selected.png written");
		}
		// the modifier lane: tap the ratchet chip, its settings replace the toolbars
		for (int i = 0; i < ui->NControls(); ++i) {
			if (auto* lane = dynamic_cast<frogui::ModLaneControl*>(ui->GetControl(i))) {
				const IRECT lr = lane->GetRECT();
				tap(*ui, lr.L + lr.W() * 4.5f / 16.f, lr.MH());
				check(lane->Selected() == 0, "a tap on the chip selects the ratchet modifier");
				break;
			}
		}
		proc.editorTurn();
		if (!shots.empty()) {
			check(proc.editorScreenshot(shots + "/mods.png"), "mods.png written");
		}
	}
	// a second track: duplicate the first, which copies the pattern and reloads the sample
	if (ui) {
		check(plug.DuplicateTrack(0), "DuplicateTrack(0)");
		proc.idle();
		check(plug.NumTracks() == 2 && fg_engine_has_sample(plug.Engine(), 1), "two tracks, the copy has its sample");
		for (int i = 0; i < ui->NControls(); ++i) {
			if (auto* strip = dynamic_cast<frogui::TrackStripControl*>(ui->GetControl(i))) {
				const IRECT sr = strip->GetRECT();
				tap(*ui, sr.L + sr.W() * 0.5f, sr.MH());   /* the second tab */
				break;
			}
		}
		proc.idle();
		proc.editorTurn();
		if (!shots.empty()) {
			check(proc.editorScreenshot(shots + "/tracks.png"), "tracks.png written");
		}
	}
	plug.StopAll();
	proc.process(outs, 2, 128);

	std::printf("=== 1280 x 720 ===\n");
	proc.setEditorVisible(false);
	proc.closeEditor();
	kc.offscreenW = 1280;
	kc.offscreenH = 720;
	why.clear();
	const bool reopened = proc.openEditor(eo, why);
	check(reopened, "reopened at 1280 x 720", why);
	if (reopened) {
		proc.setEditorVisible(true);
		proc.idle();
		proc.editorTurn();
		if (!shots.empty()) {
			check(proc.editorScreenshot(shots + "/wide.png"), "wide.png written");
		}
		proc.setEditorVisible(false);
		proc.closeEditor();
	}

	std::printf("%s\n", gFail == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
	return gFail == 0 ? 0 : 1;
}
