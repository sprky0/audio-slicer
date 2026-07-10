import AudioSlicerController from './audio-slicer-controller.js';
import Transport from './transport.js';
import grid from './beat-grid.js';
import DragControl from './drag-control.js';
import PALETTE from './palette.js';
import midiClock from './midi-clock.js';
import { getAudioContext } from './audio-context.js';
import { createTimeStretcher } from './timestretch.js';
import { renderMix, encodeWav, triggerDownload } from './export-wav.js';
import {
	saveStateRaw, loadStateRaw, clearStateRaw,
	putAudio, getAudio, deleteAudio, clearAudio,
} from './storage.js';

const slicersDiv    = document.getElementById('slicers');
const addSlicerBtn  = document.getElementById('addSlicerBtn');
const clearStateBtn = document.getElementById('clearStateBtn');
let slicerCount     = 0;

// Assign a toolbar cell's grid footprint: how many columns (and optionally rows)
// of the fluid auto-fit grid it occupies. Drives the --col-span / --row-span
// custom properties the CSS reads. cols === 'full' makes the cell span the row.
function setSpan(el, cols, rows = 1) {
	if (cols === 'full') { el.classList.add('span-full'); }
	else { el.style.setProperty('--col-span', cols); }
	if (rows !== 1) el.style.setProperty('--row-span', rows);
	return el;
}

// Section-label chip (grey plate, bold orange text). A grid citizen like any
// control — title a region with it, e.g. makeLabel('Source').
function makeLabel(text) {
	const el = document.createElement('span');
	el.className   = 'ui-label';
	el.textContent = text;
	return el;
}

// --- Background-task overlay ---------------------------------------------------
// A non-interactive status band overlaid on a slicer's waveform, showing deferred
// clip processing (today: WSOLA time-stretch builds kicked off on a cache miss).
// Absolutely positioned, so it never shifts layout; per-slicer, so each clip shows
// its own work. Each build is atomic → progress is task-count based (done/total
// within the current burst). It fades in/out gently and resets between bursts.
// hostEl must be position:relative (the .wave-wrap around the canvas).
function createWaveTaskTracker(hostEl) {
	const overlay = document.createElement('div');
	overlay.className = 'wave-task';
	const fill = document.createElement('div');
	fill.className = 'wave-task-fill';
	const label = document.createElement('span');
	label.className = 'wave-task-label';
	overlay.appendChild(fill);
	overlay.appendChild(label);
	hostEl.appendChild(overlay);

	// Progress is sample-accurate: across all jobs in the current burst, track the
	// total output samples to produce vs samples produced so far. fraction =
	// doneSamples / totalSamples (0..1) drives the fill width; activeJobs hitting 0
	// ends the burst.
	// Brief dwell at 100% before fading out, so an instant burst still registers
	// and the fade-out reads as "done" rather than a flicker. A new job during the
	// hold cancels the fade-out and continues the same running total.
	const HOLD_MS = 350;
	let totalSamples = 0, doneSamples = 0, activeJobs = 0, name = 'Working…', holdTimer = null;
	function paint(frac) {
		const pct = Math.round(Math.max(0, Math.min(1, frac)) * 100);
		fill.style.width  = pct + '%';
		label.textContent = `${name} — ${pct}%`;
	}
	function render() {
		if (activeJobs <= 0) {                   // burst drained — show 100%, then fade out
			if (totalSamples > 0) paint(1);
			if (!holdTimer) {
				holdTimer = setTimeout(() => {
					holdTimer = null;
					totalSamples = 0; doneSamples = 0;
					overlay.classList.remove('is-active');
				}, HOLD_MS);
			}
			return;
		}
		overlay.classList.add('is-active');      // fade in
		paint(totalSamples > 0 ? doneSamples / totalSamples : 0);
	}
	return {
		// Register a job by its total output-sample count. A job arriving while the
		// bar is in its post-completion hold is part of the SAME ongoing effort —
		// cancel the fade-out and keep accumulating into the same running total, so
		// the percentage stays relative to all work in flight, not per slice. The
		// counters only reset after a genuine idle gap (the hold elapsing).
		begin(taskLabel, jobSamples) {
			if (holdTimer) { clearTimeout(holdTimer); holdTimer = null; }
			// A fresh burst (totalSamples back to 0) repaints to 0% via render()
			// below; with no width transition that lands instantly from the left,
			// so the previous burst's leftover 100% never animates down.
			if (taskLabel) name = taskLabel;
			totalSamples += Math.max(0, jobSamples || 0);
			activeJobs++;
			render();
		},
		// Report samples produced since the last call (any job in flight).
		advance(deltaSamples) { doneSamples += Math.max(0, deltaSamples || 0); render(); },
		// A job finished.
		end() { activeJobs = Math.max(0, activeJobs - 1); render(); },
	};
}

// --- Persistence: registry of live slicers + debounced save ---
let nextSlicerId  = 0;
const slicers     = [];     // { id, getState, setMidiBpm, seqStart, seqStop, hasTiles }
let restoreCount  = 0;      // > 0 while restoring — suppresses save thrash
let saveTimer     = null;

// Global MIDI clock sync preferences (whether sync is on + which input), persisted
// alongside the slicers so a reload restores the connection.
const midiSettings = { enabled: false, inputId: null };

// Sequencer tile-edit mode (global — applies to every track's tile row):
//   'pack' — classic length-conserving behavior: reorder repacks the row and edge
//            resize trades units with the play-order neighbour, so there are never
//            gaps (Σ w of clips always fills the bar).
//   'gaps' — free placement: moving a clip leaves a silent gap where it was and
//            overwrites whatever it lands on; edge resize leaves a gap when
//            shrinking and overwrites the neighbour when growing.
// Gaps are just silent tiles ({ gap:true, w }) in the shared list, so Σ w still
// equals the bar (U) in both modes and pack-mode resize can still absorb them.
let seqEditMode = 'pack';
let onSeqEditModeChange = () => {};   // buildMidiBar wires this to refresh the toggle

// Monotonic wall-clock order stamped on each slicer when its sequencer starts. With
// no external MIDI clock, the lowest-stamped slicer still playing is the internal
// clock source ("first played"). Audio-clock times aren't comparable across slicers
// (each has its own AudioContext), so this separate counter decides precedence.
let playSeqCounter = 0;

function saveState() {
	saveStateRaw({
		version:      3,
		nextSlicerId,
		slicers:      slicers.map((s) => s.getState()),
		midi:         { enabled: midiSettings.enabled, inputId: midiSettings.inputId },
		master:       { bpm: masterClock.bpm, userSet: masterClock.userSet },
		seqEditMode,
	});
}

function scheduleSave() {
	if (restoreCount > 0) return;
	clearTimeout(saveTimer);
	saveTimer = setTimeout(saveState, 400);
}

function createSlicer(savedState = null) {
	slicerCount++;
	const id = savedState && typeof savedState.id === 'number' ? savedState.id : nextSlicerId++;
	let currentFileName = savedState ? (savedState.fileName || null) : null;
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
	cutBtn.textContent = 'Trim';
	cutBtn.className   = 'slicer-cut-btn';

	const resetBtn = document.createElement('button');
	resetBtn.textContent = 'Reset';
	resetBtn.className = 'slicer-reset-btn';

	const duplicateBtn = document.createElement('button');
	duplicateBtn.textContent = 'Duplicate';
	duplicateBtn.title = 'Make a copy of this slicer (audio, region, beats/step, tiles, mix)';

	const removeBtn = document.createElement('button');
	removeBtn.textContent = 'Remove';
	removeBtn.className = 'slicer-remove-btn';

	// Native file input is kept for the OS picker but hidden; a real button
	// triggers it so the control matches the rest of the toolbar.
	const fileInput = document.createElement('input');
	fileInput.type  = 'file';
	fileInput.accept= 'audio/wav,audio/mp3';
	fileInput.hidden = true;

	const selectFileBtn = document.createElement('button');
	selectFileBtn.textContent = 'Select File';
	selectFileBtn.className   = 'slicer-file-btn';
	selectFileBtn.addEventListener('click', () => fileInput.click());

	// File-details readout: filename + duration + sample rate (or a placeholder).
	const fileInfo = document.createElement('div');
	fileInfo.className = 'file-info';

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

	// Wrap the region sliders in the unified DragControl (design-system control).
	// Each mirrors its value to the range input and dispatches input/change, so the
	// existing setStart/setEnd listeners below keep driving the region + re-slice.
	// The sliders are also set PROGRAMMATICALLY (syncControls, reset, marker drags);
	// every such set is followed by ctrl.syncFromEl() to refresh the display without
	// a change-loop (syncFromEl dispatches nothing).
	const startSliderCtrl = new DragControl({ el: startSlider, label: 'Start', format: (v) => (+v).toFixed(2) });
	const endSliderCtrl   = new DragControl({ el: endSlider,   label: 'End',   format: (v) => (+v).toFixed(2) });

	// Beats: how many quarter-note beats the selection is (its musical length). This
	// sets the tempo mapping — BPM = 60·beats/selectionDuration — so e.g. 8 = two bars
	// of 4/4, 6 = two bars of 3/4. Odd counts allowed (a fixed quarter-note beat unit
	// for now; dotted/compound meters are future work). The sequencer grid (how many
	// equal slices the selection is cut into) is derived from beats × the Step
	// subdivision, not from this control directly. See cellCount().
	const beatsSelect = document.createElement('select');
	[1, 2, 3, 4, 6, 8, 12, 16].forEach(val => {
		const opt = document.createElement('option');
		opt.value = val;
		opt.textContent = val;
		beatsSelect.appendChild(opt);
	});
	beatsSelect.value = 4;

	// --- Details toggle (hides Start/End inputs + sliders behind a button) ---
	const detailsBtn = document.createElement('button');
	detailsBtn.textContent = 'Show details';
	detailsBtn.className   = 'slicer-details-btn';
	detailsBtn.setAttribute('aria-expanded', 'false');

	// Interface controls live in a fluid auto-fit grid (the "cluster").
	const cluster = document.createElement('div');
	cluster.className = 'toolbar-cluster';

	// Beats: a stepped drag-control wrapping the beat-count <select>.
	const beatsControl = new DragControl({ el: beatsSelect, label: 'Beats' });

	// --- Volume / Pan / Pitch: unified drag-controls. These shape the audible output
	// (including sequenced playback), so they live in the always-visible header — not
	// the details panel. onChange wired after the slicer exists (see control wiring).
	const volumeKnob = new DragControl({
		label: 'Vol',
		min: 0, max: 1, step: 0.01, value: 1,
		format: (v) => `${Math.round(v * 100)}%`,
	});
	const panKnob = new DragControl({
		label: 'Pan',
		min: -1, max: 1, step: 0.01, value: 0, detent: 0,
		format: (v) => (Math.abs(v) < 0.005 ? 'C' : (v < 0 ? `L${Math.round(-v * 100)}` : `R${Math.round(v * 100)}`)),
	});
	// Master pitch (semitones) — shifts every sequencer step; per-step offsets stack
	// on top. Center detent at 0, double-click to reset.
	const pitchKnob = new DragControl({
		label: 'Pitch',
		min: -5, max: 5, step: 1, value: 0, detent: 0,
		format: (v) => (v > 0 ? `+${v}` : `${v}`),
	});

	// --- Details panel: ALL source-editing controls (everything but the waveform and
	// the sequencer controls). Shown = "edit" mindset (big waveform, small tile row);
	// hidden = "perform" mindset (small waveform, big tile row). applyEditMode() trades
	// the two heights so the layout signals which task you're in.
	const detailsPanel = document.createElement('div');
	detailsPanel.className = 'slicer-details';

	// Source-editing controls, in their own auto-fit grid inside the panel.
	const editGrid = document.createElement('div');
	editGrid.className = 'toolbar-cluster';
	editGrid.appendChild(setSpan(selectFileBtn, 1));
	editGrid.appendChild(setSpan(fileInfo, 3));               // name + duration + rate
	editGrid.appendChild(setSpan(beatsControl.getElement(), 1));
	editGrid.appendChild(setSpan(cutBtn, 1));                 // "Trim"
	editGrid.appendChild(setSpan(resetBtn, 1));

	// The Start/End number fields are gone from the UI — the waveform handles and the
	// Start/End slider drag-controls cover region editing. The `startInput`/`endInput`
	// elements are kept (detached, never appended) purely as the region's value store,
	// which setStart/setEnd/Slice/Trim/restore/syncControls read + write.
	const sliderRow = document.createElement('div');
	sliderRow.className = 'slicer-slider-row';
	// The drag-controls carry their own "Start"/"End" label + value readout.
	sliderRow.appendChild(startSliderCtrl.getElement());
	sliderRow.appendChild(endSliderCtrl.getElement());

	detailsPanel.appendChild(editGrid);
	detailsPanel.appendChild(sliderRow);

	// Header (always visible): mix controls that also drive the sequencer + the mode
	// toggle + remove.
	cluster.appendChild(fileInput);                          // hidden native input
	cluster.appendChild(setSpan(makeLabel('Source'), 1));    // section title, inline with the mix controls
	cluster.appendChild(setSpan(volumeKnob.getElement(), 1));
	cluster.appendChild(setSpan(panKnob.getElement(),    1));
	cluster.appendChild(setSpan(pitchKnob.getElement(),  1));
	cluster.appendChild(setSpan(detailsBtn, 2));             // Show/Hide details
	cluster.appendChild(setSpan(duplicateBtn, 1));
	cluster.appendChild(setSpan(removeBtn, 1));
	// The details panel is a SIBLING of the header cluster, not a grid item in it.
	// Otherwise its full-width row forces the auto-fit grid to keep all columns, and
	// toggling it would resize (jump) the header cells. As a sibling it just stacks
	// below the header and can show/hide with no effect on the header layout.

	// Edit vs perform: trade waveform height for sequencer tile-row height. The tile
	// height comes from --tile-h (set per-mode by the .editing class in CSS); the
	// waveform height is driven in JS (the view forces its own canvas height).
	const WAVE_H_EDIT = 280, WAVE_H_PERFORM = 96;
	// New (empty) slicers open in edit mode (details shown) so you can load + slice;
	// restored or duplicated slicers are already configured, so they open in perform
	// mode (details hidden, big sequencer). `savedState` present ⇒ not a fresh add.
	let detailsShown = !savedState;
	const applyEditMode = (editing) => {
		detailsShown = editing;
		detailsPanel.hidden = !editing;
		container.classList.toggle('editing', editing);
		detailsBtn.textContent = editing ? 'Hide details' : 'Show details';
		detailsBtn.setAttribute('aria-expanded', String(editing));
		detailsBtn.classList.toggle('active', editing);
		if (slicer && slicer.view) slicer.view.setHeight(editing ? WAVE_H_EDIT : WAVE_H_PERFORM);
		// The tile row changed height with --tile-h → repaint its backdrops next frame.
		requestAnimationFrame(() => { try { redrawTileWaves(); } catch (err) { /* not ready */ } });
	};
	detailsBtn.addEventListener('click', () => applyEditMode(!detailsShown));

	controls.appendChild(cluster);
	controls.appendChild(detailsPanel);   // sibling of the header (see note above)

	container.appendChild(controls);
	slicersDiv.appendChild(container);

	// Slicer instance — width is responsive (driven by the slicer container).
	const slicer = new AudioSlicerController(container, { height: 200 });

	// Render the file-details readout: name (truncating) + duration + sample rate,
	// or a muted placeholder when empty. Pulls live specs from the decoded buffer.
	function renderFileInfo() {
		fileInfo.textContent = '';
		if (!currentFileName) {
			fileInfo.classList.remove('has-file');
			fileInfo.textContent = 'No file loaded';
			return;
		}
		fileInfo.classList.add('has-file');
		const name = document.createElement('span');
		name.className   = 'file-info-name';
		name.textContent = currentFileName;
		name.title       = currentFileName;
		fileInfo.appendChild(name);

		const buf = slicer.engine && slicer.engine.audioBuffer;
		if (buf) {
			const secs = buf.duration;
			const mmss = `${Math.floor(secs / 60)}:${String(Math.floor(secs % 60)).padStart(2, '0')}`;
			const meta = document.createElement('span');
			meta.className   = 'file-info-meta';
			meta.textContent = `${mmss} · ${(buf.sampleRate / 1000).toFixed(1)} kHz`;
			fileInfo.appendChild(meta);
		}
	}
	renderFileInfo();

	// Wrap the waveform canvas in a positioned host and attach the background-task
	// overlay (time-stretch progress) so it floats over the waveform without ever
	// reflowing the layout.
	const waveCanvas = container.querySelector('.slicer-waveform-canvas');
	const waveWrap   = document.createElement('div');
	waveWrap.className = 'wave-wrap';
	waveCanvas.parentNode.insertBefore(waveWrap, waveCanvas);
	waveWrap.appendChild(waveCanvas);
	const slicerTasks = createWaveTaskTracker(waveWrap);

	// Preview transport: a small floating Play/Pause + Stop over the waveform,
	// shown only while a click-to-audition preview is active. Absolutely positioned
	// (no layout shift); Stop ends the preview and dismisses the overlay.
	const waveTransport = document.createElement('div');
	waveTransport.className = 'wave-transport';
	const previewPlayPause = document.createElement('button');
	previewPlayPause.className = 'wave-transport-btn';
	previewPlayPause.textContent = '▶';
	previewPlayPause.title = 'Play / pause preview';
	const previewStop = document.createElement('button');
	previewStop.className = 'wave-transport-btn';
	previewStop.textContent = '⏹';
	previewStop.title = 'Stop preview';
	waveTransport.appendChild(previewPlayPause);
	waveTransport.appendChild(previewStop);
	waveWrap.appendChild(waveTransport);

	// --- Preview transport wiring ---
	// A click-to-audition preview (waveform click) plays free-run and loops; the
	// floating transport shows while it's active. Only previews emit
	// 'playstatechange' — the sequencer schedules directly and doesn't — so the
	// overlay never appears during master playback. Play All calls slicer.stop()
	// (fires 'stopped'), which dismisses it.
	let isPlaying = false;
	previewPlayPause.addEventListener('click', () => {
		if (isPlaying) {
			slicer.pause();
		} else if (slicer.isPaused && slicer.pausedSegment !== null) {
			slicer.resume();
		} else {
			slicer.playSegment(0);
		}
		// state reflected via the playstatechange handler below
	});
	previewStop.addEventListener('click', () => slicer.stop());   // 'stopped' hides it

	container.addEventListener('playstatechange', (e) => {
		const state = e.detail.state;
		isPlaying = (state === 'playing');
		previewPlayPause.textContent = isPlaying ? '⏸' : '▶';
		if (state === 'playing')      waveTransport.classList.add('visible');
		else if (state === 'stopped') waveTransport.classList.remove('visible');
		// 'paused' keeps the overlay visible (showing ▶ to resume).
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

		// Reset region + Beats/Step back to defaults (4 beats · 1/16 → 16-cell grid).
		startInput.value  = 0;
		endInput.value    = 1;
		startSlider.value = 0;
		endSlider.value   = 1;
		startSliderCtrl.syncFromEl();
		endSliderCtrl.syncFromEl();
		beatsSelect.value = 4;
		beatsControl.syncFromEl();
		divisionDenom = 16;
		divisionSelect.value = '16';
		stepControl.syncFromEl();

		// Stop playback, re-slice the full buffer at the default resolution
		// (segmentsliced → default tiles), and restore the auto-derived tempo.
		slicer.stop();
		playPauseBtn.textContent = 'Play';
		isPlaying = false;
		bpmManual      = false;
		applyRange(0, 1, { autoPlay: false, keepPlayhead: false });
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
		const idx = slicers.findIndex((s) => s.id === id);
		if (idx !== -1) slicers.splice(idx, 1);
		deleteAudio('audio-' + id);
		scheduleSave();
	});

	// --- Duplicate: clone this slicer via its serialized state under a new id ---
	// getState() is the same snapshot persistence uses, so createSlicer() reconstructs
	// everything (region, beats/step, tiles, mix). The audio lives in IndexedDB keyed
	// by id, so copy the blob to the new id first, then the new slicer's restore path
	// loads it. The copy lands right after this slicer (DOM + slicers[] order).
	duplicateBtn.addEventListener('click', async () => {
		const state = getState();
		const newId = nextSlicerId++;
		state.id = newId;
		try {
			const blob = await getAudio('audio-' + id);
			if (blob) await putAudio('audio-' + newId, blob);
		} catch (err) { /* nothing to copy — duplicate restores silent */ }
		const newContainer = createSlicer(state);
		if (newContainer && container.parentNode) container.after(newContainer);
		const dup = slicers.pop();                       // createSlicer pushed it last
		const srcIdx = slicers.findIndex((s) => s.id === id);
		if (dup) slicers.splice(srcIdx + 1, 0, dup);     // keep array order == DOM order
		scheduleSave();
	});

	// --- Control wiring (drag-controls fire onChange as you drag) ---
	volumeKnob.onChange = (v) => { slicer.setVolume(v); scheduleSave(); };
	panKnob.onChange    = (v) => { slicer.setPan(v);    scheduleSave(); };
	pitchKnob.onChange  = (v) => { masterPitch = v; schedulePrescan(); scheduleSave(); };

	// File loading — used by the Select File button, waveform drop, and waveform
	// click-when-empty (all three route through here).
	const loadAndInitFile = async (file) => {
		if (!file) return;
		await slicer.loadFile(file);
		// Reset selection to the whole buffer and slice at the current unit
		// resolution (loadFile slices with a placeholder count). This drives
		// segmentsliced → default tiles + deriveTransport.
		virtualStart = 0;
		virtualEnd   = 1;
		startInput.value  = 0;
		endInput.value    = 1;
		startSlider.value = 0;
		endSlider.value   = 1;
		startSliderCtrl.syncFromEl();
		endSliderCtrl.syncFromEl();
		applyRange(0, 1, { autoPlay: false, keepPlayhead: false });
		currentFileName = file.name;
		renderFileInfo();
		await putAudio('audio-' + id, file);
		scheduleSave();
	};

	fileInput.addEventListener('change', (e) => loadAndInitFile(e.target.files[0]));

	// Empty waveform doubles as a drop target + "pick file" affordance. The view
	// only fires these while the source is empty and only for supported files.
	slicer.view.addEventListener('filedrop',       (e) => loadAndInitFile(e.detail.file));
	slicer.view.addEventListener('emptyfileclick', ()  => fileInput.click());

	// --- Virtual zoom window (slider 0..1 → [virtualStart..virtualEnd] of real buffer) ---
	let virtualStart = 0;
	let virtualEnd   = 1;

	// Currently-applied selection in real-buffer fractions (the sliced region).
	// Region buffers + the unit grid are derived from this.
	let selStartFrac = 0;
	let selEndFrac   = 1;

	const MIN_GAP = 0.001;
	const clamp01 = (v) => Math.max(0, Math.min(1, v));

	const sliderToActual = (sliderVal) => virtualStart + sliderVal * (virtualEnd - virtualStart);

	const syncControls = (start, end) => {
		startInput.value  = start.toFixed(3);
		endInput.value    = end.toFixed(3);
		startSlider.value = start;
		endSlider.value   = end;
		startSliderCtrl.syncFromEl();
		endSliderCtrl.syncFromEl();
	};

	// Take slider-space [start, end] (0..1), map through virtual window, slice the engine.
	// The grid is beats × step (cellCount), and the tempo derives from beats.
	const applyRange = (sliderStart, sliderEnd, opts = { keepPlayhead: true }) => {
		const cells = cellCount();
		if (!(sliderEnd > sliderStart) || cells <= 0) return;
		const actualStart = sliderToActual(sliderStart);
		const actualEnd   = sliderToActual(sliderEnd);
		// Record the applied selection BEFORE slicing — region buffers + the unit
		// grid (built lazily during playback) read from these.
		selStartFrac = actualStart;
		selEndFrac   = actualEnd;
		slicer.slice(actualStart, actualEnd, cells, opts);
		deriveTransport(actualStart, actualEnd);
		scheduleSave();
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

	// The slider drag-controls drive the region (they mirror to startSlider/endSlider
	// and dispatch 'input'). startInput/endInput are detached value-holders now, so no
	// listeners on them.
	startSlider.addEventListener('input', (e) => setStart(e.target.value));
	endSlider.addEventListener('input',   (e) => setEnd(e.target.value));

	// Beats change → re-slice (cellCount = beats × step) + re-derive the tempo.
	beatsSelect.addEventListener('change', () => {
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

	const seqLabel = makeLabel('Sequencer');

	const seqPlayBtn = document.createElement('button');
	seqPlayBtn.textContent = 'Play';
	seqPlayBtn.className   = 'seq-play-btn';

	const seqStopBtn = document.createElement('button');
	seqStopBtn.textContent = 'Stop';
	seqStopBtn.className   = 'seq-stop-btn';

	const seqClearBtn = document.createElement('button');
	seqClearBtn.textContent = 'Reset Tiles';
	seqClearBtn.className   = 'seq-clear-btn';

	const seqLoopBtn = document.createElement('button');
	seqLoopBtn.textContent = 'Loop';
	seqLoopBtn.className   = 'seq-loop-btn';
	seqLoopBtn.setAttribute('aria-pressed', 'false');

	// --- Randomize: shuffle tile order; the level sets how many tiles move ---
	// randLevel is a 0–100% knob on the per-tile swap probability: 0 leaves the
	// pattern untouched, 100 fully shuffles it. Restored from saved state below.
	let randLevel = 50;
	const randomizeBtn = document.createElement('button');
	randomizeBtn.textContent = 'Randomize';
	randomizeBtn.className   = 'seq-randomize-btn';
	randomizeBtn.title       = 'Shuffle tile order — higher amount = more swaps / bigger change';

	const randLevelInput = document.createElement('input');
	randLevelInput.type  = 'range';
	randLevelInput.min   = 0;
	randLevelInput.max   = 100;
	randLevelInput.step  = 1;
	randLevelInput.value = randLevel;
	randLevelInput.className = 'seq-rand-level';
	// Amount: drag-control wrapping the range input (its 'input' event still drives
	// the randLevel update below).
	const amountControl = new DragControl({
		el: randLevelInput, label: 'Amt', format: (v) => `${Math.round(v)}%`,
	});

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
	// Step: stepped drag-control wrapping the division <select>.
	const stepControl = new DragControl({ el: divisionSelect, label: 'Step' });

	const seqStatus = document.createElement('span');
	seqStatus.className = 'seq-status';

	// --- Slice settings: act on the currently SELECTED tile (click a tile to select),
	// or on EVERY slice at once while the All toggle is engaged. Sits to the right of
	// Step. Disabled while nothing is targeted. Mute replaces the old per-tile mute dot.
	const sliceLabel = makeLabel('Slice');

	// All: broadcast the slice settings (Mute, Rev, Fades, Refill) to every slice.
	// Transient UI like the selection — not persisted. Lock and Dup stay per-slice
	// (they're positional: pinning or cloning one specific slice).
	let allSlices = false;
	const allToggle = document.createElement('button');
	allToggle.className = 'toggle-btn';
	allToggle.textContent = 'All';
	allToggle.title = 'Apply the slice settings (Mute, Rev, Fades, Refill) to every slice at once';
	allToggle.setAttribute('aria-pressed', 'false');
	allToggle.addEventListener('click', () => {
		allSlices = !allSlices;
		allToggle.classList.toggle('active', allSlices);
		allToggle.setAttribute('aria-pressed', String(allSlices));
		updateSliceSettings();
	});

	// The clips a slice-settings control acts on: every clip when All is engaged,
	// else just the selected one. Gaps carry no audio → nothing to mute/reverse/fade.
	const targetClips = () => {
		if (allSlices) return seq.tiles.filter((t) => !t.gap);
		const sel = seq.tiles.includes(selectedTile) ? selectedTile : null;
		return (sel && !sel.gap) ? [sel] : [];
	};

	const muteToggle = document.createElement('button');
	muteToggle.className = 'toggle-btn';
	muteToggle.textContent = 'Mute';
	muteToggle.setAttribute('aria-pressed', 'false');
	muteToggle.addEventListener('click', () => {
		// Toggle-all semantics: if every target is already muted, unmute them all;
		// otherwise mute them all. With a single target this is a plain toggle.
		const clips = targetClips();
		if (!clips.length) return;
		const on = !clips.every((t) => t.muted);
		clips.forEach((t) => { t.muted = on; });
		renderSequencer();       // repaint the tiles' muted state + refresh these controls
		scheduleSave();
	});
	// Lock: protect the selected slice from being overwritten (gaps-mode paints flow
	// around it) and from being moved by Randomize.
	const lockToggle = document.createElement('button');
	lockToggle.className = 'toggle-btn';
	lockToggle.textContent = 'Lock';
	lockToggle.setAttribute('aria-pressed', 'false');
	lockToggle.addEventListener('click', () => {
		const sel = seq.tiles.includes(selectedTile) ? selectedTile : null;
		if (!sel || sel.gap) return;
		sel.locked = !sel.locked;
		renderSequencer();
		scheduleSave();
	});
	// Reverse: flip the targeted slices' sample order (played back-to-front).
	// The flip is baked into the tile's region buffer (getRegionBuffer), so stretch
	// builds and export hear it too. Same toggle-all semantics as Mute.
	const reverseToggle = document.createElement('button');
	reverseToggle.className = 'toggle-btn';
	reverseToggle.textContent = 'Rev';
	reverseToggle.title = 'Reverse the targeted slice(s) — plays the audio back-to-front';
	reverseToggle.setAttribute('aria-pressed', 'false');
	reverseToggle.addEventListener('click', () => {
		const clips = targetClips();
		if (!clips.length) return;
		const on = !clips.every((t) => t.reversed);
		clips.forEach((t) => { t.reversed = on; });
		schedulePrescan();       // reversed stretch variants may need building
		renderSequencer();       // repaint the (mirrored) waveforms + refresh controls
		scheduleSave();
	});
	// Fade in/out: per-slice envelope, stored as a fraction of the slice's length
	// (0 = off, 1 = the whole slice) so it survives resize and tempo changes.
	// Fades are gain automation on the scheduled voice — no re-render or stretch
	// rebuild needed, just repaint the tile's envelope guides.
	const fadeFromTile  = (v) => Math.round(Math.max(0, Math.min(1, v || 0)) * 100);
	const makeFadeControl = (label, prop) => new DragControl({
		label, min: 0, max: 100, step: 1, value: 0,
		format: (v) => (v > 0 ? `${Math.round(v)}%` : 'off'),
		onChange: (v) => {
			const clips = targetClips();
			if (!clips.length) return;
			const frac = Math.max(0, Math.min(100, v)) / 100;
			clips.forEach((t) => { t[prop] = frac; });
			redrawTileWaves();
			scheduleSave();
		},
	});
	const fadeInControl  = makeFadeControl('F.In',  'fadeIn');
	const fadeOutControl = makeFadeControl('F.Out', 'fadeOut');

	// Duplicate the selected slice into the adjacent cells (◀ before / ▶ after),
	// overwriting what's under. duplicateSlice() lives with the tile-edit helpers.
	const dupBeforeBtn = document.createElement('button');
	dupBeforeBtn.textContent = 'Dup ◀';
	dupBeforeBtn.title = 'Duplicate the selected slice before it (overwrites what’s under the copy)';
	dupBeforeBtn.addEventListener('click', () => duplicateSlice(-1));
	const dupAfterBtn = document.createElement('button');
	dupAfterBtn.textContent = 'Dup ▶';
	dupAfterBtn.title = 'Duplicate the selected slice after it (overwrites what’s under the copy)';
	dupAfterBtn.addEventListener('click', () => duplicateSlice(1));
	// Refill: fill the selected empty space, or reset a moved/duplicated slice, with the
	// source audio native to its position (src = its grid position). Works on a gap OR a
	// clip; enabled only when there's something to do (a gap, or a clip whose src has
	// drifted from its position).
	const refillBtn = document.createElement('button');
	refillBtn.textContent = 'Refill';
	refillBtn.title = 'Fill the selected empty space — or reset the selected slice — to the source audio native to its position. With All: restore every slice (locked ones stay put)';
	refillBtn.addEventListener('click', () => refillSelected());

	// Start position (in units) of an entry within the bar, or -1 if not found.
	const entryStartUnit = (entry) => {
		let s = 0;
		for (const t of seq.tiles) { if (t === entry) return s; s += t.w; }
		return -1;
	};

	// A pristine entry is a clip sitting at its native source position with default
	// settings — nothing for Refill to restore. (Lock is deliberately ignored: it's
	// a protection flag, not audio state.)
	const isPristine = (t, start) => !t.gap
		&& Math.abs((t.src || 0) - start) <= 1e-6
		&& !t.offset && !t.muted && !t.reversed && !(t.fadeIn > 0) && !(t.fadeOut > 0);

	// Reflect the current target (selection, or every slice in All mode) into the
	// slice-settings controls. In All mode the toggles read as "on when ALL slices
	// are on", and the fade controls display the selected slice if any, else the
	// first clip.
	const updateSliceSettings = () => {
		const sel   = seq.tiles.includes(selectedTile) ? selectedTile : null;
		const clip  = !!(sel && !sel.gap);
		const clips = seq.tiles.filter((t) => !t.gap);
		const start = sel ? entryStartUnit(sel) : -1;
		const act   = allSlices ? clips.length > 0 : clip;
		// Refill: enabled when any target has something to restore. All mode skips
		// locked slices (they're protected — see refillTargets), so they don't count.
		let canRefill;
		if (allSlices) {
			let s = 0;
			canRefill = seq.tiles.some((t) => {
				const p = t.locked || isPristine(t, s);
				s += t.w;
				return !p;
			});
		} else {
			canRefill = !!sel && !isPristine(sel, start);
		}
		const muteOn = allSlices ? (clips.length > 0 && clips.every((t) => t.muted))    : (clip && !!sel.muted);
		const revOn  = allSlices ? (clips.length > 0 && clips.every((t) => t.reversed)) : (clip && !!sel.reversed);
		muteToggle.disabled = !act;
		muteToggle.classList.toggle('active', muteOn);
		muteToggle.setAttribute('aria-pressed', String(muteOn));
		lockToggle.disabled = !clip;
		lockToggle.classList.toggle('active', clip && !!sel.locked);
		lockToggle.setAttribute('aria-pressed', String(clip && !!sel.locked));
		reverseToggle.disabled = !act;
		reverseToggle.classList.toggle('active', revOn);
		reverseToggle.setAttribute('aria-pressed', String(revOn));
		const fadeSrc = clip ? sel : (allSlices ? clips[0] : null);
		fadeInControl.setDisabled(!act);
		fadeOutControl.setDisabled(!act);
		fadeInControl.setValue(fadeSrc ? fadeFromTile(fadeSrc.fadeIn) : 0);
		fadeOutControl.setValue(fadeSrc ? fadeFromTile(fadeSrc.fadeOut) : 0);
		dupBeforeBtn.disabled = !clip;
		dupAfterBtn.disabled  = !clip;
		refillBtn.disabled    = !canRefill;
		sliceLabel.classList.toggle('disabled', !sel && !allSlices);
	};

	// Replace an entry (gap or clip) with the default slice for its position:
	// src = its start unit, so it reads the source region that naturally sits there.
	const refillEntry = (idx, start) => {
		const fresh = { src: start, w: seq.tiles[idx].w, offset: 0, muted: false, colorIdx: Math.round(start) };
		seq.tiles[idx] = fresh;
		return fresh;
	};
	const refillSelected = () => {
		if (allSlices) {
			// Restore every entry to its native slice — except locked ones, which
			// stay protected exactly as they are.
			let s = 0;
			for (let i = 0; i < seq.tiles.length; i++) {
				const w = seq.tiles[i].w;
				if (!seq.tiles[i].locked) refillEntry(i, s);
				s += w;
			}
			selectedTile = null;      // old selection object was replaced
		} else {
			const idx = seq.tiles.indexOf(selectedTile);
			if (idx < 0) return;
			const start = entryStartUnit(selectedTile);
			if (start < 0) return;
			selectedTile = refillEntry(idx, start);   // keep the refilled entry selected
		}
		renderSequencer();
		scheduleSave();
	};

	// Per-item grid footprint (columns) for the sequencer toolbar. Transport is
	// master-only, so the per-slicer Play/Stop are not shown here.
	seqToolbar.appendChild(setSpan(seqLabel, 1));
	seqToolbar.appendChild(setSpan(seqLoopBtn, 1));
	seqToolbar.appendChild(setSpan(seqClearBtn, 1));
	seqToolbar.appendChild(setSpan(randomizeBtn, 1));
	seqToolbar.appendChild(setSpan(amountControl.getElement(), 2));  // Amount
	// Per-track BPM is hidden: the master clock governs tempo (bpmInput/bpmResetBtn
	// stay in memory so setMidiBpm's value/lock writes are harmless).
	seqToolbar.appendChild(setSpan(stepControl.getElement(), 1));    // Step
	seqToolbar.appendChild(setSpan(sliceLabel, 1));                  // Slice settings ↓
	seqToolbar.appendChild(setSpan(allToggle, 1));
	seqToolbar.appendChild(setSpan(muteToggle, 1));
	seqToolbar.appendChild(setSpan(lockToggle, 1));
	seqToolbar.appendChild(setSpan(reverseToggle, 1));
	seqToolbar.appendChild(setSpan(fadeInControl.getElement(), 2));   // Fade in
	seqToolbar.appendChild(setSpan(fadeOutControl.getElement(), 2));  // Fade out
	seqToolbar.appendChild(setSpan(dupBeforeBtn, 1));
	seqToolbar.appendChild(setSpan(dupAfterBtn, 1));
	seqToolbar.appendChild(setSpan(refillBtn, 1));
	seqToolbar.appendChild(setSpan(seqStatus, 'full'));

	const seqStepsRow = document.createElement('div');
	seqStepsRow.className = 'seq-steps';

	seqEl.appendChild(seqToolbar);
	seqEl.appendChild(seqStepsRow);
	container.appendChild(seqEl);

	// Sequencer state — an ordered list of variable-width tiles.
	//   tile = { src, w, offset, muted, colorIdx, reversed, fadeIn, fadeOut }
	//     src      : start unit (0..U-1) this tile reads source from
	//     w        : width in units (also its playback duration = w steps)
	//     offset   : per-tile pitch offset (semitones), stacks on master
	//     muted    : silent slot (keeps its timing)
	//     colorIdx : stable palette index — does NOT change on resize, so a slice's
	//                colour stays put while you drag its length.
	//     reversed : play this slice's audio back-to-front
	//     fadeIn / fadeOut : envelope, as 0..1 fractions of the slice's length
	//                (0 = off, 1 = the whole slice); linear today (fadeCurve is the
	//                future hook — see envelope.js).
	// Resizing conserves Σ w (a drag deducts from the play-order neighbour), so the
	// overall sequence length is fixed once set.
	const seq = {
		tiles:        [],
		isPlaying:    false,
		currentTile:  -1,
		loop:         false,
	};

	// Monotonic source of stable per-tile colour ids.
	let nextColorIdx = 0;

	// Beats in the loop (musical length) and the derived sequencer grid.
	const beatCount = () => parseInt(beatsSelect.value, 10) || 4;
	// Grid cells = one step each. A 1/D-note step is 4/D quarter-notes, so a loop of
	// `beats` quarters holds beats·D/4 steps. Integer for every D≥4 (quarter or finer);
	// only 1/2-note steps on an odd beat count round (rare — deferred edge case).
	const cellCount = () => Math.max(1, Math.round(beatCount() * divisionDenom / 4));

	// Grid resolution U. After a slice the engine holds U equal unit-segments, so their
	// count is the source of truth; fall back to the derived cell count while empty.
	const unitCount = () => {
		const n = (slicer.engine.getSegments() || []).length;
		return n > 0 ? n : cellCount();
	};
	const defaultTiles = (n) => {
		nextColorIdx = n;
		return Array.from({ length: n }, (_, i) => ({ src: i, w: 1, offset: 0, muted: false, colorIdx: i }));
	};
	const tileColor    = (t) => PALETTE[t.colorIdx % PALETTE.length];

	// Edge-resize snaps to 1/SUBSTEP of a unit (sub-step precision), so tile widths
	// and src positions can be fractional. SUBSTEP is a power of two, keeping these
	// values exact in binary floating point (no drift). MIN_W is the smallest a
	// dragged tile may shrink to; donors still absorb all the way to 0.
	const SUBSTEP = 4;
	const MIN_W   = 1 / SUBSTEP;
	const snapU   = (u) => Math.round(u * SUBSTEP) / SUBSTEP;
	// Tidy display of a (possibly fractional) unit count: "2", "1.25", "1.5".
	const fmtW    = (w) => String(snapU(w));

	// --- Transport tempo state ---
	// The sliced selection is treated as one bar (4 beats). On (re)slice we
	// auto-derive BPM and step division so a freshly loaded loop reconstructs
	// itself, unless the user has manually overridden either control.
	let bpm           = 120;
	let originalBPM   = 120;            // last auto-derived (natural) tempo of the selection
	let divisionDenom = 16;            // Step subdivision (1/divisionDenom note)
	let bpmManual     = false;
	let masterPitch   = 0;             // semitones; per-step offsets stack on top

	// When an external MIDI clock is driving the tempo this holds its derived BPM
	// (else null). It overrides the local `bpm` for scheduling without touching the
	// stored value, so the slicer's own tempo returns intact when sync is released.
	let midiBpm       = null;

	const stepSec = () => (60 / (midiBpm != null ? midiBpm : bpm)) * (4 / divisionDenom);

	// Called by the global MIDI clock controller. A number locks the displayed BPM to
	// the incoming clock (input goes read-only); null releases it back to local tempo.
	const setMidiBpm = (v) => {
		if (v != null && isFinite(v) && v > 0) {
			midiBpm = v;
			bpmInput.value    = v.toFixed(2);
			bpmInput.disabled = true;
		} else {
			midiBpm = null;
			bpmInput.value    = bpm.toFixed(2);
			bpmInput.disabled = false;
		}
		schedulePrescan();
	};

	// --- Time-stretch / pitch cache ---
	// Pre-rendered, pitch-preserving stretched buffers keyed by slice index + a
	// quantized stretch factor (~2% — sub-perceptual, keeps the cache tiny during a
	// BPM sweep). Cleared whenever the engine re-slices (segments[] are replaced, so
	// a positional key would otherwise return stale audio). LRU-capped.
	const STRETCH_CACHE_MAX = 128;
	const stretchCache = new Map();          // key -> AudioBuffer
	const stretchPending = new Set();        // keys with an async build in flight
	let   sliceGen = 0;                       // bumped on re-slice; aborts stale chunked builds
	const clampPitch = (s) => Math.max(-12, Math.min(12, s));
	const quantStretch = (f) => Math.round(f * 50) / 50;

	const cacheGet = (key) => {
		if (!stretchCache.has(key)) return undefined;
		const buf = stretchCache.get(key);   // refresh LRU recency
		stretchCache.delete(key);
		stretchCache.set(key, buf);
		return buf;
	};
	const cacheSet = (key, buf) => {
		stretchCache.set(key, buf);
		while (stretchCache.size > STRETCH_CACHE_MAX) {
			stretchCache.delete(stretchCache.keys().next().value);
		}
	};

	// --- Region buffers ---
	// A tile reads `w` units of source starting at unit `src`, copied straight from
	// the original buffer over the current selection (advances roadmap Tier 1c —
	// no reliance on the equal-segment copies). Unit positions clamp to [0, U] so a
	// tile that overhangs the selection (possible after reorder+resize) reads only
	// what's inside it. `reversed` bakes a sample-order flip into the copy (a
	// backwards voice can't be expressed via playbackRate). Cached by `src:w[:r]`,
	// cleared on re-slice.
	const regionCache = new Map();
	const getRegionBuffer = (src, w, reversed = false) => {
		const buf = slicer.engine.audioBuffer;
		if (!buf) return null;
		const U   = unitCount();
		const key = `${src}:${w}${reversed ? ':r' : ''}`;
		const hit = regionCache.get(key);
		if (hit) return hit;

		const span = selEndFrac - selStartFrac;
		if (!(span > 0) || U <= 0) return null;
		const u0   = Math.max(0, Math.min(U, src));
		const u1   = Math.max(0, Math.min(U, src + w));
		if (u1 <= u0) return null;
		const startSample = Math.floor((selStartFrac + (u0 / U) * span) * buf.length);
		const endSample   = Math.floor((selStartFrac + (u1 / U) * span) * buf.length);
		const len         = endSample - startSample;
		if (len <= 0) return null;

		const out = slicer.engine.audioContext.createBuffer(buf.numberOfChannels, len, buf.sampleRate);
		for (let c = 0; c < buf.numberOfChannels; c++) {
			const dst = out.getChannelData(c);
			const srcData = buf.getChannelData(c);
			if (reversed) {
				for (let j = 0; j < len; j++) dst[j] = srcData[endSample - 1 - j];
			} else {
				for (let j = 0; j < len; j++) dst[j] = srcData[startSample + j];
			}
		}
		regionCache.set(key, out);
		return out;
	};

	// Build a stretched buffer incrementally across event-loop ticks, reporting
	// sample-accurate progress to the overlay. Aborts if the slice generation
	// changes mid-build (the region/cache it targets went stale).
	const runStretchBuild = (src, w, factor, key, reversed) => {
		const region = getRegionBuffer(src, w, reversed);
		if (!region) { stretchPending.delete(key); return; }
		const channels = [];
		for (let c = 0; c < region.numberOfChannels; c++) channels.push(region.getChannelData(c));

		const stretcher = createTimeStretcher(channels, factor);
		const gen   = sliceGen;
		// ~24 visual steps per job (floor keeps tiny jobs from over-ticking).
		const CHUNK = Math.max(2048, Math.ceil(stretcher.outputLen / 24));
		slicerTasks.begin('Time-stretching (pitch-correct)', stretcher.outputLen);

		const step = () => {
			if (gen !== sliceGen) {              // re-sliced — abandon this build
				stretchPending.delete(key);
				slicerTasks.end();
				return;
			}
			let advanced = 0;
			try { advanced = stretcher.process(CHUNK); }
			catch (err) {
				console.warn('time-stretch failed:', err);
				stretchPending.delete(key);
				slicerTasks.end();
				return;
			}
			slicerTasks.advance(advanced);
			if (!stretcher.done) { setTimeout(step, 0); return; }

			try {
				const stretched = stretcher.result();
				const out = slicer.engine.audioContext.createBuffer(
					stretched.length, stretched[0].length, region.sampleRate);
				for (let c = 0; c < stretched.length; c++) out.copyToChannel(stretched[c], c);
				if (gen === sliceGen) cacheSet(key, out);
			} catch (err) {
				console.warn('time-stretch finalize failed:', err);
			} finally {
				stretchPending.delete(key);
				slicerTasks.end();
			}
		};
		setTimeout(step, 0);
	};

	// Return a cached stretched buffer, or null (and kick a chunked build off the
	// scheduler tick) on a miss. Reversed tiles stretch the reversed region, so
	// they get their own cache entries.
	const getStretched = (src, w, factor, reversed = false) => {
		const key = `${src}:${w}:${factor}${reversed ? ':r' : ''}`;
		const hit = cacheGet(key);
		if (hit) return hit;
		if (!stretchPending.has(key)) {
			stretchPending.add(key);
			runStretchBuild(src, w, factor, key, reversed);
		}
		return null;
	};

	// Transport policy: resolve a tile to a ready-to-play buffer + rate.
	// A width-w tile occupies `playDur = w * stepSec` and reads w units of source
	// (`naturalDur = w * unitDur`). At the natural BPM stepSec == unitDur, so the
	// region fills its slot raw (no stretch). The stretch factor is global
	// (stepSec/unitDur × pitch) and only departs from 1 when BPM/pitch deviate.
	const getTilePlayback = (tile, stepSecVal) => {
		if (!tile) return null;
		const playDur = (tile.w > 0 ? tile.w : 1) * stepSecVal;
		if (tile.gap || tile.muted) {
			// A gap (leave-gaps mode) is a silent slot that still occupies its time.
			return { buffer: null, playbackRate: 1, dur: playDur, fill: false };
		}
		const buf = slicer.engine.audioBuffer;
		const U   = unitCount();
		if (!buf || U <= 0) return { buffer: null, playbackRate: 1, dur: playDur, fill: false };

		// Fade descriptor: the tile stores fades as 0..1 fractions of its own length,
		// converted here to seconds of the slot it occupies (so they track BPM and
		// resize). Applied per voice at schedule time (envelope.js), never baked into
		// buffers — the caches stay envelope-agnostic.
		const rev = !!tile.reversed;
		const env = ((tile.fadeIn > 0) || (tile.fadeOut > 0)) ? {
			fadeInSec:  Math.max(0, Math.min(1, tile.fadeIn  || 0)) * playDur,
			fadeOutSec: Math.max(0, Math.min(1, tile.fadeOut || 0)) * playDur,
			curve: tile.fadeCurve || 'linear',
		} : null;

		const unitDur     = ((selEndFrac - selStartFrac) * buf.duration) / U;
		const naturalDur  = tile.w * unitDur;
		const fillStretch = naturalDur > 0 ? playDur / naturalDur : 1;   // = stepSec/unitDur
		const eff         = clampPitch(masterPitch + (tile.offset || 0));
		const P           = Math.pow(2, eff / 12);
		const rawFactor   = fillStretch * P;

		// No stretch and no pitch shift → play the raw region (natural tempo).
		if (Math.abs(rawFactor - 1) < 0.01 && Math.abs(P - 1) < 1e-6) {
			return { buffer: getRegionBuffer(tile.src, tile.w, rev), playbackRate: 1, dur: playDur, fill: false, env };
		}

		const buffer = getStretched(tile.src, tile.w, quantStretch(rawFactor), rev);
		if (buffer) {
			return { buffer, playbackRate: P, dur: playDur, fill: true, env };
		}
		// Cache miss: repitch-fill the raw region for this one pass (rate fills the
		// slot), snaps to pitch-preserving once the async build lands.
		return { buffer: getRegionBuffer(tile.src, tile.w, rev), playbackRate: fillStretch > 0 ? 1 / fillStretch : 1, dur: playDur, fill: false, env };
	};

	// Eagerly enumerate every tile's stretch need at the current tempo/pitch so all
	// builds are enqueued up front — the overlay's total is then known immediately
	// and the bar fills monotonically 0→100% instead of bouncing as the scheduler
	// discovers misses one tile at a time. Reuses getTilePlayback for the exact
	// factor math (the side effect is the enqueue; the return value is ignored).
	// Debounced so a knob drag / BPM type doesn't enqueue intermediate factors.
	let prescanTimer = null;
	const prescanStretches = () => {
		const ss = stepSec();
		for (const tile of seq.tiles) getTilePlayback(tile, ss);
	};
	const schedulePrescan = () => {
		clearTimeout(prescanTimer);
		prescanTimer = setTimeout(prescanStretches, 120);
	};

	// Re-slicing replaces segments[]; positional cache keys would go stale.
	// When the slice COUNT changes (new file, or subdivisions changed) we also
	// prepopulate the sequence with all slices in order — a useful default. Pure
	// re-slices that keep the same count (slider drags, cut) leave the sequence
	// alone. Restore is skipped (the saved sequence wins).
	// Re-slicing changes the selection and/or unit resolution, so the cached region
	// + stretched buffers go stale. When the resolution (U) changes we also reset to
	// the default one-unit-per-tile layout (keeps Σ w === U). Pure re-slices that keep
	// the same U (slider drags, cut) leave the tiles alone — their unit indices still
	// address the new selection. Restore is skipped (the saved tiles win).
	let lastSegCount = 0;
	slicer.engine.addEventListener('segmentsliced', () => {
		sliceGen++;                 // abort any in-flight chunked stretch builds
		stretchCache.clear();
		stretchPending.clear();
		regionCache.clear();
		const n = (slicer.engine.getSegments() || []).length;
		if (n !== lastSegCount) {
			lastSegCount = n;
			if (restoreCount === 0 && n > 0) {
				seq.tiles = defaultTiles(n);
				renderSequencer();
			}
		}
	});

	const updateBpmResetBtn = () => {
		const show = bpmManual && isFinite(originalBPM) && originalBPM > 0;
		bpmResetBtn.hidden = !show;
		if (show) bpmResetBtn.textContent = `↺ ${originalBPM.toFixed(2)}`;
	};

	const deriveTransport = (selStart, selEnd) => {
		const buf = slicer.engine.audioBuffer;
		if (!buf) return;
		const selDur = (selEnd - selStart) * buf.duration;
		if (selDur <= 0) return;
		originalBPM = 60 * beatCount() / selDur;   // the loop is `beats` quarter-notes long
		if (!bpmManual) {
			bpm = originalBPM;
			bpmInput.value = bpm.toFixed(2);
		}
		// Step is independent (its own control) — no longer auto-set from the grid.
		updateBpmResetBtn();

		// Master sync: the first loaded clip sets the master tempo (until the user
		// overrides it); thereafter every clip locks to the master so all loops stay
		// bar-aligned. (No-op while an external MIDI clock owns the tempo.)
		if (masterDriving()) {
			if (masterClock.bpm == null && !masterClock.userSet) setMasterBpm(originalBPM, false);
			if (masterClock.bpm != null) setMidiBpm(masterClock.bpm);
		}
	};

	bpmInput.addEventListener('input', () => {
		const v = parseFloat(bpmInput.value);
		if (!isNaN(v) && v > 0) {
			bpm = v;
			bpmManual = true;
			updateBpmResetBtn();
			schedulePrescan();
			scheduleSave();
		}
	});
	bpmResetBtn.addEventListener('click', () => {
		bpm       = originalBPM;
		bpmManual = false;
		bpmInput.value = bpm.toFixed(2);
		updateBpmResetBtn();
		schedulePrescan();
		scheduleSave();
	});
	// Step change → new subdivision means a new cell count (beats × step), so re-slice
	// the selection into that grid. Tempo is unchanged (it comes from beats). User-
	// marked slices (locked or muted) survive the change: audio + loop position are
	// rescaled onto the new grid (see preserveMarkedTilesFromPrevGrid) so a mark keeps
	// guarding the same content — contiguous unit-marks rejoin on downsize; a wide
	// mark stays wide on upsize (the user can Split it further if they want finer
	// control).
	divisionSelect.addEventListener('change', () => {
		const prevTiles = seq.tiles.slice();
		divisionDenom = parseInt(divisionSelect.value, 10);
		const start = clamp01(parseFloat(startInput.value));
		const end   = clamp01(parseFloat(endInput.value));
		applyRange(start, end);
		// applyRange → segmentsliced → seq.tiles reset to defaultTiles(U_new).
		// Reapply marks from the previous grid on top of those defaults; re-render
		// only when something actually got re-marked (nothing to preserve → no
		// second paint).
		if (preserveMarkedTilesFromPrevGrid(prevTiles)) renderSequencer();
		schedulePrescan();
		scheduleSave();
	});

	// Reorder is a pointer-driven HORIZONTAL drag (not native HTML5 DnD, which would
	// let the tile ghost float vertically). State lives outside renderSequencer so it
	// survives re-renders. dragFromIdx is the grabbed tile.
	let dragFromIdx = null;
	// True while an edge-resize pointer drag is active — suppresses a reorder drag so
	// the two gestures never collide.
	let resizing    = false;
	// The currently-selected tile (by object, so it survives reorder/resize). Clicking
	// a tile toggles selection; a selected tile shows its length handles and drives the
	// Slice settings (Mute). Transient UI — not persisted.
	let selectedTile = null;

	const updatePitchBadge = (badge, offset) => {
		if (offset) {
			badge.textContent   = (offset > 0 ? '+' : '') + offset;
			badge.style.display = '';
		} else {
			badge.textContent   = '';
			badge.style.display = 'none';
		}
	};

	// The transport's visual loop owns the `.playing` highlight (renderSequencer no
	// longer paints it). Toggling directly avoids a full DOM rebuild every step.
	let playingTileEl = null;
	const setPlayingTile = (idx) => {
		const child  = (idx >= 0 && idx < seqStepsRow.children.length) ? seqStepsRow.children[idx] : null;
		const target = (child && child.classList.contains('seq-tile')) ? child : null;
		if (target === playingTileEl) return;
		if (playingTileEl) playingTileEl.classList.remove('playing');
		if (target) target.classList.add('playing');
		playingTileEl = target;
	};

	const clearDropMarkers = () => {
		seqStepsRow.querySelectorAll('.drop-before, .drop-after')
			.forEach((el) => el.classList.remove('drop-before', 'drop-after'));
	};

	// Build a translucent floating copy of a tile to follow the pointer during a
	// reorder drag. cloneNode doesn't carry a canvas's pixels, so the waveform bitmap
	// is blitted over manually. Pinned (position:fixed) to the tile's current spot;
	// the drag only shifts it on X via transform, so it never moves vertically.
	const makeDragGhost = (srcTile) => {
		const rect = srcTile.getBoundingClientRect();
		const g = srcTile.cloneNode(true);
		g.classList.remove('dragging', 'playing', 'drop-before', 'drop-after', 'selected');
		g.classList.add('seq-tile-ghost');
		g.style.left   = `${rect.left}px`;
		g.style.top    = `${rect.top}px`;
		g.style.width  = `${rect.width}px`;
		g.style.height = `${rect.height}px`;
		const oc = srcTile.querySelector('.seq-tile-wave');
		const gc = g.querySelector('.seq-tile-wave');
		if (oc && gc && oc.width && oc.height) {
			gc.width = oc.width; gc.height = oc.height;
			try { gc.getContext('2d').drawImage(oc, 0, 0); } catch (_) { /* tainted/empty */ }
		}
		return g;
	};

	// --- Tile edits ---
	// Each tile owns an independent source window [src, src+w) into the cut. Edits
	// keep every tile within the cut (0 ≤ src, src+w ≤ U) and w ≥ 1. The loop length
	// is Σ w steps and varies as slices grow/shrink (no neighbour compensation).

	// (Packed reorder is applied live during the drag — see the reorder pointer
	// handlers below, which shuffle seq.tiles + the DOM as the pointer moves.)

	// --- Leave-gaps mode: unit-grid overwrite operations -------------------------
	// The bar is U units; edits rasterize it at SUBSTEP resolution (widths are exact
	// multiples of 1/SUBSTEP), mutate the cell grid, then rebuild the ordered
	// clip/gap list. Each cell holds { clip, srcSub } — the source sub-unit that cell
	// reads — so trimming or splitting a clip keeps each remnant's source anchored;
	// null = a gap (silence).
	const RES     = SUBSTEP;
	const gridLen = () => Math.round((unitCount() || 1) * RES);

	const rasterizeBar = (tiles) => {
		const n = gridLen();
		const cells = new Array(n).fill(null);
		let pos = 0;
		for (const t of tiles) {
			const wc = Math.round((t.w > 0 ? t.w : 0) * RES);
			if (!t.gap) {
				const base = Math.round((t.src || 0) * RES);
				for (let k = 0; k < wc && pos + k < n; k++) cells[pos + k] = { clip: t, srcSub: base + k };
			}
			pos += wc;
		}
		return cells;
	};

	// Rebuild an ordered clip/gap list from the cell grid. A run of the same clip with
	// contiguous source becomes one clip entry; a run of null becomes a gap. Split
	// remnants share the clip's colour/offset/mute (a cut clip keeps its identity).
	const rebuildFromCells = (cells) => {
		const out = [];
		let k = 0;
		while (k < cells.length) {
			const c = cells[k];
			if (!c) {
				let j = k; while (j < cells.length && !cells[j]) j++;
				// src:0 is a harmless dummy so pack-mode width arithmetic (which reads
				// .src) never sees undefined if the row still holds gaps after a switch.
				out.push({ gap: true, w: (j - k) / RES, src: 0 });
				k = j;
			} else {
				const clip = c.clip, base = c.srcSub;
				let j = k;
				while (j < cells.length && cells[j] && cells[j].clip === clip && cells[j].srcSub === base + (j - k)) j++;
				out.push({ src: base / RES, w: (j - k) / RES, offset: clip.offset || 0, muted: !!clip.muted, colorIdx: clip.colorIdx, locked: !!clip.locked, reversed: !!clip.reversed, fadeIn: clip.fadeIn || 0, fadeOut: clip.fadeOut || 0 });
				k = j;
			}
		}
		return out;
	};

	// Paint clip over [startCell, startCell+wCells), reading source from srcBaseSub —
	// overwriting whatever those cells held, EXCEPT cells belonging to a locked clip
	// (they're preserved, so a paint flows around locked slices instead of erasing them).
	const paintCells = (cells, clip, startCell, wCells, srcBaseSub) => {
		for (let k = 0; k < wCells; k++) {
			const idx = startCell + k;
			if (idx < 0 || idx >= cells.length) continue;
			if (cells[idx] && cells[idx].clip && cells[idx].clip.locked) continue;   // don't overwrite locked
			cells[idx] = { clip, srcSub: srcBaseSub + k };
		}
	};

	// True if [startCell, startCell+wCells) touches a cell owned by a LOCKED clip other
	// than `exceptClip` — used to block moves/dups that would overwrite a locked slice.
	const rangeHasLocked = (cells, startCell, wCells, exceptClip) => {
		for (let k = 0; k < wCells; k++) {
			const c = cells[startCell + k];
			if (c && c.clip && c.clip.locked && c.clip !== exceptClip) return true;
		}
		return false;
	};

	// Re-apply user-marked slices (locked or muted) from a pre-resolution-change tile
	// snapshot onto the fresh default grid in `seq.tiles`. Each marked run is rescaled
	// by r = U_new / U_old so its audio + loop position stay put; contiguous runs that
	// share contiguous source AND the same flags (locked, muted) are coalesced first
	// so a run of unit-marks at the old resolution can re-emerge as one wider mark at
	// the new one (and vice versa — splitting on upsize is just what rebuildFromCells
	// does with a wider paint). Locks and mutes coalesce independently, so a run of
	// muted tiles doesn't merge with a neighbouring locked one. Marks that shrink
	// below the sub-step grain, or would overhang the bar after rescale, are dropped
	// rather than truncated. On downsize collisions the earlier run wins.
	const isMarked   = (t) => !!t && !t.gap && (t.locked || t.muted);
	// Coalescing also requires matching reverse/fade settings — merging two runs
	// that differ in those would smear one tile's envelope/direction over both.
	const sameMarks  = (a, b) => !!a.locked === !!b.locked && !!a.muted === !!b.muted
		&& !!a.reversed === !!b.reversed
		&& (a.fadeIn  || 0) === (b.fadeIn  || 0)
		&& (a.fadeOut || 0) === (b.fadeOut || 0);
	const preserveMarkedTilesFromPrevGrid = (oldTiles) => {
		const U_new = unitCount();
		if (!(U_new > 0) || !oldTiles || oldTiles.length === 0) return false;
		const U_old = oldTiles.reduce((s, t) => s + (t.w || 0), 0);
		if (!(U_old > 0)) return false;

		// Collect marked runs from the previous grid, coalescing adjacent marks that
		// share contiguous source AND identical mark flags (so mutes don't merge into
		// locks, and a downsize can rejoin same-flag unit-marks).
		const runs = [];
		let posU = 0;
		let cur = null;
		for (const t of oldTiles) {
			if (isMarked(t)) {
				const src = t.src || 0;
				if (cur
					&& sameMarks(cur, t)
					&& Math.abs((cur.startU + cur.w) - posU) < 1e-9
					&& Math.abs((cur.src + cur.w) - src)      < 1e-9) {
					cur.w += t.w;
				} else {
					cur = {
						startU:   posU,
						w:        t.w,
						src,
						offset:   t.offset || 0,
						locked:   !!t.locked,
						muted:    !!t.muted,
						colorIdx: t.colorIdx,
						reversed: !!t.reversed,
						fadeIn:   t.fadeIn  || 0,
						fadeOut:  t.fadeOut || 0,
					};
					runs.push(cur);
				}
			} else {
				cur = null;    // any unmarked (or gap) tile breaks the chain
			}
			posU += t.w || 0;
		}
		if (runs.length === 0) return false;

		const r     = U_new / U_old;
		const cells = rasterizeBar(seq.tiles);   // the fresh defaults from segmentsliced
		let painted = false;
		for (const run of runs) {
			const startCell = Math.round(run.startU * r * RES);
			const wCells    = Math.round(run.w      * r * RES);
			const srcCell   = Math.round(run.src    * r * RES);
			if (wCells < 1) continue;                                   // sub-cell → too small to represent
			if (startCell < 0 || startCell + wCells > cells.length) continue;   // out of bar

			// Skip if these cells already carry a mark placed by an earlier run
			// (first-wins on any downsize collision — regardless of which flag).
			let conflict = false;
			for (let k = 0; k < wCells; k++) {
				const c = cells[startCell + k];
				if (c && c.clip && (c.clip.locked || c.clip.muted)) { conflict = true; break; }
			}
			if (conflict) continue;

			const clip = {
				src:      srcCell / RES,
				w:        wCells  / RES,
				offset:   run.offset,
				colorIdx: run.colorIdx,
				locked:   run.locked,
				muted:    run.muted,
				reversed: run.reversed,
				fadeIn:   run.fadeIn,
				fadeOut:  run.fadeOut,
			};
			for (let k = 0; k < wCells; k++) {
				cells[startCell + k] = { clip, srcSub: srcCell + k };
			}
			painted = true;
		}
		if (!painted) return false;
		seq.tiles = rebuildFromCells(cells);
		return true;
	};

	// Find the (non-gap) tile whose left edge sits at grid cell `cell` (RES units).
	const tileStartingAtCell = (cell) => {
		let cum = 0;
		for (const t of seq.tiles) {
			if (cum === cell && !t.gap) return t;
			cum += Math.round(t.w * RES);
		}
		return null;
	};

	// Duplicate the selected slice into the adjacent cells — dir -1 = before, +1 =
	// after — overwriting whatever is under the copy (Σ w stays U; works in both
	// modes since it's an explicit overwrite, not a drag). The copy reads the same
	// source and keeps the slice's colour, so it reads as a true clone; near a bar
	// edge it truncates to the room available. The new copy becomes the selection, so
	// repeated clicks stamp copies along the bar.
	const duplicateSlice = (dir) => {
		const sel = seq.tiles.includes(selectedTile) ? selectedTile : null;
		if (!sel || sel.gap) return;
		const cells = rasterizeBar(seq.tiles);
		let selStart = -1, selCount = 0;
		for (let k = 0; k < cells.length; k++) if (cells[k] && cells[k].clip === sel) { if (selStart < 0) selStart = k; selCount++; }
		if (selStart < 0 || selCount <= 0) return;
		let start = dir > 0 ? selStart + selCount : selStart - selCount;
		let w = selCount;
		if (start < 0) { w += start; start = 0; }                       // clamp/truncate at bar start
		if (start + w > cells.length) w = cells.length - start;         // …and bar end
		if (w <= 0) return;                                             // no room to place a copy
		if (rangeHasLocked(cells, start, w, sel)) return;               // don't overwrite a locked slice
		const copy = { src: sel.src, w: selCount / RES, offset: sel.offset || 0, muted: !!sel.muted, colorIdx: sel.colorIdx, reversed: !!sel.reversed, fadeIn: sel.fadeIn || 0, fadeOut: sel.fadeOut || 0 };
		paintCells(cells, copy, start, w, Math.round((sel.src || 0) * RES));
		seq.tiles = rebuildFromCells(cells);
		selectedTile = tileStartingAtCell(start);                       // keep the copy selected
		renderSequencer();
		scheduleSave();
	};

	// Gaps-mode reorder: the clip leaves a gap where it was and lands (left edge) at
	// unit `targetUnit`, overwriting whatever it covers. Width/source unchanged.
	const moveTileGaps = (fromIdx, targetUnit) => {
		const clip = seq.tiles[fromIdx];
		if (!clip || clip.gap) return;
		const cells  = rasterizeBar(seq.tiles);
		for (let k = 0; k < cells.length; k++) if (cells[k] && cells[k].clip === clip) cells[k] = null;
		const wCells = Math.round(clip.w * RES);
		let start    = Math.round(targetUnit * RES);
		start = Math.max(0, Math.min(start, cells.length - wCells));
		if (rangeHasLocked(cells, start, wCells, clip)) { renderSequencer(); return; }   // blocked by a locked slice → snap back
		paintCells(cells, clip, start, wCells, Math.round((clip.src || 0) * RES));
		seq.tiles = rebuildFromCells(cells);
		renderSequencer();
	};

	// Gaps-mode edge resize (pure — takes a snapshot list, returns a new list). The
	// boundary EATS the data it moves over (same rule as pack mode). END: growing
	// overwrites the cells ahead (the next clip's head goes under the paint);
	// shrinking truncates this clip's tail and leaves a gap. START: growing back
	// overwrites the previous clip's tail cells while this clip's src stays pinned
	// (its head plays earlier, more source revealed at its tail); shrinking eats this
	// clip's own head (src advances, tail kept) and leaves a front gap. Bounded to
	// the bar [0,U] and the source [0,U].
	const computeResizeGaps = (tiles, i, edge, dU) => {
		const clip = tiles[i];
		if (!clip || clip.gap) return tiles;
		const cells = rasterizeBar(tiles);
		const n     = cells.length;
		let first = -1, count = 0;
		const base = Math.round((clip.src || 0) * RES);
		for (let k = 0; k < n; k++) if (cells[k] && cells[k].clip === clip) { if (first < 0) first = k; count++; }
		if (first < 0) return tiles;
		for (let k = 0; k < n; k++) if (cells[k] && cells[k].clip === clip) cells[k] = null;   // lift the clip
		const dCells = Math.round(snapU(dU) * RES);
		const minC   = Math.max(1, Math.round(MIN_W * RES));
		let start = first, wCells = count, srcBase = base;
		if (edge === 'end') {
			wCells = Math.max(minC, count + dCells);   // front pinned
			wCells = Math.min(wCells, n - start, n - srcBase);   // bar + source end
			// Don't grow over a locked slice: stop at the first locked-other cell.
			for (let k = 0; k < wCells; k++) {
				const c = cells[start + k];
				if (c && c.clip && c.clip.locked && c.clip !== clip) { wCells = Math.max(count, k); break; }
			}
		} else {
			const end = first + count;                 // end pinned in the bar
			start = Math.max(0, Math.min(first + dCells, end - minC));
			// Don't grow back over a locked slice: start no earlier than just past the
			// rightmost locked-other cell in the range.
			for (let p = end - 1; p >= start; p--) {
				const c = cells[p];
				if (c && c.clip && c.clip.locked && c.clip !== clip) { start = p + 1; break; }
			}
			// Grow (start ≤ first): src pinned — data left-aligned, tail reveals.
			// Shrink (start > first): the boundary ate the clip's head — src advances.
			srcBase = base + Math.max(0, start - first);
			wCells = Math.min(end - start, n - start, n - srcBase);
		}
		paintCells(cells, clip, start, Math.round(wCells), srcBase);
		return rebuildFromCells(cells);
	};

	// Merge tile i into its left neighbour (or the right one if it's the first),
	// combining widths — the quick way down from 16 unit-tiles to a few wide slices.
	// The kept tile's window stays within the cut (its src may not be contiguous
	// with the absorbed one after reordering, so the combined width is capped).
	const mergeTile = (i) => {
		if (seq.tiles.length <= 1 || i < 0 || i >= seq.tiles.length) return;
		if (seq.tiles[i].gap) return;                 // gaps aren't merged
		const U = unitCount() || 1;
		if (i > 0 && !seq.tiles[i - 1].gap) {
			const a = seq.tiles[i - 1];               // left neighbour extends forward
			a.w = Math.min(a.w + seq.tiles[i].w, U - a.src);
			seq.tiles.splice(i, 1);
		} else if (i === 0 && seq.tiles[1] && !seq.tiles[1].gap) {
			const b = seq.tiles[1];                   // absorb into the right neighbour
			b.src = seq.tiles[0].src;
			b.w   = Math.min(b.w + seq.tiles[0].w, U - b.src);
			seq.tiles.splice(0, 1);
		} else {
			return;                                    // neighbour is a gap → nothing to merge into
		}
		renderSequencer();
	};

	// Split tile i in half on the unit grid (needs w ≥ 2) — the way back up to more
	// slices. Left half keeps the colour; right half gets a fresh one. The envelope
	// splits with it (fade-in stays on the left half, fade-out on the right), so the
	// pair still reads roughly as the original slice's shape.
	const splitTile = (i) => {
		const t = seq.tiles[i];
		if (!t || t.w < 2) return;
		const left = Math.floor(t.w / 2);
		seq.tiles.splice(i, 1,
			{ src: t.src,        w: left,        offset: t.offset, muted: t.muted, colorIdx: t.colorIdx,   reversed: !!t.reversed, fadeIn: t.fadeIn || 0, fadeOut: 0 },
			{ src: t.src + left, w: t.w - left,  offset: t.offset, muted: t.muted, colorIdx: nextColorIdx++, reversed: !!t.reversed, fadeIn: 0, fadeOut: t.fadeOut || 0 });
		renderSequencer();
	};

	// Draw a tile's own slice waveform (the source region [src, src+w) it reads)
	// into its backdrop canvas, in the slice's palette colour. Laid edge-to-edge,
	// the tiles read as one continuous waveform that rearranges with the slices —
	// each tile sits 1:1 over the audio it plays. A reversed tile draws its wave
	// mirrored (what you see is what plays); fades overlay as envelope guide lines.
	const drawTileWave = (canvas, tile, color) => {
		const { src, w } = tile;
		if (!canvas) return;
		const cssW = canvas.clientWidth;
		const cssH = canvas.clientHeight;
		if (cssW <= 0 || cssH <= 0) return;
		const dpr = window.devicePixelRatio || 1;
		canvas.width  = Math.max(1, Math.floor(cssW * dpr));
		canvas.height = Math.max(1, Math.floor(cssH * dpr));
		const ctx = canvas.getContext('2d');
		ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
		ctx.clearRect(0, 0, cssW, cssH);

		const buf  = slicer.engine.audioBuffer;
		const U    = unitCount() || 1;
		const span = selEndFrac - selStartFrac;
		if (!buf || !(span > 0)) return;
		const u0 = Math.max(0, Math.min(U, src));
		const u1 = Math.max(0, Math.min(U, src + w));
		if (u1 <= u0) return;

		const data = buf.getChannelData(0);
		const len  = buf.length;
		let s0 = Math.floor((selStartFrac + (u0 / U) * span) * len);
		let s1 = Math.floor((selStartFrac + (u1 / U) * span) * len);
		s0 = Math.max(0, Math.min(len, s0));
		s1 = Math.max(s0 + 1, Math.min(len, s1));
		const spp = (s1 - s0) / cssW;
		const amp = cssH / 2;
		const mid = cssH / 2;

		ctx.strokeStyle = color;
		ctx.lineWidth   = 1;
		ctx.beginPath();
		for (let px = 0; px < cssW; px++) {
			let a = s0 + Math.floor(px * spp);
			let b = s0 + Math.floor((px + 1) * spp) + 1;
			if (a < s0) a = s0;
			if (b > s1) b = s1;
			if (b <= a) b = a + 1;
			// Reversed: mirror the sample window around the region's centre. min/max
			// over a window is order-independent, so scanning the mirrored range gives
			// exactly the reversed wave.
			if (tile.reversed) {
				const a2 = s0 + s1 - b;
				const b2 = s0 + s1 - a;
				a = Math.max(s0, a2);
				b = Math.min(s1, b2);
				if (b <= a) b = a + 1;
			}
			let min = 1.0, max = -1.0;
			for (let j = a; j < b; j++) {
				const v = data[j];
				if (v < min) min = v;
				if (v > max) max = v;
			}
			if (min > max) { min = 0; max = 0; }
			const x = px + 0.5;
			ctx.moveTo(x, mid + min * amp);
			ctx.lineTo(x, mid + max * amp);
		}
		ctx.stroke();

		// Envelope guides: a line rising over the fade-in span and falling over the
		// fade-out span. Same proportional scale-down playback uses when the two
		// would overlap, so the picture matches what's heard.
		let fi = Math.max(0, Math.min(1, tile.fadeIn  || 0));
		let fo = Math.max(0, Math.min(1, tile.fadeOut || 0));
		if (fi + fo > 1) { const s = 1 / (fi + fo); fi *= s; fo *= s; }
		if (fi > 0 || fo > 0) {
			ctx.strokeStyle = 'rgba(255, 255, 255, 0.7)';
			ctx.lineWidth   = 1;
			ctx.beginPath();
			if (fi > 0) { ctx.moveTo(0.5, cssH - 0.5); ctx.lineTo(fi * cssW, 0.5); }
			if (fo > 0) { ctx.moveTo(cssW - fo * cssW, 0.5); ctx.lineTo(cssW - 0.5, cssH - 0.5); }
			ctx.stroke();
		}
	};

	// Redraw every tile's backdrop. Children may include gaps (no _tile), so read the
	// tile off each element rather than counting positions.
	const redrawTileWaves = () => {
		for (const el of seqStepsRow.children) {
			if (!el.classList.contains('seq-tile')) continue;
			const t = el._tile;
			if (!t) continue;
			// Selected slice's wave is drawn white to pop against its tinted background.
			const color = (t === selectedTile) ? '#ffffff' : tileColor(t);
			drawTileWave(el.querySelector('.seq-tile-wave'), t, color);
		}
	};

	const TILE_TITLE = 'Click: select (show handles) · drag: reorder · drag green/red edges: resize · dbl-click: split · shift-click: merge · scroll: pitch';

	const renderSequencer = () => {
		seqStepsRow.innerHTML = '';
		const U    = unitCount() || 1;
		const last = seq.tiles.length - 1;

		for (let i = 0; i < seq.tiles.length; i++) {
			const tile = seq.tiles[i];
			const el   = document.createElement('div');

			// Leave-gaps mode: a gap is a passive silent slot — it holds its width in the
			// bar but has no waveform, handles, or pointer wiring (you fill it by moving a
			// clip over it or growing a neighbour). Kept as a child so children stay 1:1
			// with seq.tiles for setPlayingTile's index lookup.
			if (tile.gap) {
				el.className       = 'seq-gap';
				el._tile           = tile;
				el.style.flexGrow  = String(tile.w);
				el.style.flexBasis = '0';
				el.title           = 'Silence (gap) — click to select, then Refill to fill it from the source; or drop/grow a clip here to overwrite it';
				if (tile === selectedTile) el.classList.add('selected');
				el.addEventListener('click', () => {
					selectedTile = (selectedTile === tile) ? null : tile;
					renderSequencer();
				});
				seqStepsRow.appendChild(el);
				continue;
			}

			el.className        = 'seq-tile';
			el._tile            = tile;             // back-ref (children may include gaps)
			el.style.flexGrow   = String(tile.w);  // width ∝ unit span (Σ flexGrow = U)
			el.style.flexBasis  = '0';
			el.title             = TILE_TITLE;
			// Editing is live: reorder/resize/split/merge all mutate seq.tiles, which the
			// transport re-reads each scheduler tick, so they take effect without stopping
			// playback. renderSequencer only rebuilds DOM (no audio calls), and the playing
			// highlight re-attaches on the next visual frame.
			if (tile.muted) el.classList.add('muted');
			if (tile.locked) el.classList.add('locked');               // protected from overwrite/randomize
			if (tile === selectedTile) el.classList.add('selected');   // shows length handles

			// Waveform backdrop: this slice's own audio, drawn after layout settles.
			const wave = document.createElement('canvas');
			wave.className = 'seq-tile-wave';
			el.appendChild(wave);

			// Lock indicator (corner badge) when the slice is protected.
			if (tile.locked) {
				const lock = document.createElement('span');
				lock.className   = 'seq-tile-lock';
				lock.textContent = '🔒';
				lock.title       = 'Locked — protected from overwrite + randomize';
				el.appendChild(lock);
			}

			// Reverse indicator (top-left badge) — the mirrored wave alone can be easy
			// to miss on short slices.
			if (tile.reversed) {
				const rev = document.createElement('span');
				rev.className   = 'seq-tile-rev';
				rev.textContent = '◀';
				rev.title       = 'Reversed — plays back-to-front';
				el.appendChild(rev);
			}

			const label = document.createElement('span');
			label.className   = 'seq-tile-label';
			label.textContent = fmtW(tile.src + 1);
			el.appendChild(label);

			const widthChip = document.createElement('span');
			widthChip.className   = 'seq-tile-width';
			widthChip.textContent = fmtW(tile.w);
			el.appendChild(widthChip);

			// Pitch-offset badge (shown only when non-zero); click it to reset.
			const badge = document.createElement('span');
			badge.className = 'seq-step-pitch';
			updatePitchBadge(badge, tile.offset || 0);
			badge.addEventListener('click', (ev) => {
				ev.stopPropagation();
				tile.offset = 0;
				updatePitchBadge(badge, 0);
				scheduleSave();
			});
			el.appendChild(badge);

			// Wheel over a tile nudges its pitch offset (±5), independent of master.
			el.addEventListener('wheel', (ev) => {
				ev.preventDefault();
				const dir = ev.deltaY < 0 ? 1 : -1;
				tile.offset = Math.max(-5, Math.min(5, (tile.offset || 0) + dir));
				updatePitchBadge(badge, tile.offset);
				schedulePrescan();
				scheduleSave();
			}, { passive: false });

			// Double-click splits the tile in half.
			el.addEventListener('dblclick', (ev) => {
				ev.preventDefault();
				splitTile(i);
			});

			// Plain click toggles this slice's SELECTION (shows/hides its length handles
			// and targets the Slice settings). Shift-click still merges into a neighbour.
			el.addEventListener('click', (ev) => {
				if (ev.shiftKey) { ev.preventDefault(); mergeTile(i); return; }
				selectedTile = (selectedTile === tile) ? null : tile;
				renderSequencer();
			});

			// --- Reorder (pointer-driven, HORIZONTAL only) ---
			// We track the pointer ourselves instead of using native HTML5 DnD, whose
			// drag image floats on both axes. The tile never moves vertically — only a
			// drop marker slides along the row, and the drop target is derived purely
			// from clientX. A small threshold keeps plain clicks (merge / handle toggle)
			// working; vertical motion is simply ignored.
			el.addEventListener('pointerdown', (ev) => {
				if (resizing || ev.button !== 0) return;   // left button only; never mid-resize
				const startX = ev.clientX;
				let dragging  = false;
				let ghost     = null;      // translucent copy that snaps to the target slot
				let startRect = null;      // the dragged tile's position when the drag began

				// Captured once the drag begins, for gaps-mode free placement: pixels/unit
				// and the cursor's offset within the tile, so the clip's LEFT edge tracks
				// the cursor (minus that offset) and snaps to the sub-unit grid.
				let pxPerU = 0, grabDX = 0;
				// Gaps mode: the clip's landing unit (left edge) under the cursor, snapped.
				const gapsTargetUnit = (clientX) => {
					const rowRect = seqStepsRow.getBoundingClientRect();
					return snapU(pxPerU > 0 ? ((clientX - grabDX) - rowRect.left) / pxPerU : 0);
				};

				const onMove = (e2) => {
					if (!dragging) {
						if (Math.abs(e2.clientX - startX) < 4) return;   // horizontal threshold
						dragging = true;
						dragFromIdx = i;
						startRect = el.getBoundingClientRect();
						const rowRect0 = seqStepsRow.getBoundingClientRect();
						pxPerU = rowRect0.width / (unitCount() || 1);
						grabDX = startX - startRect.left;
						el.classList.add('dragging');
						ghost = makeDragGhost(el);
						document.body.appendChild(ghost);
					}
					const rowRect = seqStepsRow.getBoundingClientRect();
					if (seqEditMode === 'gaps') {
						// Free placement: ghost follows the cursor, snapped to the unit grid,
						// clamped to the row. No reflow (overwrite mode has no "between").
						const u = Math.max(0, Math.min(gapsTargetUnit(e2.clientX), (unitCount() || 1) - startRect.width / pxPerU));
						const left = Math.max(rowRect.left, Math.min(rowRect.left + u * pxPerU, rowRect.right - startRect.width));
						ghost.style.transform = `translateX(${left - startRect.left}px)`;
						return;
					}
					// PACKED: live reflow — physically shuffle the underlying entries (DOM +
					// seq.tiles) so the row rearranges as you drag; the ghost floats over the
					// dragged tile's live slot. No drop line.
					// Widths are stable under reorder (flex-grow sums are unchanged), so the
					// insert index is computed on the layout as if the dragged tile were
					// removed: subtract its width from the midpoints of entries currently to
					// its right. That value is invariant to where the dragged tile sits, so
					// there's no oscillation.
					const entries    = [...seqStepsRow.children];
					const draggedDom = entries.indexOf(el);
					const others     = entries.filter((ch) => ch !== el);
					const dw = startRect.width;
					let insert = 0;
					for (const ch of others) {
						const r   = ch.getBoundingClientRect();
						const mid = r.left + r.width / 2 - (entries.indexOf(ch) > draggedDom ? dw : 0);
						if (e2.clientX > mid) insert++;
					}
					const ref = others[insert] || null;
					if (el.nextSibling !== ref) {
						seqStepsRow.insertBefore(el, ref);
						const dt  = el._tile;
						const cur = seq.tiles.indexOf(dt);
						if (cur !== -1) { seq.tiles.splice(cur, 1); seq.tiles.splice(insert, 0, dt); }
					}
					// Ghost snaps over the dragged tile's (now relocated) slot.
					const slot = el.getBoundingClientRect();
					const left = Math.max(rowRect.left, Math.min(slot.left, rowRect.right - startRect.width));
					ghost.style.transform = `translateX(${left - startRect.left}px)`;
				};
				const onUp = (e2) => {
					window.removeEventListener('pointermove', onMove);
					window.removeEventListener('pointerup',   onUp);
					el.classList.remove('dragging');
					clearDropMarkers();
					if (ghost) { ghost.remove(); ghost = null; }
					if (!dragging) return;
					// A drag ending over a different tile fires `click` on the row, not on
					// `el`, so swallow exactly the next click (capture phase) to stop the
					// reorder from also triggering merge / selection. The timeout clears the
					// trap if, in some path, no click is generated.
					const swallow = (ce) => { ce.stopPropagation(); ce.preventDefault(); };
					window.addEventListener('click', swallow, { capture: true, once: true });
					setTimeout(() => window.removeEventListener('click', swallow, { capture: true }), 0);
					const from = dragFromIdx;
					dragFromIdx = null;
					if (seqEditMode === 'gaps') {
						moveTileGaps(from, gapsTargetUnit(e2.clientX));   // leave gap + overwrite
					} else {
						// Order was applied live during the drag; re-render once to refresh
						// each tile's captured index/handlers, redraw backdrops, and commit.
						renderSequencer();
						scheduleSave();
					}
				};
				window.addEventListener('pointermove', onMove);
				window.addEventListener('pointerup',   onUp);
			});

			// --- Edge resize: green START (drag back) + red END (drag forward) ---
			// Conserves Σ w: dragging a boundary transfers units between tile i and the
			// tiles on the drag side, CASCADING nearest-first. Donors shrink and, once
			// emptied, are fully ABSORBED (w → 0, dropped on release) — so even a freshly
			// sliced all-width-1 grid (where no tile has spare units to lend) can still
			// grow a step by swallowing its neighbours. The boundary EATS the data it
			// moves over: dragging RIGHT overwrites the following clip's head (its src
			// advances), dragging LEFT overwrites the preceding clip's tail (its w
			// truncates). The growing clip always keeps its own src pinned and reveals
			// more of its own source at its tail, so eaten audio is genuinely gone —
			// it never migrates onto a neighbour. On a fresh source-ordered grid this
			// reads as sliding the cut point through one continuous waveform. Every
			// window stays inside the cut [0, U]. Each handle exists only where there's
			// a region to trade with (start: i>0, end: i<last); the outer edges are
			// pinned to the cut. Loop length is unchanged; the step count drops as
			// steps are absorbed.
			{
				const addHandle = (edge) => {
					const handle = document.createElement('span');
					handle.className = `seq-tile-resize ${edge}`;
					handle.title     = edge === 'start'
						? 'Drag the start: left grows this slice over the previous slice’s end; right trims this slice’s own start'
						: 'Drag the end: right grows this slice over the next slice’s start; left trims this slice’s own end';
					handle.draggable = false;
					handle.addEventListener('pointerdown', (ev) => {
						ev.preventDefault();
						ev.stopPropagation();
						resizing = true;       // suppresses the tile's reorder pointerdown

						// Leave-gaps mode: non-conserving edge resize. Shrinking leaves a gap;
						// growing overwrites the neighbour. Deterministic per move: recompute
						// from a start-of-drag snapshot so back-and-forth doesn't accumulate.
						if (seqEditMode === 'gaps') {
							const rowRect = seqStepsRow.getBoundingClientRect();
							const pxPerU  = rowRect.width / U;
							const startX  = ev.clientX;
							const snap    = seq.tiles.map((t) => ({ ...t }));
							const gonMove = (e2) => {
								const dU = pxPerU > 0 ? (e2.clientX - startX) / pxPerU : 0;
								seq.tiles = computeResizeGaps(snap, i, edge, dU);
								renderSequencer();
							};
							const gonUp = () => {
								window.removeEventListener('pointermove', gonMove);
								window.removeEventListener('pointerup',   gonUp);
								resizing = false;
								scheduleSave();
							};
							window.addEventListener('pointermove', gonMove);
							window.addEventListener('pointerup',   gonUp);
							return;
						}

						const lastIdx = seq.tiles.length - 1;
						const rowRect = seqStepsRow.getBoundingClientRect();
						const pxPerU  = rowRect.width / U;
						const startX  = ev.clientX;
						// Snapshot every tile so each move is a pure function of the drag
						// distance — deterministic and reversible, with no drift as the
						// pointer wanders back and forth.
						const w0   = seq.tiles.map((t) => t.w);
						const src0 = seq.tiles.map((t) => t.src);

						// END drag: grow/shrink tile i's TAIL, trading units with the tiles
						// after it. dU>0 extends forward — the boundary eats the following
						// clips' HEADS (src advances, tail kept), i reveals more of its own
						// source at its tail; donors fully consumed are ABSORBED (w → 0,
						// dropped on release), so even an all-width-1 grid has slack to
						// give. dU<0 retracts i's tail (truncating its own data) and hands
						// the width to the following tiles' tails — never their fronts, so
						// i's discarded audio doesn't re-surface on the neighbour. Bounded
						// so i's window and every donor stay inside the cut [0, U].
						const cascadeEnd = (dU) => {
							let want = dU > 0
								? Math.min(dU, U - src0[i] - w0[i])   // i's tail can't pass the cut
								: Math.max(dU, MIN_W - w0[i]);        // the dragged tile keeps w ≥ MIN_W
							let moved = 0;
							if (want > 0) {
								let need = want;
								for (let k = i + 1; k <= lastIdx && need > 0; k++) {
									const give = Math.min(need, w0[k]);   // donor may vanish (w → 0)
									seq.tiles[k].src = src0[k] + give;    // head overwritten: start eaten
									seq.tiles[k].w   = w0[k] - give;
									need -= give;
								}
								moved = want - need;
							} else if (want < 0) {
								let surplus = -want;
								for (let k = i + 1; k <= lastIdx && surplus > 0; k++) {
									const recv = Math.min(surplus, U - src0[k] - w0[k]);
									seq.tiles[k].w = w0[k] + recv;
									surplus -= recv;
								}
								moved = -(-want - surplus);
							}
							seq.tiles[i].w = w0[i] + moved;        // i.src pinned (front anchored)
						};

						// START drag: move tile i's FRONT in the bar, trading units with the
						// tiles before it. The boundary EATS whatever it moves over:
						// dU<0 grows i backward over the preceding clips' TAILS (donors
						// truncate, absorbed to w → 0 when consumed) while i keeps its src
						// pinned and reveals more of its own source at its tail — the
						// donors' lost audio is genuinely gone, never re-surfacing on i's
						// front. dU>0 moves the boundary right over i's own HEAD (src
						// advances, tail kept), handing the width to the preceding tiles'
						// tails. Growth is bounded by the source remaining after i's window.
						const cascadeStart = (dU) => {
							let want = Math.min(Math.max(dU, -(U - src0[i] - w0[i])), w0[i] - MIN_W);
							let moved = 0;
							if (want < 0) {
								let need = -want;
								for (let k = i - 1; k >= 0 && need > 0; k--) {
									const give = Math.min(need, w0[k]);   // donor may vanish (w → 0)
									seq.tiles[k].w = w0[k] - give;
									need -= give;
								}
								moved = -(-want - need);
							} else if (want > 0) {
								let surplus = want;
								for (let k = i - 1; k >= 0 && surplus > 0; k--) {
									const recv = Math.min(surplus, U - src0[k] - w0[k]);
									seq.tiles[k].w = w0[k] + recv;
									surplus -= recv;
								}
								moved = want - surplus;
							}
							if (moved > 0) seq.tiles[i].src = src0[i] + moved;   // shrink: head eaten
							seq.tiles[i].w = w0[i] - moved;                       // grow: src pinned, tail reveals
						};

						const onMove = (e2) => {
							if (seq.tiles.length !== w0.length) return;   // layout changed under us
							// Reset to the snapshot, then re-derive this move from scratch.
							for (let k = 0; k < seq.tiles.length; k++) {
								seq.tiles[k].w   = w0[k];
								seq.tiles[k].src = src0[k];
							}
							const dU = pxPerU > 0 ? snapU((e2.clientX - startX) / pxPerU) : 0;
							if (edge === 'end') cascadeEnd(dU); else cascadeStart(dU);
							// Re-weight every tile, refresh chips/labels, repaint backdrops.
							// Absorbed tiles (w === 0) are hidden outright — flexGrow:0 alone
							// wouldn't collapse them past the tile's min-width.
							let idx = 0;
							for (const child of seqStepsRow.children) {
								if (!child.classList.contains('seq-tile')) continue;
								const t = seq.tiles[idx++];
								if (!t) continue;
								if (t.w <= 0) { child.style.display = 'none'; continue; }
								child.style.display  = '';
								child.style.flexGrow = String(t.w);
								const wc = child.querySelector('.seq-tile-width');
								const lb = child.querySelector('.seq-tile-label');
								if (wc) wc.textContent = fmtW(t.w);
								if (lb) lb.textContent = fmtW(t.src + 1);
							}
							redrawTileWaves();
						};
						const onUp = () => {
							window.removeEventListener('pointermove', onMove);
							window.removeEventListener('pointerup',   onUp);
							resizing = false;
							// Commit: drop any tiles absorbed during the drag (w === 0). If the
							// count changed, rebuild once so indices/DOM realign; otherwise the
							// in-place edits already reflect the final state (no rebuild flash).
							const before = seq.tiles.length;
							seq.tiles = seq.tiles.filter((t) => t.w > 0);
							if (seq.tiles.length !== before) renderSequencer();
							else scheduleSave();
						};
						window.addEventListener('pointermove', onMove);
						window.addEventListener('pointerup',   onUp);
					});
					el.appendChild(handle);
				};
				if (i > 0)    addHandle('start');
				if (i < last) addHandle('end');
			}

			seqStepsRow.appendChild(el);
		}

		// Drop a stale selection (its tile was split/merged/re-sliced away) and reflect
		// the current selection into the Slice settings controls.
		if (selectedTile && !seq.tiles.includes(selectedTile)) selectedTile = null;
		updateSliceSettings();

		if (seq.isPlaying) {
			seqStatus.textContent = `Playing slice ${seq.currentTile + 1} of ${seq.tiles.length}`;
		} else {
			seqStatus.textContent = `${beatCount()} beats · 1/${divisionDenom} · ${U} steps`;
		}

		// Draw backdrops synchronously: reading clientWidth forces flex layout, so
		// the canvases are painted before the browser's next frame — no blank flash
		// / jump on rebuild. (Initial load with a 0-width row is covered by the
		// ResizeObserver below.)
		redrawTileWaves();

		scheduleSave();
	};

	// Redraw tile backdrops when the row's width changes (responsive container).
	const tileRowResizeObs = new ResizeObserver(() => requestAnimationFrame(redrawTileWaves));
	tileRowResizeObs.observe(seqStepsRow);

	// --- Sequencer playback (sample-accurate, via Transport on the audio clock) ---
	// Map a tile's unit span [src, src+w) to the real-buffer region it sounds, so the
	// waveform playhead can sweep across exactly that span.
	const tileRegion = (src, w) => {
		const U    = unitCount() || 1;
		const span = selEndFrac - selStartFrac;
		const u0   = Math.max(0, Math.min(U, src));
		const u1   = Math.max(0, Math.min(U, src + w));
		return {
			start: selStartFrac + (u0 / U) * span,
			end:   selStartFrac + (u1 / U) * span,
		};
	};

	const transport = new Transport({
		engine:          slicer.engine,
		grid,                                          // shared beat↔time mapping (lock-in)
		getTiles:        () => seq.tiles,
		getStepBeats:    () => 4 / divisionDenom,      // a 1/D-note step is 4/D quarter-notes
		getTilePlayback: getTilePlayback,
		isLooping:       () => seq.loop,
		onTileVisual: (tileIndex, src, w, frac) => {
			setPlayingTile(tileIndex);   // a gap child isn't a .seq-tile → no highlight
			const cur = seq.tiles[tileIndex];
			if (seq.currentTile !== tileIndex) {
				seq.currentTile = tileIndex;
				seqStatus.textContent = (cur && cur.gap)
					? `Gap ${tileIndex + 1} of ${seq.tiles.length}`
					: `Playing slice ${tileIndex + 1} of ${seq.tiles.length}`;
			}
			// During a gap there's no source region playing — clear the waveform playhead.
			if (cur && cur.gap) {
				slicer.view.setPlayingRegion(null);
				slicer.view.setIsPlaying(false);
				return;
			}
			const r = tileRegion(src, w);
			slicer.view.setPlayingRegion(r.start, r.end);
			slicer.view.setIsPlaying(true);
			slicer.view.setPlayheadPosition(frac);
		},
		onStop: () => {
			seq.isPlaying   = false;
			seq.currentTile = -1;
			playOrder       = -1;
			slicer.sequencerPlaying = false;
			setPlayingTile(-1);
			slicer.view.setIsPlaying(false);
			slicer.view.setPlayingRegion(null);
			slicer.view.setPlayheadPosition(0);
			renderSequencer();
		},
	});

	// Stamped when this slicer's sequencer starts, so the clock indicator can pick the
	// "first played" slicer as the internal clock source. -1 while stopped.
	let playOrder = -1;

	// `atTime`: shared-clock anchor from the master so every track starts aligned
	// (Tier 1c). `anchorBeat`: absolute grid beat for loop position 0 (may lie in
	// the past — the transport skips forward into the bar). Neither given
	// (individual Play): if other tracks are already running, JOIN IN PHASE by
	// anchoring to the current iteration of this loop on the shared grid;
	// otherwise self-anchor and play from the top.
	const startSeqPlayback = async (atTime, anchorBeat) => {
		if (seq.isPlaying) return;
		if (seq.tiles.length === 0) return;

		if (typeof anchorBeat !== 'number' && typeof atTime !== 'number' && grid.running) {
			const othersPlaying = slicers.some((s) => s.id !== id && s.getClockInfo && s.getClockInfo().playing);
			if (othersPlaying) {
				const L = beatCount();
				const nowBeat = grid.beatAtTime(slicer.engine.audioContext.currentTime + 0.05);
				anchorBeat = Math.floor(nowBeat / L) * L;   // this loop's current iteration start
			}
		}

		playOrder       = playSeqCounter++;
		seq.isPlaying   = true;
		seq.currentTile = -1;
		slicer.sequencerPlaying = true;
		slicer.stop();        // cancel any free-run playback + its playhead animation
		renderSequencer();    // reflect playing state (disables drag/resize)

		prescanStretches();   // enqueue all needed stretch builds up front (one smooth bar)
		await transport.start(atTime, { anchorBeat: typeof anchorBeat === 'number' ? anchorBeat : undefined });
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
		scheduleSave();
	});
	// "Reset" the tiles back to the default one-unit-per-tile layout (Σ w === U).
	// Safe mid-play: the transport re-reads seq.tiles each scheduler tick, so swapping
	// the array in place reshapes the loop without stopping playback.
	seqClearBtn.addEventListener('click', () => {
		seq.tiles = defaultTiles(unitCount());
		renderSequencer();
	});

	// Shuffle the play order. A Fisher-Yates pass where each candidate swap fires only
	// with probability `level` (0..1): the amount knob thus controls how far the result
	// drifts from the current pattern — 0 = no change, 1 = a full shuffle. Only order
	// changes; each tile keeps its src/w/offset/mute, so Σ w (loop length) and the
	// available audio are untouched. Safe mid-play: the transport re-reads seq.tiles
	// each scheduler tick.
	//
	// Locked slices stay put — both their identity AND their apparent start (= Σ w of
	// everything before them). To keep every lock in place we treat the row as a chain
	// of buckets (contiguous non-locked runs) separated by locks, and only permute in
	// ways that preserve each bucket's TOTAL WIDTH:
	//   1. Whole-bucket swaps between buckets of equal total width — this lets a run
	//      of thin tiles trade places with a single wide tile between the same locks
	//      (e.g., [1:1][2:1][3:1] ↔ [5:3] around a locked [4L:4], swapping the tile
	//      counts as well as the identities).
	//   2. Within-bucket Fisher-Yates on top, so each bucket's internal order also
	//      shuffles.
	// Buckets that don't match any other bucket's width can still shuffle internally
	// but never swap out; that's the sub-set constraint that keeps the locks anchored.
	const randomizeTiles = () => {
		const level = Math.max(0, Math.min(1, randLevel / 100));

		// Partition seq.tiles into buckets (non-locked runs) interleaved with locks:
		// [bucket0, lock0, bucket1, lock1, ..., bucketN]. Bucket .w is the invariant
		// each swap has to respect; we key equality by RES-scaled integer so fractional
		// widths compare exactly.
		const buckets = [{ tiles: [], w: 0 }];
		const locks   = [];
		for (const t of seq.tiles) {
			if (t.locked) {
				locks.push(t);
				buckets.push({ tiles: [], w: 0 });
			} else {
				const cur = buckets[buckets.length - 1];
				cur.tiles.push(t);
				cur.w += (t.w || 0);
			}
		}
		const wKey = (b) => Math.round(b.w * RES);

		let swapped = false;

		// (1) Whole-bucket swaps. Fisher-Yates over the bucket list, restricted to
		// same-width partners; empty buckets (0-width slots between adjacent locks)
		// only swap with other empty buckets, i.e. never usefully.
		for (let a = buckets.length - 1; a > 0; a--) {
			if (buckets[a].tiles.length === 0) continue;   // nothing to move out of a
			if (Math.random() >= level) continue;
			const wA = wKey(buckets[a]);
			const partners = [];
			for (let b = 0; b < a; b++) {
				if (wKey(buckets[b]) === wA && buckets[b] !== buckets[a]) partners.push(b);
			}
			if (partners.length === 0) continue;
			const b = partners[Math.floor(Math.random() * partners.length)];
			const tmp = buckets[a].tiles;
			buckets[a].tiles = buckets[b].tiles;
			buckets[b].tiles = tmp;
			// .w stays the same on both sides (that was the swap precondition).
			swapped = true;
		}

		// (2) Within-bucket Fisher-Yates.
		for (const bkt of buckets) {
			const arr = bkt.tiles;
			for (let k = arr.length - 1; k > 0; k--) {
				if (Math.random() >= level) continue;
				const j = Math.floor(Math.random() * (k + 1));
				if (j === k) continue;
				const t = arr[k]; arr[k] = arr[j]; arr[j] = t;
				swapped = true;
			}
		}

		if (!swapped) return;

		// Interleave buckets and locks back into a flat tile row.
		const rebuilt = [];
		for (let i = 0; i < buckets.length; i++) {
			for (const t of buckets[i].tiles) rebuilt.push(t);
			if (i < locks.length) rebuilt.push(locks[i]);
		}
		seq.tiles = rebuilt;
		renderSequencer();   // also schedules a save
	};
	randomizeBtn.addEventListener('click', randomizeTiles);
	randLevelInput.addEventListener('input', () => {
		randLevel = parseInt(randLevelInput.value, 10) || 0;
		scheduleSave();
	});

	// --- Persistence: expose this slicer's serializable state + register it ---
	const getState = () => ({
		id,
		fileName:       currentFileName,
		virtualStart,
		virtualEnd,
		start:          parseFloat(startInput.value),
		end:            parseFloat(endInput.value),
		beats:          beatCount(),
		bpm,
		divisionDenom,
		bpmManual,
		volume:         volumeKnob.value,
		pan:            panKnob.value,
		masterPitch,
		randLevel,
		seq:            {
			tiles: seq.tiles.map((t) => (t.gap
				? { gap: true, w: t.w }
				: { src: t.src, w: t.w, offset: t.offset || 0, muted: !!t.muted, colorIdx: t.colorIdx, locked: !!t.locked, reversed: !!t.reversed, fadeIn: t.fadeIn || 0, fadeOut: t.fadeOut || 0 })),
			loop:  seq.loop,
		},
	});
	// The MIDI clock controller drives every slicer through these hooks: setMidiBpm
	// for tempo sync, and seqStart/seqStop/hasTiles for MIDI Start/Continue/Stop.
	// Clock readout for the global indicator: effective tempo, play order, and the
	// audio-clock times needed to compute the beat phase (ctxTime − startTime).
	const getClockInfo = () => {
		const ctx = slicer.engine && slicer.engine.audioContext;
		return {
			playing:   seq.isPlaying,
			order:     playOrder,
			bpm:       (midiBpm != null ? midiBpm : bpm),
			ctxTime:   ctx ? ctx.currentTime : 0,
			startTime: transport.startTime,
		};
	};

	slicers.push({
		id, getState, setMidiBpm, getClockInfo,
		_transport: transport,       // debug/verification only — not app API
		seqStart: startSeqPlayback,
		seqStop:  stopSeqPlayback,
		hasTiles: () => seq.tiles.length > 0,
		// --- WAV export hooks (see export-wav.js) ---
		// Metadata for the export panel's checkbox list + the default-length LCM.
		// A slicer is exportable once it has decoded audio and at least one audible
		// (non-gap) tile.
		exportMeta: () => ({
			id,
			fileName: currentFileName,
			beats:    beatCount(),
			exportable: !!slicer.engine.audioBuffer && seq.tiles.some((t) => !t.gap),
		}),
		// Enqueue every WSOLA build the current master tempo/pitch needs, so a
		// pitch-correct export can await them before rendering.
		exportPrescan: () => prescanStretches(),
		// Outstanding stretch builds (0 = all cached → safe to render).
		exportPending: () => stretchPending.size,
		// Snapshot the render inputs: mix + a tile-list copy + stepSec at the master
		// tempo + the live tile→buffer policy (returns cached stretched buffers now
		// that exportPrescan's builds have landed).
		exportTrack: () => ({
			volume:  volumeKnob.value,
			pan:     panKnob.value,
			tiles:   seq.tiles.slice(),
			stepSec: stepSec(),
			getTilePlayback,
		}),
	});
	// A slicer added while sync is already live adopts the current tempo at once —
	// the external MIDI clock if it's driving, otherwise the internal master.
	if (midiClock.isEnabled() && midiClock.getBpm() != null) setMidiBpm(midiClock.getBpm());
	else if (masterClock.bpm != null) setMidiBpm(masterClock.bpm);

	if (savedState) {
		// Settings that don't need the decoded audio buffer — apply synchronously.
		// `beats` is the current field; pre-reframe saves only had `subdivisions`
		// (the old grid count) which no longer maps cleanly → fall back to the default
		// beat count and let the tile-validity check regenerate tiles for the new grid.
		if (Number.isFinite(savedState.beats)) { beatsSelect.value = String(savedState.beats); beatsControl.syncFromEl(); }
		if (Number.isFinite(savedState.start))        startInput.value = savedState.start;
		if (Number.isFinite(savedState.end))          endInput.value   = savedState.end;
		startSlider.value = startInput.value;
		endSlider.value   = endInput.value;
		startSliderCtrl.syncFromEl();
		endSliderCtrl.syncFromEl();
		if (Number.isFinite(savedState.virtualStart)) virtualStart = savedState.virtualStart;
		if (Number.isFinite(savedState.virtualEnd))   virtualEnd   = savedState.virtualEnd;

		if (Number.isFinite(savedState.bpm))           { bpm = savedState.bpm; bpmInput.value = bpm.toFixed(2); }
		if (Number.isFinite(savedState.divisionDenom)) { divisionDenom = savedState.divisionDenom; divisionSelect.value = String(divisionDenom); stepControl.syncFromEl(); }
		bpmManual      = !!savedState.bpmManual;
		updateBpmResetBtn();

		if (Number.isFinite(savedState.volume)) { volumeKnob.setValue(savedState.volume); slicer.setVolume(savedState.volume); }
		if (Number.isFinite(savedState.pan))    { panKnob.setValue(savedState.pan);       slicer.setPan(savedState.pan); }
		if (Number.isFinite(savedState.masterPitch)) { masterPitch = savedState.masterPitch; pitchKnob.setValue(masterPitch); }
		if (Number.isFinite(savedState.randLevel)) {
			randLevel = Math.max(0, Math.min(100, savedState.randLevel));
			randLevelInput.value = randLevel;
			amountControl.syncFromEl();
		}

		if (savedState.seq) {
			// v3 tiles. Pre-v3 saves used a different sequencing model (bare-number /
			// {slice,offset} steps) — those don't map onto variable-width tiles, so we
			// drop them and regenerate defaults after the audio re-slices.
			seq.tiles = Array.isArray(savedState.seq.tiles)
				? savedState.seq.tiles
					.filter((t) => t && Number.isFinite(t.w) && t.w > 0 && (t.gap || Number.isFinite(t.src)))
					.map((t, i) => (t.gap
						? { gap: true, w: t.w, src: 0 }
						: {
							src: t.src, w: t.w, offset: t.offset || 0, muted: !!t.muted,
							colorIdx: Number.isFinite(t.colorIdx) ? t.colorIdx : i, locked: !!t.locked,
							reversed: !!t.reversed,
							fadeIn:  Number.isFinite(t.fadeIn)  ? Math.max(0, Math.min(1, t.fadeIn))  : 0,
							fadeOut: Number.isFinite(t.fadeOut) ? Math.max(0, Math.min(1, t.fadeOut)) : 0,
						}))
				: [];
			nextColorIdx = seq.tiles.reduce((m, t) => Math.max(m, t.gap ? -1 : t.colorIdx), -1) + 1;
			seq.loop  = !!savedState.seq.loop;
			seqLoopBtn.classList.toggle('active', seq.loop);
			seqLoopBtn.setAttribute('aria-pressed', String(seq.loop));
		}

		// Re-load + re-slice the audio from IndexedDB, then re-apply tempo.
		restoreCount++;
		(async () => {
			try {
				const blob = await getAudio('audio-' + id);
				if (blob) {
					await slicer.loadFile(blob);
					renderFileInfo();
					slicer.setView(virtualStart, virtualEnd);
					// Re-slice with the saved window/subdivisions. applyRange →
					// deriveTransport recomputes originalBPM and respects bpmManual.
					applyRange(parseFloat(startInput.value), parseFloat(endInput.value), { keepPlayhead: false });
					bpmInput.value = bpm.toFixed(2);
					updateBpmResetBtn();
					// Saved tiles win, but fall back to defaults if absent (pre-v3 save)
					// or any tile falls outside the cut at the restored resolution.
					const Ur = unitCount();
					const valid = seq.tiles.length > 0 &&
						seq.tiles.every((t) => t.w > 0 && (t.gap || (t.src >= 0 && t.src + t.w <= Ur)));
					if (!valid) seq.tiles = defaultTiles(Ur);
					renderSequencer();
				} else {
					console.warn(`No saved audio for slicer ${id} (${currentFileName || 'unknown'}) — restored silent.`);
				}
			} catch (err) {
				console.warn('Failed to restore audio for slicer ' + id + ':', err);
			} finally {
				restoreCount--;
				if (restoreCount === 0) saveState();
			}
		})();
	}

	renderSequencer();
	applyEditMode(detailsShown);   // set initial edit/perform layout (waveform + tile heights)
	return container;              // so Duplicate can slot the copy in right after the source
}

// --- Internal master clock (groovebox transport) ---
// One master tempo drives every slicer. Each slicer treats its selection as one
// bar, so at a shared BPM all loops phase-align as bar multiples/subdivisions
// (a clip whose loop spans 2 bars repeats every 2 master bars, etc.). Master
// Play All / Stop All start/stop every sequencer together (restarted from the top
// so they're aligned). The external MIDI clock, when enabled, overrides the master.
const masterClock = { bpm: null, running: false, userSet: false };
let masterBpmControl, masterPlayBtn, masterStopBtn;

// Master tempo bounds — shared by the clamp and the BPM DragControl so the
// stored tempo can never diverge from what the control can display. A very
// short/long one-bar selection would otherwise derive an out-of-range BPM
// (e.g. a 59s clip → 240/59 ≈ 4 BPM) that the control silently clamps for
// display while masterClock.bpm kept the raw value — the two then disagreed.
const MASTER_BPM_MIN = 20, MASTER_BPM_MAX = 400;
const clampMasterBpm = (v) => Math.max(MASTER_BPM_MIN, Math.min(MASTER_BPM_MAX, v));

// True when the internal master owns the tempo (no external MIDI clock engaged).
function masterDriving() { return !midiClock.isEnabled(); }

// Push the master tempo to every slicer (locks each slicer's BPM to it) and
// re-anchor the shared beat grid — phase-continuous at "now", so every running
// transport adopts the new tempo from the SAME instant and stays locked.
function applyMasterTempo() {
	if (!masterDriving() || masterClock.bpm == null) return;
	grid.setTempo(masterClock.bpm, getAudioContext().currentTime);
	for (const s of slicers) s.setMidiBpm && s.setMidiBpm(masterClock.bpm);
}

// Set the master tempo. `user` marks an explicit edit (persisted; stops the
// first-clip auto-adopt from overriding it later).
function setMasterBpm(v, user) {
	if (!(v > 0)) return;
	masterClock.bpm = clampMasterBpm(v);
	if (user) masterClock.userSet = true;
	if (masterBpmControl) masterBpmControl.setValue(masterClock.bpm);   // no-op mid-drag (guarded)
	applyMasterTempo();
	if (user) scheduleSave();
}

// Restart every tile-bearing slicer at ONE shared-clock instant so all tracks are
// sample-aligned on the shared AudioContext (Tier 1c). Resume the context first so
// no per-track resume() delay pushes a track past the anchor; the cushion covers
// the synchronous per-track setup (renderSequencer + prescan) before each start.
async function startAllAligned() {
	const ctx = getAudioContext();
	if (ctx.state === 'suspended') { try { await ctx.resume(); } catch (err) { /* gesture needed */ } }
	const when = ctx.currentTime + 0.12;
	// Internal master: fresh grid — bar 0 for the whole rack lands at `when`.
	// External MIDI: the grid is already phase-locked to the device (PLL below);
	// anchor every track to grid beat 0 (the external Start). That instant may
	// already be in the past — the transports skip forward into the bar, so
	// Start AND Continue both land in phase with the device.
	const driving = masterDriving();
	if (driving) grid.restart(when);
	const anchor  = driving ? undefined : 0;
	for (const s of slicers) {
		if (!(s.hasTiles && s.hasTiles())) continue;
		s.seqStop  && s.seqStop();    // restart from the top
		s.seqStart && s.seqStart(when, anchor);
	}
}

function masterPlayAll() {
	if (!masterDriving()) return;
	masterClock.running = true;
	startAllAligned();
}
function masterStopAll() {
	masterClock.running = false;
	for (const s of slicers) s.seqStop && s.seqStop();
	grid.stop();   // next solo start re-anchors instead of joining a stale phase
}

// --- WAV export -------------------------------------------------------------
// Render the master mix offline → normalized stereo 16-bit WAV. The panel lets
// the user pick which slicers to include and how many beats to render; the
// default length is the LCM of the chosen slicers' beat counts so every loop
// lands on the boundary (seamless). Pitch-correct: we prescan + await each
// slicer's WSOLA builds before rendering (see export-wav.js).
let exportBtn, exportPanel, exportOpen = false, exportKeyHandler = null;

const gcd2 = (a, b) => { a = Math.abs(a); b = Math.abs(b); while (b) { [a, b] = [b, a % b]; } return a || 1; };
const lcm2 = (a, b) => (a && b) ? Math.abs(a / gcd2(a, b) * b) : (a || b || 1);
const beatsLcm = (arr) => arr.reduce((acc, n) => lcm2(acc, n), 1);

// Poll until every chosen slicer's stretch queue drains (or a safety timeout),
// so getTilePlayback returns pitch-preserving buffers instead of the fallback.
function awaitStretchReady(chosen, timeoutMs = 60000) {
	return new Promise((resolve) => {
		const start = performance.now();
		const tick = () => {
			const pending = chosen.reduce((n, s) => n + (s.exportPending ? s.exportPending() : 0), 0);
			if (pending === 0 || performance.now() - start > timeoutMs) { resolve(); return; }
			setTimeout(tick, 60);
		};
		tick();
	});
}

// Slicers eligible for export (have audio + at least one audible tile).
function exportableSlicers() {
	return slicers.filter((s) => s.exportMeta && s.exportMeta().exportable);
}

async function runExport(chosen, lengthBeats, statusEl, exportGoBtn) {
	if (!chosen.length || !(masterClock.bpm > 0)) return;
	const setStatus = (t) => { if (statusEl) statusEl.textContent = t; };
	if (exportGoBtn) exportGoBtn.disabled = true;
	try {
		setStatus('Preparing time-stretch…');
		chosen.forEach((s) => s.exportPrescan && s.exportPrescan());
		await awaitStretchReady(chosen);

		setStatus('Rendering…');
		const tracks = chosen.map((s) => s.exportTrack());
		const buffer = await renderMix({
			tracks,
			masterBpm:  masterClock.bpm,
			lengthBeats,
			sampleRate: getAudioContext().sampleRate,
		});

		setStatus('Encoding…');
		const blob = encodeWav(buffer);
		triggerDownload(blob, 'jsloop-export.wav');
		setStatus('Exported ✓');
	} catch (err) {
		console.error('WAV export failed:', err);
		setStatus('Export failed — see console');
	} finally {
		if (exportGoBtn) exportGoBtn.disabled = false;
	}
}

function closeExportPanel() {
	exportOpen = false;
	if (exportPanel) { exportPanel.remove(); exportPanel = null; }
	if (exportBtn) exportBtn.classList.remove('active');
	if (exportKeyHandler) { document.removeEventListener('keydown', exportKeyHandler); exportKeyHandler = null; }
}

// Build the export panel fresh each open (the slicer roster changes). A modal
// card over a dimming backdrop, styled with the shared tokens/DragControl.
function openExportPanel() {
	const eligible = exportableSlicers();
	if (!eligible.length) return;

	const backdrop = document.createElement('div');
	backdrop.className = 'export-backdrop';
	backdrop.addEventListener('click', (e) => { if (e.target === backdrop) closeExportPanel(); });

	const card = document.createElement('div');
	card.className = 'export-panel';

	const heading = document.createElement('div');
	heading.className = 'export-title';
	heading.textContent = 'Export WAV';

	// One checkbox row per eligible slicer.
	const list = document.createElement('div');
	list.className = 'export-list';
	const rows = eligible.map((s, i) => {
		const meta = s.exportMeta();
		const row = document.createElement('label');
		row.className = 'export-row';
		const cb = document.createElement('input');
		cb.type = 'checkbox';
		cb.checked = true;
		const name = document.createElement('span');
		name.className = 'export-row-name';
		name.textContent = meta.fileName || `Slicer ${i + 1}`;
		const beatsTag = document.createElement('span');
		beatsTag.className = 'export-row-beats';
		beatsTag.textContent = `${meta.beats} beat${meta.beats === 1 ? '' : 's'}`;
		row.appendChild(cb);
		row.appendChild(name);
		row.appendChild(beatsTag);
		list.appendChild(row);
		return { slicer: s, cb, beats: meta.beats };
	});

	// Length (beats): default = LCM of the enabled slicers' beat counts. Recomputed
	// on checkbox change until the user drags it (then their value is respected).
	let lengthTouched = false;
	const chosenBeats = () => rows.filter((r) => r.cb.checked).map((r) => r.beats);
	const lengthCtrl = new DragControl({
		label: 'Length (beats)',
		min: 1, max: 256, step: 1,
		value: beatsLcm(chosenBeats()),
		format: (v) => `${Math.round(v)}`,
		onChange: () => { lengthTouched = true; },
	});
	const syncDefaultLength = () => {
		if (lengthTouched) return;
		const b = chosenBeats();
		if (b.length) lengthCtrl.setValue(beatsLcm(b));
	};
	rows.forEach((r) => r.cb.addEventListener('change', syncDefaultLength));

	const lengthWrap = document.createElement('div');
	lengthWrap.className = 'export-length';
	lengthWrap.appendChild(lengthCtrl.getElement());

	const status = document.createElement('div');
	status.className = 'export-status';

	const actions = document.createElement('div');
	actions.className = 'export-actions';
	const cancelBtn = document.createElement('button');
	cancelBtn.textContent = 'Cancel';
	cancelBtn.className = 'seq-stop-btn';
	cancelBtn.addEventListener('click', closeExportPanel);
	const goBtn = document.createElement('button');
	goBtn.textContent = 'Export';
	goBtn.className = 'seq-play-btn';
	goBtn.addEventListener('click', () => {
		const chosen = rows.filter((r) => r.cb.checked).map((r) => r.slicer);
		const lengthBeats = Math.max(1, Math.round(lengthCtrl.getValue()));
		if (!chosen.length) { status.textContent = 'Select at least one slicer'; return; }
		runExport(chosen, lengthBeats, status, goBtn);
	});
	actions.appendChild(cancelBtn);
	actions.appendChild(goBtn);

	card.appendChild(heading);
	card.appendChild(list);
	card.appendChild(lengthWrap);
	card.appendChild(status);
	card.appendChild(actions);
	backdrop.appendChild(card);
	document.body.appendChild(backdrop);
	exportPanel = backdrop;
	exportOpen  = true;
	if (exportBtn) exportBtn.classList.add('active');
	exportKeyHandler = (e) => { if (e.key === 'Escape') closeExportPanel(); };
	document.addEventListener('keydown', exportKeyHandler);
}

function toggleExportPanel() {
	if (exportOpen) closeExportPanel();
	else openExportPanel();
}

// Master controls yield to the external MIDI clock while it's enabled.
function updateMasterControlsEnabled() {
	const ext = midiClock.isEnabled();
	if (masterBpmControl) masterBpmControl.setDisabled(ext);
	if (masterPlayBtn)  masterPlayBtn.disabled  = ext;
	if (masterStopBtn)  masterStopBtn.disabled  = ext;
	// Export renders at the master tempo, which isn't the effective tempo while an
	// external clock drives — disable it (and close an open panel) until sync is off.
	if (exportBtn) exportBtn.disabled = ext;
	if (ext && exportOpen) closeExportPanel();
}

// --- Global transport bar (master clock + MIDI sync + clock indicator) ---
// While MIDI sync is on, the derived BPM is pushed to all slicers (overriding the
// master) and MIDI Start/Continue/Stop drive their sequencers. See midi-clock.js.
const midiBar = document.getElementById('midiBar');
let syncBtn, midiDeviceSel, midiDeviceCtrl, midiStatusEl;
// Reflect the Sync toggle-button's on/off state.
function setSyncActive(on) {
	if (!syncBtn) return;
	syncBtn.classList.toggle('active', !!on);
	syncBtn.setAttribute('aria-pressed', String(!!on));
}
// Clock indicator (right side): source, master BPM, 4-beat pulse.
let clockSourceEl, clockBpmEl, beatDots = [];

// MIDI connection feedback only — the live tempo/source/beat are shown by the clock
// indicator (updateClockIndicator), which covers the internal source too.
function updateMidiStatus(mode) {
	if (!midiStatusEl) return;
	if (mode === 'unavailable' || !midiClock.isSupported) {
		midiStatusEl.textContent = midiClock.isSupported ? 'MIDI access denied' : 'Web MIDI not supported';
		return;
	}
	if (!midiClock.isEnabled()) { midiStatusEl.textContent = 'off'; return; }
	midiStatusEl.textContent = midiClock.getBpm() == null ? 'waiting for clock…' : 'receiving';
}

function populateMidiDevices() {
	if (!midiDeviceSel) return;
	const inputs  = midiClock.getInputs();
	const current = midiClock.getInputId();
	midiDeviceSel.textContent = '';
	if (inputs.length === 0) {
		const opt = document.createElement('option');
		opt.value = ''; opt.textContent = 'No MIDI inputs';
		midiDeviceSel.appendChild(opt);
		midiDeviceSel.disabled = true;
		if (midiDeviceCtrl) { midiDeviceCtrl.refreshOptions(); midiDeviceCtrl.setDisabled(true); }
		return;
	}
	for (const inp of inputs) {
		const opt = document.createElement('option');
		opt.value = inp.id; opt.textContent = inp.name;
		midiDeviceSel.appendChild(opt);
	}
	midiDeviceSel.disabled = !midiClock.isEnabled();
	if (current) midiDeviceSel.value = current;
	// Reflect the rebuilt option list + enabled state into the drag-control.
	if (midiDeviceCtrl) { midiDeviceCtrl.refreshOptions(); midiDeviceCtrl.setDisabled(midiDeviceSel.disabled); }
}

async function onMidiEnableToggle() {
	if (syncBtn.classList.contains('active')) {
		const ok = await midiClock.init();
		if (!ok) { setSyncActive(false); updateMidiStatus('unavailable'); return; }
		midiClock.setEnabled(true);
		populateMidiDevices();
		// Prefer the saved device if it's still present, else the first input.
		const inputs = midiClock.getInputs();
		const wanted = midiSettings.inputId && inputs.some((i) => i.id === midiSettings.inputId)
			? midiSettings.inputId
			: (inputs[0] ? inputs[0].id : null);
		midiClock.setInput(wanted);
		if (wanted) { midiDeviceSel.value = wanted; if (midiDeviceCtrl) midiDeviceCtrl.syncFromEl(); }
	} else {
		midiClock.setEnabled(false);
		// Hand tempo back to the internal master (re-locks every slicer to it).
		applyMasterTempo();
	}
	updateMasterControlsEnabled();
	midiSettings.enabled = syncBtn.classList.contains('active');
	midiSettings.inputId = midiClock.getInputId();
	scheduleSave();
	updateMidiStatus();
}

function buildMidiBar() {
	if (!midiBar) return;
	midiBar.className = 'midi-bar';

	// Master transport: tempo + Play All / Stop All for the whole rack.
	const masterLabel = makeLabel('Master');
	masterBpmControl = new DragControl({
		label: 'BPM',
		min: MASTER_BPM_MIN, max: MASTER_BPM_MAX, step: 1,
		value: masterClock.bpm != null ? masterClock.bpm : 120,
		format: (v) => `${Math.round(v)}`,
		onChange: (v) => setMasterBpm(v, true),
	});

	masterPlayBtn = document.createElement('button');
	masterPlayBtn.textContent = 'Play All';
	masterPlayBtn.className    = 'seq-play-btn';    // green / primary
	masterStopBtn = document.createElement('button');
	masterStopBtn.textContent = 'Stop All';
	masterStopBtn.className    = 'seq-stop-btn';     // red / stop

	masterPlayBtn.addEventListener('click', masterPlayAll);
	masterStopBtn.addEventListener('click', masterStopAll);

	// Export WAV: opens the render panel (offline mix → downloadable WAV).
	exportBtn = document.createElement('button');
	exportBtn.textContent = 'Export WAV';
	exportBtn.className    = 'toggle-btn';
	exportBtn.title        = 'Render the master mix to a downloadable stereo WAV';
	exportBtn.addEventListener('click', toggleExportPanel);

	// Global tile-edit mode: OFF = classic packed (repack + length-conserving resize),
	// ON = leave gaps (free placement, overwrite on drop/grow). A toggle-button like
	// Loop/Sync. onSeqEditModeChange re-paints it (also fired on restore).
	const seqModeBtn = document.createElement('button');
	seqModeBtn.className = 'toggle-btn';
	seqModeBtn.title = 'Tile editing: OFF = packed (reorder repacks, resize borrows from neighbour). ON = leave gaps (moving/shrinking leaves silence; dropping/growing overwrites).';
	const paintSeqModeBtn = () => {
		const on = seqEditMode === 'gaps';
		seqModeBtn.textContent = on ? 'Gaps' : 'Packed';
		seqModeBtn.classList.toggle('active', on);
		seqModeBtn.setAttribute('aria-pressed', String(on));
	};
	onSeqEditModeChange = paintSeqModeBtn;
	paintSeqModeBtn();
	seqModeBtn.addEventListener('click', () => {
		seqEditMode = (seqEditMode === 'gaps') ? 'pack' : 'gaps';
		paintSeqModeBtn();
		scheduleSave();
	});

	const title = document.createElement('span');
	title.className   = 'midi-bar-title';
	title.textContent = 'MIDI Clock';

	// Sync: a toggle-button (same on/off component as Loop / Show details).
	syncBtn = document.createElement('button');
	syncBtn.className = 'toggle-btn';
	syncBtn.textContent = 'Sync';
	syncBtn.setAttribute('aria-pressed', 'false');
	syncBtn.addEventListener('click', () => {
		setSyncActive(!syncBtn.classList.contains('active'));
		onMidiEnableToggle();
	});

	midiDeviceSel = document.createElement('select');
	midiDeviceSel.className = 'midi-device-select';
	midiDeviceSel.disabled  = true;
	// Wrap in the unified DragControl. The option list is dynamic (devices hot-plug),
	// so populateMidiDevices() calls midiDeviceCtrl.refreshOptions() after rebuilding
	// the <option>s. Starts empty + disabled until Sync is enabled.
	midiDeviceCtrl = new DragControl({ el: midiDeviceSel, label: '' });
	midiDeviceCtrl.setDisabled(true);

	midiStatusEl = document.createElement('span');
	midiStatusEl.className = 'midi-status';

	if (!midiClock.isSupported) {
		syncBtn.disabled = true;
		syncBtn.title    = 'This browser has no Web MIDI support';
	}

	// Clock indicator: source (Internal / External / —), master BPM, beat dots.
	const clockBox = document.createElement('div');
	clockBox.className = 'clock-indicator';
	const clockLabel = document.createElement('span');
	clockLabel.className   = 'clock-label';
	clockLabel.textContent = 'Clock';
	clockSourceEl = document.createElement('span');
	clockSourceEl.className = 'clock-source';
	clockBpmEl = document.createElement('span');
	clockBpmEl.className = 'clock-bpm';
	const beatsBox = document.createElement('div');
	beatsBox.className = 'clock-beats';
	beatDots = [];
	for (let b = 0; b < 4; b++) {
		const dot = document.createElement('span');
		dot.className = 'clock-beat-dot';
		beatsBox.appendChild(dot);
		beatDots.push(dot);
	}
	clockBox.appendChild(clockLabel);
	clockBox.appendChild(clockSourceEl);
	clockBox.appendChild(clockBpmEl);
	clockBox.appendChild(beatsBox);

	// Per-item grid footprint (columns), same rules as the slicer toolbars.
	midiBar.appendChild(setSpan(masterLabel, 1));
	midiBar.appendChild(setSpan(masterBpmControl.getElement(), 2));   // BPM drag-control
	midiBar.appendChild(setSpan(masterPlayBtn, 1));
	midiBar.appendChild(setSpan(masterStopBtn, 1));
	midiBar.appendChild(setSpan(seqModeBtn, 1));    // Packed / Gaps tile-edit mode
	midiBar.appendChild(setSpan(exportBtn, 1));     // Export WAV
	midiBar.appendChild(setSpan(title, 1));
	midiBar.appendChild(setSpan(syncBtn, 1));
	midiBar.appendChild(setSpan(midiDeviceCtrl.getElement(), 2));    // device names need room
	midiBar.appendChild(setSpan(midiStatusEl, 1));
	midiBar.appendChild(setSpan(clockBox, 2));         // source + BPM + beat dots

	midiDeviceSel.addEventListener('change', () => {
		midiClock.setInput(midiDeviceSel.value || null);
		midiSettings.inputId = midiClock.getInputId();
		scheduleSave();
	});

	updateMasterControlsEnabled();
	updateMidiStatus();
	requestAnimationFrame(updateClockIndicator);
}

// Resolve the active clock each frame and paint the indicator. The external MIDI
// clock wins when it's actually delivering pulses; otherwise the lowest-ordered
// slicer still playing is the internal source (the "first played" one). Beat phase
// drives a 4-dot bar pulse — from MIDI clock counts externally, from elapsed audio
// time (ctxTime − startTime, at the source's BPM) internally.
function updateClockIndicator() {
	let source = '—', bpm = null, beatIndex = -1, phase = 0;

	// External is "present" once it's delivering pulses (bpm derived) or actively
	// running between a Start and Stop; otherwise an enabled-but-idle sync falls back
	// to the internal source.
	if (midiClock.isEnabled() && (midiClock.getBpm() != null || midiClock.isRunning())) {
		source = 'External';
		bpm    = midiClock.getBpm();
		const beat = midiClock.getBeat();
		if (beat) { beatIndex = beat.index; phase = beat.phase; }
	} else if (masterClock.bpm != null) {
		// Internal master: always show the master tempo; take the beat phase from
		// the first-played slicer that's still running (if any).
		source = 'Master';
		bpm    = masterClock.bpm;
		let src = null;
		for (const s of slicers) {
			if (!s.getClockInfo) continue;
			const info = s.getClockInfo();
			if (info.playing && (!src || info.order < src.order)) src = info;
		}
		if (src && src.bpm > 0) {
			const beatSec = 60 / src.bpm;
			const elapsed = Math.max(0, src.ctxTime - src.startTime);
			beatIndex = Math.floor(elapsed / beatSec);
			phase     = (elapsed / beatSec) % 1;
		}
	}

	if (clockSourceEl) clockSourceEl.textContent = source;
	if (clockBpmEl)    clockBpmEl.textContent    = bpm != null ? `${bpm.toFixed(1)} BPM` : '— BPM';

	// Light the current beat (index % 4) and flash it: brightest at the downbeat,
	// fading across the beat. Nothing lit when no clock is running.
	const active = beatIndex >= 0 ? beatIndex % 4 : -1;
	for (let b = 0; b < beatDots.length; b++) {
		const dot = beatDots[b];
		if (b === active) {
			const intensity = 1 - Math.min(1, phase);   // 1 → 0 across the beat
			dot.classList.add('active');
			dot.style.opacity   = String(0.4 + 0.6 * intensity);
			dot.style.transform = `scale(${1 + 0.5 * intensity})`;
		} else {
			dot.classList.remove('active');
			dot.style.opacity   = '';
			dot.style.transform = '';
		}
	}

	requestAnimationFrame(updateClockIndicator);
}

// Derived tempo → the shared grid (phase-continuous) + every slicer's display.
midiClock.onBpm((bpm) => {
	if (!midiClock.isEnabled()) return;
	grid.setTempo(bpm, getAudioContext().currentTime);
	for (const s of slicers) s.setMidiBpm && s.setMidiBpm(bpm);
	updateMidiStatus();
});
// Transport: Start restarts from the top, Continue resumes, Stop halts — across all
// slicers that have a sequence.
midiClock.onTransport((ev) => {
	if (!midiClock.isEnabled()) return;
	if (ev === 'stop') {
		for (const s of slicers) s.seqStop && s.seqStop();
	} else {
		// start: the device's beat 0 is (approximately) now — hard-anchor the grid
		// there; the PLL below trims the residual within a few pulses. continue:
		// the pulse count carries on, so the existing grid phase stays valid.
		if (ev === 'start') {
			grid.syncPhase(0, getAudioContext().currentTime, midiClock.getBpm() || grid.bpm);
		}
		// (Re)start every track anchored to grid beat 0 → all in phase with the device.
		startAllAligned();
	}
	updateMidiStatus();
});

// --- External-clock phase lock (PLL) ------------------------------------------
// Tempo-matching alone still drifts: any error in the averaged BPM integrates
// into phase, and the estimate lags real tempo changes by its window. So every
// MIDI pulse is also treated as a PHASE observation: pulse n says beat n/24
// happened at its timestamp. Mapped onto the audio clock, the difference from
// grid.timeAtBeat(beat) is the grid's phase error. We smooth it over pulses
// (event delivery jitters by a few ms) and slew the grid gently toward zero,
// snapping outright on a gross error (backgrounded tab, song-position jump).
// Running transports chase the grid every scheduler tick — skipping forward
// inside a slice when the grid moves under them — so playback stays LOCKED to
// the device, not just at a matching tempo.
const PLL_SNAP = 0.08;    // s — error beyond this: hard resync
const PLL_DEAD = 0.002;   // s — error under this: leave it (sub-audible)
const PLL_SLEW = 0.1;     // fraction of the smoothed error corrected per pulse
let pllSmoothedErr = 0;
midiClock.onPhase(({ beat, timeMs }) => {
	if (!midiClock.isEnabled() || !midiClock.isRunning() || !grid.running) return;
	const ctx    = getAudioContext();
	const audioT = ctx.currentTime + (timeMs - performance.now()) / 1000;
	const err    = grid.timeAtBeat(beat) - audioT;   // >0 → the grid is running late
	pllSmoothedErr = pllSmoothedErr * 0.8 + err * 0.2;
	if (Math.abs(err) > PLL_SNAP) {
		grid.nudge(-err);
		pllSmoothedErr = 0;
	} else if (Math.abs(pllSmoothedErr) > PLL_DEAD) {
		grid.nudge(-pllSmoothedErr * PLL_SLEW);
	}
});
// Device hot-plug / enable changes → refresh the dropdown and status line.
midiClock.onState(() => { populateMidiDevices(); updateMidiStatus(); });

buildMidiBar();

addSlicerBtn.addEventListener('click', () => createSlicer());

clearStateBtn.addEventListener('click', async () => {
	if (!window.confirm('Clear all saved slicers and audio? This cannot be undone.')) return;
	clearStateRaw();
	await clearAudio();
	location.reload();
});

// Flush the small settings JSON on the way out (audio is persisted at load time).
window.addEventListener('pagehide', saveState);
window.addEventListener('beforeunload', saveState);

// Restore saved slicers if any, otherwise start with one empty slicer.
const savedAll = loadStateRaw();
// Restore the master tempo before creating slicers so they adopt it on load.
if (savedAll && savedAll.master && Number.isFinite(savedAll.master.bpm)) {
	masterClock.bpm     = clampMasterBpm(savedAll.master.bpm);
	masterClock.userSet = !!savedAll.master.userSet;
	if (masterBpmControl) masterBpmControl.setValue(masterClock.bpm);
}
// Restore the global tile-edit mode and repaint its toggle.
if (savedAll && (savedAll.seqEditMode === 'gaps' || savedAll.seqEditMode === 'pack')) {
	seqEditMode = savedAll.seqEditMode;
	onSeqEditModeChange();
}
if (savedAll && Array.isArray(savedAll.slicers) && savedAll.slicers.length) {
	const maxId  = savedAll.slicers.reduce((m, st) => Math.max(m, Number.isFinite(st.id) ? st.id : -1), -1);
	nextSlicerId = Number.isFinite(savedAll.nextSlicerId) ? Math.max(savedAll.nextSlicerId, maxId + 1) : maxId + 1;
	savedAll.slicers.forEach((st) => createSlicer(st));
} else {
	createSlicer();
}

// Restore the MIDI clock connection (best-effort: silently stays off if access is
// denied or the saved input is gone). Remember the saved input even when sync was
// off, so re-enabling reconnects to the same device.
if (savedAll && savedAll.midi) {
	midiSettings.inputId = savedAll.midi.inputId || null;
	if (savedAll.midi.enabled && midiClock.isSupported && syncBtn) {
		setSyncActive(true);
		onMidiEnableToggle();
	}
}

// Debug/inspection handle (console + automated verification): the shared beat
// grid, the MIDI clock, and the live slicer registry. Read-only by convention.
window.__jsloop = { grid, midiClock, slicers };
