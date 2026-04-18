#pragma once

#include <obs-module.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef HAVE_LIBSHOUT
#include <shout/shout.h>
#endif

#ifdef HAVE_LAME
#include <lame/lame.h>
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

	// Runtime state
	radio_state_t state;
	pthread_mutex_t state_mutex;


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

	/* MP3 sender thread — encodes in raw_audio, sends + syncs here */
#define SEND_BUF_CAPACITY (256 * 1024) /* 256 KB ≈ 14 s at 128 kbps */
	uint8_t         *send_buf;
	size_t           send_wpos;    /* monotonically increasing write position */
	size_t           send_rpos;    /* monotonically increasing read position */
	pthread_mutex_t  send_mutex;
	pthread_cond_t   send_cond;
	pthread_t        send_thread;
	volatile bool    send_running;
#endif
};

// Codec identifiers
#define RADIO_CODEC_OPUS   0
#define RADIO_CODEC_MP3    1
#define RADIO_CODEC_VORBIS 2 // Phase 2

static inline void set_state(struct radio_output *context, radio_state_t new_state)
{
	pthread_mutex_lock(&context->state_mutex);
	context->state = new_state;
	pthread_mutex_unlock(&context->state_mutex);
}

// Settings key names (used in obs_data_get_* calls)
#define SETTING_HOST            "host"
#define SETTING_PORT            "port"
#define SETTING_MOUNT           "mount"
#define SETTING_PASSWORD        "password"
#define SETTING_CODEC           "codec"
#define SETTING_BITRATE         "bitrate"
#define SETTING_RECONNECT       "reconnect_enabled"
#define SETTING_RECONNECT_DELAY "reconnect_delay"
#define SETTING_RECONNECT_MAX   "reconnect_max"

extern struct obs_output_info radio_output_info;
