// SPDX-License-Identifier: GPL-2.0-or-later
/*
obs-radio-output
Copyright (C) 2026 Aaron Cupp <mrcupp@mrcupp.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#pragma once

#include <obs.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Called from obs_module_load() after the output is registered.  Loads
 * any persisted config from disk and registers the Tools menu item.
 * Safe to call even if obs-frontend-api is not linked (no-op stub).
 */
void frontend_wire_load(void);

/*
 * Called from obs_module_unload().  Persists the current config and
 * frees module-global resources.
 */
void frontend_wire_unload(void);

/*
 * Returns a pointer to the current configuration obs_data_t.  The
 * returned data is owned by this module; the caller must NOT release
 * it.  Use obs_data_get_* to read values under the assumption that the
 * pointer remains valid for the lifetime of the module.
 *
 * Returns NULL if frontend-wire has not yet been loaded.
 */
obs_data_t *frontend_wire_get_config(void);

/*
 * Create a fresh radio_output using the current config and call
 * obs_output_start() on it.  If an output is already running, this is a
 * no-op (returns true).  Returns true on success, false if the output
 * couldn't be created or started.
 *
 * The created obs_output_t is owned by this module for the lifetime of
 * the broadcast and released by frontend_wire_stop_output().  Callers
 * should NOT release the returned handle — this function doesn't return
 * one.
 */
bool frontend_wire_start_output(void);

/*
 * Stop the currently running output (if any) and release its handle.
 * Idempotent: safe to call when nothing is running.
 */
void frontend_wire_stop_output(void);

/*
 * Merge the recognized SETTING_* keys present in `patch` into the module
 * config and persist it to disk.  Only keys explicitly set in `patch` are
 * applied; everything else is left untouched.  Takes effect on the NEXT
 * start (a running output keeps the settings it began with), matching the
 * config dialog's behavior.
 *
 * MUST be called on the OBS UI thread (it mutates the UI-thread-owned config
 * and writes to disk).  Returns false if config isn't loaded or patch is null.
 */
bool frontend_wire_apply_config(obs_data_t *patch);

#ifdef __cplusplus
}
#endif
