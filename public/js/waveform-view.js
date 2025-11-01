/**
 * WaveformView: Handles all canvas drawing and user interaction for the waveform, markers, playhead, and segment controls.
 * Emits events for marker movement, segment clicks, and enable/disable toggles.
 * No audio logic; can be instantiated multiple times for parallel slicers.
 */
class WaveformView extends EventTarget {
	constructor(container, options = {}) {
		super();
		this.container = container;
		this.options   = Object.assign({
			width: 800,
			height: 200,
			precomputedWidth: 1500,
			segmentIndicatorHeight: 24,
		}, options);

		// State
		this.waveformPeaks   = [];
		this.segments        = [];
		this.enabledSegments = [];
		this.activeSegment   = -1;
		this.startPosition   = 0;
		this.endPosition     = 1;
		this.subdivisions    = 2;
		this.playheadPosition= 0;
		this.isPlaying       = false;

		// Create canvas
		this.canvas = document.createElement('canvas');
		this.canvas.width  = this.options.width;
		this.canvas.height = this.options.height + this.options.segmentIndicatorHeight;
		this.ctx           = this.canvas.getContext('2d');
		this.canvas.style.display = 'block';
		this.canvas.style.cursor  = 'pointer'; // Indicate interactivity
		this.container.appendChild(this.canvas);

		// Add help message
		this.helpMsg = document.createElement('div');
		this.helpMsg.style.color     = '#aaa';
		this.helpMsg.style.fontSize  = '13px';
		this.helpMsg.style.margin    = '4px 0 8px 0';
		this.helpMsg.textContent     = 'Click a segment indicator below the waveform to play. Click the colored circle to enable/disable a segment.';
		this.container.insertBefore(this.helpMsg, this.canvas);

		// For offscreen waveform rendering
		this.offscreenCanvas = document.createElement('canvas');
		this.offscreenCanvas.width  = this.options.width;
		this.offscreenCanvas.height = this.options.height;
		this.offscreenCtx           = this.offscreenCanvas.getContext('2d');

		// Bind events
		this._bindCanvasEvents();
	}

	setWaveformPeaks(peaks) {
		this.waveformPeaks = peaks;
		this._renderOffscreenWaveform();
		this.draw();
	}

	setSegments(segments, enabledSegments) {
		this.segments        = segments || [];
		this.enabledSegments = enabledSegments || [];
		this.draw();
	}

	setActiveSegment(index) {
		this.activeSegment = index;
		this.draw();
	}

	setPlayheadPosition(position) {
		this.playheadPosition = position;
		this.draw();
	}

	setIsPlaying(isPlaying) {
		this.isPlaying = isPlaying;
		this.draw();
	}

	setSelection(start, end, subdivisions) {
		this.startPosition = start;
		this.endPosition   = end;
		this.subdivisions  = subdivisions;
		this.draw();
	}

	setEnabledSegments(enabledArray) {
		this.enabledSegments = enabledArray;
		this.draw();
	}

	_renderOffscreenWaveform() {
		const ctx    = this.offscreenCtx;
		const width  = this.offscreenCanvas.width;
		const height = this.offscreenCanvas.height;
		ctx.clearRect(0, 0, width, height);

		if (!this.waveformPeaks.length) return;

		const dpr        = window.devicePixelRatio || 1;
		const targetWidth= this.waveformPeaks.length;
		const scale      = targetWidth / width;
		const amp        = height / 2 / dpr;

		ctx.beginPath();
		ctx.strokeStyle = '#ffffff';
		ctx.lineWidth   = 1;

		for (let i = 0; i < width; i++) {
			const index      = Math.floor(i * scale);
			const { min, max } = this.waveformPeaks[index] || { min: 0, max: 0 };
			const x          = i / dpr;

			ctx.moveTo(x, (1 + min) * amp);
			ctx.lineTo(x, (1 + max) * amp);
		}
		ctx.stroke();
	}

	draw() {
		const ctx             = this.ctx;
		const width           = this.canvas.width;
		const height          = this.options.height;
		const indicatorHeight = this.options.segmentIndicatorHeight;
		ctx.clearRect(0, 0, width, height + indicatorHeight);

		// Draw waveform (zoomed to selected range)
		const sourceWidth = this.offscreenCanvas.width;
		const startPx = Math.floor(this.startPosition * sourceWidth);
		const endPx = Math.ceil(this.endPosition * sourceWidth);
		const rangePx = Math.max(1, endPx - startPx);
		ctx.drawImage(
			this.offscreenCanvas,
			startPx, 0, rangePx, this.offscreenCanvas.height, // source rect
			0, 0, this.canvas.width, this.options.height      // dest rect (full width)
		);

		// Draw selection highlight
		const startX = this.startPosition * width;
		const endX   = this.endPosition * width;
		ctx.fillStyle = 'rgba(52, 152, 219, 0.1)';
		ctx.fillRect(startX, 0, endX - startX, height);

		// Draw subdivisions and active segment
		if (this.subdivisions > 1) {
			const segmentWidth = (endX - startX) / this.subdivisions;

			// Active segment highlight
			if (this.isPlaying && this.activeSegment !== -1) {
				const segmentStart = startX + (segmentWidth * this.activeSegment);
				ctx.fillStyle = 'rgba(52, 152, 219, 0.2)';
				ctx.fillRect(segmentStart, 0, segmentWidth, height);
			}

			// Subdivision lines
			ctx.strokeStyle = 'rgba(52, 152, 219, 0.5)';
			ctx.lineWidth   = 1;
			for (let i = 1; i < this.subdivisions; i++) {
				const x = startX + (segmentWidth * i);
				ctx.beginPath();
				ctx.moveTo(x, 0);
				ctx.lineTo(x, height);
				ctx.stroke();
			}
		}

		// Draw playhead
		if (this.isPlaying && this.activeSegment !== -1) {
			const segmentWidth = (endX - startX) / this.subdivisions;
			const segmentStart = startX + (segmentWidth * this.activeSegment);
			const playheadX    = segmentStart + (segmentWidth * this.playheadPosition);

			ctx.strokeStyle = '#ffffff';
			ctx.lineWidth   = 2;
			ctx.beginPath();
			ctx.moveTo(playheadX, 0);
			ctx.lineTo(playheadX, height);
			ctx.stroke();
		}

		// Draw segment indicators (below waveform)
		if (this.segments.length > 0) {
			const segmentWidth = (endX - startX) / this.segments.length;
			for (let i = 0; i < this.segments.length; i++) {
				const x = startX + (segmentWidth * i);
				ctx.save();
				ctx.globalAlpha = this.enabledSegments[i] ? 1.0 : 0.3;
				ctx.fillStyle   = (i === this.activeSegment) ? '#3498db' : '#888';
				ctx.fillRect(x, height + 4, segmentWidth - 2, indicatorHeight - 8);

				// Draw segment number
				ctx.fillStyle = '#fff';
				ctx.font      = 'bold 14px sans-serif';
				ctx.textAlign = 'center';
				ctx.textBaseline = 'middle';
				ctx.fillText(`${i + 1}`, x + segmentWidth / 2, height + indicatorHeight / 2);

				// Draw enable/disable icon (simple circle)
				ctx.beginPath();
				ctx.arc(x + segmentWidth - 14, height + indicatorHeight / 2, 7, 0, 2 * Math.PI);
				ctx.fillStyle = this.enabledSegments[i] ? '#2ecc40' : '#e74c3c';
				ctx.fill();
				ctx.restore();
			}
		}
	}

	_bindCanvasEvents() {
		this.canvas.addEventListener('click', (e) => {
			const rect            = this.canvas.getBoundingClientRect();
			const x               = e.clientX - rect.left;
			const y               = e.clientY - rect.top;
			const width           = this.canvas.width;
			const height          = this.options.height;
			const indicatorHeight = this.options.segmentIndicatorHeight;

			// If click is on waveform area (not segment indicators)
			if (y >= 0 && y <= height) {
				// Compute relative position within selected region (0-1)
				const startX = this.startPosition * width;
				const endX   = this.endPosition * width;
				if (x >= startX && x <= endX) {
					const rel = (x - startX) / (endX - startX);
					this.dispatchEvent(new CustomEvent('waveformjump', { detail: { rel } }));
					return;
				}
			}

			// Check if click is on a segment indicator
			if (this.segments.length > 0 && y > height) {
				const startX      = this.startPosition * width;
				const endX        = this.endPosition * width;
				const segmentWidth= (endX - startX) / this.segments.length;
				for (let i = 0; i < this.segments.length; i++) {
					const segX = startX + (segmentWidth * i);
					if (x >= segX && x < segX + segmentWidth) {
						// If click is on enable/disable icon (right side)
						if (x > segX + segmentWidth - 28) {
							this.dispatchEvent(new CustomEvent('segmenttoggle', { detail: { index: i, enabled: !this.enabledSegments[i] } }));
						} else {
							this.dispatchEvent(new CustomEvent('segmentclick', { detail: { index: i } }));
						}
						break;
					}
				}
			}
		});
		// Additional events (marker dragging, etc.) can be added here as needed.
	}
}

export default WaveformView;
