class AudioSlicer {
    constructor() {
        // Audio context and state
        this.audioContext = null;
        this.audioBuffer = null;
        this.sourceNode = null;
        this.isPlaying = false;
        
        // Loop and segment state
        this.loopStart = 0;
        this.loopEnd = 0;
        this.subdivisions = 2;
        this.isSliced = false;
        this.segments = [];
        
        // Playback tracking
        this.currentSegment = null;
        this.activeSegmentIndex = -1;
        this.playStartTime = 0;
        this.currentPlayheadPosition = 0;
        
        // UI state
        this.draggedMarker = null;
        this.animationFrame = null;

        // DOM elements
        this.dropzone = document.getElementById('dropzone');
        this.canvas = document.getElementById('waveformCanvas');
        this.ctx = this.canvas.getContext('2d');
        this.dropzoneText = document.getElementById('dropzoneText');
        this.playButton = document.getElementById('playButton');
        this.subdivisionsSelect = document.getElementById('subdivisions');
        this.setButton = document.getElementById('setButton');

        // Initialize
        this.initializeAudioContext();
        this.setupEventListeners();
        this.handleResize();
    }

    initializeAudioContext() {
        this.audioContext = new (window.AudioContext || window.webkitAudioContext)();
    }

    setupEventListeners() {
        // File handling
        this.dropzone.addEventListener('drop', this.handleDrop.bind(this));
        this.dropzone.addEventListener('dragover', (e) => e.preventDefault());
        
        // UI controls
        this.playButton.addEventListener('click', this.togglePlayback.bind(this));
        this.subdivisionsSelect.addEventListener('change', this.handleSubdivisionChange.bind(this));
        this.setButton.addEventListener('click', this.handleSetButton.bind(this));
        
        // Marker dragging
        this.dropzone.addEventListener('mousemove', this.updateLoop.bind(this));
        this.dropzone.addEventListener('mouseup', () => this.draggedMarker = null);
        this.dropzone.addEventListener('mousedown', this.handleMouseDown.bind(this));
        
        // Window events
        window.addEventListener('resize', this.handleResize.bind(this));
    }

    handleMouseDown(e) {
        if (!this.audioBuffer || this.isSliced) return;
        
        const rect = this.canvas.getBoundingClientRect();
        const x = e.clientX - rect.left;
        const duration = this.audioBuffer.duration;
        
        const startX = (this.loopStart / duration) * this.canvas.width;
        const endX = (this.loopEnd / duration) * this.canvas.width;
        const threshold = 10;

        if (Math.abs(x - startX) < threshold) {
            this.draggedMarker = 'start';
        } else if (Math.abs(x - endX) < threshold) {
            this.draggedMarker = 'end';
        }
    }

    handleResize() {
        const rect = this.canvas.getBoundingClientRect();
        this.canvas.width = rect.width * window.devicePixelRatio;
        this.canvas.height = rect.height * window.devicePixelRatio;
        this.ctx.scale(window.devicePixelRatio, window.devicePixelRatio);
        if (this.audioBuffer) {
            this.drawWaveform();
        }
    }

    async handleDrop(e) {
        e.preventDefault();
        const file = e.dataTransfer.files[0];
        
        if (file && (file.type === 'audio/wav' || file.type === 'audio/mpeg')) {
            const arrayBuffer = await file.arrayBuffer();
            this.audioBuffer = await this.audioContext.decodeAudioData(arrayBuffer);
            this.loopEnd = this.audioBuffer.duration;
            this.dropzoneText.style.display = 'none';
            this.playButton.disabled = false;
            this.subdivisionsSelect.disabled = false;
            this.setButton.disabled = false;
            this.isSliced = false;
            this.segments = [];
            this.drawWaveform();
        }
    }

    handleSubdivisionChange(e) {
        this.subdivisions = parseInt(e.target.value);
        if (!this.isSliced) {
            this.drawWaveform();
        }
    }

    handleSetButton() {
        this.isSliced = true;
        this.calculateSegments();
        this.drawWaveform();
    }

    updateLoop(e) {
        if (!this.draggedMarker || !this.audioBuffer || this.isSliced) return;
        
        const rect = this.canvas.getBoundingClientRect();
        const x = e.clientX - rect.left;
        const duration = this.audioBuffer.duration;
        const time = (x / rect.width) * duration;
        
        if (this.draggedMarker === 'start') {
            this.loopStart = Math.max(0, Math.min(time, this.loopEnd));
        } else if (this.draggedMarker === 'end') {
            this.loopEnd = Math.min(duration, Math.max(time, this.loopStart));
        }

        this.drawWaveform();
    }

    calculateSegments() {
        const segmentDuration = (this.loopEnd - this.loopStart) / this.subdivisions;
        this.segments = [];
        
        for (let i = 0; i < this.subdivisions; i++) {
            const start = this.loopStart + (i * segmentDuration);
            const end = start + segmentDuration;
            this.segments.push({ start, end });
        }
    }

    updatePlayhead() {
        if (!this.isSliced || !this.currentSegment) return;
        
        const currentTime = this.audioContext.currentTime;
        const elapsedTime = currentTime - this.playStartTime;
        
        if (this.activeSegmentIndex >= 0 && this.segments[this.activeSegmentIndex]) {
            const segment = this.segments[this.activeSegmentIndex];
            const segmentDuration = segment.end - segment.start;
            this.currentPlayheadPosition = Math.min(elapsedTime / segmentDuration, 1);
        }
    }

    drawWaveform() {
        const width = this.canvas.width / window.devicePixelRatio;
        const height = this.canvas.height / window.devicePixelRatio;
        this.ctx.clearRect(0, 0, width, height);

        if (!this.audioBuffer) return;

        const data = this.audioBuffer.getChannelData(0);
        const duration = this.audioBuffer.duration;

        // Update playhead position for animation
        this.updatePlayhead();

        if (this.isSliced) {
            // Draw sliced view
            const startSample = Math.floor((this.loopStart / duration) * data.length);
            const endSample = Math.floor((this.loopEnd / duration) * data.length);
            const segmentData = data.slice(startSample, endSample);
            
            // Draw waveform
            this.drawWaveformData(segmentData, width, height);

            // Draw segments with highlighting
            for (let i = 0; i < this.subdivisions; i++) {
                const x = (i / this.subdivisions) * width;
                const nextX = ((i + 1) / this.subdivisions) * width;
                
                // Highlight active segment
                if (i === this.activeSegmentIndex) {
                    this.ctx.fillStyle = 'rgba(135, 206, 235, 0.2)';
                    this.ctx.fillRect(x, 0, nextX - x, height);
                    
                    // Draw playhead
                    if (this.currentPlayheadPosition > 0) {
                        const playheadX = x + ((nextX - x) * this.currentPlayheadPosition);
                        this.ctx.beginPath();
                        this.ctx.moveTo(playheadX, 0);
                        this.ctx.lineTo(playheadX, height);
                        this.ctx.strokeStyle = 'rgba(255, 255, 255, 0.8)';
                        this.ctx.lineWidth = 2;
                        this.ctx.stroke();
                        this.ctx.lineWidth = 1;
                    }
                }

                // Draw segment dividers
                if (i > 0) {
                    this.ctx.beginPath();
                    this.ctx.moveTo(x, 0);
                    this.ctx.lineTo(x, height);
                    this.ctx.strokeStyle = '#87CEEB';
                    this.ctx.stroke();
                }
            }
        } else {
            // Draw full waveform view
            this.drawWaveformData(data, width, height);

            // Draw markers and subdivisions
            const startX = (this.loopStart / duration) * width;
            const endX = (this.loopEnd / duration) * width;

            // Draw subdivision guides
            if (this.loopStart !== this.loopEnd) {
                const segmentWidth = (endX - startX) / this.subdivisions;
                for (let i = 1; i < this.subdivisions; i++) {
                    const x = startX + (i * segmentWidth);
                    this.ctx.beginPath();
                    this.ctx.moveTo(x, 0);
                    this.ctx.lineTo(x, height);
                    this.ctx.strokeStyle = '#87CEEB';
                    this.ctx.stroke();
                }
            }

            // Draw start/end markers
            this.ctx.fillStyle = '#FF0000';
            this.ctx.fillRect(startX - 2, 0, 4, height);
            
            this.ctx.fillStyle = '#00FF00';
            this.ctx.fillRect(endX - 2, 0, 4, height);
        }
    }

    drawWaveformData(data, width, height) {
        const step = Math.ceil(data.length / width);
        const amp = height / 2;

        this.ctx.beginPath();
        this.ctx.moveTo(0, amp);

        for (let i = 0; i < width; i++) {
            let min = 1.0;
            let max = -1.0;
            for (let j = 0; j < step; j++) {
                const datum = data[(i * step) + j];
                if (datum < min) min = datum;
                if (datum > max) max = datum;
            }
            this.ctx.lineTo(i, (1 + min) * amp);
            this.ctx.lineTo(i, (1 + max) * amp);
        }

        this.ctx.strokeStyle = '#2196F3';
        this.ctx.stroke();
    }

    // API Methods
    playSegment(index) {
        if (index < 0 || index >= this.segments.length) return;
        
        if (this.currentSegment) {
            this.currentSegment.stop();
        }

        const segment = this.segments[index];
        const source = this.audioContext.createBufferSource();
        source.buffer = this.audioBuffer;
        source.connect(this.audioContext.destination);
        
        this.activeSegmentIndex = index;
        this.playStartTime = this.audioContext.currentTime;
        this.currentPlayheadPosition = 0;
        
        source.start(0, segment.start, segment.end - segment.start);
        this.currentSegment = source;
        
        // Set up ended event for this segment
        source.onended = () => {
            this.activeSegmentIndex = -1;
            this.currentSegment = null;
            this.currentPlayheadPosition = 0;
            this.drawWaveform();
        };

        // Start animation
        this.animate();
    }

    getSegmentCount() {
        return this.segments.length;
    }

    togglePlayback() {
        if (!this.audioBuffer) return;

        if (this.isPlaying) {
            if (this.sourceNode) {
                this.sourceNode.stop();
                this.sourceNode = null;
            }
            if (this.animationFrame) {
                cancelAnimationFrame(this.animationFrame);
            }
            this.isPlaying = false;
            this.playButton.textContent = 'Play';
        } else {
            const source = this.audioContext.createBufferSource();
            source.buffer = this.audioBuffer;
            source.connect(this.audioContext.destination);
            
            if (this.isSliced) {
                source.loop = false;
                source.start(0, this.loopStart, this.loopEnd - this.loopStart);
            } else {
                source.loop = true;
                source.loopStart = this.loopStart;
                source.loopEnd = this.loopEnd;
                source.start(0, this.loopStart);
            }
            
            this.sourceNode = source;
            this.startTimeRef = this.audioContext.currentTime - this.loopStart;
            this.isPlaying = true;
            this.playButton.textContent = 'Stop';
            this.animate();
        }
    }

    animate() {
        this.drawWaveform();
        if (this.isPlaying || (this.isSliced && this.currentSegment)) {
            this.animationFrame = requestAnimationFrame(this.animate.bind(this));
        }
    }
}

// Initialize the application
window.addEventListener('load', () => {
    window.audioSlicer = new AudioSlicer();  // Make it globally accessible for external sequencing
});

// Example sequencing usage:
/*
async function playPattern(pattern, interval = 500) {
    const slicer = window.audioSlicer;
    for (const index of pattern) {
        slicer.playSegment(index);
        await new Promise(resolve => setTimeout(resolve, interval));
    }
}

// Example patterns:
// Forward: playPattern([0, 1, 2, 3]);
// Reverse: playPattern([3, 2, 1, 0]);
// Random:  playPattern([1, 3, 0, 2]);
// Pingpong: playPattern([0, 1, 2, 3, 2, 1]);
*/