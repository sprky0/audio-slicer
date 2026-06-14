/**
 * Transport: sample-accurate lookahead scheduler for one slicer's sequencer.
 *
 * Implements the Chris Wilson "two clocks" pattern: a coarse setTimeout loop
 * schedules audio events slightly ahead on the AudioContext clock, while a
 * separate requestAnimationFrame loop drives the visuals. The setTimeout loop
 * keeps running when the tab is backgrounded (rAF throttles to ~0), so audio
 * timing stays correct regardless of the visual loop.
 *
 * No DOM dependencies — all visual updates go through callbacks.
 *
 * Tile model: the sequence is an ordered list of variable-width tiles. A tile
 * { src, w, offset } reads `w` units of source starting at unit `src` and
 * occupies `w` steps of playback time (w * stepSec). The bar length is fixed
 * (Σ w === unit resolution), so a loop is always exactly one bar regardless of
 * how tiles are resized/reordered.
 *
 * Constructed with:
 *   engine        AudioEngine instance (owns the AudioContext + scheduleBuffer)
 *   getTiles      () => tile[]   live array of tiles to play
 *   getStepSec       () => number   seconds per unit/step (from BPM + division)
 *   getTilePlayback  (tile, stepSec) => { buffer, playbackRate, dur, fill } | null
 *                    The fill/pitch policy (lives in main.js): resolves a tile to
 *                    a ready-to-play region buffer + rate (time-stretch + pitch
 *                    shift, cached), or null for a silent slot. The engine just
 *                    plays it, so all DSP stays outside the Transport.
 *   isLooping        () => boolean  whether to wrap at the end
 *   onTileVisual     (tileIndex, src, w, frac) => void   per-frame visual update
 *   onStop           () => void     fired once when playback ends/stops
 */
class Transport {
	constructor(opts) {
		this.engine          = opts.engine;
		this.getTiles        = opts.getTiles;
		this.getStepSec      = opts.getStepSec;
		this.getTilePlayback = opts.getTilePlayback;
		this.isLooping       = opts.isLooping;
		this.onTileVisual    = opts.onTileVisual;
		this.onStop          = opts.onStop;

		this.lookahead         = 25;    // ms — how often the scheduler wakes up
		this.scheduleAheadTime = 0.1;   // s  — how far ahead to schedule audio
		this.prunePast         = 0.05;  // s  — keep notes this long after they end

		this.isPlaying        = false;
		this.nextStepTime     = 0;
		this.startTime        = 0;      // audio-clock time the current run began (for the beat clock)
		this.tileIndex        = 0;
		this.noteQueue        = [];
		this.schedulerTimerId = null;
		this.visualRafId      = null;
		this._noMoreTiles     = false;  // set when looping is off and we're past the end
	}

	async start() {
		if (this.isPlaying) return;
		const tiles = this.getTiles();
		if (!tiles || tiles.length === 0) return;

		// Browser autoplay policy: the context starts suspended until a gesture.
		await this.engine.resume();

		this.isPlaying    = true;
		this.tileIndex    = 0;
		this.noteQueue    = [];
		this._noMoreTiles = false;
		// Small offset so the first tile isn't scheduled in the past.
		this.nextStepTime = this.engine.audioContext.currentTime + 0.05;
		// Anchor the beat clock to the first scheduled step so the indicator's beats
		// line up with the bar (a loop is one bar of 4 beats).
		this.startTime    = this.nextStepTime;

		this._scheduler();
		this._visualLoop();
	}

	_scheduler() {
		if (!this.isPlaying) return;
		const ctx = this.engine.audioContext;

		while (!this._noMoreTiles && this.nextStepTime < ctx.currentTime + this.scheduleAheadTime) {
			const tiles = this.getTiles();
			const len   = tiles.length;
			// Tiles may have been cleared/shrunk mid-play — re-read length each iter.
			if (len === 0) {
				this._noMoreTiles = true;
				break;
			}
			if (this.tileIndex >= len) {
				if (this.isLooping()) {
					this.tileIndex = 0;
				} else {
					this._noMoreTiles = true;
					break;
				}
			}
			const stepSec = this.getStepSec();
			const tile    = tiles[this.tileIndex];
			const w       = (tile && tile.w > 0) ? tile.w : 1;
			const dur     = w * stepSec;
			this._scheduleTile(this.tileIndex, tile, this.nextStepTime, dur, stepSec);
			// Advance the clock continuously across loop wraps — never snap back
			// to currentTime, or drift creeps back in.
			this.nextStepTime += dur;
			this.tileIndex++;
		}

		this.schedulerTimerId = setTimeout(() => this._scheduler(), this.lookahead);
	}

	_scheduleTile(i, tile, when, dur, stepSec) {
		// The policy resolves the tile to a ready buffer + rate (or null = silent).
		// We always record the time slot so visuals + timing advance even on silence
		// (a muted or out-of-range tile is a silent slot, not a skipped one). The
		// visual playhead sweeps across the tile's source region [src, src+w) over
		// its whole `dur`.
		const src = tile ? tile.src : 0;
		const w   = (tile && tile.w > 0) ? tile.w : 1;
		const r   = tile ? this.getTilePlayback(tile, stepSec) : null;
		if (!r) {
			this.noteQueue.push({ tileIndex: i, src, w, time: when, stopAt: when + dur });
			return;
		}
		if (r.buffer) {
			this.engine.scheduleBuffer(r.buffer, when, when + dur, r.playbackRate || 1, { declick: !!r.fill });
		}
		this.noteQueue.push({ tileIndex: i, src, w, time: when, stopAt: when + dur });
	}

	_visualLoop() {
		if (!this.isPlaying) return;
		const ctx = this.engine.audioContext;
		const now = ctx.currentTime;

		// Drop notes that finished a while ago to keep the queue bounded.
		while (this.noteQueue.length && this.noteQueue[0].stopAt < now - this.prunePast) {
			this.noteQueue.shift();
		}

		// Current note = the last one whose start time has arrived (queue is time-ordered).
		let current = null;
		for (let k = 0; k < this.noteQueue.length; k++) {
			if (this.noteQueue[k].time <= now) current = this.noteQueue[k];
			else break;
		}
		if (current) {
			const dur  = current.stopAt - current.time;
			const frac = dur > 0 ? Math.min(1, Math.max(0, (now - current.time) / dur)) : 0;
			this.onTileVisual(current.tileIndex, current.src, current.w, frac);
		}

		// Done: scheduling is finished and the tail has played out.
		const lastNote = this.noteQueue[this.noteQueue.length - 1];
		if (this._noMoreTiles && (!lastNote || lastNote.stopAt <= now)) {
			this.stop();
			return;
		}

		this.visualRafId = requestAnimationFrame(() => this._visualLoop());
	}

	stop() {
		const wasPlaying = this.isPlaying;
		this.isPlaying = false;
		if (this.schedulerTimerId) {
			clearTimeout(this.schedulerTimerId);
			this.schedulerTimerId = null;
		}
		if (this.visualRafId) {
			cancelAnimationFrame(this.visualRafId);
			this.visualRafId = null;
		}
		this.engine.cancelScheduled();
		this.noteQueue    = [];
		this.tileIndex    = 0;
		this._noMoreTiles = false;
		if (wasPlaying && this.onStop) this.onStop();
	}
}

export default Transport;
