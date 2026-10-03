#pragma once

#include "IPlug_include_in_plug_hdr.h"

// Frog — sample slicer and loop performer. F2 scaffold: the plugin shell
// builds and runs silent; the C engine arrives in F5–F7 and the UI in F10.

const int kNumPresets = 1;

enum EParams {
	kParamBpm = 0,    // master tempo (internal clock); 20..400
	kParamMasterGain, // master output level, dB
	kNumParams
};

enum ECtrlTags {
	kCtrlTagVersion = 0,
	kNumCtrlTags
};

using namespace iplug;
using namespace igraphics;

class Frog final : public Plugin {
public:
	Frog(const InstanceInfo& info);

#if IPLUG_DSP
	void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
	void ProcessMidiMsg(const IMidiMsg& msg) override;
	void OnReset() override;
	void OnParamChange(int paramIdx) override;
	void OnIdle() override;

private:
	double mMasterGain = 1.0;
#endif
};
