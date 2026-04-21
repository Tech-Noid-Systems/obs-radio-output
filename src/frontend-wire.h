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

#ifdef __cplusplus
}
#endif
