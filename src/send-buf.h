// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Byte ring buffer with FIFO drop-oldest on overflow, extracted from
 * radio-output.c (issue #77) so the wraparound and drop semantics can be
 * unit-tested in isolation.
 *
 * NOT thread-safe on its own: radio-output.c serializes the producer (audio
 * thread, via send_buf_push) and the consumer (send thread) with its existing
 * send_mutex.  wpos/rpos are monotonically increasing counters; the live byte
 * count is wpos - rpos.  Operations no-op when data is NULL or capacity is 0 so
 * a not-yet-initialized buffer (the audio callback's "ready" gate is the data
 * pointer) is always safe to call into.
 */
struct radio_send_buf {
	uint8_t *data;
	size_t capacity;
	size_t wpos; /* monotonically increasing write position */
	size_t rpos; /* monotonically increasing read position */
};

/*
 * Allocate the backing buffer of `capacity` bytes.  capacity/wpos/rpos are set
 * before data so that, once a reader observes a non-NULL data pointer, the
 * other fields are already populated.  Returns false if allocation fails.
 */
bool radio_send_buf_init(struct radio_send_buf *sb, size_t capacity);

/* Free the backing buffer and reset all fields (data left NULL). */
void radio_send_buf_free(struct radio_send_buf *sb);

/* Bytes currently buffered (wpos - rpos). */
size_t radio_send_buf_used(const struct radio_send_buf *sb);

/*
 * Append len bytes, dropping the oldest data if the buffer would overflow.
 * Returns the number of oldest bytes dropped (0 in the common case) so the
 * caller can log.  No-op (returns 0) when the buffer is uninitialized or len 0.
 */
size_t radio_send_buf_push(struct radio_send_buf *sb, const uint8_t *bytes, size_t len);

/*
 * Copy up to max bytes of buffered data into dst, advancing the read position.
 * Returns the number of bytes copied.
 */
size_t radio_send_buf_read(struct radio_send_buf *sb, uint8_t *dst, size_t max);

#ifdef __cplusplus
}
#endif
