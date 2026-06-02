// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * Vorbis encoder — libvorbis + libvorbisenc (codec) + libogg (container).
 *
 * Implements the radio_encoder_ops vtable, mirroring radio-opus-encoder.c.
 * The ring buffer, send thread, reconnect logic, and libshout handling all
 * live in radio-output.c; this file only turns planar float PCM into
 * Ogg/Vorbis pages.
 *
 * Pipeline:
 *   1. init()   — vorbis_info + vorbis_encode_init (managed VBR at the user's
 *                 bitrate), comment + DSP + block state, then ogg_stream_init
 *                 and the three Vorbis header packets (identification,
 *                 comment, codebook) flushed into their own pages and handed
 *                 back to the caller so they reach the wire before audio.
 *   2. encode() — hand PCM to libvorbis via vorbis_analysis_buffer, then drain
 *                 blocks → packets → pages (libvorbis owns the PCM accumulator
 *                 and packet/granule bookkeeping, unlike the Opus path).
 *   3. flush()  — vorbis_analysis_wrote(0) signals end-of-input; libvorbis
 *                 marks the final packet e_o_s itself, no manual padding.
 *
 * Differences from Opus worth calling out:
 *   • Arbitrary sample rate — Vorbis has no 48 kHz constraint, so this works
 *     at 44.1 kHz where Opus's init guard trips.
 *   • Three header packets, not two; each on its own page (libvorbis convention
 *     via ogg_stream_flush).
 *   • Reconnect FULLY re-inits the encoder, not just the Ogg container: Vorbis
 *     data packets reference the codebook baked into header packet 3, so
 *     emitting fresh headers while keeping the old DSP state would decode to
 *     garbage on listeners.  The ~30 ms codebook recompute is imperceptible
 *     against the multi-second reconnect downtime that precedes it.
 */

#include "radio-encoder.h"
#include "radio-output.h"

#include <plugin-support.h>
#include <util/base.h>
#include <util/bmem.h>

#ifdef HAVE_VORBIS

#include <vorbis/codec.h>
#include <vorbis/vorbisenc.h>
#include <ogg/ogg.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

/*
 * Per-output Vorbis state.  Lives in radio_output::encoder_priv; allocated in
 * vorbis_init, released in vorbis_destroy.  Each libvorbis sub-object has its
 * own initialized flag so partial-init failure and reconnect teardown can
 * release exactly what was set up, in the libvorbis-recommended order.
 */
struct vorbis_state {
	vorbis_info vi;
	vorbis_comment vc;
	vorbis_dsp_state vd;
	vorbis_block vb;
	ogg_stream_state os;
	bool vi_initialized;
	bool vc_initialized;
	bool vd_initialized;
	bool vb_initialized;
	bool os_initialized;

	uint32_t sample_rate;
	int channels;

	/* Bumped on every reconnect and folded into the Ogg serial so
	 * back-to-back reconnects never reuse a serial. */
	unsigned int reconnect_epoch;
};

/*
 * Append one Ogg page (header + body) to a fixed-size output buffer.  Returns
 * false if the write would overflow cap; the caller then reports -1 to
 * raw_audio, which drops the pages rather than risk a wild write.  The ring
 * buffer is 256 KB so this should never fire for a single audio callback.
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
 * Append one Ogg page to a growable bmalloc buffer.  Used only when building
 * the three-page header blob, which is a few KB total.
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

/* Release every libvorbis/libogg sub-object that was initialized, in the
 * order libvorbis recommends (block → dsp → comment → info).  Idempotent via
 * the per-member flags so it is safe from partial-init failure, reconnect, and
 * destroy alike. */
static void vorbis_clear_state(struct vorbis_state *st)
{
	if (st->os_initialized) {
		ogg_stream_clear(&st->os);
		st->os_initialized = false;
	}
	if (st->vb_initialized) {
		vorbis_block_clear(&st->vb);
		st->vb_initialized = false;
	}
	if (st->vd_initialized) {
		vorbis_dsp_clear(&st->vd);
		st->vd_initialized = false;
	}
	if (st->vc_initialized) {
		vorbis_comment_clear(&st->vc);
		st->vc_initialized = false;
	}
	if (st->vi_initialized) {
		vorbis_info_clear(&st->vi);
		st->vi_initialized = false;
	}
}

/*
 * Build the full libvorbis encode pipeline (info → encode_init → comment →
 * dsp → block → ogg stream) and emit the three Vorbis header packets as
 * separate pages into a fresh bmalloc'd buffer returned via out_headers /
 * out_bytes (caller frees with bfree).  Shared by init() and on_reconnect();
 * the only difference between the two is reconnect_epoch having advanced.
 *
 * On any failure it releases whatever it managed to initialize and returns
 * false.  Expects st to be freshly cleared (all *_initialized false).
 */
static bool vorbis_setup_and_emit_headers(struct vorbis_state *st, int bitrate, uint8_t **out_headers,
					  size_t *out_bytes)
{
	*out_headers = NULL;
	*out_bytes = 0;

	vorbis_info_init(&st->vi);
	st->vi_initialized = true;

	/* Managed VBR: unconstrained max/min (-1), targeting the nominal bitrate.
	 * Matches the "128 kbps means ~128 kbps average" behavior of the MP3 and
	 * Opus paths. */
	int ret = vorbis_encode_init(&st->vi, st->channels, (long)st->sample_rate, -1, (long)bitrate * 1000, -1);
	if (ret != 0) {
		obs_log(LOG_ERROR, "vorbis_encode_init failed (%d): %d ch, %u Hz, %d kbps", ret, st->channels,
			st->sample_rate, bitrate);
		vorbis_clear_state(st);
		return false;
	}

	vorbis_comment_init(&st->vc);
	st->vc_initialized = true;
	vorbis_comment_add_tag(&st->vc, "ENCODER", "obs-radio-output");

	if (vorbis_analysis_init(&st->vd, &st->vi) != 0) {
		obs_log(LOG_ERROR, "vorbis_analysis_init failed");
		vorbis_clear_state(st);
		return false;
	}
	st->vd_initialized = true;

	if (vorbis_block_init(&st->vd, &st->vb) != 0) {
		obs_log(LOG_ERROR, "vorbis_block_init failed");
		vorbis_clear_state(st);
		return false;
	}
	st->vb_initialized = true;

	/* Fresh Ogg serial.  Time ⊕ pointer ⊕ reconnect epoch (golden-ratio
	 * step) keeps it unique across rapid reconnects. */
	unsigned int serial = (unsigned int)time(NULL);
	serial ^= (unsigned int)(uintptr_t)st;
	serial ^= st->reconnect_epoch * 0x9E3779B1u;
	if (ogg_stream_init(&st->os, (int)serial) != 0) {
		obs_log(LOG_ERROR, "ogg_stream_init failed");
		vorbis_clear_state(st);
		return false;
	}
	st->os_initialized = true;

	/* Three header packets: identification, comment, codebook.  libvorbis
	 * sets b_o_s / packetno on them; each goes on its own page (spec +
	 * convention) via ogg_stream_flush. */
	ogg_packet hdr_id;
	ogg_packet hdr_comment;
	ogg_packet hdr_code;
	ret = vorbis_analysis_headerout(&st->vd, &st->vc, &hdr_id, &hdr_comment, &hdr_code);
	if (ret != 0) {
		obs_log(LOG_ERROR, "vorbis_analysis_headerout failed (%d)", ret);
		vorbis_clear_state(st);
		return false;
	}
	if (ogg_stream_packetin(&st->os, &hdr_id) != 0 || ogg_stream_packetin(&st->os, &hdr_comment) != 0 ||
	    ogg_stream_packetin(&st->os, &hdr_code) != 0) {
		obs_log(LOG_ERROR, "ogg_stream_packetin failed for a Vorbis header packet");
		vorbis_clear_state(st);
		return false;
	}

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

/*
 * Drain whatever audio blocks libvorbis has buffered into packets and then
 * Ogg pages.  Uses ogg_stream_pageout (emits only complete pages); the
 * trailing partial page is forced out separately at flush time.
 */
static int vorbis_drain(struct vorbis_state *st, uint8_t *out, size_t cap, size_t *pos)
{
	while (vorbis_analysis_blockout(&st->vd, &st->vb) == 1) {
		vorbis_analysis(&st->vb, NULL);
		vorbis_bitrate_addblock(&st->vb);

		ogg_packet op;
		while (vorbis_bitrate_flushpacket(&st->vd, &op) == 1) {
			if (ogg_stream_packetin(&st->os, &op) != 0) {
				obs_log(LOG_ERROR, "ogg_stream_packetin failed for a Vorbis audio packet");
				return -1;
			}
			ogg_page og;
			while (ogg_stream_pageout(&st->os, &og)) {
				if (!write_page_fixed(out, cap, pos, &og)) {
					obs_log(LOG_ERROR, "Vorbis: scratch buffer overflow emitting page");
					return -1;
				}
			}
		}
	}
	return 0;
}

static bool vorbis_init(struct radio_output *context, uint32_t sample_rate, int channels, uint8_t **out_headers,
			size_t *out_bytes)
{
	*out_headers = NULL;
	*out_bytes = 0;

	if (channels != 1 && channels != 2) {
		obs_log(LOG_ERROR, "Vorbis: unsupported channel count %d (must be 1 or 2)", channels);
		return false;
	}
	/* Vorbis accepts a very wide rate range; reject only the absurd.  Unlike
	 * Opus there is no 48 kHz requirement, so 44.1 kHz works natively. */
	if (sample_rate < 2000 || sample_rate > 200000) {
		obs_log(LOG_ERROR, "Vorbis: sample rate %u Hz out of supported range (2000-200000)", sample_rate);
		return false;
	}

	struct vorbis_state *st = bzalloc(sizeof(struct vorbis_state));
	st->sample_rate = sample_rate;
	st->channels = channels;

	if (!vorbis_setup_and_emit_headers(st, context->bitrate, out_headers, out_bytes)) {
		bfree(st);
		return false;
	}

	context->encoder_priv = st;
	obs_log(LOG_INFO, "Vorbis encoder: %u Hz, %d ch, %d kbps", sample_rate, channels, context->bitrate);
	return true;
}

static int vorbis_on_reconnect(struct radio_output *context, uint8_t **out_headers, size_t *out_bytes)
{
	struct vorbis_state *st = context->encoder_priv;
	*out_headers = NULL;
	*out_bytes = 0;

	if (!st) {
		obs_log(LOG_ERROR, "[vorbis] on_reconnect called without encoder state");
		return -1;
	}

	/* Full re-init.  Vorbis audio packets reference the codebook carried in
	 * header packet 3; a listener joining the post-reconnect stream needs the
	 * headers AND data produced against the same DSP state, so we tear the
	 * whole encoder down and rebuild it.  Same parameters reproduce an
	 * identical codebook; the new Ogg serial + advanced epoch make this a
	 * clean fresh logical stream from gp=0. */
	vorbis_clear_state(st);
	st->reconnect_epoch++;

	if (!vorbis_setup_and_emit_headers(st, context->bitrate, out_headers, out_bytes)) {
		obs_log(LOG_ERROR, "[vorbis] failed to re-init and re-emit container headers on reconnect");
		return -1;
	}

	obs_log(LOG_INFO, "[vorbis] re-initialized and re-emitted Ogg container headers after reconnect (%zu bytes)",
		*out_bytes);
	return 0;
}

static void vorbis_destroy(struct radio_output *context)
{
	struct vorbis_state *st = context->encoder_priv;
	if (!st)
		return;

	vorbis_clear_state(st);
	bfree(st);
	context->encoder_priv = NULL;
}

static int vorbis_encode_frame(struct radio_output *context, const float *left, const float *right, size_t frames,
			       uint8_t *out, size_t cap)
{
	struct vorbis_state *st = context->encoder_priv;
	if (!st)
		return -1;
	if (frames == 0)
		return 0;

	size_t pos = 0;

	/* Hand PCM to libvorbis's own (planar) accumulator. */
	float **buffer = vorbis_analysis_buffer(&st->vd, (int)frames);
	if (st->channels == 2) {
		for (size_t i = 0; i < frames; i++) {
			buffer[0][i] = left[i];
			buffer[1][i] = right[i];
		}
	} else {
		/* Mono downmix — average L+R, matching the Opus path. */
		for (size_t i = 0; i < frames; i++)
			buffer[0][i] = 0.5f * (left[i] + right[i]);
	}
	vorbis_analysis_wrote(&st->vd, (int)frames);

	if (vorbis_drain(st, out, cap, &pos) < 0)
		return -1;

	return (int)pos;
}

static int vorbis_flush(struct radio_output *context, uint8_t *out, size_t cap)
{
	struct vorbis_state *st = context->encoder_priv;
	if (!st)
		return 0;

	size_t pos = 0;

	/* Signal end-of-input.  libvorbis drains its remaining blocks and marks
	 * the final packet e_o_s itself — no manual silence padding needed. */
	vorbis_analysis_wrote(&st->vd, 0);

	if (vorbis_drain(st, out, cap, &pos) < 0)
		return -1;

	/* Force out the trailing partial page (carries the EOS packet). */
	ogg_page og;
	while (ogg_stream_flush(&st->os, &og)) {
		if (!write_page_fixed(out, cap, &pos, &og)) {
			obs_log(LOG_ERROR, "Vorbis: scratch buffer overflow flushing final page");
			return -1;
		}
	}

	return (int)pos;
}

static size_t vorbis_max_output_for(struct radio_output *context, size_t frames)
{
	UNUSED_PARAMETER(context);
	/* Compressed output is far smaller than the equivalent PCM, but VBR can
	 * spike and a full page may flush on any callback.  frames * 2 bytes
	 * dwarfs the worst-case compressed size for one callback; the 32 KB slack
	 * absorbs Ogg page overhead and the occasional buffered-page burst.  If
	 * this is ever exceeded, write_page_fixed fails safely (drops + logs)
	 * rather than overrunning. */
	return frames * 2 + 32 * 1024;
}

const struct radio_encoder_ops radio_encoder_vorbis = {
	.name = "vorbis",
#ifdef HAVE_LIBSHOUT
	.shout_format = SHOUT_FORMAT_OGG,
#else
	.shout_format = 0,
#endif
	.init = vorbis_init,
	.destroy = vorbis_destroy,
	.encode_frame = vorbis_encode_frame,
	.flush = vorbis_flush,
	.max_output_for = vorbis_max_output_for,
	.on_reconnect = vorbis_on_reconnect,
};

#else /* !HAVE_VORBIS — stub so the plugin still links when libvorbis is absent */

static bool vorbis_stub_init(struct radio_output *context, uint32_t sample_rate, int channels, uint8_t **out_headers,
			     size_t *out_bytes)
{
	UNUSED_PARAMETER(context);
	UNUSED_PARAMETER(sample_rate);
	UNUSED_PARAMETER(channels);
	*out_headers = NULL;
	*out_bytes = 0;
	obs_log(LOG_ERROR, "Vorbis encoding not available — rebuild with libvorbis + libogg");
	return false;
}

static void vorbis_stub_destroy(struct radio_output *context)
{
	UNUSED_PARAMETER(context);
}

static int vorbis_stub_encode_frame(struct radio_output *context, const float *left, const float *right, size_t frames,
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

static int vorbis_stub_flush(struct radio_output *context, uint8_t *out, size_t cap)
{
	UNUSED_PARAMETER(context);
	UNUSED_PARAMETER(out);
	UNUSED_PARAMETER(cap);
	return 0;
}

static size_t vorbis_stub_max_output_for(struct radio_output *context, size_t frames)
{
	UNUSED_PARAMETER(context);
	UNUSED_PARAMETER(frames);
	return 0;
}

static int vorbis_stub_on_reconnect(struct radio_output *context, uint8_t **out_headers, size_t *out_bytes)
{
	UNUSED_PARAMETER(context);
	*out_headers = NULL;
	*out_bytes = 0;
	return 0;
}

const struct radio_encoder_ops radio_encoder_vorbis = {
	.name = "vorbis",
	.shout_format = 0,
	.init = vorbis_stub_init,
	.destroy = vorbis_stub_destroy,
	.encode_frame = vorbis_stub_encode_frame,
	.flush = vorbis_stub_flush,
	.max_output_for = vorbis_stub_max_output_for,
	.on_reconnect = vorbis_stub_on_reconnect,
};

#endif /* HAVE_VORBIS */
