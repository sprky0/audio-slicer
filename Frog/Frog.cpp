#include "Frog.h"
#include "IPlug_include_in_plug_src.h"

#if IPLUG_EDITOR
#include "ui/FrogVersion.h"
#endif

#include <cmath>

Frog::Frog(const InstanceInfo& info)
    : Plugin(info, MakeConfig(kNumParams, kNumPresets)) {
	GetParam(kParamBpm)->InitDouble("BPM", 120., 20., 400., 0.01, "bpm");
	GetParam(kParamMasterGain)->InitGain("Master", 0., -60., 6., 0.1);

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

#if IPLUG_DSP
void Frog::ProcessBlock(sample** inputs, sample** outputs, int nFrames) {
	// Silent until the engine lands (F6). Clear every output channel.
	const int nCh = NOutChansConnected();
	for (int c = 0; c < nCh; c++) {
		for (int s = 0; s < nFrames; s++) {
			outputs[c][s] = 0.;
		}
	}
}

void Frog::ProcessMidiMsg(const IMidiMsg& msg) {
	// Clock, Start / Stop and notes route to the engine from F9 on.
}

void Frog::OnReset() {
}

void Frog::OnParamChange(int paramIdx) {
	switch (paramIdx) {
		case kParamMasterGain:
			mMasterGain = std::pow(10., GetParam(kParamMasterGain)->Value() / 20.);
			break;
		default:
			break;
	}
}

void Frog::OnIdle() {
}
#endif
