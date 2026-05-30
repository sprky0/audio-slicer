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
 * Constructed with:
 *   engine        AudioEngine instance (owns the AudioContext + scheduleSegment)
 *   getSteps      () => number[]   live array of slice indices to play
 *   getStepSec       () => number   seconds per step (from BPM + division)
 *   getPlaybackRate  (sliceIdx) => number   per-step playback-rate policy (the
 *                    "fill" strategy — see main.js). Defaults to 1 (natural).
 *                    This is the seam for alternate fill modes (gap, repitch,
 *                    time-stretch, …): the engine just applies the returned
 *                    rate, the policy lives outside.
 *   isLooping        () => boolean  whether to wrap at the end
 *   onStepVisual     (step, sliceIdx, frac) => void   per-frame visual update
 *   onStop           () => void     fired once when playback ends/stops
 */
class Transport {
	constructor(opts) {
		this.engine          = opts.engine;
		this.getSteps        = opts.getSteps;
		this.getStepSec      = opts.getStepSec;
		this.getPlaybackRate = opts.getPlaybackRate || (() => 1);
		this.isLooping       = opts.isLooping;
		this.onStepVisual    = opts.onStepVisual;
		this.onStop          = opts.onStop;

		this.lookahead         = 25;    // ms — how often the scheduler wakes up
		this.scheduleAheadTime = 0.1;   // s  — how far ahead to schedule audio
		this.prunePast         = 0.05;  // s  — keep notes this long after they end

		this.isPlaying        = false;
		this.nextStepTime     = 0;
		this.stepIndex        = 0;
		this.noteQueue        = [];
		this.schedulerTimerId = null;
		this.visualRafId      = null;
		this._noMoreSteps     = false;  // set when looping is off and we're past the end
	}

	async start() {
		if (this.isPlaying) return;
		const steps = this.getSteps();
		if (!steps || steps.length === 0) return;

		// Browser autoplay policy: the context starts suspended until a gesture.
		await this.engine.resume();

		this.isPlaying    = true;
		this.stepIndex    = 0;
		this.noteQueue    = [];
		this._noMoreSteps = false;
		// Small offset so the first step isn't scheduled in the past.
		this.nextStepTime = this.engine.audioContext.currentTime + 0.05;

		this._scheduler();
		this._visualLoop();
	}

	_scheduler() {
		if (!this.isPlaying) return;
		const ctx = this.engine.audioContext;

		while (!this._noMoreSteps && this.nextStepTime < ctx.currentTime + this.scheduleAheadTime) {
			const steps = this.getSteps();
			const len   = steps.length;
			// Steps may have been cleared/shrunk mid-play — re-read length each iter.
			if (len === 0) {
				this._noMoreSteps = true;
				break;
			}
			if (this.stepIndex >= len) {
				if (this.isLooping()) {
					this.stepIndex = 0;
				} else {
					this._noMoreSteps = true;
					break;
				}
			}
			const stepSec = this.getStepSec();
			this._scheduleStep(this.stepIndex, this.nextStepTime, stepSec);
			// Advance the clock continuously across loop wraps — never snap back
			// to currentTime, or drift creeps back in.
			this.nextStepTime += stepSec;
			this.stepIndex++;
		}

		this.schedulerTimerId = setTimeout(() => this._scheduler(), this.lookahead);
	}

	_scheduleStep(i, when, stepSec) {
		const steps    = this.getSteps();
		const sliceIdx = steps[i];
		const segs     = this.engine.getSegments();
		const enabled  = this.engine.getEnabledSegments();

		// Schedule audio only if the slice index is in range and enabled. Either
		// way we still record the time slot below so visuals + timing advance
		// (an out-of-range or disabled step is a silent slot, not a skipped one).
		// effDur = how long the slice is actually audible (buffer length scaled by
		// playback rate); the visual playhead sweeps over this, not the raw step.
		let effDur = stepSec;
		if (segs && sliceIdx >= 0 && sliceIdx < segs.length && enabled[sliceIdx]) {
			const rate = this.getPlaybackRate(sliceIdx) || 1;
			this.engine.scheduleSegment(sliceIdx, when, when + stepSec, rate);
			effDur = segs[sliceIdx].duration / rate;
		}
		this.noteQueue.push({ step: i, sliceIdx, time: when, stopAt: when + stepSec, effDur });
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
			const dur  = current.effDur > 0 ? current.effDur : (current.stopAt - current.time);
			const frac = dur > 0 ? Math.min(1, Math.max(0, (now - current.time) / dur)) : 0;
			this.onStepVisual(current.step, current.sliceIdx, frac);
		}

		// Done: scheduling is finished and the tail has played out.
		const lastNote = this.noteQueue[this.noteQueue.length - 1];
		if (this._noMoreSteps && (!lastNote || lastNote.stopAt <= now)) {
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
		this.stepIndex    = 0;
		this._noMoreSteps = false;
		if (wasPlaying && this.onStop) this.onStop();
	}
}

export default Transport;
