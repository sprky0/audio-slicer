/**
 * beat-grid.js — the shared musical timebase (process-wide singleton).
 *
 * One linear mapping between BEATS (quarter notes) and the shared AudioContext
 * clock: timeAtBeat / beatAtTime through { originTime, originBeat, bpm }. Every
 * Transport schedules its tiles at absolute grid positions instead of
 * accumulating per-tile durations, so:
 *
 *  - a tempo change re-anchors the grid ONCE (phase-continuous at the change
 *    instant) and every track corrects to the same line — no relative drift;
 *  - a late-started track can anchor to a loop boundary in the past and the
 *    scheduler skips forward INTO the bar, joining in phase;
 *  - an external MIDI clock can steer the grid (setTempo + nudge/syncPhase from
 *    a PLL) and every track chases it the same way.
 *
 * The grid itself is passive — it never ticks. Transports read it inside their
 * lookahead loops, so a change takes effect within one scheduler wakeup.
 */

class BeatGrid {
	constructor() {
		this.bpm        = 120;
		this.originTime = 0;      // audio-clock seconds where originBeat falls
		this.originBeat = 0;
		this.running    = false;  // an anchor has been established (Play All / MIDI start)
	}

	timeAtBeat(b) { return this.originTime + (b - this.originBeat) * (60 / this.bpm); }
	beatAtTime(t) { return this.originBeat + (t - this.originTime) * (this.bpm / 60); }

	/** (Re)start the grid: beat 0 lands exactly at `atTime` (master Play All). */
	restart(atTime) {
		this.originTime = atTime;
		this.originBeat = 0;
		this.running    = true;
	}

	stop() { this.running = false; }

	/** Change tempo, preserving the beat phase at `atTime` (no jump). */
	setTempo(bpm, atTime) {
		if (!(bpm > 0) || !isFinite(bpm)) return;
		if (typeof atTime === 'number') {
			this.originBeat = this.beatAtTime(atTime);
			this.originTime = atTime;
		}
		this.bpm = bpm;
	}

	/** Hard external anchor: beat `beat` occurred at audio time `time` (MIDI Start). */
	syncPhase(beat, time, bpm) {
		if (bpm > 0 && isFinite(bpm)) this.bpm = bpm;
		this.originBeat = beat;
		this.originTime = time;
		this.running    = true;
	}

	/**
	 * Slide the grid origin by `dt` seconds without touching tempo — the PLL's
	 * phase correction. Negative dt pulls the grid earlier (we were running
	 * late); transports react by skipping forward inside the current slice.
	 */
	nudge(dt) {
		if (isFinite(dt)) this.originTime += dt;
	}
}

const grid = new BeatGrid();
export default grid;
