// SPDX-License-Identifier: GPL-2.0-or-later
//
// Unit tests for the "Now Playing" song truncation in src/metadata.c (issue
// #74).  metadata.c is compiled directly into this test target; its external
// dependencies (obs_log, the libshout metadata API, radio_output_get_active)
// are satisfied by the minimal stubs below.  The stubbed shout_metadata_add
// captures the song value so we can assert exactly what metadata_update()
// would have put on the wire — which is where the METADATA_SONG_MAX (255 byte)
// snprintf cap applies.

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <pthread.h>
#include <string>

extern "C" {
#include "metadata.h"
#include "radio-output.h"
}

// ---- Captured state + stubs for metadata.c's externals ----

static char g_captured_song[4096];
static bool g_captured;

extern "C" {
void obs_log(int log_level, const char *format, ...)
{
	(void)log_level;
	(void)format;
}

shout_metadata_t *shout_metadata_new(void)
{
	return reinterpret_cast<shout_metadata_t *>(1);
}

int shout_metadata_add(shout_metadata_t *self, const char *name, const char *value)
{
	(void)self;
	(void)name;
	std::strncpy(g_captured_song, value, sizeof(g_captured_song) - 1);
	g_captured_song[sizeof(g_captured_song) - 1] = '\0';
	g_captured = true;
	return SHOUTERR_SUCCESS;
}

void shout_metadata_free(shout_metadata_t *self)
{
	(void)self;
}

int shout_set_metadata_utf8(shout_t *self, shout_metadata_t *metadata)
{
	(void)self;
	(void)metadata;
	return SHOUTERR_SUCCESS;
}

const char *shout_get_error(shout_t *self)
{
	(void)self;
	return "stub";
}

// metadata.c also defines radio_output_update_metadata(), which references
// this; never called from the tests but must resolve at link time.
struct radio_output *radio_output_get_active(void)
{
	return nullptr;
}
}

namespace {
// Drive metadata_update() with a connected context and return the song string
// that reached shout_metadata_add (i.e. post-truncation).
std::string run_update(const std::string &title)
{
	g_captured = false;
	g_captured_song[0] = '\0';

	struct radio_output ctx;
	std::memset(&ctx, 0, sizeof(ctx));
	pthread_mutex_init(&ctx.state_mutex, nullptr);
	ctx.state = RADIO_STATE_CONNECTED;
	ctx.shout = reinterpret_cast<shout_t *>(1);

	int rc = metadata_update(&ctx, title.c_str());
	pthread_mutex_destroy(&ctx.state_mutex);

	REQUIRE(rc == 0);
	REQUIRE(g_captured);
	return std::string(g_captured_song);
}
} // namespace

TEST_CASE("metadata song shorter than the cap passes through unchanged", "[metadata]")
{
	const std::string title(100, 'a');
	REQUIRE(run_update(title) == title);
}

TEST_CASE("metadata song exactly at the 255-byte cap passes through unchanged", "[metadata]")
{
	const std::string title(255, 'b');
	const std::string got = run_update(title);
	REQUIRE(got.size() == 255);
	REQUIRE(got == title);
}

TEST_CASE("metadata song longer than the cap is truncated to 255 bytes", "[metadata]")
{
	const std::string title(512, 'c');
	const std::string got = run_update(title);
	REQUIRE(got.size() == 255);
	REQUIRE(got == std::string(255, 'c'));
}
