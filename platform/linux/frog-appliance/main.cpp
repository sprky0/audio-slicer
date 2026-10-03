// frog-appliance — Frog on the shared Linux host (ROADMAP.md F3).
//
// Everything platform-side (ALSA out on a SCHED_FIFO thread, ALSA sequencer
// in with hot-plug, the framebuffer splash, sd_notify, status lines, the
// offline --render-wav path) is rflh::runAppliance(). Everything Frog-side is
// the plugin class, driven through the iPlug2 Headless API. This file is the
// glue: construct the plugin, wrap it, run.

#include "Frog.h"
#include "FrogProcessor.h"

#include "Appliance.h"
#include "Log.h"
#include "StoragePaths.h"

#include <string>

int main(int argc, char** argv) {
	rflh::ApplianceOptions opts;
	opts.productName = "frog-appliance";
	opts.version = PLUG_VERSION_STR;
	opts.assetDir = "/opt/ratfactory/assets";
	// The USB audio device whenever one is plugged in, else the 3.5 mm jack,
	// else HDMI; --audio-device overrides.
	opts.audioSelector = "usb-then-headphones-then-hdmi";
	// The editor's GPU side is a module loaded after the first audio callback,
	// from <exe dir>/../lib; missing, Frog plays with the performance view only.
	opts.editorModule = "libfrog-editor.so";

	bool exitNow = false;
	const int rc = rflh::parseApplianceArgs(argc, argv, opts, exitNow);
	if (exitNow) {
		return rc;
	}

	// Product state under $RF_DATA_DIR (the unit sets /data/ratfactory/frog):
	// sessions and samples. Make the folders so a fresh /data works first boot.
	const std::string dataDir = rflh::dataDir();
	rflh::ensureDir(dataDir + "/sessions");
	rflh::ensureDir(dataDir + "/samples");
	rflh::logInfo("frog-appliance: data in %s", dataDir.c_str());

	// No idle Timer thread: runAppliance() ticks idle work (OnIdle, deferred
	// loads) from its main loop, and the offline render per block.
	iplug::InstanceInfo info;
	info.createIdleTimer = false;
	Frog plug(info);
	FrogProcessor processor(plug, APP_NUM_CHANNELS);
	return rflh::runAppliance(processor, opts);
}
