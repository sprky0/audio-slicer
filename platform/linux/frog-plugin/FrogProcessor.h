// FrogProcessor — the rflh::Processor the appliance runs: the shared iPlug2
// adapter underneath, plus the one thing a clock follower needs that the
// adapter leaves out: MIDI realtime bytes (clock 0xF8, start, continue,
// stop) reach the plugin. The adapter routes channel messages only and is
// final, so this wraps it and forwards every hook; a realtime byte becomes
// an IMidiMsg whose mStatus carries it, which Frog::ProcessMidiMsg reads raw.
// (If the shared adapter learns to pass realtime bytes, this file shrinks
// to a using-declaration.)
#pragma once

#include "Frog.h"

#include "iplug2/IPlug2HeadlessProcessor.h"

class FrogProcessor final : public rflh::Processor {
public:
	FrogProcessor(Frog& plug, int outputChannels)
	    : mPlug(plug), mInner(plug, outputChannels) {}

	void reset(double sampleRate, int blockSize) override { mInner.reset(sampleRate, blockSize); }
	void process(double* const* outputs, int nChannels, int nFrames) override { mInner.process(outputs, nChannels, nFrames); }
	bool midi(const unsigned char* bytes, size_t n, int sampleOffset) override {
		if (n == 1 && bytes[0] >= 0xF8) {
			iplug::IMidiMsg msg;
			msg.mOffset = sampleOffset;
			msg.mStatus = bytes[0];
			msg.mData1 = 0;
			msg.mData2 = 0;
			return mPlug.HeadlessPushMidiMsg(msg);
		}
		return mInner.midi(bytes, n, sampleOffset);
	}
	void idle() override { mInner.idle(); }
	int outputChannels() const override { return mInner.outputChannels(); }

	// board settings and presets: Frog has none of these yet; the adapter answers
	void setMaxVoices(int n) override { mInner.setMaxVoices(n); }
	int maxVoices() const override { return mInner.maxVoices(); }
	void setEngineCount(int n) override { mInner.setEngineCount(n); }
	int engineCount() const override { return mInner.engineCount(); }
	bool hasEngineWidth() const override { return mInner.hasEngineWidth(); }
	void setEngineMode(rflh::EngineMode m) override { mInner.setEngineMode(m); }
	rflh::EngineMode engineMode() const override { return mInner.engineMode(); }
	std::vector<int> maxVoicesSteps() const override { return mInner.maxVoicesSteps(); }
	PresetInfo currentPreset() const override { return mInner.currentPreset(); }
	bool stepPreset(int delta) override { return mInner.stepPreset(delta); }

	// render threads
	int maxRenderThreads() const override { return mInner.maxRenderThreads(); }
	int setRenderThreads(int threads, int rtPriority, int helperCpu) override { return mInner.setRenderThreads(threads, rtPriority, helperCpu); }
	int renderThreads() const override { return mInner.renderThreads(); }
	int64_t renderHelperCpuNs() const override { return mInner.renderHelperCpuNs(); }
	uint64_t renderSlowJoins() const override { return mInner.renderSlowJoins(); }
	bool renderHelperStatus(int& rtResult, int& affinityResult) const override { return mInner.renderHelperStatus(rtResult, affinityResult); }

	// the editor on the panel
	bool hasEditor() const override { return mInner.hasEditor(); }
	std::string editorModuleBuildId() const override { return mInner.editorModuleBuildId(); }
	bool openEditor(const EditorOptions& o, std::string& why) override { return mInner.openEditor(o, why); }
	bool setEditorVisible(bool visible) override { return mInner.setEditorVisible(visible); }
	bool editorVisible() const override { return mInner.editorVisible(); }
	double editorTurn() override { return mInner.editorTurn(); }
	int editorInputFd() const override { return mInner.editorInputFd(); }
	bool takePerformanceViewRequest() override { return mInner.takePerformanceViewRequest(); }
	int takeScreenDetectRequest() override { return mInner.takeScreenDetectRequest(); }
	void screenDetected(const std::string& summary, const std::vector<std::string>& details) override { mInner.screenDetected(summary, details); }
	bool editorScreenshot(const std::string& path) override { return mInner.editorScreenshot(path); }
	void closeEditor() override { mInner.closeEditor(); }

private:
	Frog& mPlug;
	rflh::IPlug2HeadlessProcessor<Frog> mInner;
};
