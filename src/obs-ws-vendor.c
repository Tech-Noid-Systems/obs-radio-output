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

#include "obs-ws-vendor.h"

#include <assert.h>
#include <pthread.h>
#include <string.h>

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include "frontend-wire.h"
#include "metadata.h"
#include "radio-output.h"

/* Single-translation-unit header: defines a static proc-handler cache, so it
 * must be included in exactly one .c file (this one). */
#include "obs-websocket-api.h"

#define VENDOR_NAME         "obs-radio-output"
#define REQUEST_STATUS      "radio.status"
#define REQUEST_START       "radio.start"
#define REQUEST_STOP        "radio.stop"
#define REQUEST_METADATA    "radio.pushMetadata"
#define REQUEST_APPLYCONFIG "radio.applyConfig"
#define REQUEST_GETLISTENERS "radio.getListeners"

/* obs-websocket vendor handle.  NULL until OBS_FRONTEND_EVENT_FINISHED_LOADING
 * registers it, or permanently NULL if obs-websocket is not installed. */
static obs_websocket_vendor s_vendor = NULL;

static const char *state_to_string(radio_state_t state)
{
	switch (state) {
	case RADIO_STATE_DISCONNECTED:
		return "disconnected";
	case RADIO_STATE_CONNECTING:
		return "connecting";
	case RADIO_STATE_CONNECTED:
		return "connected";
	case RADIO_STATE_RECONNECTING:
		return "reconnecting";
	case RADIO_STATE_ERROR:
		return "error";
	}
	return "unknown";
}

static const char *codec_to_string(int codec)
{
	switch (codec) {
	case RADIO_CODEC_OPUS:
		return "opus";
	case RADIO_CODEC_MP3:
		return "mp3";
	case RADIO_CODEC_VORBIS:
		return "vorbis";
	}
	return "unknown";
}

static const char *protocol_to_string(int protocol)
{
	switch (protocol) {
	case RADIO_PROTOCOL_ICECAST:
		return "icecast";
	case RADIO_PROTOCOL_SHOUTCAST:
		return "shoutcast";
	}
	return "unknown";
}

/*
 * radio.status — read-only.  Reports the live connection state and target when
 * an output is running, otherwise the saved configuration (so a caller can see
 * what a start would use).  No side effects, so it answers inline on the
 * obs-websocket thread.
 *
 * Response fields:
 *   outputActive (bool)   — is a radio output instantiated right now
 *   state        (string) — disconnected|connecting|connected|reconnecting|error
 *   host         (string)
 *   port         (int)
 *   mount        (string)
 *   codec        (string) — mp3|opus|vorbis|unknown
 *   bitrate      (int)    — kbps
 *   tls          (bool)
 *   protocol     (string) — icecast|shoutcast|unknown
 *   source       (string) — "active" (fields reflect the running output) or
 *                           "config" (fields reflect the saved configuration)
 */
static void radio_status_request_cb(obs_data_t *request_data, obs_data_t *response_data, void *priv_data)
{
	UNUSED_PARAMETER(request_data);
	UNUSED_PARAMETER(priv_data);

	struct radio_output *ctx = radio_output_get_active();
	if (ctx) {
		/* Mirror the dock's pollState() read: hold state_mutex while
		 * copying the connection fields.  obs_data_set_string copies the
		 * string internally, so populating the response under the lock
		 * keeps host/mount valid for the duration of the read. */
		pthread_mutex_lock(&ctx->state_mutex);
		obs_data_set_bool(response_data, "outputActive", true);
		obs_data_set_string(response_data, "state", state_to_string(ctx->state));
		obs_data_set_string(response_data, "host", ctx->host ? ctx->host : "");
		obs_data_set_int(response_data, "port", ctx->port);
		obs_data_set_string(response_data, "mount", ctx->mount ? ctx->mount : "");
		obs_data_set_string(response_data, "codec", codec_to_string(ctx->codec));
		obs_data_set_int(response_data, "bitrate", ctx->bitrate);
		obs_data_set_bool(response_data, "tls", ctx->use_tls);
		obs_data_set_string(response_data, "protocol", protocol_to_string(ctx->protocol));
		pthread_mutex_unlock(&ctx->state_mutex);
		obs_data_set_string(response_data, "source", "active");
		return;
	}

	obs_data_t *cfg = frontend_wire_get_config();
	obs_data_set_bool(response_data, "outputActive", false);
	obs_data_set_string(response_data, "state", state_to_string(RADIO_STATE_DISCONNECTED));
	if (cfg) {
		obs_data_set_string(response_data, "host", obs_data_get_string(cfg, SETTING_HOST));
		obs_data_set_int(response_data, "port", obs_data_get_int(cfg, SETTING_PORT));
		obs_data_set_string(response_data, "mount", obs_data_get_string(cfg, SETTING_MOUNT));
		obs_data_set_string(response_data, "codec", codec_to_string((int)obs_data_get_int(cfg, SETTING_CODEC)));
		obs_data_set_int(response_data, "bitrate", obs_data_get_int(cfg, SETTING_BITRATE));
		obs_data_set_bool(response_data, "tls", obs_data_get_bool(cfg, SETTING_TLS));
		obs_data_set_string(response_data, "protocol",
				    protocol_to_string((int)obs_data_get_int(cfg, SETTING_PROTOCOL)));
	}
	obs_data_set_string(response_data, "source", "config");
}

/*
 * UI-thread marshaling.
 *
 * Vendor request callbacks fire on the obs-websocket thread, but start/stop and
 * applyConfig touch the UI-thread-owned module state (g_active_output_handle,
 * g_config). We hop onto the UI thread via obs_queue_task(..., wait=true) so
 * these serialize with the dock and frontend-event paths. obs_in_task_thread()
 * guards against the (unlikely) case of already being on the UI thread, which
 * would deadlock a waiting queue_task.
 */
struct ui_start_task {
	bool result;
};
static void ui_run_start(void *param)
{
	((struct ui_start_task *)param)->result = frontend_wire_start_output();
}

static void ui_run_stop(void *param)
{
	UNUSED_PARAMETER(param);
	frontend_wire_stop_output();
}

struct ui_apply_task {
	obs_data_t *patch;
	bool result;
};
static void ui_run_apply(void *param)
{
	struct ui_apply_task *t = (struct ui_apply_task *)param;
	t->result = frontend_wire_apply_config(t->patch);
}

/* radio.start — create + start the radio output (UI thread). Response: ok (bool).
 * ok=true means the start was initiated; the connection itself completes
 * asynchronously on a connect thread (#61) — poll radio.status for
 * connecting → connected|error. */
static void radio_start_request_cb(obs_data_t *request_data, obs_data_t *response_data, void *priv_data)
{
	UNUSED_PARAMETER(request_data);
	UNUSED_PARAMETER(priv_data);

	struct ui_start_task t = {false};
	if (obs_in_task_thread(OBS_TASK_UI))
		t.result = frontend_wire_start_output();
	else
		obs_queue_task(OBS_TASK_UI, ui_run_start, &t, true);

	obs_data_set_bool(response_data, "ok", t.result);
}

/* radio.stop — stop the running radio output (UI thread). Idempotent. Response: ok (bool). */
static void radio_stop_request_cb(obs_data_t *request_data, obs_data_t *response_data, void *priv_data)
{
	UNUSED_PARAMETER(request_data);
	UNUSED_PARAMETER(priv_data);

	if (obs_in_task_thread(OBS_TASK_UI))
		frontend_wire_stop_output();
	else
		obs_queue_task(OBS_TASK_UI, ui_run_stop, NULL, true);

	obs_data_set_bool(response_data, "ok", true);
}

/*
 * radio.pushMetadata — set the "now playing" title. radio_output_update_metadata
 * is documented thread-safe (libshout takes its own lock), so this answers inline
 * with no UI hop. Request: title (string). Response: ok (bool) [+ error string].
 */
static void radio_metadata_request_cb(obs_data_t *request_data, obs_data_t *response_data, void *priv_data)
{
	UNUSED_PARAMETER(priv_data);

	const char *title = obs_data_get_string(request_data, "title");
	if (!title || !*title) {
		obs_data_set_bool(response_data, "ok", false);
		obs_data_set_string(response_data, "error", "missing required string field 'title'");
		return;
	}

	obs_data_set_bool(response_data, "ok", radio_output_update_metadata(title));
}

/*
 * radio.applyConfig — merge the provided SETTING_* keys into the saved config
 * (UI thread). Takes effect on the next start, matching the config dialog.
 * Request: any subset of host, port, mount, password, codec, bitrate, tls,
 * protocol, reconnect settings, start_with_streaming. Response: ok (bool).
 */
static void radio_applyconfig_request_cb(obs_data_t *request_data, obs_data_t *response_data, void *priv_data)
{
	UNUSED_PARAMETER(priv_data);

	struct ui_apply_task t = {request_data, false};
	if (obs_in_task_thread(OBS_TASK_UI))
		t.result = frontend_wire_apply_config(request_data);
	else
		obs_queue_task(OBS_TASK_UI, ui_run_apply, &t, true);

	obs_data_set_bool(response_data, "ok", t.result);
}

/*
 * radio.getListeners — read-only.  Returns the dock's last-polled listener
 * count from the shared cache (radio_output_get_listener_count).  The cache is
 * an atomic_int, so this needs no UI-thread marshaling.  The count may be up to
 * one poll interval (~10 s) stale, and is -1 ("unknown") when disconnected, not
 * yet polled, or the server stats were unparseable.
 * Response: listeners (int), valid (bool — true when listeners >= 0).
 */
static void radio_getlisteners_request_cb(obs_data_t *request_data, obs_data_t *response_data, void *priv_data)
{
	UNUSED_PARAMETER(request_data);
	UNUSED_PARAMETER(priv_data);

	const int count = radio_output_get_listener_count();
	obs_data_set_int(response_data, "listeners", count);
	obs_data_set_bool(response_data, "valid", count >= 0);
}

static void register_vendor(void)
{
	if (s_vendor)
		return;

	s_vendor = obs_websocket_register_vendor(VENDOR_NAME);
	if (!s_vendor) {
		obs_log(LOG_INFO, "[obs-ws-vendor] obs-websocket not present; remote-control API disabled");
		return;
	}

	const struct {
		const char *type;
		obs_websocket_request_callback_function cb;
	} requests[] = {
		{REQUEST_STATUS, radio_status_request_cb},
		{REQUEST_START, radio_start_request_cb},
		{REQUEST_STOP, radio_stop_request_cb},
		{REQUEST_METADATA, radio_metadata_request_cb},
		{REQUEST_APPLYCONFIG, radio_applyconfig_request_cb},
		{REQUEST_GETLISTENERS, radio_getlisteners_request_cb},
	};

	for (size_t i = 0; i < sizeof(requests) / sizeof(requests[0]); i++) {
		if (!obs_websocket_vendor_register_request(s_vendor, requests[i].type, requests[i].cb, NULL))
			obs_log(LOG_WARNING, "[obs-ws-vendor] failed to register '%s' request", requests[i].type);
	}

	obs_log(LOG_INFO,
		"[obs-ws-vendor] registered vendor '%s' (status, start, stop, pushMetadata, applyConfig, getListeners)",
		VENDOR_NAME);
}

static void on_frontend_event(enum obs_frontend_event event, void *priv)
{
	UNUSED_PARAMETER(priv);

	/* obs-websocket is itself a module; its proc handler is only guaranteed
	 * to be available once all modules have finished loading. */
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING)
		register_vendor();
}

void obs_ws_vendor_init(void)
{
	obs_frontend_add_event_callback(on_frontend_event, NULL);
}

void obs_ws_vendor_shutdown(void)
{
	obs_frontend_remove_event_callback(on_frontend_event, NULL);

	if (s_vendor) {
		obs_websocket_vendor_unregister_request(s_vendor, REQUEST_STATUS);
		obs_websocket_vendor_unregister_request(s_vendor, REQUEST_START);
		obs_websocket_vendor_unregister_request(s_vendor, REQUEST_STOP);
		obs_websocket_vendor_unregister_request(s_vendor, REQUEST_METADATA);
		obs_websocket_vendor_unregister_request(s_vendor, REQUEST_APPLYCONFIG);
		obs_websocket_vendor_unregister_request(s_vendor, REQUEST_GETLISTENERS);
		s_vendor = NULL;
	}
}
