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

#ifdef __cplusplus
extern "C" {
#endif

/*
 * obs-websocket vendor API for remote control / QA automation.
 *
 * Registers a vendor named "obs-radio-output" with obs-websocket so external
 * clients (Stream Deck, scripted now-playing systems, the QA harness) can drive
 * the plugin over the obs-websocket protocol via CallVendorRequest.
 *
 * Registration is deferred to OBS_FRONTEND_EVENT_FINISHED_LOADING because
 * obs-websocket is itself a module and its proc handler is only guaranteed to be
 * present once all modules have finished loading.  If obs-websocket is not
 * installed, registration is a no-op and the plugin loads normally.
 *
 * Verbs:
 *   - radio.status       : current connection state + the active/configured target (read-only)
 *   - radio.start        : create + start the radio output
 *   - radio.stop         : stop the running radio output (idempotent)
 *   - radio.pushMetadata : set the "now playing" title  {title: string}
 *   - radio.applyConfig  : merge SETTING_* keys into the saved config (next-start)
 *
 * radio.status and radio.pushMetadata answer inline; start/stop/applyConfig touch
 * UI-thread-owned state and are marshaled onto the OBS UI thread.
 *
 * Still deferred: radio.getListeners — the count lives in a Qt-main-thread poller
 * with no global cache; exposing it needs a cached value or a synchronous client.
 */

/* Call from obs_module_load().  Hooks the frontend event that performs the
 * actual vendor registration once obs-websocket is available. */
void obs_ws_vendor_init(void);

/* Call from obs_module_unload().  Unhooks the frontend event and unregisters
 * any requests registered with obs-websocket. */
void obs_ws_vendor_shutdown(void);

#ifdef __cplusplus
}
#endif
