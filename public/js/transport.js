/**
 * Transport: sample-accurate lookahead scheduler for one slicer's sequencer.
 *
 * Implements the Chris Wilson "two clocks" pattern: a coarse setTimeout loop
 * schedules audio events slightly ahead on the AudioContext clock, while a
 * separate requestAnimationFrame loop drives the visuals. The setTimeout loop
 * keeps running when the tab is backgrounded (rAF throttles to ~0), so audio
 * timing stays correct regardless of the visual loop.
 *
 * Timing is ABSOLUTE, against the shared BeatGrid: each tile occupies a fixed
 * span of BEATS (w × stepBeats), and its start/stop times are computed from the
 * grid inside the lookahead loop — never accumulated as seconds. When the grid
 * moves under us (tempo change, external MIDI phase correction, CPU stall),
 * the next wakeup recomputes: tiles that fell entirely into the past are
 * SKIPPED, and a tile already in progress is scheduled late with an OFFSET
 * into its audio, so the audible material stays where the grid says it
 * belongs. That's what keeps multiple slicers — and an external clock —
 * locked without accumulating drift.
 *
 * No DOM dependencies — all visual updates go through callbacks.
 *
 * Tile model: the sequence is an ordered list of variable-width tiles. A tile
 * { src, w, offset } reads `w` units of source starting at unit `src` and
 * occupies `w` steps of playback time. The bar length is fixed (Σ w === unit
 * resolution), so a loop is always exactly one bar regardless of how tiles are
 * resized/reordered.
 *
 * Constructed with:
 *   engine        AudioEngine instance (owns the AudioContext + scheduleBuffer)
 *   grid          BeatGrid singleton — the shared beat↔time mapping
 *   getTiles      () => tile[]   live array of tiles to play
 *   getStepBeats  () => number   beats per unit/step (4 / step division)
 *   getTilePlayback  (tile, stepSec) => { buffer, playbackRate, dur, fill, env } | null
 *                    The fill/pitch policy (lives in main.js): resolves a tile to
 *                    a ready-to-play region buffer + rate (time-stretch + pitch
 *                    shift, cached), or null for a silent slot. The engine just
 *                    plays it, so all DSP stays outside the Transport.
 *   isLooping        () => boolean  whether to wrap at the end
 *   onTileVisual     (tileIndex, src, w, frac) => void   per-frame visual update
 *   onStop           () => void     fired once when playback ends/stops
 */

// Minimum lead time when scheduling a late (skipped-into) tile, so the audio
// thread always has a little margin.
const MIN_LEAD = 0.01;

class Transport {
	constructor(opts) {
		this.engine          = opts.engine;
		this.grid            = opts.grid;
		this.getTiles        = opts.getTiles;
		this.getStepBeats    = opts.getStepBeats;
		this.getTilePlayback = opts.getTilePlayback;
		this.isLooping       = opts.isLooping;
		this.onTileVisual    = opts.onTileVisual;
		this.onStop          = opts.onStop;

		this.lookahead         = 25;    // ms — how often the scheduler wakes up
		this.scheduleAheadTime = 0.1;   // s  — how far ahead to schedule audio
		this.prunePast         = 0.05;  // s  — keep notes this long after they end

		this.isPlaying        = false;
		this.anchorBeat       = 0;      // grid beat where this run's loop position 0 sits
		this.posBeats         = 0;      // beats scheduled so far in this run (monotonic)
		this.startTime        = 0;      // audio-clock time of anchorBeat (for the beat indicator)
		this.tileIndex        = 0;
		this.noteQueue        = [];
		this.schedulerTimerId = null;
		this.visualRafId      = null;
		this._noMoreTiles     = false;  // set when looping is off and we're past the end
	}

	/**
	 * Start playback. `atTime` (optional): an absolute time on the shared
	 * AudioContext clock to anchor loop position 0 to. The master transport
	 * passes ONE such value to every track so they all begin on the same
	 * instant and stay sample-locked. Omitted (individual play) → self-anchor
	 * just ahead of now.
	 *
	 * `anchorBeat` (optional): anchor loop position 0 to an absolute GRID beat
	 * instead — it may lie in the past (a loop boundary already gone by), in
	 * which case the scheduler skips forward into the bar so the track joins
	 * IN PHASE with everything else on the grid.
	 */
	async start(atTime, { anchorBeat } = {}) {
		if (this.isPlaying) return;
		const tiles = this.getTiles();
		if (!tiles || tiles.length === 0) return;

		// Browser autoplay policy: the context starts suspended until a gesture.
		await this.engine.resume();

		this.isPlaying    = true;
		this.tileIndex    = 0;
		this.posBeats     = 0;
		this.noteQueue    = [];
		this._noMoreTiles = false;

		const now = this.engine.audioContext.currentTime;
		if (typeof anchorBeat === 'number') {
			this.anchorBeat = anchorBeat;
		} else {
			const t0 = (typeof atTime === 'number' && atTime > now) ? atTime : now + 0.05;
			if (!this.grid.running) this.grid.restart(t0);   // first track up anchors the grid
			this.anchorBeat = this.grid.beatAtTime(t0);
		}
		this.startTime = this.grid.timeAtBeat(this.anchorBeat);

		this._scheduler();
		this._visualLoop();
	}

	_scheduler() {
		if (!this.isPlaying) return;
		const ctx = this.engine.audioContext;

		// Everything is recomputed from the grid each pass, so tempo changes and
		// phase corrections that happened since the last wakeup are picked up here.
		for (;;) {
			if (this._noMoreTiles) break;
			const now   = ctx.currentTime;
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
			const stepBeats = this.getStepBeats();
			const tile      = tiles[this.tileIndex];
			const w         = (tile && tile.w > 0) ? tile.w : 1;
			const tileBeats = w * stepBeats;
			const startT    = this.grid.timeAtBeat(this.anchorBeat + this.posBeats);
			const stopT     = this.grid.timeAtBeat(this.anchorBeat + this.posBeats + tileBeats);

			if (startT >= now + this.scheduleAheadTime) break;   // beyond the lookahead window

			if (stopT <= now + MIN_LEAD) {
				// The whole tile is already in the past (late join, grid jumped, or a
				// stall) — skip it outright; the grid says its moment is gone.
				this.posBeats += tileBeats;
				this.tileIndex++;
				continue;
			}

			// Late partial tile: start it now-ish, offset INTO its audio by how much
			// of it the grid says has already elapsed, so material stays in place.
			const lateBy = Math.max(0, (now + MIN_LEAD) - startT);
			this._scheduleTile(this.tileIndex, tile, startT, stopT, w, lateBy);
			this.posBeats += tileBeats;
			this.tileIndex++;
		}

		this.schedulerTimerId = setTimeout(() => this._scheduler(), this.lookahead);
	}

	_scheduleTile(i, tile, when, stopAt, w, lateBy) {
		// The policy resolves the tile to a ready buffer + rate (or null = silent).
		// We always record the time slot so visuals + timing advance even on silence
		// (a muted or out-of-range tile is a silent slot, not a skipped one). The
		// visual playhead sweeps across the tile's source region [src, src+w) over
		// its whole slot.
		const src = tile ? tile.src : 0;
		// Effective seconds-per-step for THIS slot, derived from its grid span, so
		// the fill/stretch policy always fills exactly the time the grid allotted
		// (even if tempo changed since the previous tile).
		const stepSec = (stopAt - when) / w;
		const r = tile ? this.getTilePlayback(tile, stepSec) : null;
		if (r && r.buffer) {
			// A late start plays the buffer from the equivalent position inside it:
			// wall-clock lateness × playbackRate = buffer seconds already elapsed.
			const offsetSec = lateBy > 0 ? lateBy * (r.playbackRate || 1) : 0;
			this.engine.scheduleBuffer(r.buffer, when + lateBy, stopAt, r.playbackRate || 1, {
				declick: !!r.fill,
				env:     r.env || null,
				offsetSec,
			});
		}
		this.noteQueue.push({ tileIndex: i, src, w, time: when, stopAt });
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
		this.posBeats     = 0;
		this._noMoreTiles = false;
		if (wasPlaying && this.onStop) this.onStop();
	}
}

export default Transport;
