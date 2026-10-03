/* frog-render — bounce a session offline through the engine.
 *
 *   frog-render SESSION.json [OUT.wav] [--seconds N | --beats N] [--rate HZ]
 *               [--block N] [--samples DIR] [--seed N] [--no-normalize]
 *   frog-render --factory DATADIR
 *
 * --factory writes the first-run material the plugin would generate itself
 * (DATADIR/samples/amen-variation.wav and DATADIR/sessions/factory.json), for
 * a data dir that already had sessions when Frog first ran.
 *
 * Without OUT.wav it loads the session, renders in memory and prints the
 * summary and levels. Sample files resolve relative to --samples (default:
 * the session file's directory).
 */
#include "factory.h"
#include "pattern.h"
#include "render.h"
#include "session.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

static void usage(void) {
	fprintf(stderr, "usage: frog-render SESSION.json [OUT.wav] [--seconds N | --beats N] [--rate HZ] [--block N] [--samples DIR] [--seed N] [--no-normalize]\n"
	                "       frog-render --factory DATADIR\n");
}

static int factory(const char* dataDir) {
	char path[FG_PATH_MAX];
	snprintf(path, sizeof path, "%s/samples", dataDir);
	mkdir(dataDir, 0755);
	mkdir(path, 0755);
	snprintf(path, sizeof path, "%s/sessions", dataDir);
	mkdir(path, 0755);
	snprintf(path, sizeof path, "%s/samples/amen-variation.wav", dataDir);
	if (!fg_factory_write_clip(path)) {
		fprintf(stderr, "frog-render: cannot write %s\n", path);
		return 1;
	}
	printf("wrote %s\n", path);
	fg_session* s = (fg_session*)calloc(1, sizeof(fg_session));
	fg_factory_session(s, "amen-variation.wav", "amen-variation.wav");
	snprintf(path, sizeof path, "%s/sessions/factory.json", dataDir);
	const bool ok = fg_session_save_file(path, s);
	free(s);
	if (!ok) {
		fprintf(stderr, "frog-render: cannot write %s\n", path);
		return 1;
	}
	printf("wrote %s\n", path);
	return 0;
}

static double db(double v) {
	return v > 0.0 ? 20.0 * log10(v) : -200.0;
}

int main(int argc, char** argv) {
	const char* sessionPath = NULL;
	const char* outPath = NULL;
	fg_render_opts o = {0};
	o.normalize = true;
	o.seed = 1;

	if (argc == 3 && strcmp(argv[1], "--factory") == 0) {
		return factory(argv[2]);
	}
	for (int i = 1; i < argc; i++) {
		const char* a = argv[i];
		if (strcmp(a, "--seconds") == 0 && i + 1 < argc) {
			o.seconds = atof(argv[++i]);
		} else if (strcmp(a, "--beats") == 0 && i + 1 < argc) {
			o.beats = atof(argv[++i]);
		} else if (strcmp(a, "--rate") == 0 && i + 1 < argc) {
			o.sampleRate = atof(argv[++i]);
		} else if (strcmp(a, "--block") == 0 && i + 1 < argc) {
			o.blockSize = atoi(argv[++i]);
		} else if (strcmp(a, "--samples") == 0 && i + 1 < argc) {
			o.samplesDir = argv[++i];
		} else if (strcmp(a, "--seed") == 0 && i + 1 < argc) {
			o.seed = (unsigned)strtoul(argv[++i], NULL, 10);
		} else if (strcmp(a, "--no-normalize") == 0) {
			o.normalize = false;
		} else if (a[0] == '-') {
			usage();
			return 2;
		} else if (!sessionPath) {
			sessionPath = a;
		} else if (!outPath) {
			outPath = a;
		} else {
			usage();
			return 2;
		}
	}
	if (!sessionPath) {
		usage();
		return 2;
	}

	fg_session* s = (fg_session*)calloc(1, sizeof(fg_session));
	if (!fg_session_load_file(sessionPath, s)) {
		fprintf(stderr, "frog-render: cannot read %s\n", sessionPath);
		free(s);
		return 1;
	}
	char dir[FG_PATH_MAX];
	if (!o.samplesDir) {
		const char* slash = strrchr(sessionPath, '/');
		if (slash) {
			const size_t n = (size_t)(slash - sessionPath);
			memcpy(dir, sessionPath, n < sizeof dir - 1 ? n : sizeof dir - 1);
			dir[n < sizeof dir - 1 ? n : sizeof dir - 1] = 0;
			o.samplesDir = dir;
		}
	}

	printf("session: %s\n", sessionPath);
	printf("master: %.3f bpm%s, edit mode %s, %d track(s)\n",
	       s->masterBpm, s->masterUserSet ? " (user set)" : "",
	       s->editMode == FG_EDIT_GAPS ? "gaps" : "pack", s->nTracks);
	for (int i = 0; i < s->nTracks; i++) {
		const fg_track_state* t = &s->tracks[i];
		const fg_pattern* p = &t->pattern;
		printf("  track %d: %s | %d beats x 1/%d = %d units | %d tiles, %d mods | region %.3f..%.3f | vol %.2f pan %.2f%s | pitch %+d\n",
		       t->id, t->fileName[0] ? t->fileName : "(no sample)", p->beats, p->denom, fg_unit_count(p),
		       p->nTiles, p->nMods, p->start, p->end, t->mix.volume, t->mix.pan,
		       t->mix.muted ? " muted" : "", p->masterPitch);
	}

	fg_render_stats st = {0};
	const clock_t c0 = clock();
	const bool ok = fg_render_session(s, &o, outPath, &st);
	const double wall = (double)(clock() - c0) / CLOCKS_PER_SEC;
	if (!ok) {
		fprintf(stderr, "frog-render: render failed\n");
		free(s);
		return 1;
	}
	const double sr = o.sampleRate > 0.0 ? o.sampleRate : 48000.0;
	const double secs = (double)st.frames / sr;
	printf("render: %.2f s at %.0f Hz, block %d, %d track(s) with audio: cpu %.2f s (%.1fx realtime)\n",
	       secs, sr, o.blockSize > 0 ? o.blockSize : 128, st.tracksWithAudio, wall, wall > 0.0 ? secs / wall : 0.0);
	printf("levels: peak %.2f dBFS rms %.2f dBFS%s\n", db(st.peak), db(st.rms), o.normalize ? " (normalised to -0.09 dBFS on disk)" : "");
	if (outPath) {
		printf("wrote %s\n", outPath);
	}
	free(s);
	return 0;
}
