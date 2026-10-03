/* session.h — the saved state as JSON. Format v1 is the browser version's
 * `version: 3` save object, read as-is (so existing sessions import), plus
 * `format` / `formatVersion` and a per-track `samplePath`. Main thread only:
 * allocates. */
#pragma once

#include "frog_types.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Parse `json` (NUL-terminated) into `out`, validating every pattern.
 * Returns false on malformed JSON; unknown fields are ignored and missing
 * ones take their defaults. */
bool fg_session_parse(const char* json, fg_session* out);

/* Serialise to a malloc'd NUL-terminated string (caller frees), or NULL. */
char* fg_session_write(const fg_session* s);

/* Convenience: whole-file read / atomic-ish write (temp + rename). */
bool fg_session_load_file(const char* path, fg_session* out);
bool fg_session_save_file(const char* path, const fg_session* s);

/* Empty session: one track, default pattern (native row), no sample. */
void fg_session_init(fg_session* s);

const char* fg_curve_name(int curve);
int fg_curve_from_name(const char* name);
const char* fg_mod_action_name(int action);
int fg_mod_action_from_name(const char* name);

#ifdef __cplusplus
}
#endif
