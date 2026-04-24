// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct radio_output;

/*
 * Push a "Now Playing" title to the server.  Works on both Icecast and
 * SHOUTcast v1 through libshout's protocol-agnostic metadata API
 * (shout_metadata_new / shout_metadata_add / shout_set_metadata) — the
 * wire format differs per protocol but libshout hides that from us.
 *
 * Safe to call from any thread: libshout's shout_set_metadata takes its
 * own internal lock, and we acquire context->state_mutex before reading
 * the connection state so we can't race with teardown.
 *
 * Returns 0 on success, -1 on any failure (not connected, libshout
 * error, empty title).  Logs "Now playing: <title>" at LOG_INFO on
 * success; logs the libshout error string at LOG_WARNING on failure.
 */
int metadata_update(struct radio_output *context, const char *title);

/*
 * Convenience wrapper for the dock (and other Qt callers).  Looks up
 * the currently active output via radio_output_get_active() and
 * forwards to metadata_update.  Returns false if no output is active
 * or if the update failed.
 */
bool radio_output_update_metadata(const char *title);

#ifdef __cplusplus
}
#endif
