import AudioSlicerController from './audio-slicer-controller.js';

const slicersDiv = document.getElementById('slicers');
const addSlicerBtn = document.getElementById('addSlicerBtn');
let slicerCount = 0;

function createSlicer() {
	slicerCount++;
	const container = document.createElement('div');
	container.className = 'slicer-container';

	// Controls
	const controls = document.createElement('div');
	controls.className = 'slicer-controls';

	const fileInput = document.createElement('input');
	fileInput.type = 'file';
	fileInput.accept = 'audio/wav,audio/mp3';

	const sliceBtn = document.createElement('button');
	sliceBtn.textContent = 'Slice';
	sliceBtn.disabled = true;

	const startInput = document.createElement('input');
	startInput.type = 'number';
	startInput.min = 0;
	startInput.max = 1;
	startInput.step = 0.01;
	startInput.value = 0;
	startInput.classList.add('input-width-60');

	const endInput = document.createElement('input');
	endInput.type = 'number';
	endInput.min = 0;
	endInput.max = 1;
	endInput.step = 0.01;
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

	container.appendChild(controls);
	slicersDiv.appendChild(container);

	// Slicer instance
	const slicer = new AudioSlicerController(container, { width: 800, height: 200 });

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
		const start = Math.max(0, Math.min(1, parseFloat(startInput.value)));
		const end = Math.max(0, Math.min(1, parseFloat(endInput.value)));
		const subdivisions = parseInt(subdivisionsSelect.value, 10);
		if (end > start && subdivisions > 0) {
			slicer.slice(start, end, subdivisions, { keepPlayhead: true });
		}
	});

	// Subdivision change triggers immediate re-slice and redraw, keeping playhead if playing
	subdivisionsSelect.addEventListener('change', () => {
		const start = Math.max(0, Math.min(1, parseFloat(startInput.value)));
		const end = Math.max(0, Math.min(1, parseFloat(endInput.value)));
		const subdivisions = parseInt(subdivisionsSelect.value, 10);
		if (end > start && subdivisions > 0) {
			slicer.slice(start, end, subdivisions, { keepPlayhead: true });
		}
	});
}

addSlicerBtn.addEventListener('click', createSlicer);

// Create one slicer by default
createSlicer();
