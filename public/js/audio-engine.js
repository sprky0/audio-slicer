import { getAudioContext } from './audio-context.js';

/**
 * AudioEngine: Handles audio loading, slicing, playback, segment enable/disable, and state.
 * Emits events for playback and state changes. No DOM or canvas dependencies.
 */
class AudioEngine extends EventTarget {
	constructor() {
		super();
		// Shared process-wide context (Tier 1c) — all engines run on one clock so the
		// master transport can sample-lock every track. This engine still owns its own
		// gain/panner below, so per-slicer volume/pan stay independent.
		this.audioContext    = getAudioContext();
		this.audioBuffer     = null;
		this.originalBuffer  = null;
		this.segments        = [];
		this.enabledSegments = [];
		this.activeSegment   = -1;
		this.scheduledSources= [];
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

	/**
	 * Resume the AudioContext if it's suspended (browser autoplay policy).
	 */
	async resume() {
		if (this.audioContext && this.audioContext.state === 'suspended') {
			try {
				await this.audioContext.resume();
			} catch (err) {
				console.error('Failed to resume AudioContext:', err);
			}
		}
	}

	/**
	 * Schedule an arbitrary AudioBuffer to start at absolute audio-clock time
	 * `when`. This is the sequencer's playback path — event-silent and independent
	 * of the single-source playSegment()/onended path used by free-run playback.
	 * Each voice gets its own gain node so the declick ramp is per-voice.
	 *
	 * `declick`: when true, always fade to 0 ending exactly at `stopAt` and stop
	 * there — used for time-stretched fill buffers, which are built to fill the
	 * step and whose boundary sample isn't a guaranteed zero-crossing. When false,
	 * the voice only cuts if `stopAt` lands before the buffer would naturally end
	 * (so a shorter slice rings out, leaving a gap).
	 *
	 * `playbackRate` resamples (repitches). For stretched buffers it carries the
	 * pitch-shift ratio; for raw buffers it's 1 (or a fill ratio in fallback).
	 */
	scheduleBuffer(buffer, when, stopAt, playbackRate = 1, { declick = false } = {}) {
		if (!buffer) return null;
		const source = this.audioContext.createBufferSource();
		source.buffer = buffer;
		source.playbackRate.value = playbackRate > 0 ? playbackRate : 1;

		const voiceGain = this.audioContext.createGain();
		voiceGain.gain.value = 1;
		source.connect(voiceGain);
		voiceGain.connect(this.gainNode);

		const DECLICK = 0.005;
		const effDur  = source.buffer.duration / source.playbackRate.value;
		source.start(when);
		if (typeof stopAt === 'number' && stopAt > when && (declick || stopAt < when + effDur)) {
			voiceGain.gain.setValueAtTime(1, Math.max(when, stopAt - DECLICK));
			voiceGain.gain.linearRampToValueAtTime(0, stopAt);
			source.stop(stopAt + DECLICK);
		}

		const entry = { source, voiceGain };
		this.scheduledSources.push(entry);
		source.onended = () => {
			try { voiceGain.disconnect(); } catch (err) { /* already torn down */ }
			const i = this.scheduledSources.indexOf(entry);
			if (i !== -1) this.scheduledSources.splice(i, 1);
		};
		return source;
	}

	/** Convenience wrapper: schedule a raw slice by index (ring-out, no forced declick). */
	scheduleSegment(index, when, stopAt, playbackRate = 1) {
		return this.scheduleBuffer(this.segments[index], when, stopAt, playbackRate);
	}

	/**
	 * Stop and disconnect every scheduled voice, including ones whose start time
	 * is still in the future (they would otherwise fire after a stop/clear).
	 */
	cancelScheduled() {
		if (this.audioContext.state === 'closed') {
			this.scheduledSources = [];
			return;
		}
		for (const entry of this.scheduledSources) {
			try { entry.source.onended = null; entry.source.stop(); } catch (err) { /* not started / already stopped */ }
			try { entry.source.disconnect(); } catch (err) { /* already disconnected */ }
			try { entry.voiceGain.disconnect(); } catch (err) { /* already disconnected */ }
		}
		this.scheduledSources = [];
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
		this.cancelScheduled();
		// The AudioContext is shared across all engines (Tier 1c) — never close it
		// here or every other slicer loses its clock. Just detach this engine's own
		// nodes from the shared destination so it stops contributing output.
		try { this.gainNode && this.gainNode.disconnect(); } catch (err) { /* already gone */ }
		try { this.pannerNode && this.pannerNode.disconnect(); } catch (err) { /* already gone */ }
	}
}

export default AudioEngine;
