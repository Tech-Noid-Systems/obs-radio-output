// SPDX-License-Identifier: GPL-2.0-or-later

#include "radio-output.h"
#include <plugin-support.h>

static const char *radio_output_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("RadioOutput.Name");
}

static void radio_output_update(void *data, obs_data_t *settings)
{
	struct radio_output *context = data;

	bfree(context->host);
	bfree(context->mount);
	bfree(context->password);

	context->host = bstrdup(obs_data_get_string(settings, SETTING_HOST));
	context->port = (int)obs_data_get_int(settings, SETTING_PORT);
	context->mount = bstrdup(obs_data_get_string(settings, SETTING_MOUNT));
	context->password = bstrdup(obs_data_get_string(settings, SETTING_PASSWORD));
	context->codec = (int)obs_data_get_int(settings, SETTING_CODEC);
	context->bitrate = (int)obs_data_get_int(settings, SETTING_BITRATE);

	context->reconnect_enabled = obs_data_get_bool(settings, SETTING_RECONNECT);
	context->reconnect_delay_ms = (int)obs_data_get_int(settings, SETTING_RECONNECT_DELAY);
	context->reconnect_max_retries = (int)obs_data_get_int(settings, SETTING_RECONNECT_MAX);
}

static void *radio_output_create(obs_data_t *settings, obs_output_t *output)
{
	struct radio_output *context = bzalloc(sizeof(struct radio_output));
	context->output = output;
	context->state = RADIO_STATE_DISCONNECTED;
	pthread_mutex_init(&context->state_mutex, NULL);

#ifdef HAVE_LIBSHOUT
	shout_init();
#endif

	radio_output_update(context, settings);
	return context;
}

static void radio_output_destroy(void *data)
{
	struct radio_output *context = data;
	if (!context)
		return;

#ifdef HAVE_LIBSHOUT
	if (context->shout) {
		shout_close(context->shout);
		shout_free(context->shout);
	}
	shout_shutdown();
#endif

	pthread_mutex_destroy(&context->state_mutex);
	bfree(context->host);
	bfree(context->mount);
	bfree(context->password);
	bfree(context);
}

static inline void set_state(struct radio_output *context, radio_state_t state)
{
	pthread_mutex_lock(&context->state_mutex);
	context->state = state;
	pthread_mutex_unlock(&context->state_mutex);
}

static bool radio_output_start(void *data)
{
	struct radio_output *context = data;

#ifndef HAVE_LIBSHOUT
	obs_log(LOG_WARNING, "Streaming not available — libshout not present on this platform");
	obs_output_signal_stop(context->output, OBS_OUTPUT_CONNECT_FAILED);
	return false;
#else
	// --- Set up audio encoder ---
	const char *encoder_id = (context->codec == RADIO_CODEC_MP3) ? "ffmpeg_mp3" : "opus";

	obs_data_t *enc_settings = obs_data_create();
	obs_data_set_int(enc_settings, "bitrate", context->bitrate);

	obs_encoder_t *audio_enc = obs_audio_encoder_create(encoder_id, "radio_output_audio", enc_settings, 0, NULL);
	obs_data_release(enc_settings);

	if (!audio_enc) {
		obs_log(LOG_ERROR, "Failed to create audio encoder '%s'", encoder_id);
		obs_output_signal_stop(context->output, OBS_OUTPUT_CONNECT_FAILED);
		return false;
	}

	obs_output_set_audio_encoder(context->output, audio_enc, 0);
	obs_encoder_release(audio_enc);

	// --- Configure libshout ---
	context->shout = shout_new();
	if (!context->shout) {
		obs_log(LOG_ERROR, "shout_new() failed (out of memory?)");
		obs_output_signal_stop(context->output, OBS_OUTPUT_CONNECT_FAILED);
		return false;
	}

	char agent[64];
	snprintf(agent, sizeof(agent), "obs-radio-output/%s", PLUGIN_VERSION);

	shout_set_host(context->shout, context->host);
	shout_set_port(context->shout, context->port);
	shout_set_mount(context->shout, context->mount);
	shout_set_password(context->shout, context->password);
	shout_set_agent(context->shout, agent);
	shout_set_protocol(context->shout, SHOUT_PROTOCOL_HTTP);

	unsigned int shout_format = (context->codec == RADIO_CODEC_MP3) ? SHOUT_FORMAT_MP3 : SHOUT_FORMAT_OGG;
	shout_set_content_format(context->shout, shout_format, 0, NULL);

	// --- Open connection ---
	set_state(context, RADIO_STATE_CONNECTING);

	int err = shout_open(context->shout);
	if (err != SHOUTERR_SUCCESS) {
		obs_log(LOG_ERROR, "shout_open() failed: %s", shout_get_error(context->shout));
		shout_free(context->shout);
		context->shout = NULL;
		set_state(context, RADIO_STATE_ERROR);
		obs_output_signal_stop(context->output, OBS_OUTPUT_CONNECT_FAILED);
		return false;
	}

	set_state(context, RADIO_STATE_CONNECTED);
	context->reconnect_attempts = 0;
	obs_log(LOG_INFO, "Connected to %s:%d%s", context->host, context->port, context->mount);

	obs_output_begin_data_capture(context->output, 0);
	return true;
#endif
}

static void radio_output_stop(void *data, uint64_t ts)
{
	UNUSED_PARAMETER(ts);
	struct radio_output *context = data;

#ifdef HAVE_LIBSHOUT
	if (context->shout) {
		shout_close(context->shout);
		shout_free(context->shout);
		context->shout = NULL;
	}
#endif

	set_state(context, RADIO_STATE_DISCONNECTED);
	obs_log(LOG_INFO, "Disconnected from %s:%d%s", context->host, context->port, context->mount);
	obs_output_end_data_capture(context->output);
}

static void radio_output_encoded_packet(void *data, struct encoder_packet *packet)
{
#ifndef HAVE_LIBSHOUT
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(packet);
#else
	struct radio_output *context = data;

	if (!context->shout)
		return;

	int ret = shout_send(context->shout, (const unsigned char *)packet->data, packet->size);
	if (ret != SHOUTERR_SUCCESS) {
		obs_log(LOG_ERROR, "shout_send() failed: %s", shout_get_error(context->shout));
		set_state(context, RADIO_STATE_ERROR);
		obs_output_signal_stop(context->output, OBS_OUTPUT_DISCONNECTED);
		return;
	}

	shout_sync(context->shout);
#endif
}

static obs_properties_t *radio_output_get_properties(void *data)
{
	UNUSED_PARAMETER(data);
	return NULL;
}

static void radio_output_get_defaults(obs_data_t *settings)
{
	UNUSED_PARAMETER(settings);
}

struct obs_output_info radio_output_info = {
	.id = "radio_output",
	.flags = OBS_OUTPUT_AUDIO | OBS_OUTPUT_ENCODED,
	.get_name = radio_output_get_name,
	.create = radio_output_create,
	.destroy = radio_output_destroy,
	.start = radio_output_start,
	.stop = radio_output_stop,
	.encoded_packet = radio_output_encoded_packet,
	.get_properties = radio_output_get_properties,
	.get_defaults = radio_output_get_defaults,
	.update = radio_output_update,
};
