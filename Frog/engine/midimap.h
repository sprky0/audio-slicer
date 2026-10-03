/* midimap.h — CC bindings: which controller message drives which hook. A
 * hook is a named control of the panel ("track.vol", "pattern.randomize",
 * "slice.fadeIn"...); the shell resolves names to actions, this module only
 * holds the table and its JSON form (<data dir>/midimap.json, hand-editable).
 * Channel 0 means any channel; an explicit channel outranks it. Track hooks
 * act on the focused track, slice hooks on the selected slice. Main thread
 * only (the shell drains CCs on the idle tick). */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FG_MIDIMAP_MAX 128
#define FG_HOOK_NAME_MAX 32

typedef struct {
	uint8_t channel;   /* 0 any, 1..16 */
	uint8_t cc;        /* 0..127 */
	char target[FG_HOOK_NAME_MAX];
} fg_binding;

typedef struct {
	int n;
	fg_binding b[FG_MIDIMAP_MAX];
} fg_midimap;

void fg_midimap_clear(fg_midimap* m);
/* The shipped map: omni, the focused track takes every track hook. */
void fg_midimap_defaults(fg_midimap* m);

/* The binding for a message on `channel` (1..16) and `cc`, or NULL. */
const fg_binding* fg_midimap_find(const fg_midimap* m, int channel, int cc);
int fg_midimap_find_target(const fg_midimap* m, const char* target);   /* index or -1 */
bool fg_midimap_is_bound(const fg_midimap* m, const char* target);

/* Bind `target` to (channel, cc): its previous binding and whatever already
 * sat on exactly that channel + cc are dropped. False when full / bad args. */
bool fg_midimap_bind(fg_midimap* m, int channel, int cc, const char* target);
bool fg_midimap_unbind(fg_midimap* m, const char* target);

/* JSON: {"format":"frog-midimap","formatVersion":1,"bindings":[{"cc":7,"channel":0,"target":"track.vol"},...]}
 * "channel" may be omitted or "any". Malformed input returns false and
 * leaves `out` cleared; unknown fields are ignored. */
char* fg_midimap_write(const fg_midimap* m);   /* malloc'd, caller frees */
bool fg_midimap_parse(const char* json, fg_midimap* out);
bool fg_midimap_load_file(const char* path, fg_midimap* out);
bool fg_midimap_save_file(const char* path, const fg_midimap* m);

#ifdef __cplusplus
}
#endif
