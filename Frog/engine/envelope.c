#include "envelope.h"
#include "frog_types.h"

#include <math.h>

/* Clearance kept before a cut when there is no fade-out, so the declick ramp
 * has room (as the browser version's 5 ms). */
#define DECLICK_SEC 0.005

double fg_curve_shape(int curve, double x) {
	if (x <= 0.0) {
		return 0.0;
	}
	if (x >= 1.0) {
		return 1.0;
	}
	switch (curve) {
		case FG_CURVE_EXP:
			return x * x;
		case FG_CURVE_LOG:
			return sqrt(x);
		case FG_CURVE_S:
			return (1.0 - cos(FG_PI * x)) * 0.5;
		default:
			return x;
	}
}

bool fg_env_prepare(fg_env_state* st, const fg_env* env, double totalSec) {
	st->total = totalSec > 0.0 ? totalSec : 0.0;
	st->level = 1.0;
	st->fadeIn = 0.0;
	st->fadeOut = 0.0;
	st->curveIn = FG_CURVE_LINEAR;
	st->curveOut = FG_CURVE_LINEAR;
	st->fadesOut = false;
	if (!env) {
		return false;
	}
	st->level = (isfinite(env->gain) && env->gain >= 0.0) ? env->gain : 1.0;
	st->curveIn = env->curveIn;
	st->curveOut = env->curveOut;
	double in = env->fadeInSec > 0.0 ? env->fadeInSec : 0.0;
	double out = env->fadeOutSec > 0.0 ? env->fadeOutSec : 0.0;
	if (st->total <= 0.0 || (in <= 0.0 && out <= 0.0)) {
		return false;
	}
	if (in + out > st->total) {
		const double s = st->total / (in + out);
		in *= s;
		out *= s;
	}
	if (out <= 0.0) {
		const double room = st->total - DECLICK_SEC;
		in = in < room ? in : (room > 0.0 ? room : 0.0);
	}
	st->fadeIn = in;
	st->fadeOut = out;
	st->fadesOut = out > 0.0;
	return st->fadesOut;
}

double fg_env_gain(const fg_env_state* st, double tSec) {
	double g = st->level;
	if (st->fadeIn > 0.0 && tSec < st->fadeIn) {
		g *= fg_curve_shape(st->curveIn, tSec / st->fadeIn);
	}
	if (st->fadeOut > 0.0) {
		const double t0 = st->total - st->fadeOut;
		if (tSec >= t0) {
			g *= 1.0 - fg_curve_shape(st->curveOut, (tSec - t0) / st->fadeOut);
		}
	}
	return g;
}
