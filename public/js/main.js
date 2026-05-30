import AudioSlicerController from './audio-slicer-controller.js';
import Transport from './transport.js';
import Knob from './knob.js';
import PALETTE from './palette.js';

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

	// --- Play, Pause, Stop, Reset and Remove Buttons ---
	const playPauseBtn = document.createElement('button');
	playPauseBtn.textContent = 'Play';
	playPauseBtn.className = 'slicer-playpause-btn';

	const stopBtn = document.createElement('button');
	stopBtn.textContent = 'Stop';
	stopBtn.className = 'slicer-stop-btn';

	const cutBtn = document.createElement('button');
	cutBtn.textContent = 'Cut';
	cutBtn.className   = 'slicer-cut-btn';

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

	// --- Start / End range sliders ---
	const startSlider = document.createElement('input');
	startSlider.type  = 'range';
	startSlider.min   = 0;
	startSlider.max   = 1;
	startSlider.step  = 0.001;
	startSlider.value = 0;
	startSlider.className = 'slicer-range-slider slicer-range-start';

	const endSlider = document.createElement('input');
	endSlider.type  = 'range';
	endSlider.min   = 0;
	endSlider.max   = 1;
	endSlider.step  = 0.001;
	endSlider.value = 1;
	endSlider.className = 'slicer-range-slider slicer-range-end';

	const subdivisionsSelect = document.createElement('select');
	[2, 4, 8, 16].forEach(val => {
		const opt = document.createElement('option');
		opt.value = val;
		opt.textContent = val;
		subdivisionsSelect.appendChild(opt);
	});

	// --- Details toggle (hides Start/End inputs + sliders behind a button) ---
	const detailsBtn = document.createElement('button');
	detailsBtn.textContent = 'Show details';
	detailsBtn.className   = 'slicer-details-btn';
	detailsBtn.setAttribute('aria-expanded', 'false');

	controls.appendChild(document.createTextNode('File: '));
	controls.appendChild(fileInput);
	controls.appendChild(document.createTextNode(' Subdivisions: '));
	controls.appendChild(subdivisionsSelect);
	controls.appendChild(sliceBtn);
	controls.appendChild(cutBtn);
	controls.appendChild(playPauseBtn);
	controls.appendChild(stopBtn);
	controls.appendChild(resetBtn);
	controls.appendChild(detailsBtn);
	controls.appendChild(removeBtn);

	// --- Collapsible details panel: Start/End number inputs + range sliders ---
	const detailsPanel = document.createElement('div');
	detailsPanel.className = 'slicer-details';
	detailsPanel.hidden    = true;

	const numRow = document.createElement('div');
	numRow.className = 'slicer-details-row';

	const startNumLabel = document.createElement('label');
	startNumLabel.className = 'slicer-details-label';
	startNumLabel.appendChild(document.createTextNode('Start: '));
	startNumLabel.appendChild(startInput);

	const endNumLabel = document.createElement('label');
	endNumLabel.className = 'slicer-details-label';
	endNumLabel.appendChild(document.createTextNode('End: '));
	endNumLabel.appendChild(endInput);

	numRow.appendChild(startNumLabel);
	numRow.appendChild(endNumLabel);

	// --- Slider row ---
	const sliderRow = document.createElement('div');
	sliderRow.className = 'slicer-slider-row';

	const startSliderLabel = document.createElement('label');
	startSliderLabel.className = 'slicer-slider-label';
	startSliderLabel.textContent = 'Start';
	startSliderLabel.appendChild(startSlider);

	const endSliderLabel = document.createElement('label');
	endSliderLabel.className = 'slicer-slider-label';
	endSliderLabel.textContent = 'End';
	endSliderLabel.appendChild(endSlider);

	sliderRow.appendChild(startSliderLabel);
	sliderRow.appendChild(endSliderLabel);

	detailsPanel.appendChild(numRow);
	detailsPanel.appendChild(sliderRow);
	controls.appendChild(detailsPanel);

	detailsBtn.addEventListener('click', () => {
		const showing = !detailsPanel.hidden;
		detailsPanel.hidden = showing;
		detailsBtn.textContent = showing ? 'Show details' : 'Hide details';
		detailsBtn.setAttribute('aria-expanded', String(!showing));
		detailsBtn.classList.toggle('active', !showing);
	});

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

	// Slicer instance — width is responsive (driven by the slicer container).
	const slicer = new AudioSlicerController(container, { height: 200 });

	// --- Play/Pause Button Logic ---
	let isPlaying = false;
	playPauseBtn.addEventListener('click', () => {
		if (isPlaying) {
			slicer.pause();
			// UI will update via playstatechange event
		} else {
			if (slicer.isPaused && slicer.pausedSegment !== null) {
				slicer.resume();
			} else {
				slicer.playSegment(0);
			}
			// UI will update via playstatechange event
		}
	});

	// --- Sync Play/Pause Button with Slicer State ---
	container.addEventListener('playstatechange', (e) => {
		const state = e.detail.state;
		if (state === 'playing') {
			playPauseBtn.textContent = 'Pause';
			isPlaying = true;
		} else if (state === 'paused' || state === 'stopped') {
			playPauseBtn.textContent = 'Play';
			isPlaying = false;
		}
	});

	// --- Stop Button Logic ---
	stopBtn.addEventListener('click', () => {
		slicer.stop();
		playPauseBtn.textContent = 'Play';
		isPlaying = false;
	});

	// --- Reset Button Logic ---
	resetBtn.addEventListener('click', () => {
		// Reset pan and volume knobs
		volumeKnob.setValue(1);
		panKnob.setValue(0);
		slicer.setVolume(1);
		slicer.setPan(0);

		// Reset virtual zoom window back to the full buffer
		virtualStart = 0;
		virtualEnd   = 1;
		slicer.setView(0, 1);

		// Reset start/end/subdivisions UI
		startInput.value  = 0;
		endInput.value    = 1;
		startSlider.value = 0;
		endSlider.value   = 1;
		subdivisionsSelect.value = 2;

		// Stop playback and reset slicing
		slicer.stop();
		playPauseBtn.textContent = 'Play';
		isPlaying = false;
		slicer.slice(0, 1, 2, { autoPlay: false });

		// Restore auto-derived tempo.
		bpmManual      = false;
		divisionManual = false;
		deriveTransport(0, 1, 2);
	});

	// --- Remove Button Logic ---
	removeBtn.addEventListener('click', () => {
		// Stop the sequencer (cancels scheduled audio + timers) BEFORE disposing the
		// engine, so we never touch a closed AudioContext.
		stopSeqPlayback();
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
			deriveTransport(0, 1, parseInt(subdivisionsSelect.value, 10));
		}
	});

	// --- Virtual zoom window (slider 0..1 → [virtualStart..virtualEnd] of real buffer) ---
	let virtualStart = 0;
	let virtualEnd   = 1;

	const MIN_GAP = 0.001;
	const clamp01 = (v) => Math.max(0, Math.min(1, v));

	const sliderToActual = (sliderVal) => virtualStart + sliderVal * (virtualEnd - virtualStart);

	const syncControls = (start, end) => {
		startInput.value  = start.toFixed(3);
		endInput.value    = end.toFixed(3);
		startSlider.value = start;
		endSlider.value   = end;
	};

	// Take slider-space [start, end] (0..1), map through virtual window, slice the engine.
	const applyRange = (sliderStart, sliderEnd, opts = { keepPlayhead: true }) => {
		const subdivisions = parseInt(subdivisionsSelect.value, 10);
		if (!(sliderEnd > sliderStart) || subdivisions <= 0) return;
		const actualStart = sliderToActual(sliderStart);
		const actualEnd   = sliderToActual(sliderEnd);
		slicer.slice(actualStart, actualEnd, subdivisions, opts);
		deriveTransport(actualStart, actualEnd, subdivisions);
	};

	const setStart = (raw) => {
		let start  = clamp01(parseFloat(raw));
		const end  = clamp01(parseFloat(endInput.value));
		if (isNaN(start)) return;
		if (start > end - MIN_GAP) start = Math.max(0, end - MIN_GAP);
		syncControls(start, end);
		applyRange(start, end);
	};

	const setEnd = (raw) => {
		const start = clamp01(parseFloat(startInput.value));
		let end     = clamp01(parseFloat(raw));
		if (isNaN(end)) return;
		if (end < start + MIN_GAP) end = Math.min(1, start + MIN_GAP);
		syncControls(start, end);
		applyRange(start, end);
	};

	startInput.addEventListener('input',  (e) => setStart(e.target.value));
	endInput.addEventListener('input',    (e) => setEnd(e.target.value));
	startSlider.addEventListener('input', (e) => setStart(e.target.value));
	endSlider.addEventListener('input',   (e) => setEnd(e.target.value));

	// Slice button — just re-applies the current slider range.
	sliceBtn.addEventListener('click', () => {
		const start = clamp01(parseFloat(startInput.value));
		const end   = clamp01(parseFloat(endInput.value));
		applyRange(start, end);
	});

	// Subdivisions change → re-slice using current slider range.
	subdivisionsSelect.addEventListener('change', () => {
		const start = clamp01(parseFloat(startInput.value));
		const end   = clamp01(parseFloat(endInput.value));
		applyRange(start, end);
	});

	// --- Cut: commit current slider selection as the new virtual window ---
	cutBtn.addEventListener('click', () => {
		const sStart = clamp01(parseFloat(startInput.value));
		const sEnd   = clamp01(parseFloat(endInput.value));
		if (!(sEnd > sStart + MIN_GAP)) return;

		const newStart = sliderToActual(sStart);
		const newEnd   = sliderToActual(sEnd);
		// Guard against pathological collapse.
		if (newEnd - newStart < MIN_GAP) return;

		virtualStart = newStart;
		virtualEnd   = newEnd;
		slicer.setView(newStart, newEnd);

		// Sliders snap to 0..1 of the new window; the visible waveform now zooms in.
		syncControls(0, 1);
		applyRange(0, 1);
	});

	// --- Handle drag on the canvas → drive the sliders ---
	slicer.view.addEventListener('handledrag', (e) => {
		const { which, fraction } = e.detail;
		// fraction is a real-buffer fraction within the current view window.
		const span = virtualEnd - virtualStart;
		if (span <= 0) return;
		const sliderVal = clamp01((fraction - virtualStart) / span);
		if (which === 'start') {
			setStart(sliderVal);
		} else if (which === 'end') {
			setEnd(sliderVal);
		}
	});

	// ============================================================
	// Sequencer
	// ============================================================
	const seqEl = document.createElement('div');
	seqEl.className = 'sequencer';

	const seqToolbar = document.createElement('div');
	seqToolbar.className = 'seq-toolbar';

	const seqLabel = document.createElement('span');
	seqLabel.className   = 'seq-label';
	seqLabel.textContent = 'Sequencer';

	const seqPlayBtn = document.createElement('button');
	seqPlayBtn.textContent = 'Play Seq';
	seqPlayBtn.className   = 'seq-play-btn';

	const seqStopBtn = document.createElement('button');
	seqStopBtn.textContent = 'Stop Seq';
	seqStopBtn.className   = 'seq-stop-btn';

	const seqClearBtn = document.createElement('button');
	seqClearBtn.textContent = 'Clear';
	seqClearBtn.className   = 'seq-clear-btn';

	const seqLoopBtn = document.createElement('button');
	seqLoopBtn.textContent = 'Loop';
	seqLoopBtn.className   = 'seq-loop-btn';
	seqLoopBtn.setAttribute('aria-pressed', 'false');

	// --- Transport: BPM + step division ---
	const bpmInput = document.createElement('input');
	bpmInput.type  = 'number';
	bpmInput.min   = 20;
	bpmInput.max   = 400;
	bpmInput.step  = 0.01;
	bpmInput.value = 120;
	bpmInput.className = 'seq-bpm-input input-width-60';
	const bpmLabel = document.createElement('label');
	bpmLabel.className = 'seq-bpm-label';
	bpmLabel.appendChild(document.createTextNode('BPM '));
	bpmLabel.appendChild(bpmInput);

	// Shown only when BPM has been manually overridden; click resets to the
	// auto-derived original tempo.
	const bpmResetBtn = document.createElement('button');
	bpmResetBtn.className = 'seq-bpm-reset-btn';
	bpmResetBtn.hidden    = true;
	bpmResetBtn.title     = 'Reset to the original calculated BPM';

	const divisionSelect = document.createElement('select');
	divisionSelect.className = 'seq-division-select';
	// value = note denominator: 1/2, 1/4, 1/8, 1/16, 1/32
	[2, 4, 8, 16, 32].forEach((denom) => {
		const opt = document.createElement('option');
		opt.value = denom;
		opt.textContent = '1/' + denom;
		divisionSelect.appendChild(opt);
	});
	divisionSelect.value = 16;
	const divisionLabel = document.createElement('label');
	divisionLabel.className = 'seq-division-label';
	divisionLabel.appendChild(document.createTextNode('Step '));
	divisionLabel.appendChild(divisionSelect);

	const seqStatus = document.createElement('span');
	seqStatus.className = 'seq-status';

	seqToolbar.appendChild(seqLabel);
	seqToolbar.appendChild(seqPlayBtn);
	seqToolbar.appendChild(seqStopBtn);
	seqToolbar.appendChild(seqLoopBtn);
	seqToolbar.appendChild(seqClearBtn);
	seqToolbar.appendChild(bpmLabel);
	seqToolbar.appendChild(bpmResetBtn);
	seqToolbar.appendChild(divisionLabel);
	seqToolbar.appendChild(seqStatus);

	const seqStepsRow = document.createElement('div');
	seqStepsRow.className = 'seq-steps';

	seqEl.appendChild(seqToolbar);
	seqEl.appendChild(seqStepsRow);
	container.appendChild(seqEl);

	// Sequencer state.
	// cursor: null = no insertion cursor (play-on-click mode).
	//         integer 0..steps.length = insert position for next added slice.
	const seq = {
		steps:        [],
		cursor:       null,
		isPlaying:    false,
		currentStep:  -1,
		loop:         false,
	};

	// --- Transport tempo state ---
	// The sliced selection is treated as one bar (4 beats). On (re)slice we
	// auto-derive BPM and step division so a freshly loaded loop reconstructs
	// itself, unless the user has manually overridden either control.
	let bpm           = 120;
	let originalBPM   = 120;            // last auto-derived (natural) tempo of the selection
	let divisionDenom = 16;
	let bpmManual     = false;
	let divisionManual= false;

	const stepSec = () => (60 / bpm) * (4 / divisionDenom);

	// Fill strategy: how each step's slice is fit into its step. Current mode is
	// "repitch" — scale playback rate by the tempo ratio so the slice fills the
	// space created by a BPM change (rate 1 at the original tempo). This is the
	// single swap point for future fill modes (gap / time-stretch / per-slice).
	const playbackRateForStep = (/* sliceIdx */) => (originalBPM > 0 ? bpm / originalBPM : 1);

	const updateBpmResetBtn = () => {
		const show = bpmManual && isFinite(originalBPM) && originalBPM > 0;
		bpmResetBtn.hidden = !show;
		if (show) bpmResetBtn.textContent = `↺ ${originalBPM.toFixed(2)}`;
	};

	const deriveTransport = (selStart, selEnd, n) => {
		const buf = slicer.engine.audioBuffer;
		if (!buf) return;
		const selDur = (selEnd - selStart) * buf.duration;
		if (selDur <= 0) return;
		originalBPM = 240 / selDur;        // one bar (4 beats) == the selection
		if (!bpmManual) {
			bpm = originalBPM;
			bpmInput.value = bpm.toFixed(2);
		}
		if (!divisionManual && [2, 4, 8, 16, 32].includes(n)) {
			divisionDenom = n;             // each step == one slice
			divisionSelect.value = String(n);
		}
		updateBpmResetBtn();
	};

	const updateSeqMode = () => {
		slicer.sequencerMode = (seq.cursor !== null);
	};

	bpmInput.addEventListener('input', () => {
		const v = parseFloat(bpmInput.value);
		if (!isNaN(v) && v > 0) {
			bpm = v;
			bpmManual = true;
			updateBpmResetBtn();
		}
	});
	bpmResetBtn.addEventListener('click', () => {
		bpm       = originalBPM;
		bpmManual = false;
		bpmInput.value = bpm.toFixed(2);
		updateBpmResetBtn();
	});
	divisionSelect.addEventListener('change', () => {
		divisionDenom  = parseInt(divisionSelect.value, 10);
		divisionManual = true;
	});

	// Drag-and-drop state lives outside of renderSequencer so it survives re-renders.
	let dragFromIdx = null;

	// The transport's visual loop owns the `.playing` highlight (renderSequencer no
	// longer paints it). Toggling directly avoids a full DOM rebuild every step.
	// Self-healing: if renderSequencer rebuilds the row mid-play, playingStepEl
	// points at a detached node and the next frame swaps onto the fresh node.
	let playingStepEl = null;
	const setPlayingStep = (idx) => {
		const child = (idx >= 0 && idx < seqStepsRow.children.length) ? seqStepsRow.children[idx] : null;
		const target = (child && child.classList.contains('seq-step')) ? child : null;
		if (target === playingStepEl) return;
		if (playingStepEl) playingStepEl.classList.remove('playing');
		if (target) target.classList.add('playing');
		playingStepEl = target;
	};

	const clearDropMarkers = () => {
		seqStepsRow.querySelectorAll('.drop-before, .drop-after, .drop-into, .drop-clone')
			.forEach((el) => el.classList.remove('drop-before', 'drop-after', 'drop-into', 'drop-clone'));
	};

	const moveStep = (from, to) => {
		if (from < 0 || from >= seq.steps.length) return;
		if (to < 0) to = 0;
		if (to > seq.steps.length) to = seq.steps.length;

		const selectedIdx = seq.cursor !== null ? seq.cursor - 1 : null;
		const item        = seq.steps[from];
		seq.steps.splice(from, 1);
		const insertAt    = (from < to) ? to - 1 : to;
		seq.steps.splice(insertAt, 0, item);

		if (selectedIdx !== null) {
			let s;
			if (selectedIdx === from) {
				// The selected step itself was dragged — keep it selected.
				s = insertAt;
			} else {
				s = selectedIdx;
				if (from < s)        s--;        // removal shifted us left
				if (insertAt <= s)   s++;        // insert shifted us right
			}
			seq.cursor = s + 1;
		}

		// Reflect the new ordering in playback if we're mid-play.
		// (We don't try to track which "physical" step is playing — simpler to keep
		//  advancing by index in the new array.)
		renderSequencer();
	};

	// Option-drag: copy the source step to the drop position; source stays in place.
	const cloneStep = (from, to) => {
		if (from < 0 || from >= seq.steps.length) return;
		if (to < 0) to = 0;
		if (to > seq.steps.length) to = seq.steps.length;

		const sliceIdx = seq.steps[from];
		seq.steps.splice(to, 0, sliceIdx);

		// Anything at index >= `to` shifted right by 1 — including the selection.
		if (seq.cursor !== null) {
			const selectedIdx = seq.cursor - 1;
			if (to <= selectedIdx) seq.cursor++;
		}

		renderSequencer();
	};

	const renderSequencer = () => {
		seqStepsRow.innerHTML = '';

		for (let i = 0; i < seq.steps.length; i++) {
			const sliceIdx = seq.steps[i];
			const stepBox  = document.createElement('div');
			stepBox.className   = 'seq-step';
			stepBox.textContent = String(sliceIdx + 1);
			stepBox.style.background = PALETTE[sliceIdx % PALETTE.length];
			stepBox.title       = 'Click to set insertion cursor here. Drag to reorder. Shift-click to delete.';
			// Structural edits are disabled during playback (see onStepVisual / setPlayingStep).
			stepBox.draggable   = !seq.isPlaying;
			if (seq.cursor === i + 1)      stepBox.classList.add('selected');

			stepBox.addEventListener('click', (ev) => {
				if (seq.isPlaying) return;
				if (ev.shiftKey) {
					removeStep(i);
					return;
				}
				seq.cursor = (seq.cursor === i + 1) ? null : i + 1;
				updateSeqMode();
				renderSequencer();
			});

			// --- Drag source ---
			stepBox.addEventListener('dragstart', (ev) => {
				dragFromIdx = i;
				// Allow either copy or move; the actual choice is decided by altKey at drop.
				ev.dataTransfer.effectAllowed = 'copyMove';
				ev.dataTransfer.setData('text/plain', String(i));
				stepBox.classList.add('dragging');
			});
			stepBox.addEventListener('dragend', () => {
				dragFromIdx = null;
				stepBox.classList.remove('dragging');
				clearDropMarkers();
			});

			// --- Drop target ---
			stepBox.addEventListener('dragover', (ev) => {
				if (dragFromIdx === null) return;
				ev.preventDefault();
				ev.dataTransfer.dropEffect = ev.altKey ? 'copy' : 'move';
				const rect  = stepBox.getBoundingClientRect();
				const after = (ev.clientX - rect.left) > rect.width / 2;
				stepBox.classList.toggle('drop-after',  after);
				stepBox.classList.toggle('drop-before', !after);
				stepBox.classList.toggle('drop-clone',  ev.altKey);
			});
			stepBox.addEventListener('dragleave', () => {
				stepBox.classList.remove('drop-before', 'drop-after', 'drop-clone');
			});
			stepBox.addEventListener('drop', (ev) => {
				if (dragFromIdx === null) return;
				ev.preventDefault();
				const rect  = stepBox.getBoundingClientRect();
				const after = (ev.clientX - rect.left) > rect.width / 2;
				const to    = i + (after ? 1 : 0);
				const from  = dragFromIdx;
				const clone = ev.altKey;
				dragFromIdx = null;
				clearDropMarkers();
				if (clone) {
					cloneStep(from, to);
				} else {
					moveStep(from, to);
				}
			});

			seqStepsRow.appendChild(stepBox);
		}

		const addSlot = document.createElement('div');
		addSlot.className   = 'seq-add-slot';
		addSlot.textContent = '+';
		addSlot.title       = 'Click to append after the last step. Drop here to move a step to the end.';
		if (seq.cursor === seq.steps.length) addSlot.classList.add('selected');
		addSlot.addEventListener('click', () => {
			if (seq.isPlaying) return;
			seq.cursor = (seq.cursor === seq.steps.length) ? null : seq.steps.length;
			updateSeqMode();
			renderSequencer();
		});
		addSlot.addEventListener('dragover', (ev) => {
			if (dragFromIdx === null) return;
			ev.preventDefault();
			ev.dataTransfer.dropEffect = ev.altKey ? 'copy' : 'move';
			addSlot.classList.add('drop-into');
			addSlot.classList.toggle('drop-clone', ev.altKey);
		});
		addSlot.addEventListener('dragleave', () => {
			addSlot.classList.remove('drop-into', 'drop-clone');
		});
		addSlot.addEventListener('drop', (ev) => {
			if (dragFromIdx === null) return;
			ev.preventDefault();
			const from  = dragFromIdx;
			const clone = ev.altKey;
			dragFromIdx = null;
			clearDropMarkers();
			if (clone) {
				cloneStep(from, seq.steps.length);
			} else {
				moveStep(from, seq.steps.length);
			}
		});
		seqStepsRow.appendChild(addSlot);

		if (seq.isPlaying) {
			seqStatus.textContent = `Playing step ${seq.currentStep + 1} of ${seq.steps.length}`;
		} else if (seq.cursor === null) {
			seqStatus.textContent = seq.steps.length === 0
				? 'Click + to start, then click slices to add.'
				: 'Click a step (or +) to add new slices there.';
		} else {
			const pos = seq.cursor;
			const where = pos === 0 ? 'before step 1'
				: pos === seq.steps.length ? 'at end'
				: `after step ${pos}`;
			seqStatus.textContent = `Add mode — clicking a slice inserts ${where}.`;
		}
	};

	const removeStep = (i) => {
		if (i < 0 || i >= seq.steps.length) return;
		seq.steps.splice(i, 1);
		if (seq.cursor !== null) {
			if (seq.cursor > i) seq.cursor--;
			if (seq.cursor > seq.steps.length) seq.cursor = seq.steps.length;
		}
		renderSequencer();
	};

	const addStepAtCursor = (sliceIdx) => {
		if (seq.cursor === null) return;
		seq.steps.splice(seq.cursor, 0, sliceIdx);
		seq.cursor += 1;
		renderSequencer();
	};

	// Intercept segment clicks while in add mode and route them into the sequencer.
	slicer.view.addEventListener('segmentclick', (e) => {
		if (seq.cursor !== null) {
			addStepAtCursor(e.detail.index);
		}
	});

	// Re-render whenever subdivisions change — the labels we show (slice numbers)
	// still refer to indices; out-of-range steps just no-op during playback.
	subdivisionsSelect.addEventListener('change', () => renderSequencer());

	// --- Sequencer playback (sample-accurate, via Transport on the audio clock) ---
	const transport = new Transport({
		engine:          slicer.engine,
		getSteps:        () => seq.steps,
		getStepSec:      stepSec,
		getPlaybackRate: playbackRateForStep,
		isLooping:       () => seq.loop,
		onStepVisual: (step, sliceIdx, frac) => {
			setPlayingStep(step);
			if (seq.currentStep !== step) {
				seq.currentStep = step;
				seqStatus.textContent = `Playing step ${step + 1} of ${seq.steps.length}`;
			}
			// All three are required for the playhead to draw (waveform-view.js).
			slicer.view.setActiveSegment(sliceIdx);
			slicer.view.setIsPlaying(true);
			slicer.view.setPlayheadPosition(frac);
		},
		onStop: () => {
			seq.isPlaying   = false;
			seq.currentStep = -1;
			slicer.sequencerPlaying = false;
			setPlayingStep(-1);
			slicer.view.setIsPlaying(false);
			slicer.view.setActiveSegment(-1);
			slicer.view.setPlayheadPosition(0);
			renderSequencer();
		},
	});

	const startSeqPlayback = async () => {
		if (seq.isPlaying) return;
		if (seq.steps.length === 0) return;

		// Exit add mode while playing so the highlights aren't confusing.
		seq.cursor = null;
		updateSeqMode();

		seq.isPlaying   = true;
		seq.currentStep = -1;
		slicer.sequencerPlaying = true;
		slicer.stop();        // cancel any free-run playback + its playhead animation
		renderSequencer();    // reflect playing state (disables drag, clears cursor)

		await transport.start();
	};

	// transport.stop() cancels scheduled audio + both timers, then fires onStop
	// (above), which resets all UI/view state.
	const stopSeqPlayback = () => transport.stop();

	seqPlayBtn.addEventListener('click', startSeqPlayback);
	seqStopBtn.addEventListener('click', stopSeqPlayback);
	seqLoopBtn.addEventListener('click', () => {
		seq.loop = !seq.loop;
		seqLoopBtn.classList.toggle('active', seq.loop);
		seqLoopBtn.setAttribute('aria-pressed', String(seq.loop));
	});
	seqClearBtn.addEventListener('click', () => {
		stopSeqPlayback();
		seq.steps  = [];
		seq.cursor = null;
		updateSeqMode();
		renderSequencer();
	});

	renderSequencer();
}

addSlicerBtn.addEventListener('click', createSlicer);

// Create one slicer by default
createSlicer();
