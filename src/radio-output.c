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

#ifdef HAVE_LAME
	if (context->lame_gfp) {
		lame_close(context->lame_gfp);
		context->lame_gfp = NULL;
	}
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
	/* --- Initialize audio encoder --- */
	if (context->codec == RADIO_CODEC_MP3) {
#ifdef HAVE_LAME
		struct obs_audio_info oai;
		uint32_t sample_rate = 48000;
		int lame_ch = 2; /* default stereo */
		if (obs_get_audio_info(&oai))
			sample_rate = oai.samples_per_sec;

		context->lame_gfp = lame_init();
		if (!context->lame_gfp) {
			obs_log(LOG_ERROR, "lame_init() failed");
			obs_output_signal_stop(context->output, OBS_OUTPUT_CONNECT_FAILED);
			return false;
		}
		lame_set_in_samplerate(context->lame_gfp, (int)sample_rate);
		lame_set_num_channels(context->lame_gfp, lame_ch);
		lame_set_out_samplerate(context->lame_gfp, 0);
		lame_set_brate(context->lame_gfp, context->bitrate);
		lame_set_quality(context->lame_gfp, 2);
		if (lame_init_params(context->lame_gfp) < 0) {
			obs_log(LOG_ERROR, "lame_init_params() failed");
			lame_close(context->lame_gfp);
			context->lame_gfp = NULL;
			obs_output_signal_stop(context->output, OBS_OUTPUT_CONNECT_FAILED);
			return false;
		}
		obs_log(LOG_INFO, "MP3 encoder: %u Hz, %d ch, %d kbps", sample_rate, lame_ch, context->bitrate);
#else
		obs_log(LOG_ERROR, "MP3 encoding not available — rebuild with libmp3lame");
		obs_output_signal_stop(context->output, OBS_OUTPUT_CONNECT_FAILED);
		return false;
#endif
	}
	/* RADIO_CODEC_OPUS: Ogg/Opus encoding tracked in feat/raw-audio-opus */

	/* --- Configure libshout --- */
	context->shout = shout_new();
	if (!context->shout) {
		obs_log(LOG_ERROR, "shout_new() failed (out of memory?)");
#ifdef HAVE_LAME
		if (context->lame_gfp) {
			lame_close(context->lame_gfp);
			context->lame_gfp = NULL;
		}
#endif
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

	/* --- Open connection --- */
	set_state(context, RADIO_STATE_CONNECTING);

	int err = shout_open(context->shout);
	if (err != SHOUTERR_SUCCESS) {
		obs_log(LOG_ERROR, "shout_open() failed: %s", shout_get_error(context->shout));
		shout_free(context->shout);
		context->shout = NULL;
#ifdef HAVE_LAME
		if (context->lame_gfp) {
			lame_close(context->lame_gfp);
			context->lame_gfp = NULL;
		}
#endif
		set_state(context, RADIO_STATE_ERROR);
		obs_output_signal_stop(context->output, OBS_OUTPUT_CONNECT_FAILED);
		return false;
	}

	set_state(context, RADIO_STATE_CONNECTED);
	context->reconnect_attempts = 0;
	obs_log(LOG_INFO, "Connected to %s:%d%s", context->host, context->port, context->mount);

	if (!obs_output_begin_data_capture(context->output, 0)) {
		obs_log(LOG_ERROR, "obs_output_begin_data_capture() failed");
		shout_close(context->shout);
		shout_free(context->shout);
		context->shout = NULL;
#ifdef HAVE_LAME
		if (context->lame_gfp) {
			lame_close(context->lame_gfp);
			context->lame_gfp = NULL;
		}
#endif
		set_state(context, RADIO_STATE_ERROR);
		return false;
	}

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
#ifdef HAVE_LAME
	/* Flush remaining MP3 frames before closing the connection. */
	if (context->lame_gfp && context->shout) {
		uint8_t flush_buf[7200];
		int flush_bytes = lame_encode_flush(context->lame_gfp, flush_buf, sizeof(flush_buf));
		if (flush_bytes > 0)
			(void)shout_send(context->shout, flush_buf, (size_t)flush_bytes);
	}
	if (context->lame_gfp) {
		lame_close(context->lame_gfp);
		context->lame_gfp = NULL;
	}
#endif /* HAVE_LAME */
	if (context->shout) {
		shout_close(context->shout);
		shout_free(context->shout);
		context->shout = NULL;
	}
#endif /* HAVE_LIBSHOUT */

	set_state(context, RADIO_STATE_DISCONNECTED);
	obs_log(LOG_INFO, "Disconnected from %s:%d%s", context->host, context->port, context->mount);
	obs_output_end_data_capture(context->output);
}

static void radio_output_raw_audio(void *data, struct audio_data *frames)
{
#ifndef HAVE_LIBSHOUT
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(frames);
#else
	struct radio_output *context = data;

	static uint32_t diag_count = 0;
	diag_count++;

	if (!context->shout || !frames || !frames->frames) {
		if (!context->shout && frames && frames->frames)
			obs_log(LOG_WARNING, "[raw-audio-diag] #%u: shout handle NULL — skipping", diag_count);
		return;
	}

	if (context->codec == RADIO_CODEC_MP3) {
#ifdef HAVE_LAME
		if (!context->lame_gfp)
			return;

		const float *left = (const float *)frames->data[0];
		const float *right = frames->data[1] ? (const float *)frames->data[1] : left;

		/* Upper bound from LAME docs: 1.25 * nsamples + 7200 bytes. */
		int buf_size = (int)(1.25f * (float)frames->frames) + 7200;
		uint8_t *mp3buf = bmalloc((size_t)buf_size);

		int mp3_bytes = lame_encode_buffer_ieee_float(context->lame_gfp, left, right,
							       (int)frames->frames, mp3buf, buf_size);

		if (mp3_bytes > 0) {
			int ret = shout_send(context->shout, mp3buf, (size_t)mp3_bytes);
			/* Log every send result for first 2000 calls (~40 seconds of audio). */
			if (diag_count <= 2000)
				obs_log(LOG_INFO, "[raw-audio-diag] #%u: lame=%d shout_ret=%d",
					diag_count, mp3_bytes, ret);
			if (ret != SHOUTERR_SUCCESS) {
				obs_log(LOG_ERROR, "shout_send() failed at call #%u: %s", diag_count,
					shout_get_error(context->shout));
				shout_close(context->shout);
				shout_free(context->shout);
				context->shout = NULL;
				/*
				 * Keep lame_gfp alive across the reconnect window so encoder
				 * state is preserved — raw_audio resumes once the reconnect
				 * thread restores context->shout.
				 */
				if (context->reconnect_enabled) {
					reconnect_start(context);
				} else {
					set_state(context, RADIO_STATE_ERROR);
					obs_output_signal_stop(context->output, OBS_OUTPUT_DISCONNECTED);
				}
			}
		} else if (mp3_bytes < 0) {
			obs_log(LOG_ERROR, "lame_encode_buffer_ieee_float() error: %d at call #%u",
				mp3_bytes, diag_count);
		}

		bfree(mp3buf);
#endif /* HAVE_LAME */
	}
	/* RADIO_CODEC_OPUS: Ogg/Opus encoding tracked in feat/raw-audio-opus */
#endif /* HAVE_LIBSHOUT */
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
	obs_property_list_add_int(codec, obs_module_text("RadioOutput.Audio.Codec.MP3"), RADIO_CODEC_MP3);
	obs_property_list_add_int(codec, obs_module_text("RadioOutput.Audio.Codec.Opus"), RADIO_CODEC_OPUS);

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
	obs_data_set_default_int(settings, SETTING_CODEC, RADIO_CODEC_MP3);
	obs_data_set_default_int(settings, SETTING_BITRATE, 128);
	obs_data_set_default_bool(settings, SETTING_RECONNECT, true);
	obs_data_set_default_int(settings, SETTING_RECONNECT_DELAY, 5);
	obs_data_set_default_int(settings, SETTING_RECONNECT_MAX, 10);
}

struct obs_output_info radio_output_info = {
	.id = "radio_output",
	.flags = OBS_OUTPUT_AUDIO,
	.get_name = radio_output_get_name,
	.create = radio_output_create,
	.destroy = radio_output_destroy,
	.start = radio_output_start,
	.stop = radio_output_stop,
	.raw_audio = radio_output_raw_audio,
	.get_properties = radio_output_get_properties,
	.get_defaults = radio_output_get_defaults,
	.update = radio_output_update,
};
