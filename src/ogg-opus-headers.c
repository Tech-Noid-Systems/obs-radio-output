// SPDX-License-Identifier: GPL-2.0-or-later

#include "ogg-opus-headers.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static void put_le32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v & 0xFF);
	p[1] = (uint8_t)((v >> 8) & 0xFF);
	p[2] = (uint8_t)((v >> 16) & 0xFF);
	p[3] = (uint8_t)((v >> 24) & 0xFF);
}

void opus_format_head(uint8_t out[OPUS_HEAD_SIZE], uint8_t channels, uint16_t pre_skip, uint32_t input_sample_rate)
{
	memcpy(out, "OpusHead", 8);
	out[8] = 1; /* version */
	out[9] = channels;
	out[10] = (uint8_t)(pre_skip & 0xFF);
	out[11] = (uint8_t)((pre_skip >> 8) & 0xFF);
	out[12] = (uint8_t)(input_sample_rate & 0xFF);
	out[13] = (uint8_t)((input_sample_rate >> 8) & 0xFF);
	out[14] = (uint8_t)((input_sample_rate >> 16) & 0xFF);
	out[15] = (uint8_t)((input_sample_rate >> 24) & 0xFF);
	out[16] = 0; /* output gain low (0 = unity) */
	out[17] = 0; /* output gain high */
	out[18] = 0; /* channel mapping family 0 */
}

size_t opus_format_tags(uint8_t *out, size_t cap, const char *vendor)
{
	return opus_format_tags_titled(out, cap, vendor, NULL);
}

size_t opus_format_tags_titled(uint8_t *out, size_t cap, const char *vendor, const char *title)
{
	const size_t vendor_len = strlen(vendor);

	/* Build the optional "TITLE=<title>" user comment, truncated. */
	char comment[6 + OPUS_TAGS_TITLE_MAX + 1]; /* "TITLE=" + title + NUL */
	size_t comment_len = 0;
	const bool has_title = (title && *title);
	if (has_title) {
		int n = snprintf(comment, sizeof(comment), "TITLE=%.*s", OPUS_TAGS_TITLE_MAX, title);
		if (n < 0)
			return 0;
		comment_len = ((size_t)n < sizeof(comment)) ? (size_t)n : sizeof(comment) - 1;
	}

	const uint32_t ncomments = has_title ? 1u : 0u;
	const size_t total = 8 + 4 + vendor_len + 4 + (has_title ? 4 + comment_len : 0);
	if (cap < total)
		return 0;

	size_t p = 0;
	memcpy(out + p, "OpusTags", 8);
	p += 8;
	put_le32(out + p, (uint32_t)vendor_len);
	p += 4;
	memcpy(out + p, vendor, vendor_len);
	p += vendor_len;
	put_le32(out + p, ncomments);
	p += 4;
	if (has_title) {
		put_le32(out + p, (uint32_t)comment_len);
		p += 4;
		memcpy(out + p, comment, comment_len);
		p += comment_len;
	}

	return total;
}
