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
 *   getTilePlayback  (tile, stepSec, ctx) => { buffer, playbackRate, dur, fill, env } | null
 *                    The fill/pitch policy (lives in main.js): resolves a tile to
 *                    a ready-to-play region buffer + rate (time-stretch + pitch
 *                    shift, cached), or null for a silent slot. The engine just
 *                    plays it, so all DSP stays outside the Transport. `ctx` =
 *                    { posSteps, when }: the slot's position in steps since this
 *                    run began and its audible start time — the policy uses it to
 *                    resolve step modifiers (mute/reverse overrides).
 *   beforeTile       (posSteps, when) => void   optional; called just before a
 *                    slot that WILL sound is scheduled (never for skipped-past
 *                    tiles). The policy may mutate/replace the tile list here
 *                    (pattern-action modifiers) — the slot's tile is re-read
 *                    afterwards, so a change lands on this very slot.
 *   resolveRatchet   (tile, posSteps, when) =>
 *                    { mode, subdiv, subdivTo, pitchStep, lenSteps } | null
 *                    optional; rolled once per sounding slot. Non-null turns the
 *                    slot into a ratchet: the tile RETRIGGERS across `lenSteps`
 *                    steps of grid time, and the span absorbs every following
 *                    tile that starts inside it. `mode` picks the hit layout —
 *                    'even' (default): `subdiv` uniform hits per step; 'ramp':
 *                    spacing morphs from `subdiv` to `subdivTo` hits/step over
 *                    the span; 'pitch': even spacing, each successive hit
 *                    shifted `pitchStep` semitones (varispeed). See
 *                    ratchetHitSteps / ratchetHitRate below.
 *   isLooping        () => boolean  whether to wrap at the end
 *   onTileVisual     (tileIndex, src, w, frac, ratchet) => void   per-frame
 *                    visual update. `ratchet` is null for normal slots; for a
 *                    ratchet span it carries { step, hits, hitIndex, hitFrac,
 *                    wTile, hitRegionFrac, posSteps } so the UI can restart its
 *                    playhead per hit, light the firing modifier, and grey out
 *                    modifiers superseded by the span (w = consumed steps;
 *                    steps beyond wTile never resolve their own modifiers).
 *   onStop           () => void     fired once when playback ends/stops
 */

// Minimum lead time when scheduling a late (skipped-into) tile, so the audio
// thread always has a little margin.
const MIN_LEAD = 0.01;

// --- Ratchet hit layout (shared with the WAV export for bit-faithful renders) ---

// Backstop so a degenerate span can't schedule unbounded voices (the UI maxes
// out at 16 steps × 8 hits = 128).
const MAX_HITS = 128;

// Hit start positions in STEPS from the span start, per the ratchet's mode.
// 'ramp' spacing follows the instantaneous density a + (b − a)·(pos/span)
// hits/step, so the rhythm accelerates (a < b) or decelerates (a > b) smoothly
// across the span; the last hit is cut at the span end like any other.
export function ratchetHitSteps(rt, spanSteps) {
	const EPS = 1e-6;
	const a = Math.max(1, Math.round(rt.subdiv || 1));
	if (rt.mode === 'ramp') {
		const b = Math.max(1, Math.round(rt.subdivTo || a));
		const out = [];
		let pos = 0;
		while (pos < spanSteps - EPS && out.length < MAX_HITS) {
			out.push(pos);
			pos += 1 / (a + (b - a) * (pos / spanSteps));
		}
		return out;
	}
	const hits = Math.min(MAX_HITS, Math.max(1, Math.floor(spanSteps * a + EPS)));
	return Array.from({ length: hits }, (_, k) => k / a);
}

// Per-hit playbackRate multiplier: 'pitch' mode shifts hit k by k·pitchStep
// semitones as varispeed (duration changes with pitch; the cut at the next hit
// keeps time). Cumulative shift clamped to ±48 st so rates stay sane.
export function ratchetHitRate(rt, k) {
	if (rt.mode !== 'pitch') return 1;
	const st = Math.max(-48, Math.min(48, k * (rt.pitchStep || 0)));
	return st === 0 ? 1 : Math.pow(2, st / 12);
}

class Transport {
	constructor(opts) {
		this.engine          = opts.engine;
		this.grid            = opts.grid;
		this.getTiles        = opts.getTiles;
		this.getStepBeats    = opts.getStepBeats;
		this.getTilePlayback = opts.getTilePlayback;
		this.beforeTile      = opts.beforeTile || null;
		this.resolveRatchet  = opts.resolveRatchet || null;
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
			let tile        = tiles[this.tileIndex];
			let w           = (tile && tile.w > 0) ? tile.w : 1;
			let tileBeats   = w * stepBeats;
			const startT    = this.grid.timeAtBeat(this.anchorBeat + this.posBeats);
			let stopT       = this.grid.timeAtBeat(this.anchorBeat + this.posBeats + tileBeats);

			if (startT >= now + this.scheduleAheadTime) break;   // beyond the lookahead window

			if (stopT <= now + MIN_LEAD) {
				// The whole tile is already in the past (late join, grid jumped, or a
				// stall) — skip it outright; the grid says its moment is gone.
				this.posBeats += tileBeats;
				this.tileIndex++;
				continue;
			}

			// This slot will sound: give the policy a chance to fire pattern-action
			// modifiers first. The hook may replace/reorder the tile list, so re-read
			// this slot afterwards — a randomize on the loop's first step reshapes the
			// loop INCLUDING that first slot.
			const posSteps = stepBeats > 0 ? this.posBeats / stepBeats : 0;
			if (this.beforeTile) {
				this.beforeTile(posSteps, startT);
				const fresh = this.getTiles();
				if (this.tileIndex < fresh.length) {
					tile      = fresh[this.tileIndex];
					w         = (tile && tile.w > 0) ? tile.w : 1;
					tileBeats = w * stepBeats;
					stopT     = this.grid.timeAtBeat(this.anchorBeat + this.posBeats + tileBeats);
				}
			}

			// Ratchet slot: the policy rolled a retrigger for this tile — it replaces
			// the single voice with subdiv-per-step retriggers and may consume the
			// tiles that follow (they start inside the ratchet's span).
			if (this.resolveRatchet) {
				const rt = this.resolveRatchet(tile, posSteps, startT);
				if (rt) {
					const { consumedBeats, nextIndex } =
						this._scheduleRatchet(this.tileIndex, tile, w, stepBeats, rt, posSteps);
					this.posBeats += consumedBeats;
					this.tileIndex = nextIndex;
					continue;
				}
			}

			// Late partial tile: start it now-ish, offset INTO its audio by how much
			// of it the grid says has already elapsed, so material stays in place.
			const lateBy = Math.max(0, (now + MIN_LEAD) - startT);
			this._scheduleTile(this.tileIndex, tile, startT, stopT, w, lateBy, posSteps);
			this.posBeats += tileBeats;
			this.tileIndex++;
		}

		this.schedulerTimerId = setTimeout(() => this._scheduler(), this.lookahead);
	}

	// Ratchet slot: schedule grid-locked retriggers across `lenSteps` steps
	// (clamped to the end of the bar, so the span never crosses the loop wrap),
	// laid out per the ratchet's mode (see ratchetHitSteps). Each hit replays the
	// tile's audio from the top and is cut at the next hit. The span absorbs
	// every following tile that STARTS inside it — their time belongs to this
	// slot. Returns the beats consumed + the next tile index, so the caller
	// advances past the absorbed tiles.
	_scheduleRatchet(i, tile, w, stepBeats, rt, posSteps) {
		const EPS   = 1e-6;
		const now   = this.engine.audioContext.currentTime;
		const tiles = this.getTiles();
		let remaining = 0;
		for (let k = i; k < tiles.length; k++) remaining += (tiles[k].w > 0 ? tiles[k].w : 1);
		const spanSteps = Math.min(rt.lenSteps > 0 ? rt.lenSteps : 1, remaining);
		const base      = this.anchorBeat + this.posBeats;
		const startT    = this.grid.timeAtBeat(base);
		const tileStopT = this.grid.timeAtBeat(base + w * stepBeats);
		const spanEndT  = this.grid.timeAtBeat(base + spanSteps * stepBeats);
		const hitSteps  = ratchetHitSteps(rt, spanSteps);
		const hits      = hitSteps.length;
		// Same stretch policy as a normal slot (stepSec from the tile's own grid
		// span); the envelope is scaled to ONE (average) HIT so per-slice fades
		// shape each retrigger instead of stretching across the whole span.
		const stepSec = (tileStopT - startT) / w;
		const r = this.getTilePlayback(tile, stepSec, {
			posSteps, when: startT, envDurSec: (spanEndT - startT) / hits,
		});
		if (r && r.buffer) {
			for (let k = 0; k < hits; k++) {
				const h0 = this.grid.timeAtBeat(base + hitSteps[k] * stepBeats);
				const h1 = (k + 1 < hits)
					? Math.min(this.grid.timeAtBeat(base + hitSteps[k + 1] * stepBeats), spanEndT)
					: spanEndT;
				if (h1 <= now + MIN_LEAD) continue;   // this hit's moment is already gone
				const rate   = (r.playbackRate || 1) * ratchetHitRate(rt, k);
				const lateBy = Math.max(0, (now + MIN_LEAD) - h0);
				this.engine.scheduleBuffer(r.buffer, h0 + lateBy, h1, rate, {
					declick:   true,   // retrigger cuts are hard by design — always declick
					env:       r.env || null,
					offsetSec: lateBy > 0 ? lateBy * rate : 0,
				});
			}
		}
		// Absorb the tiles that start inside the span.
		let consumedSteps = w, j = i + 1;
		while (j < tiles.length && consumedSteps < spanSteps - EPS) {
			consumedSteps += (tiles[j].w > 0 ? tiles[j].w : 1);
			j++;
		}
		const stopAt = this.grid.timeAtBeat(base + consumedSteps * stepBeats);
		// Tag the note so the visual loop can derive the current hit per frame:
		// hitBounds = each hit's start as a fraction of the note's full duration
		// (hits aren't uniform in ramp mode), closed by the span end;
		// wTile/hitRegionFrac let the playhead sweep only the source a hit
		// actually consumes (of the tile's w-step region) then restart.
		const hitBounds = hitSteps.map((s) => s / consumedSteps);
		hitBounds.push(Math.min(1, spanSteps / consumedSteps));
		this.noteQueue.push({
			tileIndex: i, src: tile ? tile.src : 0, w: consumedSteps, time: startT, stopAt,
			ratchet: {
				hits,
				hitBounds,
				step:          Number.isFinite(rt.step) ? rt.step : -1,
				wTile:         w,
				hitRegionFrac: Math.min(1, (spanSteps / hits) / w),
				posSteps,
			},
		});
		return { consumedBeats: consumedSteps * stepBeats, nextIndex: j };
	}

	_scheduleTile(i, tile, when, stopAt, w, lateBy, posSteps) {
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
		const r = tile ? this.getTilePlayback(tile, stepSec, { posSteps, when }) : null;
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
			let rt = null;
			if (current.ratchet) {
				// Locate the current hit by its boundary fractions (ramp-mode hits
				// aren't uniform), then normalise frac within it.
				const rc = current.ratchet;
				const b  = rc.hitBounds;
				let hitIndex = 0;
				while (hitIndex + 1 < rc.hits && frac >= b[hitIndex + 1]) hitIndex++;
				const span = b[hitIndex + 1] - b[hitIndex];
				rt = {
					step:          rc.step,
					hits:          rc.hits,
					hitIndex,
					hitFrac:       span > 0 ? Math.min(1, (frac - b[hitIndex]) / span) : 0,
					wTile:         rc.wTile,
					hitRegionFrac: rc.hitRegionFrac,
					posSteps:      rc.posSteps,
				};
			}
			this.onTileVisual(current.tileIndex, current.src, current.w, frac, rt);
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
