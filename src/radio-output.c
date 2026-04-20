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

	obs_log(LOG_INFO, "destroy: DBG-D0 callback entered");
	reconnect_cancel(context);
	obs_log(LOG_INFO, "destroy: DBG-D1 after reconnect_cancel");

#ifdef HAVE_LAME
	/* Guard against destroy being called without stop (e.g. Lua GC). */
	if (context->send_buf) {
		context->send_running = false;
		pthread_mutex_lock(&context->send_mutex);
		pthread_cond_signal(&context->send_cond);
		pthread_mutex_unlock(&context->send_mutex);
		pthread_join(context->send_thread, NULL);
		pthread_mutex_destroy(&context->send_mutex);
		pthread_cond_destroy(&context->send_cond);
		bfree(context->send_buf);
		context->send_buf = NULL;
	}
#endif

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

#ifdef HAVE_LIBSHOUT
void shout_apply_settings(struct radio_output *context, shout_t *shout)
{
	char agent[64];
	snprintf(agent, sizeof(agent), "obs-radio-output/%s", PLUGIN_VERSION);

	unsigned int fmt = (context->codec == RADIO_CODEC_MP3) ? SHOUT_FORMAT_MP3 : SHOUT_FORMAT_OGG;

	shout_set_host(shout, context->host);
	shout_set_port(shout, (unsigned short)context->port);
	shout_set_mount(shout, context->mount);
	shout_set_password(shout, context->password);
	shout_set_agent(shout, agent);
	shout_set_protocol(shout, SHOUT_PROTOCOL_HTTP);

	/* libshout 2.4.6: shout_set_content_format() only maps SHOUT_FORMAT_MP3
	 * to "audio/mpeg" when usage == SHOUT_USAGE_AUDIO.  Without it the
	 * function falls through to OGG, causing Icecast to run its OGG parser
	 * on the raw MP3 stream and silently drop the source. */
	shout_set_content_format(shout, fmt, SHOUT_USAGE_AUDIO, NULL);

	/* Audio info: shout_sync() uses the bitrate to pace sends; without it
	 * audiorate = 0 and shout_sync can sleep indefinitely.  Also populates
	 * the audio_info stats on the Icecast admin page. */
	char ai_bitrate[16], ai_samplerate[16];
	uint32_t sample_rate = 48000;
#ifdef HAVE_LAME
	if (context->lame_gfp)
		sample_rate = (uint32_t)lame_get_in_samplerate(context->lame_gfp);
#endif
	snprintf(ai_bitrate, sizeof(ai_bitrate), "%d", context->bitrate);
	snprintf(ai_samplerate, sizeof(ai_samplerate), "%u", sample_rate);
	shout_set_audio_info(shout, SHOUT_AI_BITRATE, ai_bitrate);
	shout_set_audio_info(shout, SHOUT_AI_SAMPLERATE, ai_samplerate);
	shout_set_audio_info(shout, SHOUT_AI_CHANNELS, "2");
}

/*
 * shout_handoff_cleanup — close + free a libshout handle on a detached
 * thread so a graceful protocol-close on a half-dead socket doesn't block
 * the caller.  libshout 2.4.6's shout_close() writes a goodbye message;
 * if the peer is gone but no TCP RST has arrived, that send() blocks
 * until the kernel TCP timeout (~60 s on macOS).  Calling shout_free()
 * alone would leak the connection + fd (libshout 2.4.6 quirk: shout_free
 * does not call shout_connection_unref).  A detached thread lets the
 * kernel reap the blocked close whenever the TCP timeout fires.
 */
static void *shout_close_detached(void *data)
{
	shout_t *handle = data;
	shout_close(handle);
	shout_free(handle);
	return NULL;
}

static void shout_handoff_cleanup(shout_t *handle)
{
	if (!handle)
		return;
	pthread_t tid;
	if (pthread_create(&tid, NULL, shout_close_detached, handle) != 0) {
		obs_log(LOG_WARNING, "pthread_create for shout cleanup failed; closing inline");
		shout_close(handle);
		shout_free(handle);
		return;
	}
	pthread_detach(tid);
}
#endif /* HAVE_LIBSHOUT */

#ifdef HAVE_LAME
/*
 * mp3_send_thread — dequeues encoded MP3 frames from the ring buffer and
 * forwards them to libshout with proper bitrate pacing via shout_sync().
 *
 * Running shout_send + shout_sync on a dedicated thread keeps the OBS audio
 * callback (raw_audio) fast: it only encodes and writes to the ring buffer.
 */
static void *mp3_send_thread(void *data)
{
	struct radio_output *context = data;
	uint8_t scratch[4096];

	while (context->send_running) {
		pthread_mutex_lock(&context->send_mutex);
		while (context->send_wpos == context->send_rpos && context->send_running)
			pthread_cond_wait(&context->send_cond, &context->send_mutex);
		if (!context->send_running) {
			pthread_mutex_unlock(&context->send_mutex);
			break;
		}

		size_t avail = context->send_wpos - context->send_rpos;
		size_t to_read = avail < sizeof(scratch) ? avail : sizeof(scratch);
		size_t rpos = context->send_rpos % SEND_BUF_CAPACITY;
		size_t tail = SEND_BUF_CAPACITY - rpos;

		if (tail >= to_read) {
			memcpy(scratch, context->send_buf + rpos, to_read);
		} else {
			memcpy(scratch, context->send_buf + rpos, tail);
			memcpy(scratch + tail, context->send_buf, to_read - tail);
		}
		context->send_rpos += to_read;
		pthread_mutex_unlock(&context->send_mutex);

		if (!context->shout)
			continue;

		/* Pace to bitrate first (standard libshout pattern: sync, then send). */
		shout_sync(context->shout);

		int ret = shout_send(context->shout, scratch, to_read);
		if (ret != SHOUTERR_SUCCESS) {
			obs_log(LOG_ERROR, "[send-thread] shout_send() failed: %s", shout_get_error(context->shout));
			shout_t *dead = context->shout;
			context->shout = NULL;
			shout_handoff_cleanup(dead);
			if (context->reconnect_enabled) {
				reconnect_start(context);
			} else {
				set_state(context, RADIO_STATE_ERROR);
				obs_output_signal_stop(context->output, OBS_OUTPUT_DISCONNECTED);
			}
			continue;
		}
	}
	return NULL;
}
#endif /* HAVE_LAME */

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

	shout_apply_settings(context, context->shout);

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

#ifdef HAVE_LAME
	if (context->codec == RADIO_CODEC_MP3) {
		uint8_t *sbuf = bmalloc(SEND_BUF_CAPACITY);
		if (!sbuf) {
			obs_log(LOG_ERROR, "Failed to allocate MP3 send buffer");
			goto start_fail_after_capture;
		}
		/* Initialize mutex/cond BEFORE making send_buf visible to raw_audio. */
		context->send_wpos = 0;
		context->send_rpos = 0;
		context->send_running = true;
		pthread_mutex_init(&context->send_mutex, NULL);
		pthread_cond_init(&context->send_cond, NULL);
		context->send_buf = sbuf; /* raw_audio uses this as its "ready" gate */
		if (pthread_create(&context->send_thread, NULL, mp3_send_thread, context) != 0) {
			obs_log(LOG_ERROR, "Failed to create MP3 send thread");
			context->send_running = false;
			context->send_buf = NULL;
			pthread_mutex_destroy(&context->send_mutex);
			pthread_cond_destroy(&context->send_cond);
			bfree(sbuf);
			goto start_fail_after_capture;
		}
		obs_log(LOG_INFO, "MP3 send thread started");
	}
#endif

	return true;

#ifdef HAVE_LAME
start_fail_after_capture:
	obs_output_end_data_capture(context->output);
	shout_close(context->shout);
	shout_free(context->shout);
	context->shout = NULL;
	if (context->lame_gfp) {
		lame_close(context->lame_gfp);
		context->lame_gfp = NULL;
	}
	set_state(context, RADIO_STATE_ERROR);
	return false;
#endif
#endif
}

static void radio_output_stop(void *data, uint64_t ts)
{
	UNUSED_PARAMETER(ts);
	struct radio_output *context = data;

	obs_log(LOG_INFO, "stop: DBG0 callback entered");

	/* OBS may re-enter this callback (e.g. obs_output_release on an active
	 * output).  Bail out if we've already torn down so the disconnect log
	 * and end_data_capture fire exactly once. */
	pthread_mutex_lock(&context->state_mutex);
	if (context->state == RADIO_STATE_DISCONNECTED) {
		pthread_mutex_unlock(&context->state_mutex);
		return;
	}
	pthread_mutex_unlock(&context->state_mutex);

	obs_log(LOG_INFO, "stop: DBG1 before end_data_capture");
	/* Stop the audio callback BEFORE freeing anything it touches.  Otherwise
	 * an in-flight raw_audio callback races with the teardown below and can
	 * use-after-free context->send_buf / lame_gfp. */
	obs_output_end_data_capture(context->output);
	obs_log(LOG_INFO, "stop: DBG2 after end_data_capture");

	/* Cancel any in-progress reconnect before touching the shout handle. */
	reconnect_cancel(context);
	obs_log(LOG_INFO, "stop: DBG3 after reconnect_cancel");

#ifdef HAVE_LAME
	/* Stop the MP3 sender thread before closing the shout handle. */
	if (context->send_buf) {
		obs_log(LOG_INFO, "stop: DBG4 send_buf set, signaling send thread");
		context->send_running = false;
		pthread_mutex_lock(&context->send_mutex);
		pthread_cond_signal(&context->send_cond);
		pthread_mutex_unlock(&context->send_mutex);
		obs_log(LOG_INFO, "stop: DBG5 signal sent, about to join");
		pthread_join(context->send_thread, NULL);
		obs_log(LOG_INFO, "stop: DBG6 send thread joined");
		pthread_mutex_destroy(&context->send_mutex);
		obs_log(LOG_INFO, "stop: DBG7 mutex destroyed");
		pthread_cond_destroy(&context->send_cond);
		obs_log(LOG_INFO, "stop: DBG8 cond destroyed");
		bfree(context->send_buf);
		context->send_buf = NULL;
		obs_log(LOG_INFO, "MP3 send thread stopped");
	} else {
		obs_log(LOG_INFO, "stop: DBG4-skip send_buf was NULL");
	}
#endif /* HAVE_LAME */

#ifdef HAVE_LIBSHOUT
#ifdef HAVE_LAME
	/* Flush remaining MP3 frames before closing the connection. */
	if (context->lame_gfp && context->shout) {
		uint8_t flush_buf[7200];
		int flush_bytes = lame_encode_flush(context->lame_gfp, flush_buf, sizeof(flush_buf));
		if (flush_bytes > 0) {
			int flush_ret = shout_send(context->shout, flush_buf, (size_t)flush_bytes);
			if (flush_ret != SHOUTERR_SUCCESS)
				obs_log(LOG_WARNING, "shout_send() flush failed: %s", shout_get_error(context->shout));
		}
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
}

static void radio_output_raw_audio(void *data, struct audio_data *frames)
{
#ifndef HAVE_LIBSHOUT
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(frames);
#else
	struct radio_output *context = data;

	if (!frames || !frames->frames)
		return;

	if (context->codec == RADIO_CODEC_MP3) {
#ifdef HAVE_LAME
		if (!context->lame_gfp || !context->send_buf)
			return;

		const float *left = (const float *)frames->data[0];
		const float *right = frames->data[1] ? (const float *)frames->data[1] : left;

		/* Upper bound from LAME docs: 1.25 * nsamples + 7200 bytes. */
		int buf_size = (int)(1.25f * (float)frames->frames) + 7200;
		uint8_t *mp3buf = bmalloc((size_t)buf_size);

		int mp3_bytes = lame_encode_buffer_ieee_float(context->lame_gfp, left, right, (int)frames->frames,
							      mp3buf, buf_size);

		if (mp3_bytes > 0) {
			pthread_mutex_lock(&context->send_mutex);
			/* Drop oldest data if the ring buffer is full. */
			size_t used = context->send_wpos - context->send_rpos;
			if (used + (size_t)mp3_bytes > SEND_BUF_CAPACITY) {
				size_t drop = used + (size_t)mp3_bytes - SEND_BUF_CAPACITY;
				context->send_rpos += drop;
				obs_log(LOG_WARNING, "send buffer full, dropped %zu bytes", drop);
			}
			size_t wpos = context->send_wpos % SEND_BUF_CAPACITY;
			size_t tail = SEND_BUF_CAPACITY - wpos;
			if (tail >= (size_t)mp3_bytes) {
				memcpy(context->send_buf + wpos, mp3buf, (size_t)mp3_bytes);
			} else {
				memcpy(context->send_buf + wpos, mp3buf, tail);
				memcpy(context->send_buf, mp3buf + tail, (size_t)mp3_bytes - tail);
			}
			context->send_wpos += (size_t)mp3_bytes;
			pthread_cond_signal(&context->send_cond);
			pthread_mutex_unlock(&context->send_mutex);
		} else if (mp3_bytes < 0) {
			obs_log(LOG_ERROR, "lame_encode_buffer_ieee_float() error: %d", mp3_bytes);
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
