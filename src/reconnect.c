// SPDX-License-Identifier: GPL-2.0-or-later

#include "reconnect.h"

#include <plugin-support.h>
#include <util/platform.h>

/* -------------------------------------------------------------------------
 * Internal helpers (libshout builds only)
 * ---------------------------------------------------------------------- */

#ifdef HAVE_LIBSHOUT

/**
 * attempt_connect - Create a fresh libshout handle and open a connection.
 *
 * On success context->shout is set to the live handle and true is returned.
 * On failure the handle is freed and false is returned; context->shout is
 * not modified.
 */
static bool attempt_connect(struct radio_output *context)
{
	shout_t *shout = shout_new();
	if (!shout) {
		obs_log(LOG_ERROR, "[reconnect] shout_new() failed — out of memory?");
		return false;
	}

	char agent[64];
	snprintf(agent, sizeof(agent), "obs-radio-output/%s", PLUGIN_VERSION);

	unsigned int fmt = (context->codec == RADIO_CODEC_MP3) ? SHOUT_FORMAT_MP3 : SHOUT_FORMAT_OGG;

	shout_set_host(shout, context->host);
	shout_set_port(shout, (unsigned short)context->port);
	shout_set_mount(shout, context->mount);
	shout_set_password(shout, context->password);
	shout_set_agent(shout, agent);
	shout_set_protocol(shout, SHOUT_PROTOCOL_HTTP);
	shout_set_content_format(shout, fmt, 0, NULL);

	int err = shout_open(shout);
	if (err != SHOUTERR_SUCCESS) {
		obs_log(LOG_WARNING, "[reconnect] shout_open() failed: %s", shout_get_error(shout));
		shout_free(shout);
		return false;
	}

	context->shout = shout;
	return true;
}

/**
 * reconnect_thread_fn - Background thread that retries the Icecast connection.
 *
 * Loop:
 *   1. Sleep reconnect_delay_ms in 100 ms chunks, checking reconnect_running
 *      between each chunk so the thread can be cancelled promptly.
 *   2. Call attempt_connect().
 *   3a. Success → set state CONNECTED, log, return.
 *   3b. Failure and max retries reached → signal OBS_OUTPUT_ERROR, return.
 *   3c. Failure and retries remain → loop back to step 1.
 *
 * context->shout must be NULL when this thread starts.  On success the thread
 * sets context->shout to the live handle; encoded_packet will pick it up on the
 * next call.
 */
static void *reconnect_thread_fn(void *data)
{
	struct radio_output *context = data;

	while (context->reconnect_running) {
		/* Interruptible sleep — wake every 100 ms to check the stop flag. */
		int elapsed = 0;
		while (elapsed < context->reconnect_delay_ms && context->reconnect_running) {
			int chunk = context->reconnect_delay_ms - elapsed;
			if (chunk > 100)
				chunk = 100;
			os_sleep_ms((uint32_t)chunk);
			elapsed += chunk;
		}

		if (!context->reconnect_running)
			break;

		context->reconnect_attempts++;
		obs_log(LOG_INFO, "[reconnect] attempt %d/%d to %s:%d%s ...", context->reconnect_attempts,
			context->reconnect_max_retries, context->host, context->port, context->mount);

		if (attempt_connect(context)) {
			set_state(context, RADIO_STATE_CONNECTED);
			context->reconnect_attempts = 0;
			obs_log(LOG_INFO, "[reconnect] reconnected to %s:%d%s", context->host, context->port,
				context->mount);
			return NULL;
		}

		/* Check retry limit (0 = unlimited). */
		if (context->reconnect_max_retries > 0 &&
		    context->reconnect_attempts >= context->reconnect_max_retries) {
			obs_log(LOG_ERROR, "[reconnect] max retries (%d) exceeded — giving up",
				context->reconnect_max_retries);
			set_state(context, RADIO_STATE_ERROR);
			obs_output_signal_stop(context->output, OBS_OUTPUT_ERROR);
			return NULL;
		}
	}

	return NULL;
}

#endif /* HAVE_LIBSHOUT */

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

void reconnect_start(struct radio_output *context)
{
#ifndef HAVE_LIBSHOUT
	UNUSED_PARAMETER(context);
#else
	if (!context->reconnect_enabled)
		return;

	/*
	 * Join any thread that finished on its own (e.g. a prior reconnect that
	 * succeeded, or one that hit max retries).  This prevents a thread leak
	 * if encoded_packet calls reconnect_start a second time.
	 */
	if (context->reconnect_active) {
		context->reconnect_running = false;
		pthread_join(context->reconnect_thread, NULL);
		context->reconnect_active = false;
	}

	set_state(context, RADIO_STATE_RECONNECTING);
	context->reconnect_running = true;

	if (pthread_create(&context->reconnect_thread, NULL, reconnect_thread_fn, context) != 0) {
		obs_log(LOG_ERROR, "[reconnect] pthread_create() failed");
		context->reconnect_running = false;
		set_state(context, RADIO_STATE_ERROR);
		obs_output_signal_stop(context->output, OBS_OUTPUT_ERROR);
		return;
	}

	context->reconnect_active = true;
	obs_log(LOG_INFO, "[reconnect] reconnect thread started (delay %d ms, max %d retries)",
		context->reconnect_delay_ms, context->reconnect_max_retries);
#endif
}

void reconnect_cancel(struct radio_output *context)
{
#ifndef HAVE_LIBSHOUT
	UNUSED_PARAMETER(context);
#else
	if (!context->reconnect_active)
		return;

	context->reconnect_running = false;
	pthread_join(context->reconnect_thread, NULL);
	context->reconnect_active = false;
	obs_log(LOG_INFO, "[reconnect] reconnect thread stopped");
#endif
}
