/**
 * Shared AudioContext (Tier 1c).
 *
 * Every AudioEngine used to own its own AudioContext, so multi-track "sync" was
 * only start-together + shared-tempo: independent contexts run on independent
 * hardware clocks, so tracks drift apart over time and can't be sample-locked.
 *
 * One process-wide context fixes that: all engines read the SAME `currentTime`,
 * so the master transport can anchor every track to a single audio-clock instant
 * and they stay sample-aligned. This is the foundation for a real groovebox.
 *
 * Lazily created on first use (so the context isn't spun up until a slicer
 * actually exists) and never closed — disposing one slicer must not tear down the
 * clock the others are still playing on. The browser starts it suspended under
 * the autoplay policy; AudioEngine.resume() (called on a user gesture) resumes it.
 */

let _ctx = null;

export function getAudioContext() {
	if (!_ctx) _ctx = new (window.AudioContext || window.webkitAudioContext)();
	return _ctx;
}
