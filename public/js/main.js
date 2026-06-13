import AudioSlicerController from './audio-slicer-controller.js';
import Transport from './transport.js';
import Knob from './knob.js';
import PALETTE from './palette.js';
import midiClock from './midi-clock.js';
import { timeStretch } from './timestretch.js';
import {
	saveStateRaw, loadStateRaw, clearStateRaw,
	putAudio, getAudio, deleteAudio, clearAudio,
} from './storage.js';

const slicersDiv    = document.getElementById('slicers');
const addSlicerBtn  = document.getElementById('addSlicerBtn');
const clearStateBtn = document.getElementById('clearStateBtn');
let slicerCount     = 0;

// --- Persistence: registry of live slicers + debounced save ---
let nextSlicerId  = 0;
const slicers     = [];     // { id, getState, setMidiBpm, seqStart, seqStop, hasTiles }
let restoreCount  = 0;      // > 0 while restoring — suppresses save thrash
let saveTimer     = null;

// Global MIDI clock sync preferences (whether sync is on + which input), persisted
// alongside the slicers so a reload restores the connection.
const midiSettings = { enabled: false, inputId: null };

function saveState() {
	saveStateRaw({
		version:      3,
		nextSlicerId,
		slicers:      slicers.map((s) => s.getState()),
		midi:         { enabled: midiSettings.enabled, inputId: midiSettings.inputId },
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

	// Unit resolution U: the selection (one bar) is split into this many equal
	// units — the finest step grid. Tiles are contiguous runs of these units.
	const subdivisionsSelect = document.createElement('select');
	[2, 4, 8, 16, 32].forEach(val => {
		const opt = document.createElement('option');
		opt.value = val;
		opt.textContent = val;
		subdivisionsSelect.appendChild(opt);
	});
	subdivisionsSelect.value = 16;

	// --- Details toggle (hides Start/End inputs + sliders behind a button) ---
	const detailsBtn = document.createElement('button');
	detailsBtn.textContent = 'Show details';
	detailsBtn.className   = 'slicer-details-btn';
	detailsBtn.setAttribute('aria-expanded', 'false');

	controls.appendChild(document.createTextNode('File: '));
	controls.appendChild(fileInput);
	controls.appendChild(document.createTextNode(' Units: '));
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
		value: 1,
		format: (v) => `${Math.round(v * 100)}%`,
	});
	const panKnob = new Knob({
		label: 'Pan',
		min: -1,
		max: 1,
		step: 0.01,
		value: 0,
		format: (v) => (Math.abs(v) < 0.005 ? 'C' : (v < 0 ? `L${Math.round(-v * 100)}` : `R${Math.round(v * 100)}`)),
	});
	// Master pitch (semitones) — shifts every sequencer step; per-step offsets stack
	// on top. Center detent at 0, double-click to reset.
	const pitchKnob = new Knob({
		label: 'Pitch',
		min: -5,
		max: 5,
		step: 1,
		value: 0,
		detent: 0,
		signed: true,
	});
	controls.appendChild(volumeKnob.getElement());
	controls.appendChild(panKnob.getElement());
	controls.appendChild(pitchKnob.getElement());

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

		// Reset start/end/unit resolution UI
		startInput.value  = 0;
		endInput.value    = 1;
		startSlider.value = 0;
		endSlider.value   = 1;
		subdivisionsSelect.value = 16;

		// Stop playback, re-slice the full buffer at the default resolution
		// (segmentsliced → default tiles), and restore the auto-derived tempo.
		slicer.stop();
		playPauseBtn.textContent = 'Play';
		isPlaying = false;
		bpmManual      = false;
		divisionManual = false;
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

	// --- Knob event wiring ---
	volumeKnob.addEventListener('change', (e) => {
		slicer.setVolume(e.detail);
		scheduleSave();
	});
	panKnob.addEventListener('change', (e) => {
		slicer.setPan(e.detail);
		scheduleSave();
	});
	pitchKnob.addEventListener('change', (e) => {
		masterPitch = e.detail;
		scheduleSave();
	});

	// File loading
	fileInput.addEventListener('change', async (e) => {
		const file = e.target.files[0];
		if (file) {
			await slicer.loadFile(file);
			sliceBtn.disabled = false;
			// Reset selection to the whole buffer and slice at the current unit
			// resolution (loadFile slices with a placeholder count). This drives
			// segmentsliced → default tiles + deriveTransport.
			virtualStart = 0;
			virtualEnd   = 1;
			startInput.value  = 0;
			endInput.value    = 1;
			startSlider.value = 0;
			endSlider.value   = 1;
			applyRange(0, 1, { autoPlay: false, keepPlayhead: false });
			currentFileName = file.name;
			await putAudio('audio-' + id, file);
			scheduleSave();
		}
	});

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
	};

	// Take slider-space [start, end] (0..1), map through virtual window, slice the engine.
	const applyRange = (sliderStart, sliderEnd, opts = { keepPlayhead: true }) => {
		const subdivisions = parseInt(subdivisionsSelect.value, 10);
		if (!(sliderEnd > sliderStart) || subdivisions <= 0) return;
		const actualStart = sliderToActual(sliderStart);
		const actualEnd   = sliderToActual(sliderEnd);
		// Record the applied selection BEFORE slicing — region buffers + the unit
		// grid (built lazily during playback) read from these.
		selStartFrac = actualStart;
		selEndFrac   = actualEnd;
		slicer.slice(actualStart, actualEnd, subdivisions, opts);
		deriveTransport(actualStart, actualEnd, subdivisions);
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
	const randLevelValue = document.createElement('span');
	randLevelValue.className   = 'seq-rand-level-value';
	randLevelValue.textContent = randLevel + '%';
	const randLevelLabel = document.createElement('label');
	randLevelLabel.className = 'seq-rand-level-label';
	randLevelLabel.title     = 'Randomization amount: probability each tile is swapped';
	randLevelLabel.appendChild(document.createTextNode('Amount '));
	randLevelLabel.appendChild(randLevelInput);
	randLevelLabel.appendChild(randLevelValue);

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
	seqToolbar.appendChild(randomizeBtn);
	seqToolbar.appendChild(randLevelLabel);
	seqToolbar.appendChild(bpmLabel);
	seqToolbar.appendChild(bpmResetBtn);
	seqToolbar.appendChild(divisionLabel);
	seqToolbar.appendChild(seqStatus);

	const seqStepsRow = document.createElement('div');
	seqStepsRow.className = 'seq-steps';

	seqEl.appendChild(seqToolbar);
	seqEl.appendChild(seqStepsRow);
	container.appendChild(seqEl);

	// Sequencer state — an ordered list of variable-width tiles.
	//   tile = { src, w, offset, muted, colorIdx }
	//     src      : start unit (0..U-1) this tile reads source from
	//     w        : width in units (also its playback duration = w steps)
	//     offset   : per-tile pitch offset (semitones), stacks on master
	//     muted    : silent slot (keeps its timing)
	//     colorIdx : stable palette index — does NOT change on resize, so a slice's
	//                colour stays put while you drag its length.
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

	// Unit resolution U. After a slice the engine holds U equal unit-segments,
	// so their count is the source of truth; fall back to the select while empty.
	const unitCount = () => {
		const n = (slicer.engine.getSegments() || []).length;
		return n > 0 ? n : (parseInt(subdivisionsSelect.value, 10) || 16);
	};
	const defaultTiles = (n) => {
		nextColorIdx = n;
		return Array.from({ length: n }, (_, i) => ({ src: i, w: 1, offset: 0, muted: false, colorIdx: i }));
	};
	const totalUnits   = () => seq.tiles.reduce((sum, t) => sum + t.w, 0);
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
	let divisionDenom = 16;
	let bpmManual     = false;
	let divisionManual= false;
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
	};

	// --- Time-stretch / pitch cache ---
	// Pre-rendered, pitch-preserving stretched buffers keyed by slice index + a
	// quantized stretch factor (~2% — sub-perceptual, keeps the cache tiny during a
	// BPM sweep). Cleared whenever the engine re-slices (segments[] are replaced, so
	// a positional key would otherwise return stale audio). LRU-capped.
	const STRETCH_CACHE_MAX = 128;
	const stretchCache = new Map();          // key -> AudioBuffer
	const stretchPending = new Set();        // keys with an async build in flight
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
	// what's inside it. Cached by `src:w`, cleared on re-slice.
	const regionCache = new Map();
	const getRegionBuffer = (src, w) => {
		const buf = slicer.engine.audioBuffer;
		if (!buf) return null;
		const U   = unitCount();
		const key = `${src}:${w}`;
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
			for (let j = 0; j < len; j++) dst[j] = srcData[startSample + j];
		}
		regionCache.set(key, out);
		return out;
	};

	const buildStretchedBuffer = (src, w, factor) => {
		const region = getRegionBuffer(src, w);
		if (!region) return null;
		const channels = [];
		for (let c = 0; c < region.numberOfChannels; c++) channels.push(region.getChannelData(c));
		const stretched = timeStretch(channels, factor);
		const out = slicer.engine.audioContext.createBuffer(stretched.length, stretched[0].length, region.sampleRate);
		for (let c = 0; c < stretched.length; c++) out.copyToChannel(stretched[c], c);
		return out;
	};

	// Return a cached stretched buffer, or null (and kick an async build off the
	// scheduler tick) on a miss.
	const getStretched = (src, w, factor) => {
		const key = `${src}:${w}:${factor}`;
		const hit = cacheGet(key);
		if (hit) return hit;
		if (!stretchPending.has(key)) {
			stretchPending.add(key);
			setTimeout(() => {
				try {
					const buf = buildStretchedBuffer(src, w, factor);
					if (buf) cacheSet(key, buf);
				} catch (err) {
					console.warn('time-stretch failed:', err);
				} finally {
					stretchPending.delete(key);
				}
			}, 0);
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
		if (tile.muted) {
			return { buffer: null, playbackRate: 1, dur: playDur, fill: false };
		}
		const buf = slicer.engine.audioBuffer;
		const U   = unitCount();
		if (!buf || U <= 0) return { buffer: null, playbackRate: 1, dur: playDur, fill: false };

		const unitDur     = ((selEndFrac - selStartFrac) * buf.duration) / U;
		const naturalDur  = tile.w * unitDur;
		const fillStretch = naturalDur > 0 ? playDur / naturalDur : 1;   // = stepSec/unitDur
		const eff         = clampPitch(masterPitch + (tile.offset || 0));
		const P           = Math.pow(2, eff / 12);
		const rawFactor   = fillStretch * P;

		// No stretch and no pitch shift → play the raw region (natural tempo).
		if (Math.abs(rawFactor - 1) < 0.01 && Math.abs(P - 1) < 1e-6) {
			return { buffer: getRegionBuffer(tile.src, tile.w), playbackRate: 1, dur: playDur, fill: false };
		}

		const buffer = getStretched(tile.src, tile.w, quantStretch(rawFactor));
		if (buffer) {
			return { buffer, playbackRate: P, dur: playDur, fill: true };
		}
		// Cache miss: repitch-fill the raw region for this one pass (rate fills the
		// slot), snaps to pitch-preserving once the async build lands.
		return { buffer: getRegionBuffer(tile.src, tile.w), playbackRate: fillStretch > 0 ? 1 / fillStretch : 1, dur: playDur, fill: false };
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

	bpmInput.addEventListener('input', () => {
		const v = parseFloat(bpmInput.value);
		if (!isNaN(v) && v > 0) {
			bpm = v;
			bpmManual = true;
			updateBpmResetBtn();
			scheduleSave();
		}
	});
	bpmResetBtn.addEventListener('click', () => {
		bpm       = originalBPM;
		bpmManual = false;
		bpmInput.value = bpm.toFixed(2);
		updateBpmResetBtn();
		scheduleSave();
	});
	divisionSelect.addEventListener('change', () => {
		divisionDenom  = parseInt(divisionSelect.value, 10);
		divisionManual = true;
		scheduleSave();
	});

	// Reorder is a pointer-driven HORIZONTAL drag (not native HTML5 DnD, which would
	// let the tile ghost float vertically). State lives outside renderSequencer so it
	// survives re-renders. dragFromIdx is the grabbed tile.
	let dragFromIdx = null;
	// True while an edge-resize pointer drag is active — suppresses a reorder drag so
	// the two gestures never collide.
	let resizing    = false;
	// On touch devices there's no hover, so a tap reveals that slice's edge
	// handles. Survives re-renders via this index (transient UI, not persisted).
	const coarsePointer = !!(window.matchMedia && window.matchMedia('(hover: none)').matches);
	let handlesTileIdx  = null;

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
		g.classList.remove('dragging', 'playing', 'drop-before', 'drop-after', 'handles-visible');
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

	// Reorder: pull tile `from` out and reinsert at `to` (carries its src/w/offset).
	const moveTile = (from, to) => {
		if (from < 0 || from >= seq.tiles.length) return;
		if (to < 0) to = 0;
		if (to > seq.tiles.length) to = seq.tiles.length;
		const item     = seq.tiles[from];
		seq.tiles.splice(from, 1);
		const insertAt = (from < to) ? to - 1 : to;
		seq.tiles.splice(insertAt, 0, item);
		renderSequencer();
	};

	// Merge tile i into its left neighbour (or the right one if it's the first),
	// combining widths — the quick way down from 16 unit-tiles to a few wide slices.
	// The kept tile's window stays within the cut (its src may not be contiguous
	// with the absorbed one after reordering, so the combined width is capped).
	const mergeTile = (i) => {
		if (seq.tiles.length <= 1 || i < 0 || i >= seq.tiles.length) return;
		const U = unitCount() || 1;
		if (i > 0) {
			const a = seq.tiles[i - 1];               // left neighbour extends forward
			a.w = Math.min(a.w + seq.tiles[i].w, U - a.src);
			seq.tiles.splice(i, 1);
		} else {
			const b = seq.tiles[1];                   // absorb into the right neighbour
			b.src = seq.tiles[0].src;
			b.w   = Math.min(b.w + seq.tiles[0].w, U - b.src);
			seq.tiles.splice(0, 1);
		}
		renderSequencer();
	};

	// Split tile i in half on the unit grid (needs w ≥ 2) — the way back up to more
	// slices. Left half keeps the colour; right half gets a fresh one.
	const splitTile = (i) => {
		const t = seq.tiles[i];
		if (!t || t.w < 2) return;
		const left = Math.floor(t.w / 2);
		seq.tiles.splice(i, 1,
			{ src: t.src,        w: left,        offset: t.offset, muted: t.muted, colorIdx: t.colorIdx },
			{ src: t.src + left, w: t.w - left,  offset: t.offset, muted: t.muted, colorIdx: nextColorIdx++ });
		renderSequencer();
	};

	// Draw a tile's own slice waveform (the source region [src, src+w) it reads)
	// into its backdrop canvas, in the slice's palette colour. Laid edge-to-edge,
	// the tiles read as one continuous waveform that rearranges with the slices —
	// each tile sits 1:1 over the audio it plays.
	const drawTileWave = (canvas, src, w, color) => {
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
	};

	// Redraw every tile's backdrop (children are tiles in order).
	const redrawTileWaves = () => {
		let idx = 0;
		for (const el of seqStepsRow.children) {
			if (!el.classList.contains('seq-tile')) continue;
			const t = seq.tiles[idx++];
			if (!t) continue;
			drawTileWave(el.querySelector('.seq-tile-wave'), t.src, t.w, tileColor(t));
		}
	};

	const TILE_TITLE = 'Drag: reorder · drag green/red edges: resize · dbl-click: split · shift-click: merge · scroll: pitch';

	const renderSequencer = () => {
		seqStepsRow.innerHTML = '';
		const U    = unitCount() || 1;
		const last = seq.tiles.length - 1;

		for (let i = 0; i < seq.tiles.length; i++) {
			const tile = seq.tiles[i];
			const el   = document.createElement('div');
			el.className        = 'seq-tile';
			el.style.flexGrow   = String(tile.w);  // width ∝ unit span (Σ flexGrow = U)
			el.style.flexBasis  = '0';
			el.title             = TILE_TITLE;
			// Editing is live: reorder/resize/split/merge all mutate seq.tiles, which the
			// transport re-reads each scheduler tick, so they take effect without stopping
			// playback. renderSequencer only rebuilds DOM (no audio calls), and the playing
			// highlight re-attaches on the next visual frame.
			if (tile.muted) el.classList.add('muted');
			if (coarsePointer && handlesTileIdx === i) el.classList.add('handles-visible');

			// Waveform backdrop: this slice's own audio, drawn after layout settles.
			const wave = document.createElement('canvas');
			wave.className = 'seq-tile-wave';
			el.appendChild(wave);

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

			// Mute dot.
			const mute = document.createElement('span');
			mute.className   = 'seq-tile-mute';
			mute.textContent = tile.muted ? '✕' : '●';
			mute.title       = tile.muted ? 'Unmute' : 'Mute';
			mute.addEventListener('pointerdown', (ev) => ev.stopPropagation());
			mute.addEventListener('click', (ev) => {
				ev.stopPropagation();
				tile.muted = !tile.muted;
				renderSequencer();
			});
			el.appendChild(mute);

			// Wheel over a tile nudges its pitch offset (±5), independent of master.
			el.addEventListener('wheel', (ev) => {
				ev.preventDefault();
				const dir = ev.deltaY < 0 ? 1 : -1;
				tile.offset = Math.max(-5, Math.min(5, (tile.offset || 0) + dir));
				updatePitchBadge(badge, tile.offset);
				scheduleSave();
			}, { passive: false });

			// Double-click splits the tile in half.
			el.addEventListener('dblclick', (ev) => {
				ev.preventDefault();
				splitTile(i);
			});

			// Shift-click merges the tile into its neighbour. On touch (no hover), a
			// plain tap reveals this slice's edge handles (and hides the others').
			el.addEventListener('click', (ev) => {
				if (ev.shiftKey) { ev.preventDefault(); mergeTile(i); return; }
				if (coarsePointer) {
					handlesTileIdx = (handlesTileIdx === i) ? null : i;
					for (const c of seqStepsRow.children) c.classList.remove('handles-visible');
					if (handlesTileIdx === i) el.classList.add('handles-visible');
				}
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

				// Find the drop slot under an x coordinate: the tile it's over, and
				// whether the pointer is past that tile's midpoint (→ insert after).
				const dropTargetAt = (clientX) => {
					let idx = 0;
					for (const child of seqStepsRow.children) {
						if (!child.classList.contains('seq-tile')) continue;
						const rect = child.getBoundingClientRect();
						if (clientX < rect.right || idx === seq.tiles.length - 1) {
							return { idx, after: (clientX - rect.left) > rect.width / 2 };
						}
						idx++;
					}
					return { idx: seq.tiles.length - 1, after: true };
				};

				const onMove = (e2) => {
					if (!dragging) {
						if (Math.abs(e2.clientX - startX) < 4) return;   // horizontal threshold
						dragging = true;
						dragFromIdx = i;
						startRect = el.getBoundingClientRect();
						el.classList.add('dragging');
						ghost = makeDragGhost(el);
						document.body.appendChild(ghost);
					}
					clearDropMarkers();
					const { idx, after } = dropTargetAt(e2.clientX);
					const target = seqStepsRow.children[idx];
					if (!target) return;
					target.classList.toggle(after ? 'drop-after' : 'drop-before', true);
					// Snap the ghost to the insertion boundary (X only), clamped to the row
					// so it lands cleanly in the slot it'll occupy rather than trailing the
					// raw cursor.
					const trect   = target.getBoundingClientRect();
					const rowRect = seqStepsRow.getBoundingClientRect();
					let left = after ? trect.right : trect.left;   // insertion boundary
					left = Math.max(rowRect.left, Math.min(left, rowRect.right - startRect.width));
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
					// reorder from also triggering merge / handle-toggle. The timeout clears
					// the trap if, in some path, no click is generated.
					const swallow = (ce) => { ce.stopPropagation(); ce.preventDefault(); };
					window.addEventListener('click', swallow, { capture: true, once: true });
					setTimeout(() => window.removeEventListener('click', swallow, { capture: true }), 0);
					const { idx, after } = dropTargetAt(e2.clientX);
					const to   = idx + (after ? 1 : 0);
					const from = dragFromIdx;
					dragFromIdx = null;
					moveTile(from, to);                 // renderSequencer() inside
				};
				window.addEventListener('pointermove', onMove);
				window.addEventListener('pointerup',   onUp);
			});

			// --- Edge resize: green START (drag back) + red END (drag forward) ---
			// Conserves Σ w: dragging a boundary transfers units between tile i and the
			// tiles on the drag side, CASCADING nearest-first. Donors shrink and, once
			// emptied, are fully ABSORBED (w → 0, dropped on release) — so even a freshly
			// sliced all-width-1 grid (where no tile has spare units to lend) can still
			// grow a step by swallowing its neighbours. Donors give/receive from their
			// TAIL (src anchored), so their leading audio never jumps, and every window
			// stays inside the cut [0, U]. Each handle exists only where there's a region
			// to trade with (start: i>0, end: i<last); the outer edges are pinned to the
			// cut. Loop length is unchanged; the step count drops as steps are absorbed.
			{
				const addHandle = (edge) => {
					const handle = document.createElement('span');
					handle.className = `seq-tile-resize ${edge}`;
					handle.title     = edge === 'start'
						? 'Drag this slice’s start back/forward (borrows from the previous slice)'
						: 'Drag this slice’s end forward/back (borrows from the next slice)';
					handle.draggable = false;
					handle.addEventListener('pointerdown', (ev) => {
						ev.preventDefault();
						ev.stopPropagation();
						resizing = true;       // suppresses the tile's reorder pointerdown
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
						// after it. dU>0 extends forward, pulling from i+1, i+2, … — donors
						// shrink and are fully ABSORBED (w → 0, dropped on release) once
						// consumed, so even an all-width-1 grid has slack to give. dU<0
						// retracts (hands units back to the following tiles' tails). Bounded
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
									seq.tiles[k].w = w0[k] - give;
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

						// START drag: pin tile i's END, move its FRONT, trading units with
						// the tiles before it. dU<0 extends back, pulling from i-1, i-2, …
						// (donors absorbed to w → 0); dU>0 retracts the front (hands units
						// back to the preceding tiles' tails).
						const cascadeStart = (dU) => {
							let want = Math.min(Math.max(dU, -src0[i]), w0[i] - MIN_W);
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
							seq.tiles[i].src = src0[i] + moved;    // end pinned: src+w constant
							seq.tiles[i].w   = w0[i]  - moved;
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

		if (seq.isPlaying) {
			seqStatus.textContent = `Playing slice ${seq.currentTile + 1} of ${seq.tiles.length}`;
		} else {
			seqStatus.textContent = `${seq.tiles.length} slices · ${fmtW(totalUnits())} steps/loop · grid ${U}`;
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
		getTiles:        () => seq.tiles,
		getStepSec:      stepSec,
		getTilePlayback: getTilePlayback,
		isLooping:       () => seq.loop,
		onTileVisual: (tileIndex, src, w, frac) => {
			setPlayingTile(tileIndex);
			if (seq.currentTile !== tileIndex) {
				seq.currentTile = tileIndex;
				seqStatus.textContent = `Playing slice ${tileIndex + 1} of ${seq.tiles.length}`;
			}
			const r = tileRegion(src, w);
			slicer.view.setPlayingRegion(r.start, r.end);
			slicer.view.setIsPlaying(true);
			slicer.view.setPlayheadPosition(frac);
		},
		onStop: () => {
			seq.isPlaying   = false;
			seq.currentTile = -1;
			slicer.sequencerPlaying = false;
			setPlayingTile(-1);
			slicer.view.setIsPlaying(false);
			slicer.view.setPlayingRegion(null);
			slicer.view.setPlayheadPosition(0);
			renderSequencer();
		},
	});

	const startSeqPlayback = async () => {
		if (seq.isPlaying) return;
		if (seq.tiles.length === 0) return;

		seq.isPlaying   = true;
		seq.currentTile = -1;
		slicer.sequencerPlaying = true;
		slicer.stop();        // cancel any free-run playback + its playhead animation
		renderSequencer();    // reflect playing state (disables drag/resize)

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
		scheduleSave();
	});
	// "Reset" the tiles back to the default one-unit-per-tile layout (Σ w === U).
	// Safe mid-play: the transport re-reads seq.tiles each scheduler tick, so swapping
	// the array in place reshapes the loop without stopping playback.
	seqClearBtn.addEventListener('click', () => {
		seq.tiles = defaultTiles(unitCount());
		renderSequencer();
	});

	// Shuffle the play order. A Fisher-Yates pass where each position swaps with a
	// random earlier one only with probability `level` (0..1): the amount knob thus
	// controls how far the result drifts from the current pattern — 0 = no change,
	// 1 = a full shuffle. Only order changes; each tile keeps its src/w/offset/mute,
	// so Σ w (loop length) and the available audio are untouched. Safe mid-play: the
	// transport re-reads seq.tiles each scheduler tick.
	const randomizeTiles = () => {
		const n = seq.tiles.length;
		if (n < 2) return;
		const level = Math.max(0, Math.min(1, randLevel / 100));
		for (let i = n - 1; i > 0; i--) {
			if (Math.random() >= level) continue;
			const j = Math.floor(Math.random() * (i + 1));
			const tmp = seq.tiles[i];
			seq.tiles[i] = seq.tiles[j];
			seq.tiles[j] = tmp;
		}
		renderSequencer();   // also schedules a save
	};
	randomizeBtn.addEventListener('click', randomizeTiles);
	randLevelInput.addEventListener('input', () => {
		randLevel = parseInt(randLevelInput.value, 10) || 0;
		randLevelValue.textContent = randLevel + '%';
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
		subdivisions:   parseInt(subdivisionsSelect.value, 10),
		bpm,
		divisionDenom,
		bpmManual,
		divisionManual,
		volume:         volumeKnob.value,
		pan:            panKnob.value,
		masterPitch,
		randLevel,
		seq:            {
			tiles: seq.tiles.map((t) => ({ src: t.src, w: t.w, offset: t.offset || 0, muted: !!t.muted, colorIdx: t.colorIdx })),
			loop:  seq.loop,
		},
	});
	// The MIDI clock controller drives every slicer through these hooks: setMidiBpm
	// for tempo sync, and seqStart/seqStop/hasTiles for MIDI Start/Continue/Stop.
	slicers.push({
		id, getState, setMidiBpm,
		seqStart: startSeqPlayback,
		seqStop:  stopSeqPlayback,
		hasTiles: () => seq.tiles.length > 0,
	});
	// A slicer added while sync is already live should adopt the current tempo at once.
	if (midiClock.isEnabled() && midiClock.getBpm() != null) setMidiBpm(midiClock.getBpm());

	if (savedState) {
		// Settings that don't need the decoded audio buffer — apply synchronously.
		if (Number.isFinite(savedState.subdivisions)) subdivisionsSelect.value = String(savedState.subdivisions);
		if (Number.isFinite(savedState.start))        startInput.value = savedState.start;
		if (Number.isFinite(savedState.end))          endInput.value   = savedState.end;
		startSlider.value = startInput.value;
		endSlider.value   = endInput.value;
		if (Number.isFinite(savedState.virtualStart)) virtualStart = savedState.virtualStart;
		if (Number.isFinite(savedState.virtualEnd))   virtualEnd   = savedState.virtualEnd;

		if (Number.isFinite(savedState.bpm))           { bpm = savedState.bpm; bpmInput.value = bpm.toFixed(2); }
		if (Number.isFinite(savedState.divisionDenom)) { divisionDenom = savedState.divisionDenom; divisionSelect.value = String(divisionDenom); }
		bpmManual      = !!savedState.bpmManual;
		divisionManual = !!savedState.divisionManual;
		updateBpmResetBtn();

		if (Number.isFinite(savedState.volume)) { volumeKnob.setValue(savedState.volume); slicer.setVolume(savedState.volume); }
		if (Number.isFinite(savedState.pan))    { panKnob.setValue(savedState.pan);       slicer.setPan(savedState.pan); }
		if (Number.isFinite(savedState.masterPitch)) { masterPitch = savedState.masterPitch; pitchKnob.setValue(masterPitch); }
		if (Number.isFinite(savedState.randLevel)) {
			randLevel = Math.max(0, Math.min(100, savedState.randLevel));
			randLevelInput.value       = randLevel;
			randLevelValue.textContent = randLevel + '%';
		}

		if (savedState.seq) {
			// v3 tiles. Pre-v3 saves used a different sequencing model (bare-number /
			// {slice,offset} steps) — those don't map onto variable-width tiles, so we
			// drop them and regenerate defaults after the audio re-slices.
			seq.tiles = Array.isArray(savedState.seq.tiles)
				? savedState.seq.tiles
					.filter((t) => t && Number.isFinite(t.src) && Number.isFinite(t.w) && t.w > 0)
					.map((t, i) => ({ src: t.src, w: t.w, offset: t.offset || 0, muted: !!t.muted, colorIdx: Number.isFinite(t.colorIdx) ? t.colorIdx : i }))
				: [];
			nextColorIdx = seq.tiles.reduce((m, t) => Math.max(m, t.colorIdx), -1) + 1;
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
					slicer.setView(virtualStart, virtualEnd);
					// Re-slice with the saved window/subdivisions. applyRange →
					// deriveTransport recomputes originalBPM and respects bpmManual.
					applyRange(parseFloat(startInput.value), parseFloat(endInput.value), { keepPlayhead: false });
					bpmInput.value = bpm.toFixed(2);
					updateBpmResetBtn();
					sliceBtn.disabled = false;
					// Saved tiles win, but fall back to defaults if absent (pre-v3 save)
					// or any tile falls outside the cut at the restored resolution.
					const Ur = unitCount();
					const valid = seq.tiles.length > 0 &&
						seq.tiles.every((t) => t.w > 0 && t.src >= 0 && t.src + t.w <= Ur);
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
}

// --- Global MIDI clock sync bar ---
// One external clock acts as the master tempo for every slicer. While sync is on,
// the derived BPM is pushed to all slicers (overriding their manual BPM) and MIDI
// Start/Continue/Stop drive their sequencers. See midi-clock.js for the signal.
const midiBar = document.getElementById('midiBar');
let midiEnableChk, midiDeviceSel, midiStatusEl;

function updateMidiStatus(mode) {
	if (!midiStatusEl) return;
	if (mode === 'unavailable' || !midiClock.isSupported) {
		midiStatusEl.textContent = midiClock.isSupported ? 'MIDI access denied' : 'Web MIDI not supported';
		return;
	}
	if (!midiClock.isEnabled()) { midiStatusEl.textContent = 'off'; return; }
	const bpm = midiClock.getBpm();
	if (bpm == null) { midiStatusEl.textContent = 'waiting for clock…'; return; }
	midiStatusEl.textContent = `${midiClock.isRunning() ? '▶' : '⏸'} ${bpm.toFixed(1)} BPM`;
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
		return;
	}
	for (const inp of inputs) {
		const opt = document.createElement('option');
		opt.value = inp.id; opt.textContent = inp.name;
		midiDeviceSel.appendChild(opt);
	}
	midiDeviceSel.disabled = !midiClock.isEnabled();
	if (current) midiDeviceSel.value = current;
}

async function onMidiEnableToggle() {
	if (midiEnableChk.checked) {
		const ok = await midiClock.init();
		if (!ok) { midiEnableChk.checked = false; updateMidiStatus('unavailable'); return; }
		midiClock.setEnabled(true);
		populateMidiDevices();
		// Prefer the saved device if it's still present, else the first input.
		const inputs = midiClock.getInputs();
		const wanted = midiSettings.inputId && inputs.some((i) => i.id === midiSettings.inputId)
			? midiSettings.inputId
			: (inputs[0] ? inputs[0].id : null);
		midiClock.setInput(wanted);
		if (wanted) midiDeviceSel.value = wanted;
	} else {
		midiClock.setEnabled(false);
		for (const s of slicers) s.setMidiBpm && s.setMidiBpm(null);   // release tempo
	}
	midiSettings.enabled = midiEnableChk.checked;
	midiSettings.inputId = midiClock.getInputId();
	scheduleSave();
	updateMidiStatus();
}

function buildMidiBar() {
	if (!midiBar) return;
	midiBar.className = 'midi-bar';

	const title = document.createElement('span');
	title.className   = 'midi-bar-title';
	title.textContent = 'MIDI Clock';

	midiEnableChk      = document.createElement('input');
	midiEnableChk.type = 'checkbox';
	midiEnableChk.id   = 'midiEnable';
	const enableLabel  = document.createElement('label');
	enableLabel.className = 'midi-enable-label';
	enableLabel.appendChild(midiEnableChk);
	enableLabel.appendChild(document.createTextNode(' Sync'));

	midiDeviceSel = document.createElement('select');
	midiDeviceSel.className = 'midi-device-select';
	midiDeviceSel.disabled  = true;

	midiStatusEl = document.createElement('span');
	midiStatusEl.className = 'midi-status';

	if (!midiClock.isSupported) {
		midiEnableChk.disabled = true;
		enableLabel.title      = 'This browser has no Web MIDI support';
	}

	midiBar.appendChild(title);
	midiBar.appendChild(enableLabel);
	midiBar.appendChild(midiDeviceSel);
	midiBar.appendChild(midiStatusEl);

	midiEnableChk.addEventListener('change', onMidiEnableToggle);
	midiDeviceSel.addEventListener('change', () => {
		midiClock.setInput(midiDeviceSel.value || null);
		midiSettings.inputId = midiClock.getInputId();
		scheduleSave();
	});

	updateMidiStatus();
}

// Derived tempo → every slicer follows the master clock.
midiClock.onBpm((bpm) => {
	if (!midiClock.isEnabled()) return;
	for (const s of slicers) s.setMidiBpm && s.setMidiBpm(bpm);
	updateMidiStatus();
});
// Transport: Start restarts from the top, Continue resumes, Stop halts — across all
// slicers that have a sequence.
midiClock.onTransport((ev) => {
	if (!midiClock.isEnabled()) return;
	for (const s of slicers) {
		if (ev === 'stop') { s.seqStop && s.seqStop(); continue; }
		if (!(s.hasTiles && s.hasTiles())) continue;
		if (ev === 'start' && s.seqStop) s.seqStop();   // from the beginning
		s.seqStart && s.seqStart();
	}
	updateMidiStatus();
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
	if (savedAll.midi.enabled && midiClock.isSupported && midiEnableChk) {
		midiEnableChk.checked = true;
		onMidiEnableToggle();
	}
}
