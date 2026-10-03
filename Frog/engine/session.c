#include "session.h"
#include "pattern.h"
#include "third_party/cJSON.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- names ---------------------------------------------------------------- */

static const char* kCurveNames[FG_CURVE_COUNT] = {"linear", "exp", "log", "s"};
static const char* kActionNames[FG_MOD_ACTION_COUNT] = {"mute", "rev", "gain", "rand", "reset", "ratchet", "pitch"};
static const char* kRatchetModeNames[3] = {"even", "ramp", "pitch"};

const char* fg_curve_name(int curve) {
	return (curve >= 0 && curve < FG_CURVE_COUNT) ? kCurveNames[curve] : kCurveNames[0];
}

int fg_curve_from_name(const char* name) {
	for (int i = 0; name && i < FG_CURVE_COUNT; i++) {
		if (strcmp(name, kCurveNames[i]) == 0) {
			return i;
		}
	}
	return FG_CURVE_LINEAR;
}

const char* fg_mod_action_name(int action) {
	return (action >= 0 && action < FG_MOD_ACTION_COUNT) ? kActionNames[action] : kActionNames[0];
}

int fg_mod_action_from_name(const char* name) {
	for (int i = 0; name && i < FG_MOD_ACTION_COUNT; i++) {
		if (strcmp(name, kActionNames[i]) == 0) {
			return i;
		}
	}
	return FG_MOD_MUTE;
}

static int ratchet_mode_from_name(const char* name) {
	for (int i = 0; name && i < 3; i++) {
		if (strcmp(name, kRatchetModeNames[i]) == 0) {
			return i;
		}
	}
	return FG_RATCHET_EVEN;
}

/* --- small accessors ------------------------------------------------------ */

static double num(const cJSON* o, const char* key, double dflt) {
	const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, key);
	return (cJSON_IsNumber(v) && isfinite(v->valuedouble)) ? v->valuedouble : dflt;
}

static bool boolean(const cJSON* o, const char* key, bool dflt) {
	const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, key);
	if (cJSON_IsBool(v)) {
		return cJSON_IsTrue(v);
	}
	if (cJSON_IsNumber(v)) {
		return v->valuedouble != 0.0;
	}
	return dflt;
}

static const char* str(const cJSON* o, const char* key) {
	const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, key);
	return cJSON_IsString(v) ? v->valuestring : NULL;
}

static void copy_str(char* dst, size_t cap, const char* src) {
	if (!src) {
		dst[0] = 0;
		return;
	}
	strncpy(dst, src, cap - 1);
	dst[cap - 1] = 0;
}

/* --- init ----------------------------------------------------------------- */

void fg_session_init(fg_session* s) {
	memset(s, 0, sizeof(*s));
	s->formatVersion = 1;
	s->editMode = FG_EDIT_PACK;
	s->nextTrackId = 1;
	s->nTracks = 1;
	fg_track_state* t = &s->tracks[0];
	t->id = 0;
	t->bpm = 120.0;
	t->mix.volume = 1.0;
	fg_pattern_init(&t->pattern);
	fg_pattern_default_tiles(&t->pattern);
}

/* --- parse ---------------------------------------------------------------- */

static void parse_tile(const cJSON* o, fg_tile* t) {
	fg_tile_init(t, 0.0, 1.0, 0);
	t->gap = boolean(o, "gap", false);
	t->w = num(o, "w", 1.0);
	if (t->gap) {
		return;
	}
	t->src = num(o, "src", 0.0);
	t->offset = (int8_t)lround(num(o, "offset", 0.0));
	t->muted = boolean(o, "muted", false);
	t->locked = boolean(o, "locked", false);
	t->reversed = boolean(o, "reversed", false);
	t->colorIdx = (uint8_t)lround(num(o, "colorIdx", lround(t->src)));
	t->fadeIn = (float)num(o, "fadeIn", 0.0);
	t->fadeOut = (float)num(o, "fadeOut", 0.0);
	t->curveIn = (uint8_t)fg_curve_from_name(str(o, "fadeInCurve"));
	t->curveOut = (uint8_t)fg_curve_from_name(str(o, "fadeOutCurve"));
	t->gain = (float)num(o, "gain", 1.0);
}

static void parse_mod(const cJSON* o, fg_mod* m) {
	memset(m, 0, sizeof(*m));
	m->step = (int16_t)lround(num(o, "step", 0.0));
	m->action = (uint8_t)fg_mod_action_from_name(str(o, "action"));
	const char* fm = str(o, "fireMode");
	m->fireMode = (fm && strcmp(fm, "every") == 0) ? FG_FIRE_EVERY : FG_FIRE_PROB;
	m->fireValue = (int16_t)lround(num(o, "fireValue", m->fireMode == FG_FIRE_EVERY ? 2.0 : 100.0));
	m->gainAmt = (int16_t)lround(num(o, "gainAmt", 50.0));
	m->mode = (uint8_t)ratchet_mode_from_name(str(o, "mode"));
	m->subdiv = (int8_t)lround(num(o, "subdiv", 2.0));
	m->subdivTo = (int8_t)lround(num(o, "subdivTo", 4.0));
	m->pitchStep = (int8_t)lround(num(o, "pitchStep", 0.0));
	m->lenSteps = (int8_t)lround(num(o, "lenSteps", 1.0));
	m->pitchAmt = (int8_t)lround(num(o, "pitchAmt", 12.0));
	m->flags = (uint8_t)((boolean(o, "tail", false) ? FG_MODF_TAIL : 0) | (boolean(o, "keepPitch", false) ? FG_MODF_KEEP_PITCH : 0));
}

static void parse_track(const cJSON* o, fg_track_state* t) {
	memset(t, 0, sizeof(*t));
	t->id = (int)lround(num(o, "id", 0.0));
	copy_str(t->fileName, sizeof t->fileName, str(o, "fileName"));
	copy_str(t->samplePath, sizeof t->samplePath, str(o, "samplePath"));
	t->bpm = num(o, "bpm", 120.0);
	t->bpmManual = boolean(o, "bpmManual", false);
	t->mix.volume = num(o, "volume", 1.0);
	t->mix.pan = num(o, "pan", 0.0);
	t->mix.muted = boolean(o, "muted", false);
	t->mix.solo = boolean(o, "solo", false);

	fg_pattern* p = &t->pattern;
	fg_pattern_init(p);
	p->virtualStart = num(o, "virtualStart", 0.0);
	p->virtualEnd = num(o, "virtualEnd", 1.0);
	p->start = num(o, "start", 0.0);
	p->end = num(o, "end", 1.0);
	p->beats = (int)lround(num(o, "beats", 4.0));
	p->denom = (int)lround(num(o, "divisionDenom", 16.0));
	p->masterPitch = (int)lround(num(o, "masterPitch", 0.0));
	p->randLevel = (int)lround(num(o, "randLevel", 50.0));

	const cJSON* seq = cJSON_GetObjectItemCaseSensitive(o, "seq");
	bool haveTiles = false;
	if (cJSON_IsObject(seq)) {
		p->loop = boolean(seq, "loop", true);
		const cJSON* tiles = cJSON_GetObjectItemCaseSensitive(seq, "tiles");
		if (cJSON_IsArray(tiles)) {
			haveTiles = true;
			const cJSON* it;
			cJSON_ArrayForEach(it, tiles) {
				if (p->nTiles < FG_MAX_TILES && cJSON_IsObject(it)) {
					parse_tile(it, &p->tiles[p->nTiles++]);
				}
			}
		}
		const cJSON* mods = cJSON_GetObjectItemCaseSensitive(seq, "mods");
		if (cJSON_IsArray(mods)) {
			const cJSON* it;
			cJSON_ArrayForEach(it, mods) {
				if (p->nMods < FG_MAX_MODS && cJSON_IsObject(it)) {
					parse_mod(it, &p->mods[p->nMods++]);
				}
			}
		}
	}
	if (!haveTiles) {
		fg_pattern_default_tiles(p);
	}
	fg_pattern_validate(p);
}

bool fg_session_parse(const char* json, fg_session* out) {
	cJSON* root = cJSON_Parse(json);
	if (!root || !cJSON_IsObject(root)) {
		cJSON_Delete(root);
		return false;
	}
	memset(out, 0, sizeof(*out));
	out->formatVersion = 1;

	const cJSON* master = cJSON_GetObjectItemCaseSensitive(root, "master");
	if (cJSON_IsObject(master)) {
		out->masterBpm = num(master, "bpm", 0.0);
		out->masterUserSet = boolean(master, "userSet", false);
	}
	const char* mode = str(root, "seqEditMode");
	out->editMode = (mode && strcmp(mode, "gaps") == 0) ? FG_EDIT_GAPS : FG_EDIT_PACK;
	const cJSON* midi = cJSON_GetObjectItemCaseSensitive(root, "midi");
	if (cJSON_IsObject(midi)) {
		out->midiEnabled = boolean(midi, "enabled", false);
		copy_str(out->midiInputId, sizeof out->midiInputId, str(midi, "inputId"));
	}

	int maxId = -1;
	const cJSON* tracks = cJSON_GetObjectItemCaseSensitive(root, "slicers");
	if (!cJSON_IsArray(tracks)) {
		tracks = cJSON_GetObjectItemCaseSensitive(root, "tracks");
	}
	if (cJSON_IsArray(tracks)) {
		const cJSON* it;
		cJSON_ArrayForEach(it, tracks) {
			if (out->nTracks < FG_MAX_TRACKS && cJSON_IsObject(it)) {
				fg_track_state* t = &out->tracks[out->nTracks++];
				parse_track(it, t);
				if (t->id > maxId) {
					maxId = t->id;
				}
			}
		}
	}
	const int savedNext = (int)lround(num(root, "nextSlicerId", 0.0));
	out->nextTrackId = savedNext > maxId + 1 ? savedNext : maxId + 1;
	if (out->nTracks == 0) {
		fg_session tmp;
		fg_session_init(&tmp);
		out->nTracks = 1;
		out->tracks[0] = tmp.tracks[0];
		out->nextTrackId = 1;
	}
	cJSON_Delete(root);
	return true;
}

/* --- write ---------------------------------------------------------------- */

static cJSON* write_tile(const fg_tile* t) {
	cJSON* o = cJSON_CreateObject();
	if (t->gap) {
		cJSON_AddBoolToObject(o, "gap", true);
		cJSON_AddNumberToObject(o, "w", t->w);
		return o;
	}
	cJSON_AddNumberToObject(o, "src", t->src);
	cJSON_AddNumberToObject(o, "w", t->w);
	cJSON_AddNumberToObject(o, "offset", t->offset);
	cJSON_AddBoolToObject(o, "muted", t->muted);
	cJSON_AddNumberToObject(o, "colorIdx", t->colorIdx);
	cJSON_AddBoolToObject(o, "locked", t->locked);
	cJSON_AddBoolToObject(o, "reversed", t->reversed);
	cJSON_AddNumberToObject(o, "fadeIn", t->fadeIn);
	cJSON_AddNumberToObject(o, "fadeOut", t->fadeOut);
	cJSON_AddStringToObject(o, "fadeInCurve", fg_curve_name(t->curveIn));
	cJSON_AddStringToObject(o, "fadeOutCurve", fg_curve_name(t->curveOut));
	cJSON_AddNumberToObject(o, "gain", t->gain);
	return o;
}

static cJSON* write_mod(const fg_mod* m) {
	cJSON* o = cJSON_CreateObject();
	cJSON_AddNumberToObject(o, "step", m->step);
	cJSON_AddStringToObject(o, "action", fg_mod_action_name(m->action));
	cJSON_AddStringToObject(o, "fireMode", m->fireMode == FG_FIRE_EVERY ? "every" : "prob");
	cJSON_AddNumberToObject(o, "fireValue", m->fireValue);
	if (m->action == FG_MOD_GAIN) {
		cJSON_AddNumberToObject(o, "gainAmt", m->gainAmt);
	}
	if (m->action == FG_MOD_RATCHET) {
		cJSON_AddStringToObject(o, "mode", kRatchetModeNames[m->mode < 3 ? m->mode : 0]);
		cJSON_AddNumberToObject(o, "subdiv", m->subdiv);
		cJSON_AddNumberToObject(o, "subdivTo", m->subdivTo);
		cJSON_AddNumberToObject(o, "pitchStep", m->pitchStep);
		cJSON_AddNumberToObject(o, "lenSteps", m->lenSteps);
		cJSON_AddBoolToObject(o, "tail", (m->flags & FG_MODF_TAIL) != 0);
		cJSON_AddBoolToObject(o, "keepPitch", (m->flags & FG_MODF_KEEP_PITCH) != 0);
	}
	if (m->action == FG_MOD_PITCH) {
		cJSON_AddNumberToObject(o, "pitchAmt", m->pitchAmt);
	}
	return o;
}

static cJSON* write_track(const fg_track_state* t) {
	const fg_pattern* p = &t->pattern;
	cJSON* o = cJSON_CreateObject();
	cJSON_AddNumberToObject(o, "id", t->id);
	if (t->fileName[0]) {
		cJSON_AddStringToObject(o, "fileName", t->fileName);
	} else {
		cJSON_AddNullToObject(o, "fileName");
	}
	if (t->samplePath[0]) {
		cJSON_AddStringToObject(o, "samplePath", t->samplePath);
	}
	cJSON_AddNumberToObject(o, "virtualStart", p->virtualStart);
	cJSON_AddNumberToObject(o, "virtualEnd", p->virtualEnd);
	cJSON_AddNumberToObject(o, "start", p->start);
	cJSON_AddNumberToObject(o, "end", p->end);
	cJSON_AddNumberToObject(o, "beats", p->beats);
	cJSON_AddNumberToObject(o, "bpm", t->bpm);
	cJSON_AddNumberToObject(o, "divisionDenom", p->denom);
	cJSON_AddBoolToObject(o, "bpmManual", t->bpmManual);
	cJSON_AddNumberToObject(o, "volume", t->mix.volume);
	cJSON_AddNumberToObject(o, "pan", t->mix.pan);
	cJSON_AddBoolToObject(o, "muted", t->mix.muted);
	cJSON_AddBoolToObject(o, "solo", t->mix.solo);
	cJSON_AddNumberToObject(o, "masterPitch", p->masterPitch);
	cJSON_AddNumberToObject(o, "randLevel", p->randLevel);

	cJSON* seq = cJSON_AddObjectToObject(o, "seq");
	cJSON* tiles = cJSON_AddArrayToObject(seq, "tiles");
	for (int i = 0; i < p->nTiles; i++) {
		cJSON_AddItemToArray(tiles, write_tile(&p->tiles[i]));
	}
	cJSON* mods = cJSON_AddArrayToObject(seq, "mods");
	for (int i = 0; i < p->nMods; i++) {
		cJSON_AddItemToArray(mods, write_mod(&p->mods[i]));
	}
	cJSON_AddBoolToObject(seq, "loop", p->loop);
	return o;
}

char* fg_session_write(const fg_session* s) {
	cJSON* root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "format", "frog-session");
	cJSON_AddNumberToObject(root, "formatVersion", s->formatVersion > 0 ? s->formatVersion : 1);
	cJSON_AddNumberToObject(root, "version", 3);  /* the browser version's key */
	cJSON_AddNumberToObject(root, "nextSlicerId", s->nextTrackId);
	cJSON* tracks = cJSON_AddArrayToObject(root, "slicers");
	for (int i = 0; i < s->nTracks; i++) {
		cJSON_AddItemToArray(tracks, write_track(&s->tracks[i]));
	}
	cJSON* midi = cJSON_AddObjectToObject(root, "midi");
	cJSON_AddBoolToObject(midi, "enabled", s->midiEnabled);
	if (s->midiInputId[0]) {
		cJSON_AddStringToObject(midi, "inputId", s->midiInputId);
	} else {
		cJSON_AddNullToObject(midi, "inputId");
	}
	cJSON* master = cJSON_AddObjectToObject(root, "master");
	if (s->masterBpm > 0.0) {
		cJSON_AddNumberToObject(master, "bpm", s->masterBpm);
	} else {
		cJSON_AddNullToObject(master, "bpm");
	}
	cJSON_AddBoolToObject(master, "userSet", s->masterUserSet);
	cJSON_AddStringToObject(root, "seqEditMode", s->editMode == FG_EDIT_GAPS ? "gaps" : "pack");
	char* out = cJSON_Print(root);
	cJSON_Delete(root);
	return out;
}

/* --- files ---------------------------------------------------------------- */

bool fg_session_load_file(const char* path, fg_session* out) {
	FILE* f = fopen(path, "rb");
	if (!f) {
		return false;
	}
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (n < 0 || n > (64L << 20)) {
		fclose(f);
		return false;
	}
	char* buf = (char*)malloc((size_t)n + 1);
	if (!buf) {
		fclose(f);
		return false;
	}
	size_t got = fread(buf, 1, (size_t)n, f);
	fclose(f);
	buf[got] = 0;
	bool ok = fg_session_parse(buf, out);
	free(buf);
	return ok;
}

bool fg_session_save_file(const char* path, const fg_session* s) {
	char* json = fg_session_write(s);
	if (!json) {
		return false;
	}
	char tmp[FG_PATH_MAX + 8];
	snprintf(tmp, sizeof tmp, "%s.tmp", path);
	FILE* f = fopen(tmp, "wb");
	bool ok = false;
	if (f) {
		const size_t n = strlen(json);
		ok = fwrite(json, 1, n, f) == n && fputc('\n', f) != EOF;
		ok = (fclose(f) == 0) && ok;
		if (ok) {
			ok = rename(tmp, path) == 0;
		}
		if (!ok) {
			remove(tmp);
		}
	}
	free(json);
	return ok;
}
