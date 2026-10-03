/* frog-render — bounce a session offline.
 *
 *   frog-render SESSION.json [OUT.wav] [--seconds N | --bars N] [--rate HZ]
 *               [--samples DIR] [--seed N]
 *
 * Without OUT.wav it only loads the session and prints a summary, which is
 * what the F5 scaffold does; the render itself arrives with the engine (F6).
 * Sample files resolve relative to --samples (default: the session's dir).
 */
#include "pattern.h"
#include "session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void) {
	fprintf(stderr, "usage: frog-render SESSION.json [OUT.wav] [--seconds N | --bars N] [--rate HZ] [--samples DIR] [--seed N]\n");
}

int main(int argc, char** argv) {
	const char* sessionPath = NULL;
	const char* outPath = NULL;
	double seconds = 0.0;
	int bars = 0;
	double rate = 48000.0;
	const char* samplesDir = NULL;
	unsigned seed = 1;

	for (int i = 1; i < argc; i++) {
		const char* a = argv[i];
		if (strcmp(a, "--seconds") == 0 && i + 1 < argc) {
			seconds = atof(argv[++i]);
		} else if (strcmp(a, "--bars") == 0 && i + 1 < argc) {
			bars = atoi(argv[++i]);
		} else if (strcmp(a, "--rate") == 0 && i + 1 < argc) {
			rate = atof(argv[++i]);
		} else if (strcmp(a, "--samples") == 0 && i + 1 < argc) {
			samplesDir = argv[++i];
		} else if (strcmp(a, "--seed") == 0 && i + 1 < argc) {
			seed = (unsigned)strtoul(argv[++i], NULL, 10);
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
	(void)seconds;
	(void)bars;
	(void)rate;
	(void)samplesDir;
	(void)seed;
	if (outPath) {
		fprintf(stderr, "frog-render: rendering is not implemented yet (F6)\n");
		free(s);
		return 3;
	}
	free(s);
	return 0;
}
