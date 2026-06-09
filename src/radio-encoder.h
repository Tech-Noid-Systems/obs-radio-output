// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct radio_output;

/*
 * Codec vtable.  One instance per supported codec (radio_encoder_mp3,
 * radio_encoder_opus, …).  Selected at radio_output_start() time based on
 * context->codec, then dispatched through context->encoder.
 *
 * The encoder is responsible for turning planar IEEE-float PCM into the
 * bytes that go on the wire (MP3 frames, Ogg/Opus pages, etc.).  The ring
 * buffer, send thread, reconnect logic, and libshout handling stay in
 * radio-output.c; encoders only produce bytes.
 *
 * Thread model:
 *   - init / destroy / flush: OBS main thread in radio_output_start and
 *     radio_output_teardown.  flush() runs after the send thread has been
 *     joined, so its output is handed to shout_send directly.
 *   - encode_frame:            OBS audio thread (radio_output_raw_audio).
 *
 * Ownership of the scratch output buffer is with the caller; encoders must
 * never write more than `cap` bytes.  Encoders that buffer input internally
 * (Opus requires whole 20 ms frames) may legally return 0 for calls that
 * don't complete a frame.
 */
struct radio_encoder_ops {
	const char *name;          /* "mp3", "opus" — appears in log lines */
	unsigned int shout_format; /* SHOUT_FORMAT_MP3 or SHOUT_FORMAT_OGG */

	/*
	 * Initialize encoder state at the given samplerate (Hz) and channel
	 * count (1 or 2).  If the container format emits startup bytes (Ogg
	 * OpusHead + OpusTags pages, for example), the encoder bmalloc()s a
	 * buffer, writes them into it, and returns (*out_headers, *out_bytes).
	 * The caller takes ownership of the buffer and releases it with bfree.
	 * Encoders with no startup bytes (MP3) set both out params to 0/NULL.
	 *
	 * Returns true on success.  On failure the encoder must clean up its
	 * own state; destroy() will NOT be called after a failed init.
	 */
	bool (*init)(struct radio_output *context, uint32_t sample_rate, int channels, uint8_t **out_headers,
		     size_t *out_bytes);

	/* Release all encoder state.  Safe to call multiple times. */
	void (*destroy)(struct radio_output *context);

	/*
	 * Encode one batch of planar float samples into out[0..cap).  Returns
	 * the byte count written (>= 0) or -1 on error.  0 is valid — it means
	 * the encoder buffered the input and will emit bytes on a future call.
	 */
	int (*encode_frame)(struct radio_output *context, const float *left, const float *right, size_t frames,
			    uint8_t *out, size_t cap);

	/*
	 * Flush any residual bytes at stream end (LAME: lame_encode_flush;
	 * Opus: final EOS page).  Returns byte count or -1 on error.  Called
	 * once during teardown, on the main thread, after the send thread has
	 * been stopped.
	 */
	int (*flush)(struct radio_output *context, uint8_t *out, size_t cap);

	/*
	 * Called from the reconnect thread after shout_open() succeeds on the
	 * new connection, BEFORE context->shout is made visible to the send
	 * thread.  Encoders with container formats that require startup
	 * headers (Ogg/Opus: OpusHead + OpusTags pages) reset their container
	 * state, re-emit those headers, and return them via out_headers /
	 * out_bytes — same ownership contract as init().  Encoders without
	 * container overhead (MP3) return 0 bytes.
	 *
	 * This exists because on a reconnect Icecast treats the new source
	 * connection as a fresh stream: the old mount's cached container
	 * headers are gone, and listeners that rejoin (or joined against the
	 * new Icecast instance after a crash) cannot decode Ogg/Opus without
	 * a BOS page.  Frame-synced codecs like MP3 don't care.
	 *
	 * The encoder's compression state (OpusEncoder perceptual context,
	 * LAME state) is intentionally preserved across reconnect — only the
	 * container is reset.  Returns 0 on success (even if 0 bytes written)
	 * and -1 on error.
	 */
	int (*on_reconnect)(struct radio_output *context, uint8_t **out_headers, size_t *out_bytes);

	/*
	 * Upper bound on bytes encode_frame() may produce for `frames` input
	 * samples per channel.  Used by radio_output_raw_audio to size its
	 * scratch buffer.  Must account for any pending internal buffering.
	 */
	size_t (*max_output_for)(struct radio_output *context, size_t frames);
};

extern const struct radio_encoder_ops radio_encoder_mp3;
extern const struct radio_encoder_ops radio_encoder_opus;
extern const struct radio_encoder_ops radio_encoder_vorbis;

#ifdef __cplusplus
}
#endif
