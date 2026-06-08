// SPDX-License-Identifier: GPL-2.0-or-later

#include "send-buf.h"

#include <stdlib.h>
#include <string.h>

bool radio_send_buf_init(struct radio_send_buf *sb, size_t capacity)
{
	sb->capacity = capacity;
	sb->wpos = 0;
	sb->rpos = 0;
	sb->data = malloc(capacity); /* set last — it is the reader's "ready" gate */
	return sb->data != NULL;
}

void radio_send_buf_free(struct radio_send_buf *sb)
{
	free(sb->data);
	sb->data = NULL;
	sb->capacity = 0;
	sb->wpos = 0;
	sb->rpos = 0;
}

size_t radio_send_buf_used(const struct radio_send_buf *sb)
{
	return sb->wpos - sb->rpos;
}

size_t radio_send_buf_push(struct radio_send_buf *sb, const uint8_t *bytes, size_t len)
{
	if (!sb->data || sb->capacity == 0 || len == 0)
		return 0;

	size_t dropped = 0;
	size_t used = sb->wpos - sb->rpos;
	if (used + len > sb->capacity) {
		dropped = used + len - sb->capacity;
		sb->rpos += dropped;
	}

	size_t wpos = sb->wpos % sb->capacity;
	size_t tail = sb->capacity - wpos;
	if (tail >= len) {
		memcpy(sb->data + wpos, bytes, len);
	} else {
		memcpy(sb->data + wpos, bytes, tail);
		memcpy(sb->data, bytes + tail, len - tail);
	}
	sb->wpos += len;
	return dropped;
}

size_t radio_send_buf_read(struct radio_send_buf *sb, uint8_t *dst, size_t max)
{
	if (!sb->data || sb->capacity == 0)
		return 0;

	size_t avail = sb->wpos - sb->rpos;
	size_t to_read = avail < max ? avail : max;
	if (to_read == 0)
		return 0;

	size_t rpos = sb->rpos % sb->capacity;
	size_t tail = sb->capacity - rpos;
	if (tail >= to_read) {
		memcpy(dst, sb->data + rpos, to_read);
	} else {
		memcpy(dst, sb->data + rpos, tail);
		memcpy(dst + tail, sb->data, to_read - tail);
	}
	sb->rpos += to_read;
	return to_read;
}
