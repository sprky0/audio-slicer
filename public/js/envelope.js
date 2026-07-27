/**
 * envelope.js — per-voice gain automation (fades + slice gain), shared by the
 * live AudioEngine and the offline WAV export so both render the same envelope.
 *
 * An envelope descriptor is plain data resolved by the tile→playback policy:
 *   env = { fadeInSec, fadeOutSec, curveIn, curveOut, gain }
 * Times are in seconds of *playback* time (the policy converts a tile's stored
 * 0..1 length-fractions using its slot duration). `curveIn`/`curveOut` name
 * shapes in FADE_SHAPES; `gain` (default 1) is the voice's level — the ceiling
 * every fade rises to / falls from, so a quiet slice's fades stay proportional.
 */

// curve name -> normalized shape f(x): x 0..1 (time within the fade) -> 0..1
// (fraction of the fade travelled). Pure functions, so the tile UI can draw
// the exact curve playback uses. New shapes only need an entry here.
export const FADE_SHAPES = {
	linear: (x) => x,
	exp:    (x) => x * x,                          // slow start, late rise
	log:    (x) => Math.sqrt(x),                   // fast start, long tail
	s:      (x) => (1 - Math.cos(Math.PI * x)) / 2,   // smooth both ends
};

export const DEFAULT_CURVE = 'linear';

/** The voice's level from an env descriptor (1 when absent/invalid). */
export const envGain = (env) =>
	(env && Number.isFinite(env.gain) && env.gain >= 0) ? env.gain : 1;

// Keep the linear fade as native ramps (exact + cheap); sampled shapes go
// through setValueCurveAtTime so one entry serves live + offline identically.
const CURVE_SAMPLES = 65;
const scheduleFade = (param, shape, from, to, t0, t1) => {
	if (!(t1 > t0)) return;
	if (shape === FADE_SHAPES.linear) {
		param.setValueAtTime(from, t0);
		param.linearRampToValueAtTime(to, t1);
		return;
	}
	const vals = new Float32Array(CURVE_SAMPLES);
	for (let i = 0; i < CURVE_SAMPLES; i++) {
		vals[i] = from + (to - from) * shape(i / (CURVE_SAMPLES - 1));
	}
	param.setValueCurveAtTime(vals, t0, t1 - t0);
};

/**
 * Schedule a voice's fades on `gainParam` between `t0` (voice start) and `t1`
 * (its audible end), rising to / falling from the env's gain. Overlong fades
 * are scaled proportionally so in + out never exceed the audible span (they
 * meet at the crossover instead of fighting). Returns true when a fade-out
 * lands exactly on `t1` — the caller can then skip its own end-of-slot declick
 * ramp (the fade already reaches silence).
 *
 * The caller still owns the voice's base level: set gainParam.value to
 * envGain(env) before start, and anchor any declick ramp at that value.
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
	// No fade-out means the caller may append its own declick ramp at the cut;
	// a sampled curve throws if another event lands inside its interval, so
	// leave the ramp's 5ms clear of the fade-in.
	if (fadeOut <= 0) fadeIn = Math.min(fadeIn, Math.max(0, avail - 0.005));
	const g        = envGain(env);
	const shapeIn  = FADE_SHAPES[env.curveIn]  || FADE_SHAPES[DEFAULT_CURVE];
	const shapeOut = FADE_SHAPES[env.curveOut] || FADE_SHAPES[DEFAULT_CURVE];
	if (fadeIn  > 0) scheduleFade(gainParam, shapeIn,  0, g, t0, t0 + fadeIn);
	if (fadeOut > 0) scheduleFade(gainParam, shapeOut, g, 0, t1 - fadeOut, t1);
	return fadeOut > 0;
}
