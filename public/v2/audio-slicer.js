class AudioSlicer {
    constructor() {
        this.initializeProperties();
        this.setupAudioContext();
        this.setupCanvas();
        this.setupEventListeners();
    }

    initializeProperties() {
        this.originalBuffer = null; // Store original buffer for reset
        this.audioBuffer = null;
        this.audioSource = null;
        this.startPosition = 0;
        this.endPosition = 1;
        this.subdivisions = 2;
        this.segments = [];
        this.isPlaying = false;
        this.activeSegment = -1;
        this.draggingMarker = null;
        this.lastFrameTime = 0;
        this.playheadPosition = 0;
        this.animationFrameId = null;
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
        
        this.setButton = setButton; // Store reference for updating text

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

        subdivSelect.addEventListener('change', (e) => {
            this.subdivisions = parseInt(e.target.value);
            this.drawWaveform();
        });

        setButton.addEventListener('click', () => {
            if (setButton.textContent === 'SET') {
                this.finalizeSlicing();
            } else {
                this.resetSlicing();
            }
        });

        this.setupMarkerDragging(startMarker, endMarker);
    }

    setupMarkerDragging(startMarker, endMarker) {
        const markers = [startMarker, endMarker];
        
        markers.forEach(marker => {
            marker.addEventListener('mousedown', (e) => {
                this.draggingMarker = marker;
                document.addEventListener('mousemove', this.handleMarkerDrag);
                document.addEventListener('mouseup', () => {
                    this.draggingMarker = null;
                    document.removeEventListener('mousemove', this.handleMarkerDrag);
                });
            });
        });

        this.handleMarkerDrag = (e) => {
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
    }

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
            this.originalBuffer = this.audioBuffer; // Store original buffer for reset
            
            document.getElementById('subdivisions').disabled = false;
            document.getElementById('setButton').disabled = false;
            this.setButton.textContent = 'SET';
            
            this.startPosition = 0;
            this.endPosition = 1;
            this.updateMarkerPositions();
            this.drawWaveform();
        } catch (error) {
            console.error('Error loading audio file:', error);
        }
    }

    drawWaveform() {
        if (!this.canvas || !this.ctx) return;

        const width = this.canvas.width;
        const height = this.canvas.height;
        const dpr = window.devicePixelRatio || 1;

        // Clear canvas
        this.ctx.clearRect(0, 0, width, height);

        if (!this.audioBuffer) return;

        const data = this.audioBuffer.getChannelData(0);
        const step = Math.ceil(data.length / width);
        const amp = height / 2;

        // Draw selection background
        const startX = this.startPosition * width / dpr;
        const endX = this.endPosition * width / dpr;
        this.ctx.fillStyle = 'rgba(52, 152, 219, 0.1)';
        this.ctx.fillRect(startX, 0, endX - startX, height / dpr);

        // Draw waveform
        this.ctx.beginPath();
        this.ctx.strokeStyle = '#ffffff';
        this.ctx.lineWidth = 1;

        for (let i = 0; i < width; i++) {
            const x = i / dpr;
            let min = 1.0;
            let max = -1.0;

            for (let j = 0; j < step; j++) {
                const datum = data[(i * step) + j] || 0;
                if (datum < min) min = datum;
                if (datum > max) max = datum;
            }

            this.ctx.moveTo(x, (1 + min) * amp / dpr);
            this.ctx.lineTo(x, (1 + max) * amp / dpr);
        }

        this.ctx.stroke();

        // Draw subdivisions and highlight active segment
        if (this.subdivisions > 1) {
            const segmentWidth = (endX - startX) / this.subdivisions;
            
            // Draw active segment highlight
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

        // Draw playhead if playing
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

        // Request next frame if playing
        if (this.isPlaying) {
            this.animationFrameId = requestAnimationFrame(() => this.updatePlayhead());
        }
    }

    updatePlayhead() {
        if (!this.isPlaying || this.activeSegment === -1) return;

        const currentTime = this.audioContext.currentTime;
        const segmentDuration = this.segments[this.activeSegment].duration;
        
        if (currentTime >= this.playStartTime + segmentDuration) {
            this.stopPlayback();
            return;
        }

        this.playheadPosition = (currentTime - this.playStartTime) / segmentDuration;
        
        // Add smooth animation
        requestAnimationFrame(() => {
            this.drawWaveform();
            if (this.isPlaying) {
                this.updatePlayhead();
            }
        });
    }

    resetSlicing() {
        // Restore original buffer
        this.audioBuffer = this.originalBuffer;
        this.segments = [];
        this.startPosition = 0;
        this.endPosition = 1;
        
        // Reset UI
        this.setButton.textContent = 'SET';
        document.getElementById('subdivisions').disabled = false;
        this.updateMarkerPositions();
        
        // Stop any current playback
        this.stopPlayback();
        
        // Redraw waveform
        this.drawWaveform();
    }

    finalizeSlicing() {
        if (!this.audioBuffer) return;

        const startSample = Math.floor(this.startPosition * this.audioBuffer.length);
        const endSample = Math.floor(this.endPosition * this.audioBuffer.length);
        const segmentLength = Math.floor((endSample - startSample) / this.subdivisions);

        this.segments = [];

        for (let i = 0; i < this.subdivisions; i++) {
            const segmentStart = startSample + (i * segmentLength);
            const segmentEnd = segmentStart + segmentLength;
            
            const segmentBuffer = this.audioContext.createBuffer(
                this.audioBuffer.numberOfChannels,
                segmentLength,
                this.audioBuffer.sampleRate
            );

            for (let channel = 0; channel < this.audioBuffer.numberOfChannels; channel++) {
                const channelData = this.audioBuffer.getChannelData(channel);
                const segmentData = segmentBuffer.getChannelData(channel);
                
                for (let j = 0; j < segmentLength; j++) {
                    segmentData[j] = channelData[segmentStart + j];
                }
            }

            this.segments.push(segmentBuffer);
        }

        document.getElementById('setButton').disabled = true;
        document.getElementById('subdivisions').disabled = true;
        this.setButton.textContent = 'RESET';
    }

    async playSegment(index) {
        if (index < 0 || index >= this.segments.length || this.isPlaying) return;

        // Stop any current playback
        this.stopPlayback();

        this.isPlaying = true;
        this.activeSegment = index;
        this.playheadPosition = 0;

        const source = this.audioContext.createBufferSource();
        source.buffer = this.segments[index];
        source.connect(this.audioContext.destination);
        
        this.audioSource = source;
        this.playStartTime = this.audioContext.currentTime;
        
        source.start();
        source.onended = () => this.stopPlayback();

        // Start animation
        this.updatePlayhead();
    }

    stopPlayback() {
        if (this.audioSource) {
            this.audioSource.stop();
            this.audioSource.disconnect();
            this.audioSource = null;
        }

        this.isPlaying = false;
        this.activeSegment = -1;
        this.playheadPosition = 0;

        if (this.animationFrameId) {
            cancelAnimationFrame(this.animationFrameId);
            this.animationFrameId = null;
        }

        this.drawWaveform();
    }

    getSegmentCount() {
        return this.segments.length;
    }

    // Cleanup method
    dispose() {
        this.stopPlayback();
        if (this.audioContext) {
            this.audioContext.close();
        }
        window.removeEventListener('resize', this.resizeCanvas);
    }
}

// Create global instance
window.audioSlicer = new AudioSlicer();