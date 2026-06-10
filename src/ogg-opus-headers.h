// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Pure serializers for the Opus container header packets (RFC 7845).  Split out
 * of radio-opus-encoder.c so the byte layout can be unit-tested in isolation,
 * with no libopus / libogg / OBS dependency (issue #75).  The encoder still
 * owns the ogg_stream packetin/flush plumbing; these only pack bytes.
 */

/* OpusHead is exactly 19 bytes for channel mapping family 0 (RFC 7845 §5.1). */
#define OPUS_HEAD_SIZE 19

/*
 * Serialize an OpusHead packet (RFC 7845 §5.1, channel mapping family 0) into
 * out[0..OPUS_HEAD_SIZE).  pre_skip and input_sample_rate are written
 * little-endian per the spec; output gain is fixed at 0 (unity).
 */
void opus_format_head(uint8_t out[OPUS_HEAD_SIZE], uint8_t channels, uint16_t pre_skip, uint32_t input_sample_rate);

/*
 * Serialize an OpusTags packet (RFC 7845 §5.2) carrying the given vendor string
 * and zero user comments into out[0..cap).  Returns the number of bytes written
 * (8 + 4 + strlen(vendor) + 4), or 0 if cap is too small.
 */
size_t opus_format_tags(uint8_t *out, size_t cap, const char *vendor);

/*
 * Like opus_format_tags() but carrying one optional "TITLE=<title>" user comment
 * (VorbisComment) for in-band "Now Playing" metadata (issue #67).  A NULL or
 * empty title emits zero user comments — byte-identical to opus_format_tags().
 * The title is truncated to OPUS_TAGS_TITLE_MAX bytes to bound the packet size.
 * Returns the number of bytes written, or 0 if cap is too small.
 */
#define OPUS_TAGS_TITLE_MAX 255
size_t opus_format_tags_titled(uint8_t *out, size_t cap, const char *vendor, const char *title);

#ifdef __cplusplus
}
#endif
