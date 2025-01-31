class AudioSlicer {
    constructor() {
        this.initializeProperties();
        this.setupAudioContext();
        this.setupCanvas();
        this.setupEventListeners();
    }

    initializeProperties() {
        this.originalBuffer = null;      // Store original buffer for reset
        this.audioBuffer = null;
        this.audioSource = null;
        this.startPosition = 0;
        this.endPosition = 1;
        this.subdivisions = 2;
        this.segments = [];
        this.isPlaying = false;
        this.activeSegment = -1;
        this.nextSegmentIndex = null;    // Tracks a queued next segment if user clicks another “Play” while something is playing
        this.draggingMarker = null;

        this.playheadPosition = 0;
        this.animationFrameId = null;

        // Will hold the precomputed min/max for the waveform so we don’t rescan raw PCM on every draw
        this.waveformPeaks = [];
        this.precomputedWidth = 1500; // Arbitrary resolution for precomputation
    }

    setupAudioContext() {
        this.audioContext = new (window.AudioContext || window.webkitAudioContext)();
    }

    setupCanvas() {
        this.canvas = document.getElementById('waveformCanvas');
        this.ctx = this.canvas.getContext('2d');
        this.resizeCanvas();

        window.addEventListener('resize', () => this.resizeCanvas());
    }

    resizeCanvas() {
        const dpr = window.devicePixelRatio || 1;
        const rect = this.canvas.getBoundingClientRect();
        
        this.canvas.width = rect.width * dpr;
        this.canvas.height = rect.height * dpr;
        this.ctx.scale(dpr, dpr);
        
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

        // Subdivisions select
        subdivSelect.addEventListener('change', (e) => {
            this.subdivisions = parseInt(e.target.value);
            this.drawWaveform();
        });

        // SET / RESET button
        setButton.addEventListener('click', () => {
            if (setButton.textContent === 'SET') {
                this.finalizeSlicing();
            } else {
                this.resetSlicing();
            }
        });

        // Draggable start/end markers
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
            this.originalBuffer = this.audioBuffer; // For resetting

            // Precompute waveform once
            this.precomputeWaveformPeaks();

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

    /**
     * Precompute min/max values for a fixed resolution (this.precomputedWidth).
     * This avoids scanning the entire audio buffer on every draw.
     */
    precomputeWaveformPeaks() {
        if (!this.audioBuffer) return;

        const data = this.audioBuffer.getChannelData(0);
        const length = data.length;

        // The number of "columns" to precompute
        const targetWidth = this.precomputedWidth;
        this.waveformPeaks = new Array(targetWidth).fill(null).map(() => ({ min: 1.0, max: -1.0 }));
        
        // Determine how many samples each "column" covers
        const samplesPerBucket = length / targetWidth;

        // Scan once
        for (let i = 0; i < targetWidth; i++) {
            const start = Math.floor(i * samplesPerBucket);
            const end = Math.floor((i + 1) * samplesPerBucket);
            
            let min = 1.0;
            let max = -1.0;
            for (let j = start; j < end; j++) {
                const datum = data[j];
                if (datum < min) min = datum;
                if (datum > max) max = datum;
            }
            this.waveformPeaks[i] = { min, max };
        }
    }

    drawWaveform() {
        if (!this.canvas || !this.ctx) return;

        const width = this.canvas.width;
        const height = this.canvas.height;
        const dpr = window.devicePixelRatio || 1;

        this.ctx.clearRect(0, 0, width, height);

        if (!this.audioBuffer || !this.waveformPeaks.length) {
            return;
        }

        // Calculate selection in canvas coords
        const startX = this.startPosition * width / dpr;
        const endX = this.endPosition * width / dpr;

        // Highlight selection region
        this.ctx.fillStyle = 'rgba(52, 152, 219, 0.1)';
        this.ctx.fillRect(startX, 0, endX - startX, height / dpr);

        // Draw precomputed waveform
        this.ctx.beginPath();
        this.ctx.strokeStyle = '#ffffff';
        this.ctx.lineWidth = 1;

        // We'll scale from this.waveformPeaks to the actual canvas width
        const targetWidth = this.waveformPeaks.length;
        const scale = targetWidth / width;

        const amp = height / 2 / dpr;

        for (let i = 0; i < width; i++) {
            // Find the precomputed index
            const index = Math.floor(i * scale);
            const { min, max } = this.waveformPeaks[index] || { min: 0, max: 0 };

            const x = i / dpr;
            // Move to min, line to max
            this.ctx.moveTo(x, (1 + min) * amp);
            this.ctx.lineTo(x, (1 + max) * amp);
        }
        this.ctx.stroke();

        // Subdivisions and highlight
        if (this.subdivisions > 1) {
            const segmentWidth = (endX - startX) / this.subdivisions;
            
            // Highlight active segment if playing
            if (this.isPlaying && this.activeSegment !== -1) {
                const segmentStart = startX + (segmentWidth * this.activeSegment);
                this.ctx.fillStyle = 'rgba(52, 152, 219, 0.2)';
                this.ctx.fillRect(segmentStart, 0, segmentWidth, height / dpr);
            }
            
            // Draw subdivision lines
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

        // Continue animating
        if (this.isPlaying) {
            this.animationFrameId = requestAnimationFrame(() => this.updatePlayhead());
        }
    }

    updatePlayhead() {
        if (!this.isPlaying || this.activeSegment === -1) return;

        const currentTime = this.audioContext.currentTime;
        const segmentDuration = this.segments[this.activeSegment].duration;
        
        if (currentTime >= this.playStartTime + segmentDuration) {
            // Segment finished
            this.handleSegmentEnd();
            return;
        }

        // Update fraction of current segment
        this.playheadPosition = (currentTime - this.playStartTime) / segmentDuration;

        // Request next frame for smooth animation
        requestAnimationFrame(() => {
            this.drawWaveform();
            // Keep updating while playing
            if (this.isPlaying) {
                this.updatePlayhead();
            }
        });
    }

    handleSegmentEnd() {
        // The current segment finished
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
            const segmentStart = startSample + Math.floor((totalSamples / this.subdivisions) * i);
            const segmentEnd = (i === this.subdivisions - 1)
                ? endSample
                : startSample + Math.floor((totalSamples / this.subdivisions) * (i + 1));
            const length = segmentEnd - segmentStart;

            const segmentBuffer = this.audioContext.createBuffer(
                this.audioBuffer.numberOfChannels,
                length,
                this.audioBuffer.sampleRate
            );

            for (let channel = 0; channel < this.audioBuffer.numberOfChannels; channel++) {
                const channelData = this.audioBuffer.getChannelData(channel);
                const segmentData = segmentBuffer.getChannelData(channel);
                for (let j = 0; j < length; j++) {
                    segmentData[j] = channelData[segmentStart + j];
                }
            }
            this.segments.push(segmentBuffer);
        }

        document.getElementById('subdivisions').disabled = true;
        document.getElementById('setButton').disabled = true;
        this.setButton.textContent = 'RESET';

        // Create segment controls
        const segmentControls = document.getElementById('segmentControls');
        segmentControls.innerHTML = '';
        for (let i = 0; i < this.segments.length; i++) {
            const playButton = document.createElement('button');
            playButton.textContent = `Play Segment ${i + 1}`;
            playButton.addEventListener('click', () => {
                // If a segment is playing, queue this as next; else play immediately
                if (this.isPlaying) {
                    this.nextSegmentIndex = i;
                } else {
                    this.playSegment(i);
                }
            });
            segmentControls.appendChild(playButton);
        }

        // A general "Stop" button
        const stopButton = document.createElement('button');
        stopButton.textContent = 'Stop';
        stopButton.addEventListener('click', () => {
            this.stopPlayback();
        });
        segmentControls.appendChild(stopButton);
    }

    resetSlicing() {
        // Restore original buffer
        this.audioBuffer = this.originalBuffer;
        this.segments = [];
        this.startPosition = 0;
        this.endPosition = 1;
        
        // Restore UI
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

        // Stop any existing playback
        this.stopPlayback();

        this.isPlaying = true;
        this.activeSegment = index;
        this.playheadPosition = 0;

        const source = this.audioContext.createBufferSource();
        source.buffer = this.segments[index];
        source.connect(this.audioContext.destination);
        
        this.audioSource = source;
        this.playStartTime = this.audioContext.currentTime;

        // When current segment finishes, chain to next
        source.onended = () => {
            this.handleSegmentEnd();
        };

        source.start();
        this.updatePlayhead(); // Start the animation loop
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
        this.nextSegmentIndex = null; // Clear any queued “next”

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

// Create a global instance
window.audioSlicer = new AudioSlicer();
