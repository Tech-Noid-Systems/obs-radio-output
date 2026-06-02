// SPDX-License-Identifier: GPL-2.0-or-later

#include "ogg-opus-headers.h"

#include <string.h>

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
	const size_t vendor_len = strlen(vendor);
	const size_t total = 8 + 4 + vendor_len + 4;
	if (cap < total)
		return 0;

	memcpy(out, "OpusTags", 8);
	const uint32_t vlen = (uint32_t)vendor_len;
	out[8] = (uint8_t)(vlen & 0xFF);
	out[9] = (uint8_t)((vlen >> 8) & 0xFF);
	out[10] = (uint8_t)((vlen >> 16) & 0xFF);
	out[11] = (uint8_t)((vlen >> 24) & 0xFF);
	memcpy(out + 12, vendor, vendor_len);

	/* user_comment_list_length = 0 (no per-comment data follows) */
	out[12 + vendor_len + 0] = 0;
	out[12 + vendor_len + 1] = 0;
	out[12 + vendor_len + 2] = 0;
	out[12 + vendor_len + 3] = 0;

	return total;
}
