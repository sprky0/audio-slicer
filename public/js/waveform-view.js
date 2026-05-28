import PALETTE from './palette.js';

/**
 * WaveformView: Handles all canvas drawing and user interaction for the waveform, markers, playhead, and segment controls.
 * Emits events for marker movement, segment clicks, and enable/disable toggles.
 * No audio logic; can be instantiated multiple times for parallel slicers.
 *
 * Two pairs of fractions to keep straight:
 *   - viewStart/viewEnd      : fractions of the real audio buffer that the canvas renders.
 *   - selectionStart/selectionEnd : fractions of the real audio buffer for the active slice.
 * Sliders in the UI manipulate selection (within view). "Cut" replaces view with selection.
 */
class WaveformView extends EventTarget {
	constructor(container, options = {}) {
		super();
		this.container = container;
		this.options   = Object.assign({
			height: 200,
			segmentIndicatorHeight: 52,
			peakBucketTarget: 250000, // upper bound on cached peak buckets
			handleTriangleW:  9,
			handleTriangleH:  12,
			handleLineWidth:  3,
			handleHitTolerance: 8,
			startHandleColor: '#2ecc40',
			endHandleColor:   '#e74c3c',
			// Segment indicator inner layout
			indPadTop:        4,
			indNumberH:       24,
			indGap:           4,
			indToggleH:       18,
			indMinToggleW:    28,
		}, options);

		// Audio buffer + peak cache
		this.channelData         = null;
		this.audioBufferLength   = 0;
		this.peakBuckets         = null;
		this.peakBucketSize      = 0;
		this.peakBucketCount     = 0;

		// Slicing state
		this.segments            = [];
		this.enabledSegments     = [];
		this.activeSegment       = -1;
		this.subdivisions        = 2;
		this.playheadPosition    = 0;
		this.isPlaying           = false;
		this.isPaused            = false;

		// View window (real-buffer fractions) — what the canvas displays
		this.viewStart           = 0;
		this.viewEnd             = 1;

		// Selection (real-buffer fractions) — what gets sliced
		this.selectionStart      = 0;
		this.selectionEnd        = 1;

		// Drag state
		this._draggingHandle     = null; // 'start' | 'end' | null
		this._dragMoved          = false;

		// Pixel sizing
		this._cssWidth           = 0;
		this._cssHeight          = this.options.height + this.options.segmentIndicatorHeight;
		this._dpr                = window.devicePixelRatio || 1;

		// rAF throttle
		this._drawScheduled      = false;

		// Help message (inserted before canvas)
		this.helpMsg = document.createElement('div');
		this.helpMsg.style.color    = '#aaa';
		this.helpMsg.style.fontSize = '13px';
		this.helpMsg.style.margin   = '4px 0 8px 0';
		this.helpMsg.textContent    = 'Drag the green/red triangle handles or the sliders to pick a region. Click a numbered tile to play it; click its on/off row to mute it.';
		this.container.appendChild(this.helpMsg);

		// Canvas
		this.canvas = document.createElement('canvas');
		this.canvas.className     = 'slicer-waveform-canvas';
		this.canvas.style.display = 'block';
		this.canvas.style.width   = '100%';
		this.canvas.style.height  = this._cssHeight + 'px';
		this.canvas.style.cursor  = 'pointer';
		this.canvas.style.touchAction = 'none';
		this.ctx                  = this.canvas.getContext('2d');
		this.container.appendChild(this.canvas);

		this._resizeCanvas();

		this._resizeObserver = new ResizeObserver(() => this._handleResize());
		this._resizeObserver.observe(this.container);

		this._bindCanvasEvents();
	}

	setAudioBuffer(audioBuffer) {
		if (audioBuffer) {
			this.channelData       = audioBuffer.getChannelData(0);
			this.audioBufferLength = audioBuffer.length;
			this._buildPeakCache();
		} else {
			this.channelData       = null;
			this.audioBufferLength = 0;
			this.peakBuckets       = null;
			this.peakBucketCount   = 0;
		}
		this._scheduleDraw();
	}

	setView(start, end) {
		this.viewStart = start;
		this.viewEnd   = end;
		this._scheduleDraw();
	}

	setSegments(segments, enabledSegments) {
		this.segments        = segments || [];
		this.enabledSegments = enabledSegments || [];
		this._scheduleDraw();
	}

	setActiveSegment(index) {
		this.activeSegment = index;
		this._scheduleDraw();
	}

	setPlayheadPosition(position) {
		this.playheadPosition = position;
		this._scheduleDraw();
	}

	setIsPlaying(isPlaying) {
		this.isPlaying = isPlaying;
		this._scheduleDraw();
	}

	setIsPaused(isPaused) {
		this.isPaused = isPaused;
		this._scheduleDraw();
	}

	setSelection(start, end, subdivisions) {
		this.selectionStart = start;
		this.selectionEnd   = end;
		this.subdivisions   = subdivisions;
		this._scheduleDraw();
	}

	setEnabledSegments(enabledArray) {
		this.enabledSegments = enabledArray;
		this._scheduleDraw();
	}

	dispose() {
		if (this._resizeObserver) {
			this._resizeObserver.disconnect();
			this._resizeObserver = null;
		}
		if (this.canvas && this.canvas.parentNode) {
			this.canvas.parentNode.removeChild(this.canvas);
		}
		if (this.helpMsg && this.helpMsg.parentNode) {
			this.helpMsg.parentNode.removeChild(this.helpMsg);
		}
	}

	_handleResize() {
		this._resizeCanvas();
		this._scheduleDraw();
	}

	_resizeCanvas() {
		const dpr      = window.devicePixelRatio || 1;
		// Use the canvas's own laid-out width — the container's clientWidth includes
		// padding, which would push the right-edge handle outside the canvas.
		const measured = this.canvas.getBoundingClientRect().width || this.canvas.clientWidth;
		const cssWidth = Math.max(1, Math.floor(measured));
		const cssHeight= this.options.height + this.options.segmentIndicatorHeight;

		this._cssWidth  = cssWidth;
		this._cssHeight = cssHeight;
		this._dpr       = dpr;

		this.canvas.style.height = cssHeight + 'px';
		this.canvas.width        = Math.floor(cssWidth  * dpr);
		this.canvas.height       = Math.floor(cssHeight * dpr);
		this.ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
	}

	_buildPeakCache() {
		if (!this.channelData || this.audioBufferLength === 0) {
			this.peakBuckets     = null;
			this.peakBucketCount = 0;
			this.peakBucketSize  = 0;
			return;
		}
		const target          = this.options.peakBucketTarget;
		const samplesPerBucket= Math.max(1, Math.ceil(this.audioBufferLength / target));
		const numBuckets      = Math.ceil(this.audioBufferLength / samplesPerBucket);
		const buckets         = new Float32Array(numBuckets * 2);
		const data            = this.channelData;
		const len             = this.audioBufferLength;
		for (let b = 0; b < numBuckets; b++) {
			const start = b * samplesPerBucket;
			const end   = Math.min(len, start + samplesPerBucket);
			let min = 1.0;
			let max = -1.0;
			for (let j = start; j < end; j++) {
				const v = data[j];
				if (v < min) min = v;
				if (v > max) max = v;
			}
			buckets[b * 2]     = min;
			buckets[b * 2 + 1] = max;
		}
		this.peakBuckets     = buckets;
		this.peakBucketCount = numBuckets;
		this.peakBucketSize  = samplesPerBucket;
	}

	_scheduleDraw() {
		if (this._drawScheduled) return;
		this._drawScheduled = true;
		requestAnimationFrame(() => {
			this._drawScheduled = false;
			this._draw();
		});
	}

	draw() {
		this._scheduleDraw();
	}

	// Real-buffer fraction -> CSS pixel x within canvas (based on view window).
	_fractionToX(frac) {
		const span = this.viewEnd - this.viewStart;
		if (span <= 0) return 0;
		return ((frac - this.viewStart) / span) * this._cssWidth;
	}

	// CSS pixel x -> real-buffer fraction (clamped to view).
	_xToFraction(x) {
		const span = this.viewEnd - this.viewStart;
		if (span <= 0) return this.viewStart;
		const frac = this.viewStart + (x / this._cssWidth) * span;
		return Math.max(this.viewStart, Math.min(this.viewEnd, frac));
	}

	_draw() {
		const ctx             = this.ctx;
		const width           = this._cssWidth;
		const height          = this.options.height;
		const indicatorHeight = this.options.segmentIndicatorHeight;
		ctx.clearRect(0, 0, width, height + indicatorHeight);

		this._drawWaveform(width, height);
		this._drawSelectionOverlay(width, height);
		this._drawSubdivisions(width, height);
		this._drawPlayhead(width, height);
		this._drawSegmentIndicators(width, height, indicatorHeight);
		this._drawHandles(width, height);
	}

	_drawWaveform(width, height) {
		if (!this.channelData || this.audioBufferLength === 0) return;
		const ctx = this.ctx;
		const amp = height / 2;
		const mid = amp;

		const startSample = Math.floor(this.viewStart * this.audioBufferLength);
		const endSample   = Math.max(startSample + 1, Math.ceil(this.viewEnd * this.audioBufferLength));
		const samplesInRange = endSample - startSample;
		const samplesPerPx   = samplesInRange / width;

		ctx.beginPath();
		ctx.strokeStyle = '#ffffff';
		ctx.lineWidth   = 1;

		const useBuckets = this.peakBuckets && samplesPerPx >= this.peakBucketSize;

		if (useBuckets) {
			const buckets        = this.peakBuckets;
			const bucketSize     = this.peakBucketSize;
			const startBucketF   = startSample / bucketSize;
			const endBucketF     = endSample   / bucketSize;
			const bucketsPerPx   = (endBucketF - startBucketF) / width;
			const totalBuckets   = this.peakBucketCount;
			for (let px = 0; px < width; px++) {
				let bStart = Math.floor(startBucketF + px       * bucketsPerPx);
				let bEnd   = Math.floor(startBucketF + (px + 1) * bucketsPerPx) + 1;
				if (bStart < 0) bStart = 0;
				if (bEnd > totalBuckets) bEnd = totalBuckets;
				if (bEnd <= bStart) bEnd = bStart + 1;
				let min = 1.0;
				let max = -1.0;
				for (let b = bStart; b < bEnd; b++) {
					const bMin = buckets[b * 2];
					const bMax = buckets[b * 2 + 1];
					if (bMin < min) min = bMin;
					if (bMax > max) max = bMax;
				}
				if (min > max) { min = 0; max = 0; }
				const x = px + 0.5;
				ctx.moveTo(x, mid + min * amp);
				ctx.lineTo(x, mid + max * amp);
			}
		} else {
			const data = this.channelData;
			const len  = this.audioBufferLength;
			for (let px = 0; px < width; px++) {
				let sStart = startSample + Math.floor(px       * samplesPerPx);
				let sEnd   = startSample + Math.floor((px + 1) * samplesPerPx) + 1;
				if (sStart < 0) sStart = 0;
				if (sEnd > len) sEnd = len;
				if (sEnd <= sStart) sEnd = sStart + 1;
				let min = 1.0;
				let max = -1.0;
				for (let j = sStart; j < sEnd; j++) {
					const v = data[j];
					if (v < min) min = v;
					if (v > max) max = v;
				}
				if (min > max) { min = 0; max = 0; }
				const x = px + 0.5;
				ctx.moveTo(x, mid + min * amp);
				ctx.lineTo(x, mid + max * amp);
			}
		}
		ctx.stroke();
	}

	_drawSelectionOverlay(width, height) {
		const startX = this._fractionToX(this.selectionStart);
		const endX   = this._fractionToX(this.selectionEnd);
		if (endX <= startX) return;
		const ctx = this.ctx;
		// Dim everything outside the selection.
		ctx.fillStyle = 'rgba(0, 0, 0, 0.45)';
		if (startX > 0)      ctx.fillRect(0, 0, startX, height);
		if (endX   < width)  ctx.fillRect(endX, 0, width - endX, height);
		// Light blue tint inside the selection.
		ctx.fillStyle = 'rgba(52, 152, 219, 0.10)';
		ctx.fillRect(startX, 0, endX - startX, height);
	}

	_drawSubdivisions(width, height) {
		if (this.subdivisions <= 1) return;
		const ctx          = this.ctx;
		const startX       = this._fractionToX(this.selectionStart);
		const endX         = this._fractionToX(this.selectionEnd);
		const selWidth     = endX - startX;
		if (selWidth <= 0) return;
		const segmentWidth = selWidth / this.subdivisions;

		if (this.isPlaying && this.activeSegment !== -1) {
			ctx.fillStyle = 'rgba(52, 152, 219, 0.2)';
			ctx.fillRect(startX + segmentWidth * this.activeSegment, 0, segmentWidth, height);
		}

		ctx.strokeStyle = 'rgba(52, 152, 219, 0.5)';
		ctx.lineWidth   = 1;
		for (let i = 1; i < this.subdivisions; i++) {
			const x = startX + segmentWidth * i;
			ctx.beginPath();
			ctx.moveTo(x, 0);
			ctx.lineTo(x, height);
			ctx.stroke();
		}
	}

	_drawPlayhead(width, height) {
		if (!(this.isPlaying || this.isPaused) || this.activeSegment === -1) return;
		const ctx          = this.ctx;
		const startX       = this._fractionToX(this.selectionStart);
		const endX         = this._fractionToX(this.selectionEnd);
		const selWidth     = endX - startX;
		if (selWidth <= 0) return;
		const segCount     = this.subdivisions > 0 ? this.subdivisions : 1;
		const segmentWidth = selWidth / segCount;
		const segmentStart = startX + segmentWidth * this.activeSegment;
		const playheadX    = segmentStart + (segmentWidth * this.playheadPosition);

		ctx.strokeStyle = '#ffffff';
		ctx.lineWidth   = 2;
		ctx.beginPath();
		ctx.moveTo(playheadX, 0);
		ctx.lineTo(playheadX, height);
		ctx.stroke();
	}

	// Layout for one segment's indicator block, given the waveform `height`.
	_indicatorLayout(height) {
		const o = this.options;
		const numberY = height + o.indPadTop;
		const toggleY = numberY + o.indNumberH + o.indGap;
		return { numberY, numberH: o.indNumberH, toggleY, toggleH: o.indToggleH };
	}

	_drawSegmentIndicators(width, height, indicatorHeight) {
		if (this.segments.length === 0) return;
		const ctx          = this.ctx;
		const startX       = this._fractionToX(this.selectionStart);
		const endX         = this._fractionToX(this.selectionEnd);
		const selWidth     = endX - startX;
		if (selWidth <= 0) return;
		const segmentWidth = selWidth / this.segments.length;
		const layout       = this._indicatorLayout(height);
		const minToggleW   = this.options.indMinToggleW;

		for (let i = 0; i < this.segments.length; i++) {
			const x       = startX + segmentWidth * i;
			const boxW    = Math.max(1, segmentWidth - 2);
			const color   = PALETTE[i % PALETTE.length];
			const enabled = this.enabledSegments[i];

			ctx.save();

			// --- Number rectangle (top) ---
			ctx.globalAlpha = enabled ? 1.0 : 0.4;
			ctx.fillStyle   = color;
			ctx.fillRect(x, layout.numberY, boxW, layout.numberH);

			if (i === this.activeSegment) {
				ctx.strokeStyle = '#ffffff';
				ctx.lineWidth   = 2;
				ctx.strokeRect(x + 1, layout.numberY + 1, boxW - 2, layout.numberH - 2);
			}

			ctx.globalAlpha  = 1.0;
			ctx.fillStyle    = '#ffffff';
			ctx.font         = 'bold 14px sans-serif';
			ctx.textAlign    = 'center';
			ctx.textBaseline = 'middle';
			ctx.fillText(`${i + 1}`, x + boxW / 2, layout.numberY + layout.numberH / 2);

			// --- on/off toggle rectangle (bottom) ---
			if (boxW >= minToggleW) {
				ctx.fillStyle = enabled ? '#2ecc40' : '#e74c3c';
				ctx.fillRect(x, layout.toggleY, boxW, layout.toggleH);
				ctx.fillStyle = '#ffffff';
				ctx.font      = 'bold 11px sans-serif';
				ctx.fillText(enabled ? 'on' : 'off', x + boxW / 2, layout.toggleY + layout.toggleH / 2 + 0.5);
			}

			ctx.restore();
		}
	}

	_drawHandles(width, height) {
		if (!this.channelData) return;
		this._drawHandle(this._fractionToX(this.selectionStart), this.options.startHandleColor, height);
		this._drawHandle(this._fractionToX(this.selectionEnd),   this.options.endHandleColor,   height);
	}

	_drawHandle(x, color, height) {
		const ctx  = this.ctx;
		const triW = this.options.handleTriangleW;
		const triH = this.options.handleTriangleH;

		// Vertical line from below the triangle down to the bottom of the waveform.
		ctx.strokeStyle = color;
		ctx.lineWidth   = this.options.handleLineWidth;
		ctx.beginPath();
		ctx.moveTo(x, triH);
		ctx.lineTo(x, height);
		ctx.stroke();

		// Triangle pointing down, sitting at the top.
		ctx.fillStyle = color;
		ctx.beginPath();
		ctx.moveTo(x - triW, 0);
		ctx.lineTo(x + triW, 0);
		ctx.lineTo(x,        triH);
		ctx.closePath();
		ctx.fill();

		// Subtle outline so handle pops against the waveform.
		ctx.strokeStyle = 'rgba(0,0,0,0.45)';
		ctx.lineWidth   = 1;
		ctx.stroke();
	}

	_hitTestHandle(x, y) {
		const triH = this.options.handleTriangleH;
		const triW = this.options.handleTriangleW;
		const tol  = this.options.handleHitTolerance;
		const startX = this._fractionToX(this.selectionStart);
		const endX   = this._fractionToX(this.selectionEnd);

		// Generous hit box covering the triangle and a bit of the line below it.
		const inTopBand = y >= 0 && y <= triH + tol;
		const inLineBand = y > triH && y <= this.options.height;

		const nearStart = Math.abs(x - startX) <= Math.max(triW, tol);
		const nearEnd   = Math.abs(x - endX)   <= Math.max(triW, tol);

		if (inTopBand) {
			// Pick the closer handle if both are nearby.
			if (nearStart && nearEnd) {
				return Math.abs(x - startX) <= Math.abs(x - endX) ? 'start' : 'end';
			}
			if (nearStart) return 'start';
			if (nearEnd)   return 'end';
		}
		if (inLineBand) {
			// Smaller tolerance once below the triangle, so it doesn't steal waveform clicks.
			if (Math.abs(x - startX) <= tol / 2) return 'start';
			if (Math.abs(x - endX)   <= tol / 2) return 'end';
		}
		return null;
	}

	_bindCanvasEvents() {
		this.canvas.addEventListener('pointerdown', (e) => {
			if (e.button !== undefined && e.button !== 0) return;
			const rect = this.canvas.getBoundingClientRect();
			const x    = e.clientX - rect.left;
			const y    = e.clientY - rect.top;
			const hit  = this._hitTestHandle(x, y);
			if (hit) {
				this._draggingHandle = hit;
				this._dragMoved      = false;
				try { this.canvas.setPointerCapture(e.pointerId); } catch (_) {}
				this.canvas.style.cursor = 'ew-resize';
				e.preventDefault();
			}
		});

		this.canvas.addEventListener('pointermove', (e) => {
			const rect = this.canvas.getBoundingClientRect();
			const x    = e.clientX - rect.left;
			const y    = e.clientY - rect.top;

			if (this._draggingHandle) {
				this._dragMoved = true;
				const frac = this._xToFraction(Math.max(0, Math.min(this._cssWidth, x)));
				this.dispatchEvent(new CustomEvent('handledrag', {
					detail: { which: this._draggingHandle, fraction: frac }
				}));
				return;
			}

			// Hover cursor feedback.
			const hit = this._hitTestHandle(x, y);
			this.canvas.style.cursor = hit ? 'ew-resize' : 'pointer';
		});

		const endDrag = (e) => {
			if (this._draggingHandle) {
				this._draggingHandle = null;
				try { this.canvas.releasePointerCapture(e.pointerId); } catch (_) {}
				this.canvas.style.cursor = 'pointer';
			}
		};
		this.canvas.addEventListener('pointerup',     endDrag);
		this.canvas.addEventListener('pointercancel', endDrag);

		this.canvas.addEventListener('click', (e) => {
			// Drag-then-release shouldn't fire a jump.
			if (this._dragMoved) {
				this._dragMoved = false;
				return;
			}
			const rect            = this.canvas.getBoundingClientRect();
			const x               = e.clientX - rect.left;
			const y               = e.clientY - rect.top;
			const width           = this._cssWidth;
			const height          = this.options.height;
			const indicatorHeight = this.options.segmentIndicatorHeight;

			// Waveform area — only meaningful inside the current selection.
			if (y >= 0 && y <= height) {
				const startX = this._fractionToX(this.selectionStart);
				const endX   = this._fractionToX(this.selectionEnd);
				if (endX > startX && x >= startX && x <= endX) {
					const rel = (x - startX) / (endX - startX);
					this.dispatchEvent(new CustomEvent('waveformjump', { detail: { rel } }));
				}
				return;
			}

			// Segment indicator strip.
			if (this.segments.length > 0 && y > height && y <= height + indicatorHeight) {
				const startX       = this._fractionToX(this.selectionStart);
				const endX         = this._fractionToX(this.selectionEnd);
				const selWidth     = endX - startX;
				if (selWidth <= 0) return;
				const segmentWidth = selWidth / this.segments.length;
				const layout       = this._indicatorLayout(height);
				const minToggleW   = this.options.indMinToggleW;
				const inToggleRow  = y >= layout.toggleY && y <= layout.toggleY + layout.toggleH;
				for (let i = 0; i < this.segments.length; i++) {
					const segX = startX + segmentWidth * i;
					if (x >= segX && x < segX + segmentWidth) {
						const boxW = Math.max(1, segmentWidth - 2);
						if (inToggleRow && boxW >= minToggleW) {
							this.dispatchEvent(new CustomEvent('segmenttoggle', { detail: { index: i, enabled: !this.enabledSegments[i] } }));
						} else {
							this.dispatchEvent(new CustomEvent('segmentclick', { detail: { index: i } }));
						}
						break;
					}
				}
			}
		});
	}
}

export default WaveformView;
