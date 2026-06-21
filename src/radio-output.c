// SPDX-License-Identifier: GPL-2.0-or-later

#include "radio-output.h"
#include "radio-encoder.h"
#include "reconnect.h"
#include <plugin-support.h>

#include <util/threading.h>

/*
 * Module-global pointer to the currently instantiated output, for the dock
 * widget (src/radio-output-dock.cpp) and frontend event handler (§B.3) to
 * read connection state from.  Assigned in radio_output_create, cleared in
 * radio_output_destroy.  Read-only access elsewhere; callers must hold
 * context->state_mutex to read the state field safely.
 *
 * Pointer reads/writes are atomic at the machine level on all supported
 * platforms, and all writers run on the OBS main thread (create/destroy
 * callbacks), so no explicit synchronization is needed around the pointer
 * itself.  The lifetime race — destroy clearing the pointer while a reader
 * dereferences it — is precluded because the dock (the only reader today)
 * runs its QTimer on the main thread too, serialized with destroy.
 */
static struct radio_output *g_active_output = NULL;

struct radio_output *radio_output_get_active(void)
{
	return g_active_output;
}

/*
 * Cached listener count for the obs-websocket radio.getListeners verb.
 * The dock's ListenerPoll (src/radio-output-dock.cpp) is the sole writer —
 * it stores each parsed Icecast/SHOUTcast count here (or -1 when not polling,
 * on parse error, or when leaving CONNECTED).  The vendor request callback
 * runs on the obs-websocket thread and reads it; an atomic_int lets that read
 * happen without locking.  -1 means "unknown" (disconnected, not yet polled,
 * or stats unparsable).  Value may be up to one poll interval (~10 s) stale.
 *
 * Uses libobs' os_atomic_*_long rather than C11 <stdatomic.h>: MSVC only
 * enables stdatomic under /experimental:c11atomics, which the OBS Windows
 * build doesn't set, so stdatomic fails to compile there.  os_atomic_* is
 * portable across MSVC/clang/gcc.
 */
static volatile long g_listener_count = -1;

void radio_output_set_listener_count(int count)
{
	os_atomic_set_long(&g_listener_count, count);
}

int radio_output_get_listener_count(void)
{
	return (int)os_atomic_load_long(&g_listener_count);
}

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
	context->protocol = (int)obs_data_get_int(settings, SETTING_PROTOCOL);
	context->use_tls = obs_data_get_bool(settings, SETTING_TLS);
	context->stream_samplerate = (uint32_t)obs_data_get_int(settings, SETTING_STREAM_SAMPLERATE);
	context->lame_quality = (int)obs_data_get_int(settings, SETTING_LAME_QUALITY);
	context->channel_mode = (int)obs_data_get_int(settings, SETTING_CHANNEL_MODE);
	context->bitrate_mode = (int)obs_data_get_int(settings, SETTING_BITRATE_MODE);
	context->vbr_quality = (int)obs_data_get_int(settings, SETTING_VBR_QUALITY);
	context->vbr_min_bitrate = (int)obs_data_get_int(settings, SETTING_VBR_MIN_BITRATE);
	context->vbr_max_bitrate = (int)obs_data_get_int(settings, SETTING_VBR_MAX_BITRATE);

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
	pthread_mutex_init(&context->encoder_mutex, NULL);

#ifdef HAVE_LIBSHOUT
	shout_init();
#endif

	radio_output_update(context, settings);
	g_active_output = context;
	return context;
}

#ifdef HAVE_LIBSHOUT
static void connect_cancel(struct radio_output *context);
static void shout_handoff_cleanup(shout_t *handle);
#endif

/*
 * radio_output_teardown — idempotent teardown used by both the stop and
 * destroy callbacks.  Must be called before freeing the context itself.
 *
 * Why this lives here: on the OBS_OUTPUT_ERROR path (reconnect gives up →
 * obs_output_signal_stop), OBS internally marks the output as stopped, so
 * a subsequent obs_output_stop() becomes a no-op and our stop callback
 * never fires — only destroy does, via obs_output_release.  Both paths
 * need identical teardown, including the "Disconnected from …" and
 * "MP3 send thread stopped" logs, so share one helper.
 *
 * The state_mutex guard makes this safe to call from stop AND destroy on
 * the happy path (stop runs first → state becomes DISCONNECTED → destroy's
 * call no-ops).
 */
static void radio_output_teardown(struct radio_output *context)
{
	pthread_mutex_lock(&context->state_mutex);
	radio_state_t entry_state = context->state;
	if (entry_state == RADIO_STATE_DISCONNECTED) {
		pthread_mutex_unlock(&context->state_mutex);
		return;
	}
	pthread_mutex_unlock(&context->state_mutex);

#ifdef HAVE_LIBSHOUT
	/* Cancel any in-flight async connect FIRST so the connect thread can't
	 * complete a start underneath this teardown.  Returns immediately —
	 * never waits out a blocked shout_open (#61). */
	connect_cancel(context);
#endif

	/* Stop the audio callback BEFORE freeing anything it touches. */
	obs_output_end_data_capture(context->output);

	reconnect_cancel(context);

#ifdef HAVE_LIBSHOUT
	/* Stop the send thread before flushing / closing shout so the thread
	 * can't race with our flush shout_send. */
	if (context->send_buf.data) {
		context->send_running = false;
		pthread_mutex_lock(&context->send_mutex);
		pthread_cond_signal(&context->send_cond);
		pthread_mutex_unlock(&context->send_mutex);
		pthread_join(context->send_thread, NULL);
		pthread_mutex_destroy(&context->send_mutex);
		pthread_cond_destroy(&context->send_cond);
		radio_send_buf_free(&context->send_buf);
		obs_log(LOG_INFO, "Encoder send thread stopped");
	}

	/* Encoder flush (trailing MP3 frames / final Ogg EOS page) + destroy, both
	 * under encoder_mutex so an in-flight raw_audio encode_frame can't run
	 * against freed encoder state (end_data_capture above does not drain a
	 * callback already executing on libobs's shared audio thread).  The audio
	 * thread trylocks the same mutex and drops its callback while we hold it.
	 * The flush bytes are produced under the lock but sent to the network
	 * AFTER releasing it, so a slow/dead peer can't stall the audio thread. */
	uint8_t flush_buf[16 * 1024];
	int flush_bytes = 0;
	pthread_mutex_lock(&context->encoder_mutex);
	if (context->encoder && context->shout)
		flush_bytes = context->encoder->flush(context, flush_buf, sizeof(flush_buf));
	if (context->encoder) {
		context->encoder->destroy(context);
		context->encoder = NULL;
	}
	pthread_mutex_unlock(&context->encoder_mutex);
	if (flush_bytes > 0 && context->shout) {
		int flush_ret = shout_send(context->shout, flush_buf, (size_t)flush_bytes);
		if (flush_ret != SHOUTERR_SUCCESS)
			obs_log(LOG_WARNING, "shout_send() flush failed: %s", shout_get_error(context->shout));
	}
	/* Detached close (#80): libshout's graceful protocol-close writes a
	 * goodbye message, and against a half-dead peer (no RST yet) that send
	 * blocks until the kernel TCP timeout (~60 s on macOS).  The send-thread
	 * failure path has used shout_handoff_cleanup since PR #21; the teardown
	 * path kept an inline close because it usually runs against a healthy
	 * connection — but "usually" beachballs OBS shutdown the day the peer
	 * died quietly.  Hand the close to the detached thread unconditionally. */
	if (context->shout) {
		shout_handoff_cleanup(context->shout);
		context->shout = NULL;
	}
#endif /* HAVE_LIBSHOUT */

	/* Preserve RADIO_STATE_ERROR so the dock keeps showing red until the
	 * user acknowledges by clicking Stop, which releases the obs_output_t
	 * handle and calls destroy — at which point the context is freed and
	 * the dock polls g_active_output == NULL, rendering Disconnected. */
	if (entry_state != RADIO_STATE_ERROR)
		set_state(context, RADIO_STATE_DISCONNECTED);
	obs_log(LOG_INFO, "Disconnected from %s:%d%s", context->host, context->port, context->mount);
}

static void radio_output_destroy(void *data)
{
	struct radio_output *context = data;
	if (!context)
		return;

	radio_output_teardown(context);

	/* Clear the module-global pointer only if it still points at us.  If a
	 * second output was created in parallel (unlikely but possible via Lua
	 * during transition), g_active_output tracks whichever was created
	 * last; don't clobber it. */
	if (g_active_output == context)
		g_active_output = NULL;

#ifdef HAVE_LIBSHOUT
	shout_shutdown();
#endif

	pthread_mutex_destroy(&context->state_mutex);
	pthread_mutex_destroy(&context->encoder_mutex);
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

	unsigned int fmt = context->encoder ? context->encoder->shout_format : SHOUT_FORMAT_MP3;

	shout_set_host(shout, context->host);
	shout_set_port(shout, (unsigned short)context->port);
	shout_set_mount(shout, context->mount);
	shout_set_password(shout, context->password);
	shout_set_agent(shout, agent);

	/* TLS mode.  SHOUT_TLS_AUTO_NO_PLAIN attempts TLS and refuses to
	 * silently fall back to plain HTTP — credentials must not leak
	 * unencrypted if the server turns out not to support TLS.  Plain AUTO
	 * would downgrade silently, which is a trust violation.  SHOUTcast v1
	 * (ICY) has no TLS in the protocol, so the combo is nonsensical:
	 * warn, then force DISABLED so shout_open gives a clean plain-ICY
	 * error rather than a confusing "TLS unsupported". */
	if (context->protocol == RADIO_PROTOCOL_SHOUTCAST && context->use_tls) {
		obs_log(LOG_WARNING, "TLS is not supported by SHOUTcast v1 (ICY protocol); "
				     "ignoring the TLS setting for this connection");
		shout_set_tls(shout, SHOUT_TLS_DISABLED);
	} else if (context->use_tls) {
		shout_set_tls(shout, SHOUT_TLS_AUTO_NO_PLAIN);
	} else {
		shout_set_tls(shout, SHOUT_TLS_DISABLED);
	}

	/* libshout protocol mapping.  SHOUT_PROTOCOL_ICY is SHOUTcast v1
	 * (no mount path, ICY metadata headers); SHOUT_PROTOCOL_HTTP is
	 * Icecast 2.x (standard HTTP PUT / SOURCE with a mount path). */
	const int shout_proto = (context->protocol == RADIO_PROTOCOL_SHOUTCAST) ? SHOUT_PROTOCOL_ICY
										: SHOUT_PROTOCOL_HTTP;
	shout_set_protocol(shout, shout_proto);

	/* libshout 2.4.6: shout_set_content_format() only maps SHOUT_FORMAT_MP3
	 * to "audio/mpeg" when usage == SHOUT_USAGE_AUDIO.  Without it the
	 * function falls through to OGG, causing Icecast to run its OGG parser
	 * on the raw MP3 stream and silently drop the source. */
	shout_set_content_format(shout, fmt, SHOUT_USAGE_AUDIO, NULL);

	/* Audio info: shout_sync() uses the bitrate to pace sends; without it
	 * audiorate = 0 and shout_sync can sleep indefinitely.  Also populates
	 * the audio_info stats on the Icecast admin page. */
	char ai_bitrate[16];
	char ai_samplerate[16];
	/* Report the OUTPUT rate the encoder produces (out_samplerate), falling
	 * back to the input rate before the encoder has initialized.  This keeps
	 * the Icecast admin page honest when the user selects a stream samplerate
	 * that differs from the OBS input rate (MP3 resampling). */
	uint32_t sample_rate = context->out_samplerate;
	if (!sample_rate)
		sample_rate = context->sample_rate ? context->sample_rate : 48000;
	snprintf(ai_bitrate, sizeof(ai_bitrate), "%d", context->bitrate);
	snprintf(ai_samplerate, sizeof(ai_samplerate), "%u", sample_rate);
	shout_set_audio_info(shout, SHOUT_AI_BITRATE, ai_bitrate);
	shout_set_audio_info(shout, SHOUT_AI_SAMPLERATE, ai_samplerate);
	/* MP3 mono mode produces a single-channel stream; everything else is
	 * stereo.  Opus/Vorbis ignore channel_mode (always stereo here). */
	const bool mono = (context->codec == RADIO_CODEC_MP3 && context->channel_mode == RADIO_CHANNEL_MONO);
	shout_set_audio_info(shout, SHOUT_AI_CHANNELS, mono ? "1" : "2");
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

/*
 * encoder_send_thread — dequeues encoded bytes (MP3 frames, Ogg pages, …)
 * from the ring buffer and forwards them to libshout with bitrate pacing
 * via shout_sync().
 *
 * Running shout_send + shout_sync on a dedicated thread keeps the OBS
 * audio callback (raw_audio) fast: it only encodes and writes to the ring
 * buffer.  This is codec-agnostic; the encoder's output format is
 * transparent at this layer.
 */
static void *encoder_send_thread(void *data)
{
	struct radio_output *context = data;
	uint8_t scratch[4096];

	while (context->send_running) {
		pthread_mutex_lock(&context->send_mutex);
		while (radio_send_buf_used(&context->send_buf) == 0 && context->send_running)
			pthread_cond_wait(&context->send_cond, &context->send_mutex);
		if (!context->send_running) {
			pthread_mutex_unlock(&context->send_mutex);
			break;
		}

		size_t to_read = radio_send_buf_read(&context->send_buf, scratch, sizeof(scratch));
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
				/* OBS_OUTPUT_ERROR (not _DISCONNECTED) so OBS's own
				 * framework reconnect doesn't kick in and re-call
				 * radio_output_start behind our back; the user's
				 * auto-reconnect-disabled choice means stop here. */
				set_state(context, RADIO_STATE_ERROR);
				obs_output_signal_stop(context->output, OBS_OUTPUT_ERROR);
			}
			continue;
		}
	}
	return NULL;
}

/*
 * send_buf_push — append bytes to the shared ring buffer and wake the send
 * thread.  Used by the audio thread (encoder output) and the start path
 * (to queue Opus startup pages before audio begins).  Drops oldest data
 * if the buffer would overflow.
 */
static void send_buf_push(struct radio_output *context, const uint8_t *bytes, size_t len)
{
	if (!context->send_buf.data || len == 0)
		return;

	pthread_mutex_lock(&context->send_mutex);
	size_t dropped = radio_send_buf_push(&context->send_buf, bytes, len);
	if (dropped)
		obs_log(LOG_WARNING, "send buffer full, dropped %zu bytes", dropped);
	pthread_cond_signal(&context->send_cond);
	pthread_mutex_unlock(&context->send_mutex);
}

/* -------------------------------------------------------------------------
 * Async initial connect (#61)
 *
 * shout_open blocks — DNS, TCP connect, the libshout greeting, and (with TLS
 * on) the full TLS handshake all run inside it; ~14 s observed against a
 * server that silently eats the ClientHello.  radio_output_start runs on the
 * Qt main thread (dock Start, Tools menu, the streaming auto-start handler,
 * and the obs-websocket radio.start verb all marshal to it), so the open
 * happens on a short-lived detached connect thread instead: start returns
 * immediately with state CONNECTING and the thread finishes the start —
 * publish the handle, begin data capture, stand up the send thread — when
 * shout_open returns.  Failures surface via
 * obs_output_signal_stop(OBS_OUTPUT_CONNECT_FAILED), the same channel the
 * reconnect give-up path already uses.
 *
 * Cancellation: shout_open cannot be interrupted, so Stop must never wait
 * for it.  The job below is refcounted — the connect thread and
 * context->connect_job each hold one ref.  Teardown atomically takes the
 * context's pointer (under state_mutex), flags the job canceled (under
 * job->mutex), and moves on; the blocked open finishes in the background and
 * hands an opened handle to shout_handoff_cleanup.  A canceled thread never
 * touches the context again, so destroy can free it safely.  Conversely,
 * while the thread holds job->mutex with canceled still false, teardown is
 * blocked at the flag-set, which keeps the context alive for the entire
 * completion.
 * ---------------------------------------------------------------------- */

struct radio_connect_job {
	struct radio_output *context; /* read only while !canceled under mutex */
	shout_t *shout;               /* configured + unopened at spawn; thread-owned */
	uint8_t *startup_bytes;       /* container startup pages (Ogg headers); job-owned */
	size_t startup_len;
	pthread_mutex_t mutex; /* guards canceled/completed; serializes completion vs cancel */
	bool canceled;
	bool completed;
	volatile long refs;
};

static void connect_job_release(struct radio_connect_job *job)
{
	if (os_atomic_dec_long(&job->refs) > 0)
		return;
	pthread_mutex_destroy(&job->mutex);
	bfree(job);
}

static void log_shout_open_failure(struct radio_output *context, shout_t *shout, int err)
{
	/* Surface TLS-specific failures with actionable text rather than
	 * libshout's generic error string, which often reads as an
	 * opaque socket/TLS mashup.  SHOUTERR_NOTLS fires only when the
	 * server cleanly signals "no TLS" via an HTTP response; plain
	 * servers that silently eat a TLS ClientHello yield
	 * SHOUTERR_SOCKET after a ~14s TLS handshake timeout, so when
	 * use_tls is on and we hit any non-TLS-specific failure, add
	 * a TLS-may-be-the-cause hint so the user knows what to try. */
	if (err == SHOUTERR_NOTLS) {
		obs_log(LOG_ERROR, "TLS requested but the server does not support it; "
				   "either disable TLS in Tools → Radio Output… or use a TLS-capable server");
	} else if (err == SHOUTERR_TLSBADCERT) {
		obs_log(LOG_ERROR, "TLS certificate validation failed — server cert not trusted by the OS CA store, "
				   "or hostname does not match cert CN/SAN");
	} else if (context->use_tls) {
		obs_log(LOG_ERROR,
			"shout_open() failed with TLS enabled: %s. If the server does not speak TLS on "
			"this port, uncheck 'Use TLS (HTTPS)' in Tools → Radio Output… and retry",
			shout_get_error(shout));
	} else {
		obs_log(LOG_ERROR, "shout_open() failed: %s", shout_get_error(shout));
	}
}

/*
 * connect_complete — finish the start sequence after a successful shout_open:
 * publish the handle, begin data capture, stand up the send thread, queue
 * container startup bytes.  Mirrors the pre-async inline code that lived in
 * radio_output_start.  Runs on the connect thread with job->mutex held and
 * the context guaranteed alive.  Returns false with everything it created
 * torn back down (the caller then signals OBS_OUTPUT_CONNECT_FAILED).
 */
static bool connect_complete(struct radio_output *context, shout_t *shout, const uint8_t *startup_bytes,
			     size_t startup_len)
{
	context->shout = shout;
	set_state(context, RADIO_STATE_CONNECTED);
	context->reconnect_attempts = 0;
	obs_log(LOG_INFO, "Connected to %s:%d%s", context->host, context->port, context->mount);

	if (!obs_output_begin_data_capture(context->output, 0)) {
		obs_log(LOG_ERROR, "obs_output_begin_data_capture() failed");
		goto fail_after_open;
	}

	/* Initialize mutex/cond BEFORE making send_buf visible to raw_audio;
	 * radio_send_buf_init sets the data pointer last, so raw_audio's
	 * send_buf.data "ready" gate only opens once the buffer is usable. */
	context->send_running = true;
	pthread_mutex_init(&context->send_mutex, NULL);
	pthread_cond_init(&context->send_cond, NULL);
	if (!radio_send_buf_init(&context->send_buf, SEND_BUF_CAPACITY)) {
		obs_log(LOG_ERROR, "Failed to allocate send buffer");
		context->send_running = false;
		pthread_mutex_destroy(&context->send_mutex);
		pthread_cond_destroy(&context->send_cond);
		goto fail_after_capture;
	}
	if (pthread_create(&context->send_thread, NULL, encoder_send_thread, context) != 0) {
		obs_log(LOG_ERROR, "Failed to create encoder send thread");
		context->send_running = false;
		radio_send_buf_free(&context->send_buf);
		pthread_mutex_destroy(&context->send_mutex);
		pthread_cond_destroy(&context->send_cond);
		goto fail_after_capture;
	}
	obs_log(LOG_INFO, "Encoder send thread started (codec=%s)", context->encoder->name);

	/* Queue container startup bytes (Ogg codecs: header pages) so the
	 * send thread ships them before any audio.  MP3 has none. */
	if (startup_bytes && startup_len > 0)
		send_buf_push(context, startup_bytes, startup_len);

	return true;

fail_after_capture:
	obs_output_end_data_capture(context->output);
fail_after_open:
	shout_close(context->shout);
	shout_free(context->shout);
	context->shout = NULL;
	return false;
}

static void *connect_thread_fn(void *data)
{
	struct radio_connect_job *job = data;

	const int err = shout_open(job->shout);

	pthread_mutex_lock(&job->mutex);
	if (job->canceled) {
		/* Stop/destroy won the race; the context may already be freed.
		 * Clean up only what the job owns. */
		pthread_mutex_unlock(&job->mutex);
		if (err == SHOUTERR_SUCCESS)
			shout_handoff_cleanup(job->shout);
		else
			shout_free(job->shout);
		bfree(job->startup_bytes);
		connect_job_release(job);
		return NULL;
	}

	/* Not canceled while holding job->mutex ⇒ teardown is blocked at its
	 * cancel step, so the context stays alive for this whole block. */
	struct radio_output *context = job->context;
	bool ok = false;

	if (err == SHOUTERR_SUCCESS) {
		ok = connect_complete(context, job->shout, job->startup_bytes, job->startup_len);
	} else {
		log_shout_open_failure(context, job->shout, err);
		shout_free(job->shout);
	}

	if (!ok) {
		/* Mirror the synchronous failure path: drop the encoder under
		 * encoder_mutex (a raw_audio callback could be in flight if
		 * connect_complete failed after begin_data_capture), mark
		 * ERROR so the dock shows red until the user acknowledges. */
		pthread_mutex_lock(&context->encoder_mutex);
		if (context->encoder) {
			context->encoder->destroy(context);
			context->encoder = NULL;
		}
		pthread_mutex_unlock(&context->encoder_mutex);
		set_state(context, RADIO_STATE_ERROR);
	}

	/* Detach the job from the context.  The == check matters: teardown may
	 * have already swapped the pointer out while waiting on job->mutex, in
	 * which case the field ref is teardown's to drop, not ours. */
	bool drop_field_ref = false;
	pthread_mutex_lock(&context->state_mutex);
	if (context->connect_job == job) {
		context->connect_job = NULL;
		drop_field_ref = true;
	}
	pthread_mutex_unlock(&context->state_mutex);

	if (!ok)
		obs_output_signal_stop(context->output, OBS_OUTPUT_CONNECT_FAILED);

	job->completed = true;
	pthread_mutex_unlock(&job->mutex);

	bfree(job->startup_bytes);
	if (drop_field_ref)
		connect_job_release(job);
	connect_job_release(job);
	return NULL;
}

/*
 * connect_cancel — detach and cancel any in-flight async connect.  Never
 * waits out a blocked shout_open: the job is flagged canceled and the
 * connect thread cleans up after itself in the background.  If the thread
 * is mid-completion, the brief job->mutex wait here lets it finish, after
 * which the caller (teardown) tears down the now-started output normally.
 */
static void connect_cancel(struct radio_output *context)
{
	pthread_mutex_lock(&context->state_mutex);
	struct radio_connect_job *job = context->connect_job;
	context->connect_job = NULL;
	pthread_mutex_unlock(&context->state_mutex);
	if (!job)
		return;

	pthread_mutex_lock(&job->mutex);
	job->canceled = true;
	const bool was_pending = !job->completed;
	pthread_mutex_unlock(&job->mutex);

	if (was_pending)
		obs_log(LOG_INFO, "connect attempt canceled; any in-flight handshake is reaped in the background");
	connect_job_release(job);
}
#endif /* HAVE_LIBSHOUT */

/*
 * In-band "Now Playing" metadata for container codecs (Opus/Vorbis, #67).
 * Icecast discards admin/ICY metadata on Ogg mounts, so the encoder chains a
 * new Ogg logical stream whose OpusTags / VorbisComment carries the title; we
 * ship those bytes through the ring buffer so they interleave with the audio.
 *
 * Runs under encoder_mutex — the same lock encode_frame trylocks — so the
 * container reset can't race an in-flight audio encode.  Acts only while
 * CONNECTED.  Returns false (no-op) when the active codec has no in-band path
 * (MP3) or we're not streaming; the caller then uses the libshout path.
 */
bool radio_output_emit_inband_metadata(struct radio_output *context, const char *title)
{
#ifndef HAVE_LIBSHOUT
	UNUSED_PARAMETER(context);
	UNUSED_PARAMETER(title);
	return false;
#else
	if (!context || !context->encoder || !context->encoder->update_metadata)
		return false;

	pthread_mutex_lock(&context->state_mutex);
	bool connected = (context->state == RADIO_STATE_CONNECTED);
	pthread_mutex_unlock(&context->state_mutex);
	if (!connected) {
		obs_log(LOG_WARNING, "metadata update failed: not connected");
		return false;
	}

	uint8_t *bytes = NULL;
	size_t len = 0;
	pthread_mutex_lock(&context->encoder_mutex);
	int rc = context->encoder->update_metadata(context, title, &bytes, &len);
	pthread_mutex_unlock(&context->encoder_mutex);

	if (rc != 0) {
		bfree(bytes);
		return false;
	}
	if (bytes && len)
		send_buf_push(context, bytes, len);
	bfree(bytes);
	return true;
#endif
}

/* -------------------------------------------------------------------------
 * MP3 encoder vtable (libmp3lame)
 *
 * Thin adapters over lame_* calls so the rest of radio-output.c can dispatch
 * through context->encoder instead of hard-coding codec paths.  Opus lives
 * in radio-opus-encoder.c; add further codecs the same way.
 * ---------------------------------------------------------------------- */

#ifdef HAVE_LAME
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — sample_rate/channels mirror the LAME API call order
static bool mp3_encoder_init(struct radio_output *context, uint32_t sample_rate, int channels, uint8_t **out_headers,
			     size_t *out_bytes)
{
	*out_headers = NULL;
	*out_bytes = 0;

	context->lame_gfp = lame_init();
	if (!context->lame_gfp) {
		obs_log(LOG_ERROR, "lame_init() failed");
		return false;
	}
	/* libmp3lame resamples internally when in != out.  out=0 tells LAME to
	 * match the input rate (the historical default); a user-selected target
	 * routes through LAME's resampler. */
	const int out_rate = context->stream_samplerate ? (int)context->stream_samplerate : 0;
	lame_set_in_samplerate(context->lame_gfp, (int)sample_rate);
	lame_set_num_channels(context->lame_gfp, channels);
	lame_set_out_samplerate(context->lame_gfp, out_rate);
	/* Bitrate mode: CBR (fixed `bitrate`), ABR (average `bitrate`), or VBR
	 * (quality-driven within [min,max]).  min/max are applied only when set
	 * (>0) so an unconfigured value doesn't clamp LAME's own defaults. */
	const char *brate_mode_str = "cbr";
	switch (context->bitrate_mode) {
	case RADIO_BITRATE_ABR:
		brate_mode_str = "abr";
		lame_set_VBR(context->lame_gfp, vbr_abr);
		lame_set_VBR_mean_bitrate_kbps(context->lame_gfp, context->bitrate);
		if (context->vbr_min_bitrate > 0)
			lame_set_VBR_min_bitrate_kbps(context->lame_gfp, context->vbr_min_bitrate);
		if (context->vbr_max_bitrate > 0)
			lame_set_VBR_max_bitrate_kbps(context->lame_gfp, context->vbr_max_bitrate);
		break;
	case RADIO_BITRATE_VBR: {
		brate_mode_str = "vbr";
		int vq = context->vbr_quality;
		if (vq < 0 || vq > 9)
			vq = 4;
		lame_set_VBR(context->lame_gfp, vbr_default);
		lame_set_VBR_q(context->lame_gfp, vq);
		if (context->vbr_min_bitrate > 0)
			lame_set_VBR_min_bitrate_kbps(context->lame_gfp, context->vbr_min_bitrate);
		if (context->vbr_max_bitrate > 0)
			lame_set_VBR_max_bitrate_kbps(context->lame_gfp, context->vbr_max_bitrate);
		break;
	}
	case RADIO_BITRATE_CBR:
	default:
		lame_set_VBR(context->lame_gfp, vbr_off);
		lame_set_brate(context->lame_gfp, context->bitrate);
		break;
	}
	/* Encoding quality 0 (best/slowest) .. 9 (fastest); clamp defensively. */
	int quality = context->lame_quality;
	if (quality < 0 || quality > 9)
		quality = 2;
	lame_set_quality(context->lame_gfp, quality);
	/* Channel mode.  num_channels above stays at the stereo input count; LAME
	 * downmixes L+R internally when the output MPEG_mode is MONO, so the audio
	 * callback path is unchanged. */
	MPEG_mode lame_mode = STEREO;
	switch (context->channel_mode) {
	case RADIO_CHANNEL_JOINT_STEREO:
		lame_mode = JOINT_STEREO;
		break;
	case RADIO_CHANNEL_MONO:
		lame_mode = MONO;
		break;
	case RADIO_CHANNEL_STEREO:
	default:
		lame_mode = STEREO;
		break;
	}
	lame_set_mode(context->lame_gfp, lame_mode);
	if (lame_init_params(context->lame_gfp) < 0) {
		obs_log(LOG_ERROR, "lame_init_params() failed");
		lame_close(context->lame_gfp);
		context->lame_gfp = NULL;
		return false;
	}
	/* lame_get_out_samplerate() reflects LAME's final choice (it snaps to the
	 * nearest valid MPEG rate), so report that — not the requested value. */
	context->out_samplerate = (uint32_t)lame_get_out_samplerate(context->lame_gfp);
	const char *mode_str = "stereo";
	if (lame_mode == MONO)
		mode_str = "mono";
	else if (lame_mode == JOINT_STEREO)
		mode_str = "joint-stereo";
	if (context->out_samplerate != sample_rate)
		obs_log(LOG_INFO, "MP3 encoder: %u Hz in -> %u Hz out (resampled), %s, q%d, %s %d kbps", sample_rate,
			context->out_samplerate, mode_str, quality, brate_mode_str, context->bitrate);
	else
		obs_log(LOG_INFO, "MP3 encoder: %u Hz, %s, q%d, %s %d kbps", sample_rate, mode_str, quality,
			brate_mode_str, context->bitrate);
	return true;
}

static void mp3_encoder_destroy(struct radio_output *context)
{
	if (context->lame_gfp) {
		lame_close(context->lame_gfp);
		context->lame_gfp = NULL;
	}
}

static int mp3_encoder_encode_frame(struct radio_output *context, const float *left, const float *right, size_t frames,
				    uint8_t *out, size_t cap)
{
	if (!context->lame_gfp)
		return -1;
	return lame_encode_buffer_ieee_float(context->lame_gfp, left, right, (int)frames, out, (int)cap);
}

static int mp3_encoder_flush(struct radio_output *context, uint8_t *out, size_t cap)
{
	if (!context->lame_gfp)
		return 0;
	int flushed = lame_encode_flush(context->lame_gfp, out, (int)cap);
	return flushed < 0 ? -1 : flushed;
}

static size_t mp3_encoder_max_output_for(struct radio_output *context, size_t frames)
{
	UNUSED_PARAMETER(context);
	/* LAME docs: worst-case 1.25 * nsamples + 7200 bytes. */
	return (size_t)(1.25f * (float)frames) + 7200;
}

static int mp3_encoder_on_reconnect(struct radio_output *context, uint8_t **out_headers, size_t *out_bytes)
{
	UNUSED_PARAMETER(context);
	/* MP3 is frame-synced — decoders resync on the next frame header.  No
	 * container metadata to re-emit; the LAME encoder keeps its state. */
	*out_headers = NULL;
	*out_bytes = 0;
	return 0;
}

const struct radio_encoder_ops radio_encoder_mp3 = {
	.name = "mp3",
#ifdef HAVE_LIBSHOUT
	.shout_format = SHOUT_FORMAT_MP3,
#else
	.shout_format = 0,
#endif
	.init = mp3_encoder_init,
	.destroy = mp3_encoder_destroy,
	.encode_frame = mp3_encoder_encode_frame,
	.flush = mp3_encoder_flush,
	.max_output_for = mp3_encoder_max_output_for,
	.on_reconnect = mp3_encoder_on_reconnect,
};
#else  /* !HAVE_LAME */
static bool mp3_stub_init(struct radio_output *context, uint32_t sample_rate, int channels, uint8_t **out_headers,
			  size_t *out_bytes)
{
	UNUSED_PARAMETER(context);
	UNUSED_PARAMETER(sample_rate);
	UNUSED_PARAMETER(channels);
	*out_headers = NULL;
	*out_bytes = 0;
	obs_log(LOG_ERROR, "MP3 encoding not available — rebuild with libmp3lame");
	return false;
}
static void mp3_stub_destroy(struct radio_output *context)
{
	UNUSED_PARAMETER(context);
}
static int mp3_stub_encode_frame(struct radio_output *context, const float *left, const float *right, size_t frames,
				 uint8_t *out, size_t cap)
{
	UNUSED_PARAMETER(context);
	UNUSED_PARAMETER(left);
	UNUSED_PARAMETER(right);
	UNUSED_PARAMETER(frames);
	UNUSED_PARAMETER(out);
	UNUSED_PARAMETER(cap);
	return -1;
}
static int mp3_stub_flush(struct radio_output *context, uint8_t *out, size_t cap)
{
	UNUSED_PARAMETER(context);
	UNUSED_PARAMETER(out);
	UNUSED_PARAMETER(cap);
	return 0;
}
static size_t mp3_stub_max_output_for(struct radio_output *context, size_t frames)
{
	UNUSED_PARAMETER(context);
	UNUSED_PARAMETER(frames);
	return 0;
}
static int mp3_stub_on_reconnect(struct radio_output *context, uint8_t **out_headers, size_t *out_bytes)
{
	UNUSED_PARAMETER(context);
	*out_headers = NULL;
	*out_bytes = 0;
	return 0;
}
const struct radio_encoder_ops radio_encoder_mp3 = {
	.name = "mp3",
	.shout_format = 0,
	.init = mp3_stub_init,
	.destroy = mp3_stub_destroy,
	.encode_frame = mp3_stub_encode_frame,
	.flush = mp3_stub_flush,
	.max_output_for = mp3_stub_max_output_for,
	.on_reconnect = mp3_stub_on_reconnect,
};
#endif /* HAVE_LAME */

static bool radio_output_start(void *data)
{
	struct radio_output *context = data;

#ifndef HAVE_LIBSHOUT
	obs_log(LOG_WARNING, "Streaming not available — libshout not present on this platform");
	obs_output_signal_stop(context->output, OBS_OUTPUT_CONNECT_FAILED);
	return false;
#else
	/* SHOUTcast v1 (ICY) only carries MP3.  Warn loudly if the user
	 * picked Opus or Vorbis alongside SHOUTcast — the connection will
	 * almost certainly be rejected by the server and we want the log
	 * to make the cause obvious. */
	if (context->protocol == RADIO_PROTOCOL_SHOUTCAST && context->codec != RADIO_CODEC_MP3) {
		obs_log(LOG_WARNING,
			"SHOUTcast v1 protocol only supports MP3; selected codec is likely to be rejected by the server");
	}

	/* --- Select + initialize encoder --- */
	struct obs_audio_info oai;
	context->sample_rate = 48000;
	context->channels = 2;
	if (obs_get_audio_info(&oai))
		context->sample_rate = oai.samples_per_sec;

	switch (context->codec) {
	case RADIO_CODEC_OPUS:
		context->encoder = &radio_encoder_opus;
		break;
	case RADIO_CODEC_VORBIS:
		context->encoder = &radio_encoder_vorbis;
		break;
	case RADIO_CODEC_MP3:
	default:
		context->encoder = &radio_encoder_mp3;
		break;
	}

	uint8_t *startup_bytes = NULL;
	size_t startup_len = 0;
	if (!context->encoder->init(context, context->sample_rate, context->channels, &startup_bytes, &startup_len)) {
		/* init() cleans up its own partial state on failure. */
		context->encoder = NULL;
		obs_output_signal_stop(context->output, OBS_OUTPUT_CONNECT_FAILED);
		return false;
	}

	/* --- Configure libshout --- */
	shout_t *shout = shout_new();
	if (!shout) {
		obs_log(LOG_ERROR, "shout_new() failed (out of memory?)");
		context->encoder->destroy(context);
		context->encoder = NULL;
		bfree(startup_bytes);
		obs_output_signal_stop(context->output, OBS_OUTPUT_CONNECT_FAILED);
		return false;
	}

	shout_apply_settings(context, shout);

	/* --- Open connection (async, #61) ---
	 * shout_open blocks and this callback runs on the Qt main thread, so
	 * the open happens on a short-lived detached connect thread.  State is
	 * CONNECTING before the thread spawns so the dock's next poll shows it
	 * without a DISCONNECTED flicker; the thread finishes the start (or
	 * signals CONNECT_FAILED) when shout_open returns.  context->shout
	 * stays NULL until the thread publishes the opened handle. */
	set_state(context, RADIO_STATE_CONNECTING);

	struct radio_connect_job *job = bzalloc(sizeof(*job));
	job->context = context;
	job->shout = shout;
	job->startup_bytes = startup_bytes;
	job->startup_len = startup_len;
	pthread_mutex_init(&job->mutex, NULL);
	job->refs = 2; /* connect thread + context->connect_job */

	pthread_mutex_lock(&context->state_mutex);
	context->connect_job = job;
	pthread_mutex_unlock(&context->state_mutex);

	pthread_t connect_tid;
	if (pthread_create(&connect_tid, NULL, connect_thread_fn, job) != 0) {
		obs_log(LOG_ERROR, "Failed to create connect thread");
		pthread_mutex_lock(&context->state_mutex);
		context->connect_job = NULL;
		pthread_mutex_unlock(&context->state_mutex);
		pthread_mutex_destroy(&job->mutex);
		bfree(job);
		shout_free(shout);
		context->encoder->destroy(context);
		context->encoder = NULL;
		bfree(startup_bytes);
		set_state(context, RADIO_STATE_ERROR);
		obs_output_signal_stop(context->output, OBS_OUTPUT_CONNECT_FAILED);
		return false;
	}
	pthread_detach(connect_tid);

	obs_log(LOG_INFO, "Connecting to %s:%d%s ...", context->host, context->port, context->mount);
	return true;
#endif
}

static void radio_output_stop(void *data, uint64_t ts)
{
	UNUSED_PARAMETER(ts);
	radio_output_teardown((struct radio_output *)data);
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

	/* ALL access to context->encoder / encoder_priv happens under
	 * encoder_mutex, serialized against the reconnect thread's full re-init
	 * (encoder->on_reconnect) AND teardown's encoder->destroy.  trylock (not
	 * lock) so we never block libobs's shared audio thread: if a re-init or
	 * teardown is in progress we drop this callback's audio, which lands in a
	 * reconnect/teardown gap and is inaudible.  send_buf.data is the "ready"
	 * gate (set last by start, freed in teardown); re-checked here under the
	 * lock so a half-rebuilt or freed encoder is never touched. */
	if (pthread_mutex_trylock(&context->encoder_mutex) != 0)
		return;
	if (!context->encoder || !context->send_buf.data) {
		pthread_mutex_unlock(&context->encoder_mutex);
		return;
	}

	const float *left = (const float *)frames->data[0];
	const float *right = frames->data[1] ? (const float *)frames->data[1] : left;

	size_t cap = context->encoder->max_output_for(context, frames->frames);
	if (cap < 4096)
		cap = 4096;
	uint8_t *buf = bmalloc(cap);
	int bytes = context->encoder->encode_frame(context, left, right, (size_t)frames->frames, buf, cap);
	pthread_mutex_unlock(&context->encoder_mutex);

	/* send_buf_push runs AFTER the unlock (it takes send_mutex; the ring
	 * buffer no-ops on a freed/zero-capacity buffer, so this is safe even if
	 * teardown freed send_buf in the meantime). */
	if (bytes > 0)
		send_buf_push(context, buf, (size_t)bytes);
	else if (bytes < 0)
		obs_log(LOG_ERROR, "[%s] encode_frame error: %d", context->encoder->name, bytes);

	bfree(buf);
#endif /* HAVE_LIBSHOUT */
}

static bool reconnect_toggled(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(property);
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
	obs_properties_add_bool(server, SETTING_TLS, obs_module_text("RadioOutput.Server.UseTLS"));
	obs_properties_add_group(props, "server", obs_module_text("RadioOutput.Server.Group"), OBS_GROUP_NORMAL,
				 server);

	/* ---- Audio ---- */
	obs_properties_t *audio = obs_properties_create();

	obs_property_t *codec = obs_properties_add_list(audio, SETTING_CODEC,
							obs_module_text("RadioOutput.Audio.Codec"), OBS_COMBO_TYPE_LIST,
							OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(codec, obs_module_text("RadioOutput.Audio.Codec.MP3"), RADIO_CODEC_MP3);
	obs_property_list_add_int(codec, obs_module_text("RadioOutput.Audio.Codec.Opus"), RADIO_CODEC_OPUS);
	obs_property_list_add_int(codec, obs_module_text("RadioOutput.Audio.Codec.Vorbis"), RADIO_CODEC_VORBIS);

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
	obs_data_set_default_bool(settings, SETTING_START_WITH_STREAMING, false);
	obs_data_set_default_int(settings, SETTING_PROTOCOL, RADIO_PROTOCOL_ICECAST);
	obs_data_set_default_bool(settings, SETTING_TLS, false);
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
