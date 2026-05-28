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
		this.isPaused           = false;
		this.pausedSegment      = null;
		this.pausedOffset       = 0;
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
			// Hand the raw buffer to the view; it builds its own peak cache.
			this.view.setAudioBuffer(this.engine.audioBuffer);

			// Reset the view window to the full buffer.
			this.setView(0, 1);

			// Immediately slice but do NOT play first segment.
			this.slice(0, 1, 2, { autoPlay: false });
		} catch (err) {
			console.error('Error loading file:', err);
		}
	}

	setView(start, end) {
		this.view.setView(start, end);
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
		// If requested segment is disabled or out of bounds, find first enabled
		const enabled = this.engine.getEnabledSegments();
		let startIndex = index;
		if (
			typeof startIndex !== "number" ||
			startIndex < 0 ||
			startIndex >= enabled.length ||
			!enabled[startIndex]
		) {
			const next = this._findNextEnabledSegment();
			if (next !== null) {
				startIndex = next;
				offsetSec = 0;
			} else {
				this.stop();
				return;
			}
		}
		this.isPaused = false;
		this.pausedSegment = null;
		this.pausedOffset = 0;
		this.view.setIsPaused(false);
		this.engine.playSegment(startIndex, offsetSec);
		this._startPlayheadAnimation(startIndex, offsetSec);
		// Emit playstatechange event
		this.container.dispatchEvent(new CustomEvent('playstatechange', { detail: { state: 'playing' } }));
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
		let idx = typeof currentIndex === "number" ? currentIndex : -1;
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
		this.isPaused = false;
		this.pausedSegment = null;
		this.pausedOffset = 0;
		if (this._playheadAnimId) {
			cancelAnimationFrame(this._playheadAnimId);
			this._playheadAnimId = null;
		}
		this.view.setIsPlaying(false);
		this.view.setIsPaused(false);
		this.view.setPlayheadPosition(0);
		// Emit playstatechange event
		this.container.dispatchEvent(new CustomEvent('playstatechange', { detail: { state: 'stopped' } }));
	}

	pause() {
		if (this.engine.isPlaying && this.engine.activeSegment !== -1) {
			const segmentIdx = this.engine.activeSegment;
			const segment = this.engine.segments[segmentIdx];
			if (segment) {
				const elapsed = this.engine.audioContext.currentTime - this.engine.playStartTime;
				this.pausedSegment = segmentIdx;
				this.pausedOffset = Math.min(segment.duration, Math.max(0, elapsed));
				this.isPaused = true;
			}
			this.engine.stop();
			if (this._playheadAnimId) {
				cancelAnimationFrame(this._playheadAnimId);
				this._playheadAnimId = null;
			}
			this.view.setIsPlaying(false);
			this.view.setIsPaused(true);
			this.view.setPlayheadPosition(this.pausedOffset / segment.duration);
			// Emit playstatechange event
			this.container.dispatchEvent(new CustomEvent('playstatechange', { detail: { state: 'paused' } }));
		}
	}

	resume() {
		if (this.isPaused && this.pausedSegment !== null) {
			this.view.setIsPaused(false);
			this.playSegmentAtPosition(this.pausedSegment, this.pausedOffset);
			// Emit playstatechange event
			this.container.dispatchEvent(new CustomEvent('playstatechange', { detail: { state: 'playing' } }));
		}
	}

	enableSegment(index, enabled) {
		this.engine.enableSegment(index, enabled);
	}

	dispose() {
		this.engine.dispose();
		if (this.view && typeof this.view.dispose === 'function') {
			this.view.dispose();
		}
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
			const disabledIndex = e.detail.index;
			const isNowDisabled = !e.detail.enabled;
			const isPlaying = this.engine.isPlaying && this.engine.activeSegment !== -1;
			const isPaused = this.isPaused && this.pausedSegment !== null;
			const activeSegment = this.engine.activeSegment;
			const pausedSegment = this.pausedSegment;

			// If the playhead is in the segment that was just disabled
			if (isNowDisabled && (
				(isPlaying && activeSegment === disabledIndex) ||
				(isPaused && pausedSegment === disabledIndex)
			)) {
				const next = this._findNextEnabledSegment(disabledIndex);
				if (next !== null) {
					if (isPlaying) {
						this.playSegmentAtPosition(next, 0);
					} else if (isPaused) {
						this.pausedSegment = next;
						this.pausedOffset = 0;
						this.view.setIsPlaying(false);
						this.view.setActiveSegment(next);
						this.view.setPlayheadPosition(0);
					}
				} else {
					// No enabled segments left, stop and reset playhead
					this.stop();
				}
			}
		});
	}

	_bindViewEvents() {
		this.view.addEventListener('segmentclick', (e) => {
			// Only play if segment is enabled
			if (this.engine.getEnabledSegments()[e.detail.index]) {
				if (this.isPaused) {
					// Move playhead to segment, do not play
					this.pausedSegment = e.detail.index;
					this.pausedOffset = 0;
					this.view.setIsPlaying(false);
					this.view.setActiveSegment(e.detail.index);
					this.view.setPlayheadPosition(0);
				} else {
					this.playSegment(e.detail.index);
				}
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
				if (this.isPaused) {
					// Move playhead, do not play
					this.pausedSegment = segmentIdx;
					this.pausedOffset = offsetSec;
					this.view.setIsPlaying(false);
					this.view.setActiveSegment(segmentIdx);
					this.view.setPlayheadPosition(offsetSec / segment.duration);
				} else {
					this.playSegmentAtPosition(segmentIdx, offsetSec);
				}
			} else {
				// Find next enabled segment
				const next = this._findNextEnabledSegment(segmentIdx);
				if (next !== null) {
					if (this.isPaused) {
						this.pausedSegment = next;
						this.pausedOffset = 0;
						this.view.setIsPlaying(false);
						this.view.setActiveSegment(next);
						this.view.setPlayheadPosition(0);
					} else {
						this.playSegmentAtPosition(next, 0);
					}
				}
			}
		});
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
