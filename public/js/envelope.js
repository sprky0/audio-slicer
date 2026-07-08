/**
 * envelope.js — per-voice fade-in/out gain automation, shared by the live
 * AudioEngine and the offline WAV export so both render the same envelope.
 *
 * An envelope descriptor is plain data resolved by the tile→playback policy:
 *   env = { fadeInSec, fadeOutSec, curve }
 * Times are in seconds of *playback* time (the policy converts a tile's stored
 * 0..1 length-fractions using its slot duration). `curve` names a shape in
 * FADE_CURVES — 'linear' today; new shapes only need an entry here and every
 * caller (live + export) picks them up.
 */

// curve name -> (param, from, to, t0, t1). Non-linear shapes should sample the
// curve and use setValueCurveAtTime so the same entry serves live + offline.
export const FADE_CURVES = {
	linear(param, from, to, t0, t1) {
		param.setValueAtTime(from, t0);
		param.linearRampToValueAtTime(to, t1);
	},
};

export const DEFAULT_CURVE = 'linear';

/**
 * Schedule a voice's fades on `gainParam` between `t0` (voice start) and `t1`
 * (its audible end). Overlong fades are scaled proportionally so in + out never
 * exceed the audible span (they meet at the crossover instead of fighting).
 * Returns true when a fade-out lands exactly on `t1` — the caller can then skip
 * its own end-of-slot declick ramp (the fade already reaches silence).
 */
export function applyEnvelope(gainParam, t0, t1, env) {
	if (!env) return false;
	let fadeIn  = Math.max(0, env.fadeInSec  || 0);
	let fadeOut = Math.max(0, env.fadeOutSec || 0);
	const avail = t1 - t0;
	if (!(avail > 0) || (fadeIn <= 0 && fadeOut <= 0)) return false;
	if (fadeIn + fadeOut > avail) {
		const s = avail / (fadeIn + fadeOut);
		fadeIn  *= s;
		fadeOut *= s;
	}
	const shape = FADE_CURVES[env.curve] || FADE_CURVES[DEFAULT_CURVE];
	if (fadeIn  > 0) shape(gainParam, 0, 1, t0, t0 + fadeIn);
	if (fadeOut > 0) shape(gainParam, 1, 0, t1 - fadeOut, t1);
	return fadeOut > 0;
}
