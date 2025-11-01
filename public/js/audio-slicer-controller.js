import AudioEngine from './audio-engine.js';
import WaveformView from './waveform-view.js';

/**
 * AudioSlicerController: Connects AudioEngine and WaveformView.
 * Handles event wiring, performance monitoring, and supports multiple independent slicers.
 */
class AudioSlicerController {
	constructor(container, options = {}) {
		this.container = container;
		this.options   = options;

		// Create modules
		this.engine = new AudioEngine();
		this.view   = new WaveformView(container, options);

		// State
		this.waveformPeaks      = [];
		// this.performanceOverlay = null;

		// Bind events
		this._bindEngineEvents();
		this._bindViewEvents();

		// Performance overlay
		// this._createPerformanceOverlay();
	}

	async loadFile(file) {
		try {
			await this.engine.loadFile(file);
			// Precompute waveform peaks for visualization
			this.waveformPeaks = this._precomputeWaveformPeaks(this.engine.audioBuffer, this.view.options.precomputedWidth);
			this.view.setWaveformPeaks(this.waveformPeaks);

			// Immediately slice and play first segment
			const start        = 0;
			const end          = 1;
			const subdivisions = 2;
			this.slice(start, end, subdivisions, { autoPlay: true });
		} catch (err) {
			console.error('Error loading file:', err);
		}
	}

	/**
	 * Slices audio and updates view.
	 * options: { autoPlay: bool, keepPlayhead: bool }
	 */
	slice(start, end, subdivisions, options = {}) {
		// Track playhead and segment if needed
		let playheadFrac      = 0;
		let wasPlaying        = false;
		let prevActiveSegment = this.engine.activeSegment;
		if (options.keepPlayhead && this.engine.isPlaying && prevActiveSegment !== -1) {
			// Estimate playhead fraction in current segment
			const segment = this.engine.segments[prevActiveSegment];
			if (segment) {
				const elapsed = this.engine.audioContext.currentTime - this.engine.playStartTime;
				playheadFrac  = Math.min(1, Math.max(0, elapsed / segment.duration));
				wasPlaying    = true;
			}
		}

		this.engine.slice(start, end, subdivisions);
		this.view.setSelection(start, end, subdivisions);

		// If autoPlay or keepPlayhead, play the correct segment
		if (options.autoPlay || (options.keepPlayhead && wasPlaying)) {
			let segmentToPlay = 0;
			let playheadOffset = 0;
			if (options.keepPlayhead && wasPlaying) {
				// Map old playhead to new segment
				segmentToPlay = Math.floor(playheadFrac * subdivisions);
				playheadOffset = playheadFrac * this.engine.segments[segmentToPlay].duration;
			}
			this.playSegmentAtPosition(segmentToPlay, playheadOffset);
		}
	}

	/**
	 * Play a segment, optionally starting at a given offset (in seconds).
	 * Starts playhead animation.
	 */
	async playSegmentAtPosition(index, offsetSec = 0) {
		// Ensure AudioContext is resumed (required by browser autoplay policy)
		if (this.engine.audioContext && this.engine.audioContext.state === 'suspended') {
			try {
				await this.engine.audioContext.resume();
			} catch (err) {
				console.error('Failed to resume AudioContext:', err);
			}
		}
		this.engine.playSegment(index, offsetSec);
		this._startPlayheadAnimation(index, offsetSec);
	}

	/**
	 * Starts the playhead animation loop for the active segment.
	 */
	_startPlayheadAnimation(segmentIndex, offsetSec = 0) {
		if (this._playheadAnimId) {
			cancelAnimationFrame(this._playheadAnimId);
			this._playheadAnimId = null;
		}
		const update = () => {
			if (!this.engine.isPlaying || this.engine.activeSegment !== segmentIndex) {
				this.view.setIsPlaying(false);
				this.view.setPlayheadPosition(0);
				return;
			}
			const segment = this.engine.segments[segmentIndex];
			if (!segment) return;
			const elapsed = this.engine.audioContext.currentTime - this.engine.playStartTime;
			let playheadFrac = Math.min(1, Math.max(0, (elapsed + (offsetSec || 0)) / segment.duration));
			this.view.setIsPlaying(true);
			this.view.setPlayheadPosition(playheadFrac);
			this._playheadAnimId = requestAnimationFrame(update);
		};
		update();
	}

	/**
	 * Find the next enabled segment index after the given index, wrapping around.
	 * Returns null if no enabled segments exist.
	 */
	_findNextEnabledSegment(currentIndex) {
		const enabled = this.engine.getEnabledSegments();
		const n       = enabled.length;
		if (n === 0) return null;
		let idx = currentIndex;
		for (let i = 1; i <= n; i++) {
			const next = (idx + i) % n;
			if (enabled[next]) return next;
		}
		return null;
	}

	async playSegment(index) {
		this.playSegmentAtPosition(index, 0);
	}

	stop() {
		this.engine.stop();
		if (this._playheadAnimId) {
			cancelAnimationFrame(this._playheadAnimId);
			this._playheadAnimId = null;
		}
		this.view.setIsPlaying(false);
		this.view.setPlayheadPosition(0);
	}

	enableSegment(index, enabled) {
		this.engine.enableSegment(index, enabled);
	}

	dispose() {
		this.engine.dispose();
		// Remove canvas and overlay
		if (this.view.canvas.parentNode) this.view.canvas.parentNode.removeChild(this.view.canvas);
		if (this.performanceOverlay && this.performanceOverlay.parentNode) {
			this.performanceOverlay.parentNode.removeChild(this.performanceOverlay);
		}
	}

	_bindEngineEvents() {
		this.engine.addEventListener('fileloaded', (e) => {
			// this._updatePerformanceOverlay();
		});
		this.engine.addEventListener('segmentsliced', (e) => {
			this.view.setSegments(this.engine.getSegments(), this.engine.getEnabledSegments());
			// this._updatePerformanceOverlay();
		});
		this.engine.addEventListener('segmentplay', (e) => {
			this.view.setActiveSegment(e.detail.index);
			// this._updatePerformanceOverlay();
			// Start playhead animation for new segment
			this._startPlayheadAnimation(e.detail.index, 0);
		});
		this.engine.addEventListener('segmentend', (e) => {
			// Find and play the next enabled segment, or stop if none
			const next = this._findNextEnabledSegment(e.detail.index);
			if (next !== null && next !== e.detail.index) {
				this.playSegmentAtPosition(next, 0);
			} else {
				this.view.setActiveSegment(-1);
				// this._updatePerformanceOverlay();
				if (this._playheadAnimId) {
					cancelAnimationFrame(this._playheadAnimId);
					this._playheadAnimId = null;
				}
				this.view.setIsPlaying(false);
				this.view.setPlayheadPosition(0);
			}
		});
		this.engine.addEventListener('segmentenable', (e) => {
			this.view.setEnabledSegments(this.engine.getEnabledSegments());
		});
	}

	_bindViewEvents() {
		this.view.addEventListener('segmentclick', (e) => {
			// Only play if segment is enabled
			if (this.engine.getEnabledSegments()[e.detail.index]) {
				this.playSegment(e.detail.index);
			}
		});
		this.view.addEventListener('segmenttoggle', (e) => {
			this.enableSegment(e.detail.index, e.detail.enabled);
		});
		// Additional events (marker movement, etc.) can be handled here.

		// Waveform click-to-jump
		this.view.addEventListener('waveformjump', (e) => {
			const rel = e.detail.rel;
			const n   = this.engine.segments.length;
			if (n === 0) return;
			// Map rel (0-1) to segment and offset
			const segmentIdx = Math.floor(rel * n);
			const segment    = this.engine.segments[segmentIdx];
			if (!segment) return;
			const segRel     = (rel * n) - segmentIdx;
			const offsetSec  = segRel * segment.duration;
			const enabled    = this.engine.getEnabledSegments();
			if (enabled[segmentIdx]) {
				this.playSegmentAtPosition(segmentIdx, offsetSec);
			} else {
				// Find next enabled segment
				const next = this._findNextEnabledSegment(segmentIdx);
				if (next !== null) {
					this.playSegmentAtPosition(next, 0);
				}
			}
		});
	}

	_precomputeWaveformPeaks(audioBuffer, precomputedWidth) {
		if (!audioBuffer) return [];
		const data            = audioBuffer.getChannelData(0);
		const length          = data.length;
		const peaks           = new Array(precomputedWidth);
		const samplesPerBucket= length / precomputedWidth;
		for (let i = 0; i < precomputedWidth; i++) {
			let start = Math.floor(i * samplesPerBucket);
			let end   = Math.floor((i + 1) * samplesPerBucket);
			if (end > length) end = length;
			let min = 1.0;
			let max = -1.0;
			for (let j = start; j < end; j++) {
				const val = data[j];
				if (val < min) min = val;
				if (val > max) max = val;
			}
			peaks[i] = { min, max };
		}
		return peaks;
	}

	// Performance overlay disabled
	// _createPerformanceOverlay() { ... }
	// _updatePerformanceOverlay() { ... }

	setVolume(val) {
		this.engine.setVolume(val);
	}

	setPan(val) {
		this.engine.setPan(val);
	}
}

export default AudioSlicerController;
