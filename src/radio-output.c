// SPDX-License-Identifier: GPL-2.0-or-later

#include "radio-output.h"
#include "reconnect.h"
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
	/* Setting stores seconds; convert to milliseconds for internal use. */
	context->reconnect_delay_ms = (int)obs_data_get_int(settings, SETTING_RECONNECT_DELAY) * 1000;
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

	reconnect_cancel(context);

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

	/* Cancel any in-progress reconnect before touching the shout handle. */
	reconnect_cancel(context);

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

		/* Close the dead connection before attempting to reconnect. */
		shout_close(context->shout);
		shout_free(context->shout);
		context->shout = NULL;

		if (context->reconnect_enabled) {
			/*
			 * reconnect_start sets state to RECONNECTING and spawns
			 * the background thread.  Encoded packets will be silently
			 * dropped (shout == NULL guard above) until the thread
			 * restores the connection.
			 */
			reconnect_start(context);
		} else {
			set_state(context, RADIO_STATE_ERROR);
			obs_output_signal_stop(context->output, OBS_OUTPUT_DISCONNECTED);
		}
		return;
	}

	shout_sync(context->shout);
#endif
}

static bool reconnect_toggled(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	bool enabled = obs_data_get_bool(settings, SETTING_RECONNECT);
	obs_property_set_visible(obs_properties_get(props, SETTING_RECONNECT_DELAY), enabled);
	obs_property_set_visible(obs_properties_get(props, SETTING_RECONNECT_MAX), enabled);
	return true;
}

static obs_properties_t *radio_output_get_properties(void *data)
{
	UNUSED_PARAMETER(data);

	obs_properties_t *props = obs_properties_create();

	/* ---- Server ---- */
	obs_properties_t *server = obs_properties_create();
	obs_properties_add_text(server, SETTING_HOST, obs_module_text("RadioOutput.Server.Host"), OBS_TEXT_DEFAULT);
	obs_properties_add_int(server, SETTING_PORT, obs_module_text("RadioOutput.Server.Port"), 1, 65535, 1);
	obs_properties_add_text(server, SETTING_MOUNT, obs_module_text("RadioOutput.Server.Mount"), OBS_TEXT_DEFAULT);
	obs_properties_add_text(server, SETTING_PASSWORD, obs_module_text("RadioOutput.Server.Password"),
				OBS_TEXT_PASSWORD);
	obs_properties_add_group(props, "server", obs_module_text("RadioOutput.Server.Group"), OBS_GROUP_NORMAL,
				 server);

	/* ---- Audio ---- */
	obs_properties_t *audio = obs_properties_create();

	obs_property_t *codec = obs_properties_add_list(audio, SETTING_CODEC,
							obs_module_text("RadioOutput.Audio.Codec"), OBS_COMBO_TYPE_LIST,
							OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(codec, obs_module_text("RadioOutput.Audio.Codec.Opus"), RADIO_CODEC_OPUS);
	obs_property_list_add_int(codec, obs_module_text("RadioOutput.Audio.Codec.MP3"), RADIO_CODEC_MP3);

	obs_property_t *bitrate = obs_properties_add_list(audio, SETTING_BITRATE,
							  obs_module_text("RadioOutput.Audio.Bitrate"),
							  OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	static const int bitrate_values[] = {32, 64, 96, 128, 160, 192, 256, 320};
	for (size_t idx = 0; idx < sizeof(bitrate_values) / sizeof(bitrate_values[0]); idx++) {
		char label[8];
		snprintf(label, sizeof(label), "%d", bitrate_values[idx]);
		obs_property_list_add_int(bitrate, label, bitrate_values[idx]);
	}

	obs_properties_add_group(props, "audio", obs_module_text("RadioOutput.Audio.Group"), OBS_GROUP_NORMAL, audio);

	/* ---- Auto-Reconnect ---- */
	obs_properties_t *reconnect = obs_properties_create();

	obs_property_t *toggle =
		obs_properties_add_bool(reconnect, SETTING_RECONNECT, obs_module_text("RadioOutput.Reconnect.Enable"));
	obs_property_set_modified_callback(toggle, reconnect_toggled);

	obs_properties_add_int(reconnect, SETTING_RECONNECT_DELAY, obs_module_text("RadioOutput.Reconnect.Delay"), 1,
			       60, 1);
	obs_properties_add_int(reconnect, SETTING_RECONNECT_MAX, obs_module_text("RadioOutput.Reconnect.Max"), 0, 100,
			       1);

	obs_properties_add_group(props, "reconnect", obs_module_text("RadioOutput.Reconnect.Group"), OBS_GROUP_NORMAL,
				 reconnect);

	return props;
}

static void radio_output_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, SETTING_HOST, "localhost");
	obs_data_set_default_int(settings, SETTING_PORT, 8000);
	obs_data_set_default_string(settings, SETTING_MOUNT, "/stream");
	obs_data_set_default_string(settings, SETTING_PASSWORD, "");
	obs_data_set_default_int(settings, SETTING_CODEC, RADIO_CODEC_OPUS);
	obs_data_set_default_int(settings, SETTING_BITRATE, 128);
	obs_data_set_default_bool(settings, SETTING_RECONNECT, true);
	obs_data_set_default_int(settings, SETTING_RECONNECT_DELAY, 5);
	obs_data_set_default_int(settings, SETTING_RECONNECT_MAX, 10);
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
