// SPDX-License-Identifier: GPL-2.0-or-later

#include "metadata.h"
#include "radio-output.h"

#include <plugin-support.h>

#include <stdio.h>
#include <string.h>

#ifdef HAVE_LIBSHOUT
#include <shout/shout.h>
#endif

/*
 * libshout truncates internally somewhere around 256 bytes for the song
 * metadata field.  Capping here means the log line matches what hits the
 * wire; the +1 leaves room for the NUL terminator.
 */
#define METADATA_SONG_MAX 255

int metadata_update(struct radio_output *context, const char *title)
{
#ifndef HAVE_LIBSHOUT
	UNUSED_PARAMETER(context);
	UNUSED_PARAMETER(title);
	return -1;
#else
	if (!context) {
		obs_log(LOG_WARNING, "metadata update failed: no active output");
		return -1;
	}

	if (!title || !*title) {
		obs_log(LOG_WARNING, "metadata update failed: empty title");
		return -1;
	}

	/* Read state under the mutex so we don't race with teardown
	 * freeing context->shout out from under us.  libshout itself takes
	 * an internal lock on shout_set_metadata, but it can't protect us
	 * from seeing a stale shout handle after destroy; the state gate
	 * does. */
	pthread_mutex_lock(&context->state_mutex);
	bool connected = (context->state == RADIO_STATE_CONNECTED);
	shout_t *shout = context->shout;
	pthread_mutex_unlock(&context->state_mutex);

	if (!connected || !shout) {
		obs_log(LOG_WARNING, "metadata update failed: not connected");
		return -1;
	}

	char song[METADATA_SONG_MAX + 1];
	snprintf(song, sizeof(song), "%s", title);

	shout_metadata_t *meta = shout_metadata_new();
	if (!meta) {
		obs_log(LOG_ERROR, "metadata: shout_metadata_new() failed");
		return -1;
	}
	shout_metadata_add(meta, "song", song);

	int err = shout_set_metadata(shout, meta);
	shout_metadata_free(meta);

	if (err != SHOUTERR_SUCCESS) {
		obs_log(LOG_WARNING, "metadata update failed: %s", shout_get_error(shout));
		return -1;
	}

	obs_log(LOG_INFO, "Now playing: %s", song);
	return 0;
#endif
}

bool radio_output_update_metadata(const char *title)
{
	struct radio_output *active = radio_output_get_active();
	if (!active)
		return false;
	return metadata_update(active, title) == 0;
}
