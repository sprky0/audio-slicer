import AudioSlicerController from './audio-slicer-controller.js';
import Knob from './knob.js';

const slicersDiv    = document.getElementById('slicers');
const addSlicerBtn  = document.getElementById('addSlicerBtn');
let slicerCount     = 0;

function createSlicer() {
	slicerCount++;
	const container = document.createElement('div');
	container.className = 'slicer-container';

	// Controls
	const controls = document.createElement('div');
	controls.className = 'slicer-controls';

	// --- Reset and Remove Buttons ---
	const resetBtn = document.createElement('button');
	resetBtn.textContent = 'Reset';
	resetBtn.className = 'slicer-reset-btn';

	const removeBtn = document.createElement('button');
	removeBtn.textContent = 'Remove';
	removeBtn.className = 'slicer-remove-btn';

	const fileInput = document.createElement('input');
	fileInput.type  = 'file';
	fileInput.accept= 'audio/wav,audio/mp3';

	const sliceBtn = document.createElement('button');
	sliceBtn.textContent = 'Slice';
	sliceBtn.disabled    = true;

	const startInput = document.createElement('input');
	startInput.type  = 'number';
	startInput.min   = 0;
	startInput.max   = 1;
	startInput.step  = 0.01;
	startInput.value = 0;
	startInput.classList.add('input-width-60');

	const endInput = document.createElement('input');
	endInput.type  = 'number';
	endInput.min   = 0;
	endInput.max   = 1;
	endInput.step  = 0.01;
	endInput.value = 1;
	endInput.classList.add('input-width-60');

	const subdivisionsSelect = document.createElement('select');
	[2, 4, 8, 16].forEach(val => {
		const opt = document.createElement('option');
		opt.value = val;
		opt.textContent = val;
		subdivisionsSelect.appendChild(opt);
	});

	controls.appendChild(document.createTextNode('File: '));
	controls.appendChild(fileInput);
	controls.appendChild(document.createTextNode(' Start: '));
	controls.appendChild(startInput);
	controls.appendChild(document.createTextNode(' End: '));
	controls.appendChild(endInput);
	controls.appendChild(document.createTextNode(' Subdivisions: '));
	controls.appendChild(subdivisionsSelect);
	controls.appendChild(sliceBtn);
	controls.appendChild(resetBtn);
	controls.appendChild(removeBtn);

	// --- Volume and Pan Knobs ---
	const volumeKnob = new Knob({
		label: 'Volume',
		min: 0,
		max: 1,
		step: 0.01,
		value: 1
	});
	const panKnob = new Knob({
		label: 'Pan',
		min: -1,
		max: 1,
		step: 0.01,
		value: 0
	});
	controls.appendChild(volumeKnob.getElement());
	controls.appendChild(panKnob.getElement());

	container.appendChild(controls);
	slicersDiv.appendChild(container);

	// Slicer instance
	const slicer = new AudioSlicerController(container, { width: 800, height: 200 });

	// --- Reset Button Logic ---
	resetBtn.addEventListener('click', () => {
		// Reset pan and volume knobs
		volumeKnob.setValue(1);
		panKnob.setValue(0);
		slicer.setVolume(1);
		slicer.setPan(0);

		// Reset start/end/subdivisions UI
		startInput.value = 0;
		endInput.value = 1;
		subdivisionsSelect.value = 2;

		// Stop playback and reset slicing
		slicer.stop();
		slicer.slice(0, 1, 2, { autoPlay: false });

		// Optionally clear waveform view (if needed)
		// slicer.view.setWaveformPeaks([]);
		// Optionally clear file input (if you want to force re-upload)
		// fileInput.value = '';
	});

	// --- Remove Button Logic ---
	removeBtn.addEventListener('click', () => {
		slicer.dispose();
		if (container.parentNode) {
			container.parentNode.removeChild(container);
		}
	});

	// --- Knob event wiring ---
	volumeKnob.addEventListener('change', (e) => {
		slicer.setVolume(e.detail);
	});
	panKnob.addEventListener('change', (e) => {
		slicer.setPan(e.detail);
	});

	// File loading
	fileInput.addEventListener('change', async (e) => {
		const file = e.target.files[0];
		if (file) {
			await slicer.loadFile(file);
			sliceBtn.disabled = false;
		}
	});

	// Slicing
	sliceBtn.addEventListener('click', () => {
		const start        = Math.max(0, Math.min(1, parseFloat(startInput.value)));
		const end          = Math.max(0, Math.min(1, parseFloat(endInput.value)));
		const subdivisions = parseInt(subdivisionsSelect.value, 10);
		if (end > start && subdivisions > 0) {
			slicer.slice(start, end, subdivisions, { keepPlayhead: true });
		}
	});

	// Subdivision change triggers immediate re-slice and redraw, keeping playhead if playing
	subdivisionsSelect.addEventListener('change', () => {
		const start        = Math.max(0, Math.min(1, parseFloat(startInput.value)));
		const end          = Math.max(0, Math.min(1, parseFloat(endInput.value)));
		const subdivisions = parseInt(subdivisionsSelect.value, 10);
		if (end > start && subdivisions > 0) {
			slicer.slice(start, end, subdivisions, { keepPlayhead: true });
		}
	});

	// Real-time zoom on start/end input change
	const updateZoom = () => {
		const start        = Math.max(0, Math.min(1, parseFloat(startInput.value)));
		const end          = Math.max(0, Math.min(1, parseFloat(endInput.value)));
		const subdivisions = parseInt(subdivisionsSelect.value, 10);
		if (end > start && subdivisions > 0) {
			slicer.slice(start, end, subdivisions, { keepPlayhead: true });
		}
	};
	startInput.addEventListener('input', updateZoom);
	endInput.addEventListener('input', updateZoom);
}

addSlicerBtn.addEventListener('click', createSlicer);

// Create one slicer by default
createSlicer();
