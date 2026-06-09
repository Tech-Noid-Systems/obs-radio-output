#pragma once

#include <obs-module.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#include "send-buf.h"

#ifdef HAVE_LIBSHOUT
#include <shout/shout.h>
#endif

#ifdef HAVE_LAME
#include <lame/lame.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define RADIO_OUTPUT_RECONNECT_DELAY_MS  5000
#define RADIO_OUTPUT_RECONNECT_MAX       10

typedef enum {
	RADIO_STATE_DISCONNECTED,
	RADIO_STATE_CONNECTING,
	RADIO_STATE_CONNECTED,
	RADIO_STATE_RECONNECTING,
	RADIO_STATE_ERROR,
} radio_state_t;

/* Forward-declared; the full struct lives in radio-encoder.h. */
struct radio_encoder_ops;

struct radio_output {
	obs_output_t *output; // back-pointer to OBS output object
#ifdef HAVE_LIBSHOUT
	shout_t *shout; // libshout connection handle
#endif

	// Connection settings (copied from obs_data at start)
	char *host;
	int port;
	char *mount;
	char *password;
	bool use_tls; // Phase 2
	int protocol; // SHOUT_PROTOCOL_HTTP or SHOUT_PROTOCOL_ICY

	// Encoder settings
	int codec; // RADIO_CODEC_OPUS, RADIO_CODEC_MP3, etc.
	int bitrate;

	/* Codec dispatch.  Selected in radio_output_start() from the codec id
	 * (RADIO_CODEC_MP3 / RADIO_CODEC_OPUS).  NULL means no encoder is
	 * currently active — raw_audio must bail. */
	const struct radio_encoder_ops *encoder;

	/* Opaque per-encoder state.  Ownership lives with the encoder:
	 * radio_encoder_ops->init allocates it, ->destroy frees it.  MP3 uses
	 * lame_gfp instead (historical), so this is only populated by Opus
	 * today.  Do not touch from outside an encoder callback. */
	void *encoder_priv;

	/* Ambient samplerate at stream start (Hz).  Set in radio_output_start
	 * from obs_get_audio_info; this is the INPUT rate the raw_audio callback
	 * delivers. */
	uint32_t sample_rate;
	int channels; /* 1 or 2 */

	/* User-selected target STREAM samplerate (Hz), from SETTING_STREAM_SAMPLERATE.
	 * 0 = "match OBS" (use sample_rate).  Only MP3 can honor a value different
	 * from the input rate (libmp3lame has a built-in resampler); Opus is always
	 * 48 kHz and Vorbis has no resampler, so both warn-and-ignore a mismatch. */
	uint32_t stream_samplerate;

	/* Effective OUTPUT samplerate (Hz) the encoder actually produces.  Set by
	 * each encoder's init(): MP3 = resampled target, Opus = 48000, Vorbis =
	 * input rate.  Reported to Icecast via SHOUT_AI_SAMPLERATE so the admin
	 * page matches what listeners receive. */
	uint32_t out_samplerate;

	// Runtime state
	radio_state_t state;
	pthread_mutex_t state_mutex;

	/* Serializes the audio thread's encode_frame (raw_audio) against the
	 * reconnect thread's full encoder re-init (encoder->on_reconnect).  The
	 * audio thread trylocks and DROPS its callback on contention rather than
	 * block libobs's shared audio thread; reconnect holds it across the
	 * re-init.  Without this, the audio thread can call vorbis_analysis_buffer
	 * on a half-rebuilt Vorbis DSP state → null deref / crash. */
	pthread_mutex_t encoder_mutex;

	// Reconnect settings
	bool reconnect_enabled;
	int reconnect_delay_ms;
	int reconnect_max_retries;
	int reconnect_attempts;

	// Reconnect thread
	pthread_t reconnect_thread;
	bool reconnect_active;           // true = thread was spawned and must be joined
	volatile bool reconnect_running; // false = signal thread to stop

#ifdef HAVE_LAME
	lame_global_flags *lame_gfp; /* LAME MP3 encoder handle — NULL when inactive */
#endif

#ifdef HAVE_LIBSHOUT
	/*
	 * Shared sender thread.  The encoder writes compressed bytes into
	 * send_buf from the audio thread; this thread drains them to libshout
	 * with shout_sync() pacing.  Originally MP3-only (hence the naming in
	 * older logs); now generic across MP3 and Opus.  The ring buffer itself
	 * lives in send-buf.{c,h}; send_mutex serializes producer/consumer access
	 * to it (see send_buf_push and encoder_send_thread).
	 */
#define SEND_BUF_CAPACITY (256 * 1024) /* 256 KB ≈ 14 s at 128 kbps */
	struct radio_send_buf send_buf;
	pthread_mutex_t send_mutex;
	pthread_cond_t send_cond;
	pthread_t send_thread;
	volatile bool send_running;
#endif
};

// Codec identifiers
#define RADIO_CODEC_OPUS   0
#define RADIO_CODEC_MP3    1
#define RADIO_CODEC_VORBIS 2 // Phase 2

// Protocol identifiers for the config + UI.  Mapped to libshout's
// SHOUT_PROTOCOL_HTTP / SHOUT_PROTOCOL_ICY in shout_apply_settings.
#define RADIO_PROTOCOL_ICECAST   0
#define RADIO_PROTOCOL_SHOUTCAST 1

static inline void set_state(struct radio_output *context, radio_state_t new_state)
{
	pthread_mutex_lock(&context->state_mutex);
	context->state = new_state;
	pthread_mutex_unlock(&context->state_mutex);
}

#ifdef HAVE_LIBSHOUT
/**
 * shout_apply_settings - Configure a fresh shout_t handle with the plugin's
 * Icecast settings (host, port, mount, password, agent, protocol, content
 * format, audio info).
 *
 * Used by both the initial connect (radio_output_start) and the reconnect
 * path (reconnect.c) so the two cannot drift.  Caller owns shout_new() and
 * shout_open().
 */
void shout_apply_settings(struct radio_output *context, shout_t *shout);
#endif

// Settings key names (used in obs_data_get_* calls)
#define SETTING_HOST                 "host"
#define SETTING_PORT                 "port"
#define SETTING_MOUNT                "mount"
#define SETTING_PASSWORD             "password"
#define SETTING_CODEC                "codec"
#define SETTING_BITRATE              "bitrate"
#define SETTING_RECONNECT            "reconnect_enabled"
#define SETTING_RECONNECT_DELAY      "reconnect_delay"
#define SETTING_RECONNECT_MAX        "reconnect_max"
#define SETTING_START_WITH_STREAMING "start_with_streaming"
#define SETTING_PROTOCOL             "protocol"
#define SETTING_TLS                  "tls_enabled"
#define SETTING_STREAM_SAMPLERATE    "stream_samplerate"

extern struct obs_output_info radio_output_info;

/*
 * Returns the currently instantiated output's context, or NULL if none is
 * active.  Intended for the dock widget and frontend event handler to
 * read connection state.  Callers that read state fields must acquire
 * context->state_mutex — see radio_output_get_active() comment in
 * radio-output.c for the lifetime guarantee.
 */
struct radio_output *radio_output_get_active(void);

#ifdef __cplusplus
}
#endif
