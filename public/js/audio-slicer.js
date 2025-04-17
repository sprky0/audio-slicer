class AudioSlicer {
	constructor() {
		this.initializeProperties();
		this.setupAudioContext();
		this.setupCanvas();
		this.setupEventListeners();
	}

	initializeProperties() {
		this.originalBuffer = null; // For reset
		this.audioBuffer = null;
		this.audioSource = null;
		this.startPosition = 0;
		this.endPosition = 1;
		this.subdivisions = 2;
		this.segments = [];
		this.isPlaying = false;
		this.activeSegment = -1;
		this.nextSegmentIndex = null; // If user clicks another "Play" while playing, queue it
		this.draggingMarker = null;

		this.playheadPosition = 0;
		this.animationFrameId = null;

		// Precompute peaks at a certain resolution
		this.waveformPeaks = [];
		this.precomputedWidth = 1500;

		// Offscreen canvas for static waveform
		this.offscreenCanvas = document.createElement('canvas');
		this.offscreenCtx = this.offscreenCanvas.getContext('2d');
	}

	setupAudioContext() {
		this.audioContext = new (window.AudioContext || window.webkitAudioContext)();
	}

	setupCanvas() {
		this.canvas = document.getElementById('waveformCanvas');
		this.ctx = this.canvas.getContext('2d');
		this.resizeCanvas();

		// Re-render if the user resizes the browser
		window.addEventListener('resize', () => this.resizeCanvas());
	}

	resizeCanvas() {
		const dpr = window.devicePixelRatio || 1;
		const rect = this.canvas.getBoundingClientRect();
		
		this.canvas.width = rect.width * dpr;
		this.canvas.height = rect.height * dpr;
		this.ctx.scale(dpr, dpr);

		// Make offscreen match the main canvas size
		this.offscreenCanvas.width = this.canvas.width;
		this.offscreenCanvas.height = this.canvas.height;
		this.offscreenCtx.scale(dpr, dpr);

		// Re-draw the static waveform if we already have an audioBuffer loaded
		if (this.audioBuffer) {
			this.renderOffscreenCanvas();
		}

		this.drawWaveform();
	}

	setupEventListeners() {
		const dropzone = document.getElementById('dropzone');
		const subdivSelect = document.getElementById('subdivisions');
		const setButton = document.getElementById('setButton');
		const startMarker = document.getElementById('startMarker');
		const endMarker = document.getElementById('endMarker');
		
		this.setButton = setButton;

		// Drag-and-drop
		dropzone.addEventListener('dragover', (e) => {
			e.preventDefault();
			dropzone.classList.add('drag-over');
		});
		dropzone.addEventListener('dragleave', () => {
			dropzone.classList.remove('drag-over');
		});
		dropzone.addEventListener('drop', async (e) => {
			e.preventDefault();
			dropzone.classList.remove('drag-over');
			
			const file = e.dataTransfer.files[0];
			if (file && (file.type === 'audio/wav' || file.type === 'audio/mp3')) {
				await this.loadAudioFile(file);
			}
		});

		// Click to open file
		dropzone.addEventListener('click', () => {
			const input = document.createElement('input');
			input.type = 'file';
			input.accept = 'audio/wav,audio/mp3';
			input.onchange = async (e) => {
				const file = e.target.files[0];
				if (file) {
					await this.loadAudioFile(file);
				}
			};
			input.click();
		});

		// Subdivisions
		subdivSelect.addEventListener('change', (e) => {
			this.subdivisions = parseInt(e.target.value);
			this.drawWaveform();
		});

		// SET / RESET
		setButton.addEventListener('click', () => {
			if (setButton.textContent === 'SET') {
				this.finalizeSlicing();
			} else {
				this.resetSlicing();
			}
		});

		// Markers
		this.setupMarkerDragging(startMarker, endMarker);
	}

	setupMarkerDragging(startMarker, endMarker) {
		[startMarker, endMarker].forEach(marker => {
			marker.addEventListener('mousedown', () => {
				this.draggingMarker = marker;
				document.addEventListener('mousemove', this.handleMarkerDrag);
				document.addEventListener('mouseup', this.stopMarkerDrag);
			});
		});
	}

	handleMarkerDrag = (e) => {
		if (!this.draggingMarker) return;

		const rect = this.canvas.getBoundingClientRect();
		const x = (e.clientX - rect.left) / rect.width;
		
		if (this.draggingMarker.classList.contains('start')) {
			this.startPosition = Math.max(0, Math.min(x, this.endPosition - 0.01));
		} else {
			this.endPosition = Math.max(this.startPosition + 0.01, Math.min(x, 1));
		}

		this.updateMarkerPositions();
		this.drawWaveform();
	};

	stopMarkerDrag = () => {
		this.draggingMarker = null;
		document.removeEventListener('mousemove', this.handleMarkerDrag);
		document.removeEventListener('mouseup', this.stopMarkerDrag);
	};

	updateMarkerPositions() {
		const startMarker = document.getElementById('startMarker');
		const endMarker = document.getElementById('endMarker');

		startMarker.style.left = `${this.startPosition * 100}%`;
		endMarker.style.left = `${this.endPosition * 100}%`;
	}

	async loadAudioFile(file) {
		try {
			const arrayBuffer = await file.arrayBuffer();
			this.audioBuffer = await this.audioContext.decodeAudioData(arrayBuffer);
			this.originalBuffer = this.audioBuffer;

			// Precompute peaks
			this.precomputeWaveformPeaks();

			// Render static waveform into offscreen canvas
			this.renderOffscreenCanvas();

			// Enable UI
			document.getElementById('subdivisions').disabled = false;
			document.getElementById('setButton').disabled = false;
			this.setButton.textContent = 'SET';
			
			// Reset selection
			this.startPosition = 0;
			this.endPosition = 1;
			this.updateMarkerPositions();
			this.drawWaveform();
		} catch (error) {
			console.error('Error loading audio file:', error);
		}
	}

	precomputeWaveformPeaks() {
		if (!this.audioBuffer) return;

		const data = this.audioBuffer.getChannelData(0);
		const length = data.length;

		this.waveformPeaks = new Array(this.precomputedWidth);
		const samplesPerBucket = length / this.precomputedWidth;

		for (let i = 0; i < this.precomputedWidth; i++) {
			let start = Math.floor(i * samplesPerBucket);
			let end = Math.floor((i + 1) * samplesPerBucket);
			if (end > length) end = length;

			let min = 1.0;
			let max = -1.0;
			for (let j = start; j < end; j++) {
				const val = data[j];
				if (val < min) min = val;
				if (val > max) max = val;
			}
			this.waveformPeaks[i] = { min, max };
		}
	}

	/**
	 * Renders the static waveform to an offscreen canvas exactly once.
	 * We'll later draw this offscreen image onto our main canvas for each animation frame.
	 */
	renderOffscreenCanvas() {
		if (!this.audioBuffer || !this.waveformPeaks.length) return;

		// Clear offscreen
		this.offscreenCtx.clearRect(0, 0, this.offscreenCanvas.width, this.offscreenCanvas.height);

		const width = this.offscreenCanvas.width;
		const height = this.offscreenCanvas.height;
		const dpr = window.devicePixelRatio || 1;

		// We'll scale from our precomputed array to the canvas's actual width
		const targetWidth = this.waveformPeaks.length;
		const scale = targetWidth / width;
		const amp = height / 2 / dpr;

		// Draw the waveform
		this.offscreenCtx.beginPath();
		this.offscreenCtx.strokeStyle = '#ffffff';
		this.offscreenCtx.lineWidth = 1;

		for (let i = 0; i < width; i++) {
			const index = Math.floor(i * scale);
			const { min, max } = this.waveformPeaks[index] || { min: 0, max: 0 };
			const x = i / dpr;

			this.offscreenCtx.moveTo(x, (1 + min) * amp);
			this.offscreenCtx.lineTo(x, (1 + max) * amp);
		}
		this.offscreenCtx.stroke();
	}

	/**
	 * Draws the waveform from the offscreen canvas, then overlays selection, subdivisions, and the playhead.
	 */
	drawWaveform() {
		if (!this.canvas || !this.ctx) return;

		const width = this.canvas.width;
		const height = this.canvas.height;
		const dpr = window.devicePixelRatio || 1;

		// Clear main canvas
		this.ctx.clearRect(0, 0, width, height);

		// Draw the static waveform from offscreen cache
		this.ctx.drawImage(this.offscreenCanvas, 0, 0);

		// If no audio loaded, exit
		if (!this.audioBuffer) return;

		// Draw the selection highlight
		const startX = this.startPosition * width / dpr;
		const endX = this.endPosition * width / dpr;
		this.ctx.fillStyle = 'rgba(52, 152, 219, 0.1)';
		this.ctx.fillRect(startX, 0, endX - startX, height / dpr);

		// Draw subdivisions / highlight
		if (this.subdivisions > 1) {
			const segmentWidth = (endX - startX) / this.subdivisions;
			
			// Active segment highlight
			if (this.isPlaying && this.activeSegment !== -1) {
				const segmentStart = startX + (segmentWidth * this.activeSegment);
				this.ctx.fillStyle = 'rgba(52, 152, 219, 0.2)';
				this.ctx.fillRect(segmentStart, 0, segmentWidth, height / dpr);
			}
			
			// Subdivision lines
			this.ctx.strokeStyle = 'rgba(52, 152, 219, 0.5)';
			this.ctx.lineWidth = 1;
			for (let i = 1; i < this.subdivisions; i++) {
				const x = startX + (segmentWidth * i);
				this.ctx.beginPath();
				this.ctx.moveTo(x, 0);
				this.ctx.lineTo(x, height / dpr);
				this.ctx.stroke();
			}
		}

		// Draw playhead
		if (this.isPlaying && this.activeSegment !== -1) {
			const segmentWidth = (endX - startX) / this.subdivisions;
			const segmentStart = startX + (segmentWidth * this.activeSegment);
			const playheadX = segmentStart + (segmentWidth * this.playheadPosition);

			this.ctx.strokeStyle = '#ffffff';
			this.ctx.lineWidth = 2;
			this.ctx.beginPath();
			this.ctx.moveTo(playheadX, 0);
			this.ctx.lineTo(playheadX, height / dpr);
			this.ctx.stroke();
		}

		// Keep animating if playing
		if (this.isPlaying) {
			this.animationFrameId = requestAnimationFrame(() => this.updatePlayhead());
		}
	}

	updatePlayhead() {
		if (!this.isPlaying || this.activeSegment === -1) return;

		const currentTime = this.audioContext.currentTime;
		const segmentDuration = this.segments[this.activeSegment].duration;
		
		if (currentTime >= this.playStartTime + segmentDuration) {
			this.handleSegmentEnd();
			return;
		}

		// Fraction of the current segment that’s done
		this.playheadPosition = (currentTime - this.playStartTime) / segmentDuration;

		// Request next frame for smooth animation
		requestAnimationFrame(() => {
			this.drawWaveform();
			if (this.isPlaying) {
				this.updatePlayhead();
			}
		});
	}

	handleSegmentEnd() {

		console.log("end");

		const userNext = this.nextSegmentIndex;
		this.nextSegmentIndex = null; // Clear queue

		let nextIndex;
		if (typeof userNext === 'number') {
			nextIndex = userNext;
		} else {
			nextIndex = this.activeSegment + 1;
			if (nextIndex >= this.segments.length) {
				nextIndex = 0; // wrap around
			}
		}

		if (this.segments.length > 0) {
			this.playSegment(nextIndex);
		} else {
			this.stopPlayback();
		}
	}

	finalizeSlicing() {
		if (!this.audioBuffer) return;

		const startSample = Math.floor(this.startPosition * this.audioBuffer.length);
		const endSample = Math.floor(this.endPosition * this.audioBuffer.length);
		const totalSamples = endSample - startSample;
		if (totalSamples <= 0) return;

		this.segments = [];
		for (let i = 0; i < this.subdivisions; i++) {
			const segStart = startSample + Math.floor((totalSamples / this.subdivisions) * i);
			const segEnd = (i === this.subdivisions - 1)
				? endSample
				: startSample + Math.floor((totalSamples / this.subdivisions) * (i + 1));
			const length = segEnd - segStart;

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
		}

		// Update UI
		document.getElementById('subdivisions').disabled = true;
		document.getElementById('setButton').disabled = true;
		this.setButton.textContent = 'RESET';

		// Create dynamic segment buttons
		const segmentControls = document.getElementById('segmentControls');
		segmentControls.innerHTML = '';
		for (let i = 0; i < this.segments.length; i++) {
			const playButton = document.createElement('button');
			playButton.textContent = `Play Segment ${i + 1}`;
			playButton.addEventListener('click', () => {
				if (this.isPlaying) {
					this.nextSegmentIndex = i;
				} else {
					this.playSegment(i);
				}
			});
			segmentControls.appendChild(playButton);
		}

		// STOP button
		const stopButton = document.createElement('button');
		stopButton.textContent = 'Stop';
		stopButton.addEventListener('click', () => {
			this.stopPlayback();
		});
		segmentControls.appendChild(stopButton);
	}

	resetSlicing() {
		// Restore the original buffer
		this.audioBuffer = this.originalBuffer;
		this.segments = [];
		this.startPosition = 0;
		this.endPosition = 1;
		
		// Update UI
		this.setButton.textContent = 'SET';
		document.getElementById('subdivisions').disabled = false;
		this.updateMarkerPositions();
		this.stopPlayback();

		// Clear segment controls
		const segmentControls = document.getElementById('segmentControls');
		segmentControls.innerHTML = '';

		// Redraw
		this.drawWaveform();
	}

	playSegment(index) {
		if (index < 0 || index >= this.segments.length) return;

		// Stop existing
		this.stopPlayback();

		this.isPlaying = true;
		this.activeSegment = index;
		this.playheadPosition = 0;

		const source = this.audioContext.createBufferSource();
		source.buffer = this.segments[index];
		source.connect(this.audioContext.destination);
		
		this.audioSource = source;
		this.playStartTime = this.audioContext.currentTime;

		source.onended = () => {
			this.handleSegmentEnd();
		};

		source.start();
		this.updatePlayhead();
	}

	stopPlayback() {
		if (this.audioSource) {
			this.audioSource.onended = null;
			this.audioSource.stop();
			this.audioSource.disconnect();
			this.audioSource = null;
		}
		this.isPlaying = false;
		this.activeSegment = -1;
		this.playheadPosition = 0;
		this.nextSegmentIndex = null;

		if (this.animationFrameId) {
			cancelAnimationFrame(this.animationFrameId);
			this.animationFrameId = null;
		}
		this.drawWaveform(); // Redraw without playhead
	}

	dispose() {
		this.stopPlayback();
		if (this.audioContext) {
			this.audioContext.close();
		}
		window.removeEventListener('resize', this.resizeCanvas);
	}
}

// Global instance
window.audioSlicer = new AudioSlicer();
