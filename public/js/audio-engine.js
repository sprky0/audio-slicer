/**
 * AudioEngine: Handles audio loading, slicing, playback, segment enable/disable, and state.
 * Emits events for playback and state changes. No DOM or canvas dependencies.
 */
class AudioEngine extends EventTarget {
	constructor() {
		super();
		this.audioContext    = new (window.AudioContext || window.webkitAudioContext)();
		this.audioBuffer     = null;
		this.originalBuffer  = null;
		this.segments        = [];
		this.enabledSegments = [];
		this.activeSegment   = -1;
		this.isPlaying       = false;
		this.audioSource     = null;
		this.playStartTime   = 0;
		this.segmentStartTime= 0;
		this.performance     = {
			decode:         0,
			slice:          0,
			playbackLatency:0,
		};
		// --- Volume and Pan ---
		this.gainNode = this.audioContext.createGain();
		this.gainNode.gain.value = 1;
		this.pannerNode = this.audioContext.createStereoPanner
			? this.audioContext.createStereoPanner()
			: null;
		if (this.pannerNode) {
			this.pannerNode.pan.value = 0;
			this.gainNode.connect(this.pannerNode);
			this.pannerNode.connect(this.audioContext.destination);
		} else {
			this.gainNode.connect(this.audioContext.destination);
		}
	}

	async loadFile(file) {
		const t0         = performance.now();
		const arrayBuffer= await file.arrayBuffer();
		this.audioBuffer = await this.audioContext.decodeAudioData(arrayBuffer);
		this.originalBuffer = this.audioBuffer;
		const t1         = performance.now();
		this.performance.decode = t1 - t0;
		this.dispatchEvent(new CustomEvent('fileloaded', { detail: { buffer: this.audioBuffer, decodeTime: this.performance.decode } }));
	}

	slice(startPosition, endPosition, subdivisions) {
		const t0 = performance.now();
		if (!this.audioBuffer) return;
		const startSample = Math.floor(startPosition * this.audioBuffer.length);
		const endSample   = Math.floor(endPosition * this.audioBuffer.length);
		const totalSamples= endSample - startSample;
		if (totalSamples <= 0) return;

		this.segments        = [];
		this.enabledSegments = [];
		for (let i = 0; i < subdivisions; i++) {
			const segStart = startSample + Math.floor((totalSamples / subdivisions) * i);
			const segEnd   = (i === subdivisions - 1)
				? endSample
				: startSample + Math.floor((totalSamples / subdivisions) * (i + 1));
			const length   = segEnd - segStart;

			const segmentBuffer = this.audioContext.createBuffer(
				this.audioBuffer.numberOfChannels,
				length,
				this.audioBuffer.sampleRate
			);

			for (let channel = 0; channel < this.audioBuffer.numberOfChannels; channel++) {
				const channelData = this.audioBuffer.getChannelData(channel);
				const segmentData = segmentBuffer.getChannelData(channel);
				for (let j = 0; j < length; j++) {
					segmentData[j] = channelData[segStart + j];
				}
			}
			this.segments.push(segmentBuffer);
			this.enabledSegments.push(true);
		}
		const t1 = performance.now();
		this.performance.slice = t1 - t0;
		this.dispatchEvent(new CustomEvent('segmentsliced', { detail: { segments: this.segments, sliceTime: this.performance.slice } }));
	}

	/**
	 * Play a segment, optionally starting at a given offset (in seconds).
	 */
	playSegment(index, offsetSec = 0) {
		if (!this.segments[index] || !this.enabledSegments[index]) return;
		this.stop();
		this.isPlaying     = true;
		this.activeSegment = index;

		const source = this.audioContext.createBufferSource();
		source.buffer = this.segments[index];
		// Connect: source -> gain -> (panner) -> destination
		source.connect(this.gainNode);

		this.audioSource = source;

		source.onended = () => {
			this.isPlaying     = false;
			this.activeSegment = -1;
			this.dispatchEvent(new CustomEvent('segmentend', { detail: { index } }));
		};

		this.playStartTime    = this.audioContext.currentTime - (offsetSec || 0);
		this.segmentStartTime = performance.now();
		try {
			source.start(0, offsetSec || 0);
		} catch (err) {
			console.error('Error starting playback at offset:', err);
			source.start();
		}

		// Measure playback latency
		setTimeout(() => {
			this.performance.playbackLatency = performance.now() - this.segmentStartTime;
			this.dispatchEvent(new CustomEvent('segmentplay', { detail: { index, playbackLatency: this.performance.playbackLatency } }));
		}, 0);
	}

	stop() {
		if (this.audioSource) {
			this.audioSource.onended = null;
			this.audioSource.stop();
			this.audioSource.disconnect();
			this.audioSource = null;
		}
		this.isPlaying     = false;
		this.activeSegment = -1;
	}
	// --- Volume and Pan Controls ---
	setVolume(val) {
		this.gainNode.gain.value = val;
	}
	setPan(val) {
		if (this.pannerNode) {
			this.pannerNode.pan.value = val;
		}
	}

	enableSegment(index, enabled) {
		if (index < 0 || index >= this.enabledSegments.length) return;
		this.enabledSegments[index] = enabled;
		this.dispatchEvent(new CustomEvent('segmentenable', { detail: { index, enabled } }));
	}

	getSegments() {
		return this.segments;
	}

	getEnabledSegments() {
		return this.enabledSegments;
	}

	getPerformance() {
		return { ...this.performance };
	}

	dispose() {
		this.stop();
		if (this.audioContext) {
			this.audioContext.close();
		}
	}
}

export default AudioEngine;
