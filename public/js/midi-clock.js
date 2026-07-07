/**
 * MidiClock: receives MIDI realtime messages from a chosen input and turns them
 * into a tempo + transport signal the app can sync to.
 *
 * MIDI clock is 24 pulses per quarter note (PPQN). We time the gaps between
 * 0xF8 pulses and average over a window to derive a stable BPM, and surface
 * 0xFA / 0xFB / 0xFC (Start / Continue / Stop) as transport events.
 *
 * We sync TEMPO, not individual steps. The app's scheduler keeps its own
 * sample-accurate lookahead clock and just reads the derived BPM, so the jitter
 * inherent in event-loop-delivered MIDI messages never reaches the audio.
 *
 * No DOM dependencies — consumers subscribe via onBpm / onTransport / onState.
 */

const CLOCK    = 0xF8;   // timing clock (24 per quarter note)
const START    = 0xFA;   // play from the beginning
const CONTINUE = 0xFB;   // resume from the current position
const STOP      = 0xFC;  // stop

// A gap longer than this between pulses means the clock paused/restarted, so the
// timing window is stale — drop it and start measuring afresh.
const STALE_GAP_MS = 500;
// Pulses kept for averaging: 48 intervals ≈ two quarter notes — enough to smooth
// jitter without lagging real tempo changes.
const WINDOW = 49;
// Only push a new BPM when it moved at least this much, to avoid thrashing
// consumers (and the time-stretch cache) on sub-perceptual wobble.
const BPM_EPSILON = 0.1;

class MidiClock {
	constructor() {
		this.access     = null;
		this.input      = null;    // the MIDIInput we're currently listening to
		this.enabled    = false;
		this.running    = false;   // between a Start/Continue and the next Stop
		this.bpm        = null;    // last derived tempo, or null until enough pulses
		this._clocks    = 0;       // 0xF8 pulses since the last Start (24 per beat)
		this._stamps    = [];      // recent pulse timestamps (ms) for averaging
		this._onMessage = (e) => this._handle(e);
		this._listeners = { bpm: new Set(), transport: new Set(), state: new Set() };
	}

	get isSupported() {
		return typeof navigator !== 'undefined' && typeof navigator.requestMIDIAccess === 'function';
	}

	// Request MIDI access (lazily, on first enable). Resolves true on success.
	async init() {
		if (this.access) return true;
		if (!this.isSupported) return false;
		try {
			this.access = await navigator.requestMIDIAccess({ sysex: false });
			// Devices can be hot-plugged; re-emit so the UI refreshes its list.
			this.access.onstatechange = () => this._emit('state');
			return true;
		} catch (_) {
			this.access = null;
			return false;
		}
	}

	getInputs() {
		if (!this.access) return [];
		return Array.from(this.access.inputs.values()).map((i) => ({ id: i.id, name: i.name || i.id }));
	}

	getInputId() { return this.input ? this.input.id : null; }

	// Attach to the input with the given id (or detach when id is falsy).
	setInput(id) {
		if (this.input) {
			this.input.removeEventListener('midimessage', this._onMessage);
			this.input = null;
		}
		this._resetTiming();
		if (id && this.access) {
			const next = this.access.inputs.get(id);
			if (next) {
				this.input = next;
				this.input.addEventListener('midimessage', this._onMessage);
			}
		}
		this._emit('state');
	}

	setEnabled(on) {
		this.enabled = !!on;
		if (!this.enabled) { this.running = false; this._resetTiming(); }
		this._emit('state');
	}

	isEnabled() { return this.enabled; }
	isRunning() { return this.running; }
	getBpm()    { return this.bpm; }

	// Beat position for the indicator: 24 clocks per beat, 4 beats per bar. `index`
	// counts beats since the last Start (so index % 4 is the bar position); `phase`
	// is 0..1 within the current beat. null until a clock is actually arriving.
	getBeat() {
		if (this.bpm == null) return null;
		return { index: Math.floor(this._clocks / 24), phase: (this._clocks % 24) / 24, running: this.running };
	}

	onBpm(cb)       { return this._sub('bpm', cb); }
	onTransport(cb) { return this._sub('transport', cb); }
	onState(cb)     { return this._sub('state', cb); }

	_sub(kind, cb) {
		this._listeners[kind].add(cb);
		return () => this._listeners[kind].delete(cb);
	}
	_emit(kind, arg) { this._listeners[kind].forEach((cb) => cb(arg)); }

	_resetTiming() { this._stamps = []; this.bpm = null; this._clocks = 0; }

	_handle(e) {
		if (!this.enabled) return;
		const status = e.data[0];
		switch (status) {
			case CLOCK:    this._onPulse(e.timeStamp || performance.now()); break;
			case START:    this.running = true;  this._resetTiming(); this._emit('transport', 'start');    break;
			case CONTINUE: this.running = true;                        this._emit('transport', 'continue'); break;
			case STOP:     this.running = false;                       this._emit('transport', 'stop');     break;
			default: break;   // note/CC/active-sensing/etc. — not our concern
		}
	}

	_onPulse(now) {
		this._clocks++;
		const stamps = this._stamps;
		const last   = stamps[stamps.length - 1];
		// A long silence means the previous window is stale (clock was paused).
		if (last !== undefined && now - last > STALE_GAP_MS) stamps.length = 0;
		stamps.push(now);
		if (stamps.length > WINDOW) stamps.shift();
		if (stamps.length < 2) return;

		// Average pulse interval across the window → BPM (24 pulses per quarter).
		const span    = stamps[stamps.length - 1] - stamps[0];
		const avgIvl  = span / (stamps.length - 1);
		if (avgIvl <= 0) return;
		const bpm = 60000 / (avgIvl * 24);
		if (!(bpm > 0) || !isFinite(bpm)) return;

		if (this.bpm === null || Math.abs(bpm - this.bpm) >= BPM_EPSILON) {
			this.bpm = bpm;
			this._emit('bpm', bpm);
		}
	}
}

export default new MidiClock();
