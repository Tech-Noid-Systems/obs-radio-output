// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "radio-output.h"

/**
 * reconnect_start - Spawn the reconnect background thread.
 *
 * Call this when a send failure is detected inside encoded_packet (i.e. after
 * context->shout has been closed and set to NULL).  If reconnect_enabled is
 * false the call is a no-op.  If a previous reconnect thread is still alive it
 * is joined first.
 */
void reconnect_start(struct radio_output *context);

/**
 * reconnect_cancel - Stop and join the reconnect thread.
 *
 * Safe to call when no thread is running.  Always call this before closing the
 * shout handle in radio_output_stop / radio_output_destroy so the thread
 * cannot write to a freed handle.
 */
void reconnect_cancel(struct radio_output *context);
