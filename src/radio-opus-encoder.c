// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * Opus encoder — libopus (codec) + libogg (container).
 *
 * Implements the radio_encoder_ops vtable so the rest of radio-output.c
 * can treat codec selection as a dispatch through context->encoder.
 *
 * Pipeline:
 *   1. init()   — create OpusEncoder + ogg_stream_state; build OpusHead +
 *                 OpusTags packets; flush both into header pages returned
 *                 to the caller.  Those pages are queued on the shared
 *                 send buffer so they hit the wire before audio.
 *   2. encode() — interleave incoming PCM into a 20 ms accumulator.  When
 *                 full (960 samples/ch @ 48 kHz), call opus_encode_float,
 *                 wrap the packet in an ogg_packet, push through the
 *                 stream state, and pull out any pages ready for emission.
 *   3. flush()  — encode a final frame with e_o_s set (zero-padded if we
 *                 had no residual audio) and force-flush any remaining
 *                 pages.  Emits the terminating Ogg page.
 *
 * Design choices:
 *   • Require 48 kHz input.  OBS's default is 48 kHz; if the user has
 *     lowered it we fail init with a clear message rather than silently
 *     piping mis-rated PCM into libopus.
 *   • Mono/stereo via channel mapping family 0.  Family 1 (surround) is
 *     out of scope for internet radio.
 *   • Packets go through ogg_stream_pageout during the audio loop, then
 *     ogg_stream_flush at teardown.  Default Ogg pagination (~4 KB per
 *     page) yields ~250 ms of buffering — negligible next to our 14 s
 *     ring buffer.
 */

#include "radio-encoder.h"
#include "radio-output.h"
#include "ogg-opus-headers.h"

#include <plugin-support.h>
#include <util/base.h>
#include <util/bmem.h>

#ifdef HAVE_OPUS

#include <opus/opus.h>
#include <ogg/ogg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define OPUS_FRAME_SIZE 960  /* 20 ms at 48 kHz */
#define OPUS_MAX_PACKET 4000 /* RFC 7845 practical upper bound for 20 ms */

/*
 * Per-output Opus state.  Lives in radio_output::encoder_priv; allocated
 * in opus_init, released in opus_destroy.
 */
struct opus_state {
	OpusEncoder *enc;
	ogg_stream_state os;
	bool os_initialized;

	uint32_t sample_rate; /* must be 48000 (libopus constraint for our path) */
	int channels;         /* 1 or 2 */
	opus_int32 preskip;   /* queried at init; re-used when re-emitting headers on reconnect */

	/* Interleaved PCM accumulator (holds up to one 20 ms frame). */
	float accum[OPUS_FRAME_SIZE * 2];
	size_t accum_fill; /* sample-frames currently buffered */

	int64_t packetno;
	int64_t granulepos;

	/* Reconnect counter — xor'd into the Ogg serial on each reset so
	 * back-to-back reconnects never collide on the same serial. */
	unsigned int reconnect_epoch;

	/* Current "Now Playing" title, emitted in OpusTags.  Empty until the
	 * first metadata push; re-emitted on every container reset (reconnect or
	 * metadata update) so a rejoining listener sees the live title (#67). */
	char title[OPUS_TAGS_TITLE_MAX + 1];
};

/*
 * Append one Ogg page (header + body) to a fixed-size output buffer.
 * Returns false if the write would overflow cap; the caller then reports
 * -1 to raw_audio, which drops the pages — better than a wild write.
 * The ring buffer is 256 KB so this limit should never fire in practice
 * for a single audio callback.
 */
static bool write_page_fixed(uint8_t *out, size_t cap, size_t *pos, const ogg_page *og)
{
	size_t need = *pos + (size_t)og->header_len + (size_t)og->body_len;
	if (need > cap)
		return false;
	memcpy(out + *pos, og->header, (size_t)og->header_len);
	*pos += (size_t)og->header_len;
	memcpy(out + *pos, og->body, (size_t)og->body_len);
	*pos += (size_t)og->body_len;
	return true;
}

/*
 * Append one Ogg page to a growable bmalloc buffer.  Used only by init()
 * to build the header-page blob, which is a few hundred bytes total.
 */
static void append_page(uint8_t **dst, size_t *dst_len, size_t *dst_cap, const ogg_page *og)
{
	size_t need = *dst_len + (size_t)og->header_len + (size_t)og->body_len;
	if (need > *dst_cap) {
		size_t new_cap = *dst_cap ? *dst_cap : 1024;
		while (new_cap < need)
			new_cap *= 2;
		*dst = brealloc(*dst, new_cap);
		*dst_cap = new_cap;
	}
	memcpy(*dst + *dst_len, og->header, (size_t)og->header_len);
	*dst_len += (size_t)og->header_len;
	memcpy(*dst + *dst_len, og->body, (size_t)og->body_len);
	*dst_len += (size_t)og->body_len;
}

/*
 * Encode the single full 20 ms frame currently in st->accum, wrap it as
 * an Ogg packet, push through the stream, and pull whatever pages come
 * out into the caller's fixed-size buffer.  Caller manages whether to
 * use pageout (during normal audio) or flush (at teardown).
 */
static int emit_one_frame(struct opus_state *st, uint8_t *out, size_t cap, size_t *pos, bool last)
{
	uint8_t pkt[OPUS_MAX_PACKET];
	int pkt_bytes = opus_encode_float(st->enc, st->accum, OPUS_FRAME_SIZE, pkt, (opus_int32)sizeof(pkt));
	if (pkt_bytes < 0) {
		obs_log(LOG_ERROR, "opus_encode_float failed: %s", opus_strerror(pkt_bytes));
		return -1;
	}

	st->granulepos += OPUS_FRAME_SIZE;

	ogg_packet op = {0};
	op.packet = pkt;
	op.bytes = pkt_bytes;
	op.b_o_s = 0;
	op.e_o_s = last ? 1 : 0;
	op.granulepos = st->granulepos;
	op.packetno = st->packetno++;
	if (ogg_stream_packetin(&st->os, &op) != 0) {
		obs_log(LOG_ERROR, "ogg_stream_packetin failed");
		return -1;
	}

	ogg_page page;
	int (*page_fn)(ogg_stream_state *, ogg_page *) = last ? ogg_stream_flush : ogg_stream_pageout;
	while (page_fn(&st->os, &page)) {
		if (!write_page_fixed(out, cap, pos, &page)) {
			obs_log(LOG_ERROR, "Opus: scratch buffer overflow emitting page");
			return -1;
		}
	}
	return 0;
}

/*
 * Build OpusHead + OpusTags packets, push them through st->os, and flush
 * the resulting pages into a fresh bmalloc'd buffer returned via
 * out_headers/out_bytes.  Shared between init() and on_reconnect() — the
 * only difference between the two call sites is whether ogg_stream_state
 * is new or just-reset.
 *
 * Expects st->os to be freshly initialized and st->packetno = 0.
 * Returns true on success, false if libogg rejects a packetin or a bmalloc
 * fails — in practice both should be unreachable.
 */
static bool emit_opus_container_headers(struct opus_state *st, uint8_t **out_headers, size_t *out_bytes)
{
	*out_headers = NULL;
	*out_bytes = 0;

	/* ---- OpusHead (RFC 7845 §5.1, 19 bytes for channel mapping family 0) ----
	 * Byte layout lives in ogg-opus-headers.c so it can be unit-tested. */
	uint8_t head[OPUS_HEAD_SIZE];
	opus_format_head(head, (uint8_t)st->channels, (uint16_t)st->preskip, st->sample_rate);

	ogg_packet op_head = {0};
	op_head.packet = head;
	op_head.bytes = sizeof(head);
	op_head.b_o_s = 1;
	op_head.e_o_s = 0;
	op_head.granulepos = 0;
	op_head.packetno = st->packetno++;
	if (ogg_stream_packetin(&st->os, &op_head) != 0) {
		obs_log(LOG_ERROR, "ogg_stream_packetin(OpusHead) failed");
		return false;
	}

	/* ---- OpusTags (RFC 7845 §5.2) ----
	 * Vendor "obs-radio-output" (16 bytes) → a 32-byte packet; the 64-byte
	 * stack buffer is ample.  libogg copies the packet bytes in
	 * ogg_stream_packetin, so the buffer needn't outlive this call.  Byte
	 * layout lives in ogg-opus-headers.c. */
	/* Sized for the vendor string plus one full-length TITLE comment. */
	uint8_t tags[8 + 4 + 16 + 4 + 4 + OPUS_TAGS_TITLE_MAX + 16];
	const size_t tags_len = opus_format_tags_titled(tags, sizeof(tags), "obs-radio-output", st->title);

	ogg_packet op_tags = {0};
	op_tags.packet = tags;
	op_tags.bytes = (long)tags_len;
	op_tags.b_o_s = 0;
	op_tags.e_o_s = 0;
	op_tags.granulepos = 0;
	op_tags.packetno = st->packetno++;
	if (ogg_stream_packetin(&st->os, &op_tags) != 0) {
		obs_log(LOG_ERROR, "ogg_stream_packetin(OpusTags) failed");
		return false;
	}

	/* Spec: OpusHead and OpusTags each MUST live on their own page; flush
	 * forces them out separately. */
	uint8_t *pages = NULL;
	size_t pages_len = 0;
	size_t pages_cap = 0;
	ogg_page page;
	while (ogg_stream_flush(&st->os, &page))
		append_page(&pages, &pages_len, &pages_cap, &page);

	*out_headers = pages;
	*out_bytes = pages_len;
	return true;
}

static bool opus_init(struct radio_output *context, uint32_t sample_rate, int channels, uint8_t **out_headers,
		      size_t *out_bytes)
{
	*out_headers = NULL;
	*out_bytes = 0;

	if (sample_rate != 48000) {
		obs_log(LOG_ERROR,
			"Opus requires 48 kHz audio; OBS is at %u Hz (change in Settings → Audio → Sample Rate)",
			sample_rate);
		return false;
	}
	if (channels != 1 && channels != 2) {
		obs_log(LOG_ERROR, "Opus: unsupported channel count %d (must be 1 or 2)", channels);
		return false;
	}

	/* Opus is intrinsically 48 kHz; a user-selected stream samplerate can't
	 * change that.  Warn-and-ignore so the dialog's selector doesn't silently
	 * mislead. */
	if (context->stream_samplerate && context->stream_samplerate != 48000)
		obs_log(LOG_WARNING, "Opus always streams at 48 kHz; ignoring stream samplerate %u Hz",
			context->stream_samplerate);
	context->out_samplerate = 48000;

	struct opus_state *st = bzalloc(sizeof(struct opus_state));
	st->sample_rate = sample_rate;
	st->channels = channels;

	int err = 0;
	st->enc = opus_encoder_create((opus_int32)sample_rate, channels, OPUS_APPLICATION_AUDIO, &err);
	if (!st->enc || err != OPUS_OK) {
		obs_log(LOG_ERROR, "opus_encoder_create failed: %s", opus_strerror(err));
		bfree(st);
		return false;
	}

	/* Configure for internet-radio-style stereo music:
	 *   - bitrate from user setting (kbps → bps)
	 *   - signal hint = music (vs. voice)
	 *   - complexity 10 = highest encoder quality
	 *   - VBR on (default true, but set explicitly)
	 */
	opus_encoder_ctl(st->enc, OPUS_SET_BITRATE((opus_int32)context->bitrate * 1000));
	opus_encoder_ctl(st->enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));
	opus_encoder_ctl(st->enc, OPUS_SET_COMPLEXITY(10));
	opus_encoder_ctl(st->enc, OPUS_SET_VBR(1));

	/* Pre-skip: samples the decoder discards at stream start to account for
	 * encoder lookahead.  Passed through in OpusHead so the player drops
	 * the right amount.  Default 3840 if the query fails (shouldn't).
	 * Stashed on st so on_reconnect() can re-emit OpusHead without
	 * re-querying. */
	st->preskip = 3840;
	opus_encoder_ctl(st->enc, OPUS_GET_LOOKAHEAD(&st->preskip));

	/* Random-ish Ogg serial number.  Any int32 that doesn't collide with a
	 * second logical stream on the same physical stream is fine; we only
	 * ever carry one logical stream per connection. */
	unsigned int serial = (unsigned int)time(NULL);
	serial ^= (unsigned int)(uintptr_t)st;
	ogg_stream_init(&st->os, (int)serial);
	st->os_initialized = true;

	if (!emit_opus_container_headers(st, out_headers, out_bytes)) {
		ogg_stream_clear(&st->os);
		opus_encoder_destroy(st->enc);
		bfree(st);
		return false;
	}

	context->encoder_priv = st;

	obs_log(LOG_INFO, "Opus encoder: %u Hz, %d ch, %d kbps, pre-skip %d", sample_rate, channels, context->bitrate,
		st->preskip);
	return true;
}

/*
 * Start a fresh Ogg logical stream (new serial) and emit OpusHead + OpusTags
 * into out_headers/out_bytes — the chaining primitive shared by reconnect and
 * in-band metadata.  The OpusEncoder and the PCM accumulator are intentionally
 * preserved (only the container is reset): perceptual state stays valid and any
 * partial frame in st->accum continues into the new stream.  packetno/granulepos
 * restart at 0, so a listener joining the new stream begins at gp=0.
 */
static int opus_restart_logical_stream(struct opus_state *st, uint8_t **out_headers, size_t *out_bytes)
{
	*out_headers = NULL;
	*out_bytes = 0;

	if (st->os_initialized) {
		ogg_stream_clear(&st->os);
		st->os_initialized = false;
	}
	st->reconnect_epoch++;
	unsigned int serial = (unsigned int)time(NULL);
	serial ^= (unsigned int)(uintptr_t)st;
	serial ^= st->reconnect_epoch * 0x9E3779B1u; /* golden-ratio hash step */
	ogg_stream_init(&st->os, (int)serial);
	st->os_initialized = true;

	st->packetno = 0;
	st->granulepos = 0;

	if (!emit_opus_container_headers(st, out_headers, out_bytes))
		return -1;
	return 0;
}

static int opus_on_reconnect(struct radio_output *context, uint8_t **out_headers, size_t *out_bytes)
{
	struct opus_state *st = context->encoder_priv;
	*out_headers = NULL;
	*out_bytes = 0;

	if (!st) {
		obs_log(LOG_ERROR, "[opus] on_reconnect called without encoder state");
		return -1;
	}

	/* Icecast treats the new source connection as a fresh stream, so
	 * listeners need a BOS page with OpusHead before they can decode. */
	if (opus_restart_logical_stream(st, out_headers, out_bytes) != 0) {
		obs_log(LOG_ERROR, "[opus] failed to re-emit container headers on reconnect");
		return -1;
	}

	obs_log(LOG_INFO, "[opus] re-emitted Ogg container headers after reconnect (%zu bytes)", *out_bytes);
	return 0;
}

static int opus_update_metadata(struct radio_output *context, const char *title, uint8_t **out_bytes, size_t *out_len)
{
	struct opus_state *st = context->encoder_priv;
	*out_bytes = NULL;
	*out_len = 0;

	if (!st) {
		obs_log(LOG_ERROR, "[opus] update_metadata called without encoder state");
		return -1;
	}

	/* Stash the title so it survives a later reconnect's header re-emit, then
	 * chain a new logical stream whose OpusTags carries it. */
	snprintf(st->title, sizeof(st->title), "%s", title ? title : "");

	if (opus_restart_logical_stream(st, out_bytes, out_len) != 0) {
		obs_log(LOG_ERROR, "[opus] failed to chain new stream for metadata update");
		return -1;
	}

	obs_log(LOG_INFO, "[opus] in-band metadata: chained new Ogg stream with updated OpusTags (%zu bytes)",
		*out_len);
	return 0;
}

static void opus_destroy(struct radio_output *context)
{
	struct opus_state *st = context->encoder_priv;
	if (!st)
		return;

	if (st->os_initialized) {
		ogg_stream_clear(&st->os);
		st->os_initialized = false;
	}
	if (st->enc) {
		opus_encoder_destroy(st->enc);
		st->enc = NULL;
	}
	bfree(st);
	context->encoder_priv = NULL;
}

static int opus_encode_frame(struct radio_output *context, const float *left, const float *right, size_t frames,
			     uint8_t *out, size_t cap)
{
	struct opus_state *st = context->encoder_priv;
	if (!st)
		return -1;

	size_t pos = 0;
	size_t i = 0;

	while (i < frames) {
		size_t take = OPUS_FRAME_SIZE - st->accum_fill;
		size_t avail = frames - i;
		if (take > avail)
			take = avail;

		if (st->channels == 2) {
			for (size_t k = 0; k < take; k++) {
				st->accum[(st->accum_fill + k) * 2 + 0] = left[i + k];
				st->accum[(st->accum_fill + k) * 2 + 1] = right[i + k];
			}
		} else {
			/* Mono downmix — average the two channels.  OBS always
			 * hands us stereo in data[0]/data[1]; if mono is ever
			 * added to the UI this keeps both channels represented. */
			for (size_t k = 0; k < take; k++)
				st->accum[st->accum_fill + k] = 0.5f * (left[i + k] + right[i + k]);
		}
		st->accum_fill += take;
		i += take;

		if (st->accum_fill == OPUS_FRAME_SIZE) {
			if (emit_one_frame(st, out, cap, &pos, false) < 0)
				return -1;
			st->accum_fill = 0;
		}
	}

	return (int)pos;
}

static int opus_flush(struct radio_output *context, uint8_t *out, size_t cap)
{
	struct opus_state *st = context->encoder_priv;
	if (!st)
		return 0;

	size_t pos = 0;

	/* Always emit exactly one terminating frame with e_o_s set so the
	 * decoder sees a proper EOS marker.  Zero-pad whatever residual PCM
	 * sits in the accumulator — silence is the most honest filler. */
	const size_t total = OPUS_FRAME_SIZE * (size_t)st->channels;
	const size_t start = st->accum_fill * (size_t)st->channels;
	for (size_t k = start; k < total; k++)
		st->accum[k] = 0.0f;
	st->accum_fill = OPUS_FRAME_SIZE;
	if (emit_one_frame(st, out, cap, &pos, true) < 0)
		return -1;
	st->accum_fill = 0;

	/* Force any still-buffered pages out of ogg_stream. */
	ogg_page page;
	while (ogg_stream_flush(&st->os, &page)) {
		if (!write_page_fixed(out, cap, &pos, &page))
			return -1;
	}

	return (int)pos;
}

static size_t opus_max_output_for(struct radio_output *context, size_t frames)
{
	struct opus_state *st = context->encoder_priv;
	if (!st)
		return 16 * 1024;

	/* Upper bound for this call: ceil((new frames + residual) / 960)
	 * packets, each bounded by OPUS_MAX_PACKET, plus Ogg page overhead. */
	size_t total = frames + st->accum_fill;
	size_t packets = total / OPUS_FRAME_SIZE + 1;
	return packets * (OPUS_MAX_PACKET + 300);
}

const struct radio_encoder_ops radio_encoder_opus = {
	.name = "opus",
#ifdef HAVE_LIBSHOUT
	.shout_format = SHOUT_FORMAT_OGG,
#else
	.shout_format = 0,
#endif
	.init = opus_init,
	.destroy = opus_destroy,
	.encode_frame = opus_encode_frame,
	.flush = opus_flush,
	.max_output_for = opus_max_output_for,
	.on_reconnect = opus_on_reconnect,
	.update_metadata = opus_update_metadata,
};

#else /* !HAVE_OPUS — stub so the plugin still links when libopus is absent */

static bool opus_stub_init(struct radio_output *context, uint32_t sample_rate, int channels, uint8_t **out_headers,
			   size_t *out_bytes)
{
	UNUSED_PARAMETER(context);
	UNUSED_PARAMETER(sample_rate);
	UNUSED_PARAMETER(channels);
	*out_headers = NULL;
	*out_bytes = 0;
	obs_log(LOG_ERROR, "Opus encoding not available — rebuild with libopus + libogg");
	return false;
}

static void opus_stub_destroy(struct radio_output *context)
{
	UNUSED_PARAMETER(context);
}

static int opus_stub_encode_frame(struct radio_output *context, const float *left, const float *right, size_t frames,
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

static int opus_stub_flush(struct radio_output *context, uint8_t *out, size_t cap)
{
	UNUSED_PARAMETER(context);
	UNUSED_PARAMETER(out);
	UNUSED_PARAMETER(cap);
	return 0;
}

static size_t opus_stub_max_output_for(struct radio_output *context, size_t frames)
{
	UNUSED_PARAMETER(context);
	UNUSED_PARAMETER(frames);
	return 0;
}

static int opus_stub_on_reconnect(struct radio_output *context, uint8_t **out_headers, size_t *out_bytes)
{
	UNUSED_PARAMETER(context);
	*out_headers = NULL;
	*out_bytes = 0;
	return 0;
}

const struct radio_encoder_ops radio_encoder_opus = {
	.name = "opus",
	.shout_format = 0,
	.init = opus_stub_init,
	.destroy = opus_stub_destroy,
	.encode_frame = opus_stub_encode_frame,
	.flush = opus_stub_flush,
	.max_output_for = opus_stub_max_output_for,
	.on_reconnect = opus_stub_on_reconnect,
};

#endif /* HAVE_OPUS */
