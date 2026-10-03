#include "midimap.h"
#include "frog_types.h"
#include "third_party/cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void fg_midimap_clear(fg_midimap* m) {
	memset(m, 0, sizeof *m);
}

void fg_midimap_defaults(fg_midimap* m) {
	static const struct { uint8_t cc; const char* target; } defs[] = {
		/* mix and transport */
		{7, "track.vol"}, {10, "track.pan"}, {20, "track.mute"}, {14, "master.gain"},
		{27, "transport.perform"}, {28, "transport.play"}, {29, "transport.stop"},
		/* the focused track's pattern */
		{21, "track.pitch"}, {22, "pattern.amt"}, {23, "track.loop"},
		{24, "pattern.randomize"}, {25, "pattern.resetOrder"}, {26, "pattern.resetAll"},
		/* the selected slice */
		{70, "slice.fadeIn"}, {71, "slice.fadeOut"}, {72, "slice.gain"}, {73, "slice.pitch"},
		{74, "slice.mute"}, {75, "slice.rev"}, {76, "slice.lock"}, {77, "slice.curveIn"}, {78, "slice.curveOut"},
		{79, "slice.select"}, {80, "track.focus"},
		/* sessions */
		{30, "session.prev"}, {31, "session.next"},
	};
	fg_midimap_clear(m);
	for (size_t i = 0; i < sizeof defs / sizeof defs[0]; i++) {
		fg_midimap_bind(m, 0, defs[i].cc, defs[i].target);
	}
}

const fg_binding* fg_midimap_find(const fg_midimap* m, int channel, int cc) {
	const fg_binding* any = NULL;
	for (int i = 0; i < m->n; i++) {
		const fg_binding* b = &m->b[i];
		if (b->cc != cc) {
			continue;
		}
		if (b->channel == channel) {
			return b;
		}
		if (b->channel == 0 && !any) {
			any = b;
		}
	}
	return any;
}

int fg_midimap_find_target(const fg_midimap* m, const char* target) {
	for (int i = 0; i < m->n; i++) {
		if (strcmp(m->b[i].target, target) == 0) {
			return i;
		}
	}
	return -1;
}

bool fg_midimap_is_bound(const fg_midimap* m, const char* target) {
	return fg_midimap_find_target(m, target) >= 0;
}

static void remove_at(fg_midimap* m, int i) {
	memmove(&m->b[i], &m->b[i + 1], sizeof(fg_binding) * (size_t)(m->n - i - 1));
	m->n--;
}

bool fg_midimap_bind(fg_midimap* m, int channel, int cc, const char* target) {
	if (channel < 0 || channel > 16 || cc < 0 || cc > 127 || !target || !target[0] || strlen(target) >= FG_HOOK_NAME_MAX) {
		return false;
	}
	for (int i = m->n - 1; i >= 0; i--) {
		if (strcmp(m->b[i].target, target) == 0 || (m->b[i].channel == channel && m->b[i].cc == cc)) {
			remove_at(m, i);
		}
	}
	if (m->n >= FG_MIDIMAP_MAX) {
		return false;
	}
	fg_binding* b = &m->b[m->n++];
	b->channel = (uint8_t)channel;
	b->cc = (uint8_t)cc;
	snprintf(b->target, sizeof b->target, "%s", target);
	return true;
}

bool fg_midimap_unbind(fg_midimap* m, const char* target) {
	const int i = fg_midimap_find_target(m, target);
	if (i < 0) {
		return false;
	}
	remove_at(m, i);
	return true;
}

char* fg_midimap_write(const fg_midimap* m) {
	cJSON* root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "format", "frog-midimap");
	cJSON_AddNumberToObject(root, "formatVersion", 1);
	cJSON* arr = cJSON_AddArrayToObject(root, "bindings");
	for (int i = 0; i < m->n; i++) {
		cJSON* o = cJSON_CreateObject();
		cJSON_AddNumberToObject(o, "cc", m->b[i].cc);
		if (m->b[i].channel) {
			cJSON_AddNumberToObject(o, "channel", m->b[i].channel);
		} else {
			cJSON_AddStringToObject(o, "channel", "any");
		}
		cJSON_AddStringToObject(o, "target", m->b[i].target);
		cJSON_AddItemToArray(arr, o);
	}
	char* out = cJSON_Print(root);   /* formatted: the file is meant to be edited */
	cJSON_Delete(root);
	return out;
}

bool fg_midimap_parse(const char* json, fg_midimap* out) {
	fg_midimap_clear(out);
	cJSON* root = cJSON_Parse(json);
	if (!root || !cJSON_IsObject(root)) {
		cJSON_Delete(root);
		return false;
	}
	const cJSON* fmt = cJSON_GetObjectItemCaseSensitive(root, "format");
	const cJSON* arr = cJSON_GetObjectItemCaseSensitive(root, "bindings");
	if (!cJSON_IsString(fmt) || strcmp(fmt->valuestring, "frog-midimap") != 0 || !cJSON_IsArray(arr)) {
		cJSON_Delete(root);
		return false;
	}
	const cJSON* o;
	cJSON_ArrayForEach(o, arr) {
		const cJSON* cc = cJSON_GetObjectItemCaseSensitive(o, "cc");
		const cJSON* ch = cJSON_GetObjectItemCaseSensitive(o, "channel");
		const cJSON* tg = cJSON_GetObjectItemCaseSensitive(o, "target");
		if (!cJSON_IsNumber(cc) || !cJSON_IsString(tg)) {
			continue;
		}
		int channel = 0;
		if (cJSON_IsNumber(ch)) {
			channel = (int)ch->valuedouble;
		}
		fg_midimap_bind(out, channel, (int)cc->valuedouble, tg->valuestring);
	}
	cJSON_Delete(root);
	return true;
}

bool fg_midimap_load_file(const char* path, fg_midimap* out) {
	FILE* f = fopen(path, "rb");
	if (!f) {
		return false;
	}
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (n < 0 || n > (1L << 20)) {
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
	bool ok = fg_midimap_parse(buf, out);
	free(buf);
	return ok;
}

bool fg_midimap_save_file(const char* path, const fg_midimap* m) {
	char* json = fg_midimap_write(m);
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
