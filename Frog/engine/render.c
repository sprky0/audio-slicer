#include "render.h"
#include "engine.h"
#include "sample.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int gcd(int a, int b) {
	while (b) {
		const int t = a % b;
		a = b;
		b = t;
	}
	return a;
}

static void join_path(char* out, size_t cap, const char* dir, const char* name) {
	if (!dir || !dir[0] || name[0] == '/') {
		snprintf(out, cap, "%s", name);
	} else {
		snprintf(out, cap, "%s/%s", dir, name);
	}
}

double fg_render_loop_beats(const fg_session* s) {
	int l = 1;
	for (int i = 0; i < s->nTracks; i++) {
		const int b = s->tracks[i].pattern.beats > 0 ? s->tracks[i].pattern.beats : 4;
		l = l / gcd(l, b) * b;
	}
	return (double)l;
}

bool fg_render_session(const fg_session* s, const fg_render_opts* o, const char* outPath, fg_render_stats* stats) {
	const double sr = o->sampleRate > 0.0 ? o->sampleRate : 48000.0;
	const int block = o->blockSize > 0 ? o->blockSize : 128;
	const double bpm = s->masterBpm > 0.0 ? s->masterBpm : (s->nTracks > 0 && s->tracks[0].bpm > 0.0 ? s->tracks[0].bpm : 120.0);

	double beats = o->beats;
	if (o->seconds <= 0.0 && beats <= 0.0) {
		beats = fg_render_loop_beats(s);
	}
	const int64_t frames = o->seconds > 0.0 ? (int64_t)llround(o->seconds * sr) : (int64_t)llround(beats * 60.0 / bpm * sr);
	if (frames <= 0) {
		return false;
	}

	fg_engine* e = fg_engine_create(s->nTracks, o->seed ? o->seed : 1);
	if (!e) {
		return false;
	}
	fg_engine_reset(e, sr, block);
	fg_engine_load_session(e, s);
	fg_engine_set_tempo(e, bpm);

	fg_sample* samples[FG_MAX_TRACKS] = {0};
	int withAudio = 0;
	for (int i = 0; i < s->nTracks && i < FG_MAX_TRACKS; i++) {
		const fg_track_state* t = &s->tracks[i];
		const char* name = t->samplePath[0] ? t->samplePath : t->fileName;
		if (!name[0]) {
			continue;
		}
		char path[FG_PATH_MAX * 2];
		join_path(path, sizeof path, o->samplesDir, name);
		samples[i] = fg_sample_load_wav(path, sr, o->maxSampleSeconds);
		if (!samples[i]) {
			fprintf(stderr, "render: track %d: cannot load %s\n", t->id, path);
			continue;
		}
		fg_engine_set_sample(e, i, samples[i]);
		withAudio++;
	}

	double* L = (double*)calloc((size_t)frames, sizeof(double));
	double* R = (double*)calloc((size_t)frames, sizeof(double));
	double* bufs[2];
	if (!L || !R) {
		free(L);
		free(R);
		fg_engine_destroy(e);
		return false;
	}
	fg_engine_play_all(e);
	for (int64_t at = 0; at < frames; at += block) {
		const int n = (int)(frames - at < block ? frames - at : block);
		bufs[0] = L + at;
		bufs[1] = R + at;
		fg_engine_process(e, bufs, 2, n);
	}

	double peak = 0.0, sq = 0.0;
	for (int64_t i = 0; i < frames; i++) {
		const double a = fabs(L[i]), b = fabs(R[i]);
		peak = a > peak ? a : peak;
		peak = b > peak ? b : peak;
		sq += L[i] * L[i] + R[i] * R[i];
	}
	if (o->normalize && peak > 0.0) {
		const double g = 0.99 / peak;
		for (int64_t i = 0; i < frames; i++) {
			L[i] *= g;
			R[i] *= g;
		}
	}
	if (stats) {
		stats->frames = frames;
		stats->peak = peak;
		stats->rms = sqrt(sq / (double)(2 * frames));
		stats->tracksWithAudio = withAudio;
	}
	const double* chans[2] = {L, R};
	const bool ok = outPath ? fg_sample_write_wav16(outPath, chans, 2, frames, sr) : true;

	free(L);
	free(R);
	fg_engine_destroy(e);
	for (int i = 0; i < FG_MAX_TRACKS; i++) {
		fg_sample_free(samples[i]);
	}
	return ok;
}
