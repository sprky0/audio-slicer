#include "voice.h"

#include <math.h>
#include <string.h>

#define DECLICK_SEC 0.005

/* Stream sample `i` of channel `c`: the stretched stream, or the region
 * directly when the stretch stage is bypassed. */
static inline float stream_at(fg_voice* v, int c, int64_t i) {
	if (v->stretched) {
		return fg_wsola_get(&v->ws, c, i);
	}
	if (i < 0 || i >= v->regionLen) {
		return 0.f;
	}
	const int64_t src = v->reversed ? (v->regionStart + v->regionLen - 1 - i) : (v->regionStart + i);
	return v->smp->ch[c][src];
}

/* 4-point Hermite interpolation at fractional stream position `p`. */
static inline double read_at(fg_voice* v, int c, double p) {
	const double fl = floor(p);
	const int64_t i = (int64_t)fl;
	const double t = p - fl;
	const double y0 = stream_at(v, c, i - 1);
	const double y1 = stream_at(v, c, i);
	const double y2 = stream_at(v, c, i + 1);
	const double y3 = stream_at(v, c, i + 2);
	const double c1 = 0.5 * (y2 - y0);
	const double c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3;
	const double c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
	return ((c3 * t + c2) * t + c1) * t + y1;
}

bool fg_voice_start(fg_voice* v, const fg_voice_spec* s, double sampleRate) {
	memset(v, 0, sizeof(*v));
	if (!s->smp || s->regionLen <= 0 || s->stop <= s->start || !(s->rate > 0.0)) {
		return false;
	}
	v->sampleRate = sampleRate;
	v->smp = s->smp;
	v->regionStart = s->regionStart;
	v->regionLen = s->regionLen;
	v->reversed = s->reversed;
	v->rate = s->rate;
	v->start = s->start;
	v->stop = s->stop;
	v->tileIndex = s->tileIndex;
	v->stretched = fabs(s->factor - 1.0) >= 0.01;
	const double streamLen = v->stretched ? (double)llround((double)s->regionLen * s->factor) : (double)s->regionLen;
	/* output frames the material lasts, from the skip point */
	const double skipOut = s->skipOut > 0 ? (double)s->skipOut : 0.0;
	const double natural = (streamLen - skipOut * v->rate) / v->rate;
	if (natural <= 0.0) {
		return false;
	}
	const int64_t naturalEnd = v->start + (int64_t)floor(natural);
	v->end = naturalEnd < v->stop ? naturalEnd : v->stop;
	if (v->end <= v->start) {
		return false;
	}
	v->pos = skipOut * v->rate;
	if (v->stretched) {
		fg_wsola_init(&v->ws, s->smp, s->regionStart, s->regionLen, s->reversed, s->factor, (int64_t)floor(v->pos));
	}
	const bool fadesOut = fg_env_prepare(&v->env, s->env, (double)(v->end - v->start) / sampleRate);
	/* ramp when forced or when the cut lands before the material ends, unless
	 * a fade-out already reaches silence there */
	v->rampAtStop = !fadesOut && (s->declick || v->stop < naturalEnd);
	v->active = true;
	return true;
}

void fg_voice_render(fg_voice* v, double* L, double* R, int64_t blockStart, int n) {
	if (!v->active) {
		return;
	}
	const int64_t blockEnd = blockStart + n;
	int64_t from = v->start > blockStart ? v->start : blockStart;
	int64_t to = v->end < blockEnd ? v->end : blockEnd;
	if (from >= to) {
		if (blockStart >= v->end) {
			v->active = false;
		}
		return;
	}
	const double invSr = 1.0 / v->sampleRate;
	const double rampLen = DECLICK_SEC * v->sampleRate;
	const double rampStart = (double)v->stop - rampLen;
	const bool stereo = v->smp->nCh > 1;
	for (int64_t s = from; s < to; s++) {
		const double tSec = (double)(s - v->start) * invSr;
		double g = fg_env_gain(&v->env, tSec);
		if (v->rampAtStop && (double)s >= rampStart) {
			const double x = ((double)v->stop - (double)s) / rampLen;
			g *= x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x);
		}
		const int i = (int)(s - blockStart);
		const double l = read_at(v, 0, v->pos);
		const double r = stereo ? read_at(v, 1, v->pos) : l;
		L[i] += l * g;
		R[i] += r * g;
		v->pos += v->rate;
	}
	if (to >= v->end) {
		v->active = false;
	}
}

void fg_voice_stop(fg_voice* v) {
	v->active = false;
}
