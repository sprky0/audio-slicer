#include "sample.h"
#include "frog_types.h"

#define DR_WAV_IMPLEMENTATION
#include "third_party/dr_wav.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

fg_sample* fg_sample_create(int nCh, int64_t frames, double sampleRate) {
	if (nCh < 1 || nCh > FG_SAMPLE_MAX_CH || frames < 0) {
		return NULL;
	}
	fg_sample* s = (fg_sample*)calloc(1, sizeof(fg_sample));
	if (!s) {
		return NULL;
	}
	s->nCh = nCh;
	s->frames = frames;
	s->sampleRate = sampleRate;
	for (int c = 0; c < nCh; c++) {
		s->ch[c] = (float*)calloc((size_t)(frames > 0 ? frames : 1), sizeof(float));
		if (!s->ch[c]) {
			fg_sample_free(s);
			return NULL;
		}
	}
	return s;
}

void fg_sample_free(fg_sample* s) {
	if (!s) {
		return;
	}
	for (int c = 0; c < FG_SAMPLE_MAX_CH; c++) {
		free(s->ch[c]);
	}
	free(s);
}

/* --- resampling ------------------------------------------------------------ */

/* Kaiser-windowed sinc, 2·TAPS points, cutoff at the lower of the two rates. */
#define TAPS 16

static double bessel_i0(double x) {
	double sum = 1.0, term = 1.0;
	const double hx = x * 0.5;
	for (int k = 1; k < 32; k++) {
		term *= (hx / k) * (hx / k);
		sum += term;
		if (term < 1e-12 * sum) {
			break;
		}
	}
	return sum;
}

int64_t fg_resample(const float* in, int64_t inFrames, double inRate, float* out, int64_t outCap, double outRate) {
	if (inFrames <= 0 || inRate <= 0.0 || outRate <= 0.0 || outCap <= 0) {
		return 0;
	}
	const double ratio = inRate / outRate;  /* input frames per output frame */
	int64_t outFrames = (int64_t)llround((double)inFrames / ratio);
	if (outFrames > outCap) {
		outFrames = outCap;
	}
	if (fabs(ratio - 1.0) < 1e-12) {
		memcpy(out, in, (size_t)outFrames * sizeof(float));
		return outFrames;
	}
	const double cutoff = ratio > 1.0 ? 1.0 / ratio : 1.0;  /* in units of input Nyquist */
	const double beta = 8.0;
	const double i0b = bessel_i0(beta);
	for (int64_t t = 0; t < outFrames; t++) {
		const double x = (double)t * ratio;
		const int64_t k0 = (int64_t)floor(x);
		double acc = 0.0, wsum = 0.0;
		for (int64_t k = k0 - TAPS + 1; k <= k0 + TAPS; k++) {
			const double d = x - (double)k;
			const double u = d / TAPS;  /* −1..1 across the window */
			if (u <= -1.0 || u >= 1.0) {
				continue;
			}
			const double win = bessel_i0(beta * sqrt(1.0 - u * u)) / i0b;
			const double arg = FG_PI * d * cutoff;
			const double sinc = fabs(arg) < 1e-9 ? 1.0 : sin(arg) / arg;
			const double w = sinc * win * cutoff;
			wsum += sinc * win;  /* unity-gain normalisation */
			const float v = (k >= 0 && k < inFrames) ? in[k] : 0.f;
			acc += w * v;
		}
		(void)wsum;
		out[t] = (float)acc;
	}
	return outFrames;
}

/* --- WAV ------------------------------------------------------------------- */

fg_sample* fg_sample_load_wav(const char* path, double targetRate, double maxSeconds) {
	drwav w;
	if (!drwav_init_file(&w, path, NULL)) {
		return NULL;
	}
	const unsigned inCh = w.channels;
	const double inRate = (double)w.sampleRate;
	uint64_t frames = w.totalPCMFrameCount;
	if (inCh == 0 || inRate <= 0.0 || frames == 0) {
		drwav_uninit(&w);
		return NULL;
	}
	if (maxSeconds > 0.0) {
		const uint64_t cap = (uint64_t)(maxSeconds * inRate);
		if (frames > cap) {
			frames = cap;
		}
	}
	float* inter = (float*)malloc((size_t)frames * inCh * sizeof(float));
	if (!inter) {
		drwav_uninit(&w);
		return NULL;
	}
	const uint64_t got = drwav_read_pcm_frames_f32(&w, frames, inter);
	drwav_uninit(&w);
	if (got == 0) {
		free(inter);
		return NULL;
	}
	frames = got;

	/* de-interleave into ≤ 2 channels (extra channels are dropped) */
	const int nCh = inCh >= 2 ? 2 : 1;
	float* chans[FG_SAMPLE_MAX_CH] = {NULL, NULL};
	for (int c = 0; c < nCh; c++) {
		chans[c] = (float*)malloc((size_t)frames * sizeof(float));
		if (!chans[c]) {
			free(inter);
			free(chans[0]);
			return NULL;
		}
		for (uint64_t i = 0; i < frames; i++) {
			chans[c][i] = inter[i * inCh + c];
		}
	}
	free(inter);

	const double outRate = targetRate > 0.0 ? targetRate : inRate;
	const int64_t outFrames = (int64_t)llround((double)frames * outRate / inRate);
	fg_sample* s = fg_sample_create(nCh, outFrames, outRate);
	if (!s) {
		for (int c = 0; c < nCh; c++) {
			free(chans[c]);
		}
		return NULL;
	}
	for (int c = 0; c < nCh; c++) {
		fg_resample(chans[c], (int64_t)frames, inRate, s->ch[c], outFrames, outRate);
		free(chans[c]);
	}
	const char* base = strrchr(path, '/');
	strncpy(s->name, base ? base + 1 : path, sizeof s->name - 1);
	return s;
}

bool fg_sample_write_wav16(const char* path, const double* const* ch, int nCh, int64_t frames, double sampleRate) {
	return fg_sample_write_wav(path, ch, nCh, frames, sampleRate, 16);
}

bool fg_sample_write_wav(const char* path, const double* const* ch, int nCh, int64_t frames, double sampleRate, int bits) {
	bits = bits == 24 ? 24 : 16;
	drwav_data_format fmt;
	fmt.container = drwav_container_riff;
	fmt.format = DR_WAVE_FORMAT_PCM;
	fmt.channels = (drwav_uint32)nCh;
	fmt.sampleRate = (drwav_uint32)sampleRate;
	fmt.bitsPerSample = (drwav_uint32)bits;
	drwav w;
	if (!drwav_init_file_write(&w, path, &fmt, NULL)) {
		return false;
	}
	const int64_t CHUNK = 4096;
	const size_t bytesPerSample = (size_t)bits / 8;
	unsigned char* buf = (unsigned char*)malloc((size_t)CHUNK * (size_t)nCh * bytesPerSample);
	if (!buf) {
		drwav_uninit(&w);
		return false;
	}
	bool ok = true;
	for (int64_t at = 0; at < frames && ok; at += CHUNK) {
		const int64_t n = frames - at < CHUNK ? frames - at : CHUNK;
		for (int64_t i = 0; i < n; i++) {
			for (int c = 0; c < nCh; c++) {
				double v = ch[c][at + i];
				v = v > 1.0 ? 1.0 : (v < -1.0 ? -1.0 : v);
				unsigned char* o = buf + ((size_t)i * (size_t)nCh + (size_t)c) * bytesPerSample;
				if (bits == 24) {
					const int32_t q = (int32_t)lrint(v * 8388607.0);   /* little-endian, 3 bytes */
					o[0] = (unsigned char)(q & 0xFF);
					o[1] = (unsigned char)((q >> 8) & 0xFF);
					o[2] = (unsigned char)((q >> 16) & 0xFF);
				} else {
					const int16_t q = (int16_t)lrint(v * 32767.0);
					o[0] = (unsigned char)(q & 0xFF);
					o[1] = (unsigned char)((q >> 8) & 0xFF);
				}
			}
		}
		ok = drwav_write_pcm_frames(&w, (drwav_uint64)n, buf) == (drwav_uint64)n;
	}
	free(buf);
	drwav_uninit(&w);
	return ok;
}
