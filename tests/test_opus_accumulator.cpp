// SPDX-License-Identifier: GPL-2.0-or-later
//
// Opus PCM accumulator + frame-emission tests (issue #76), exercised against
// real libopus + libogg.  radio-opus-encoder.c is compiled into this target;
// its OBS dependencies (obs_log, the bmem allocators) are stubbed below, while
// libopus and libogg are linked for real because their output is deterministic
// for a given input + settings.
//
// The test drives the radio_encoder_opus vtable with synthetic PCM, collects
// the emitted Ogg/Opus bytes (init headers + per-call encode output + flush),
// and parses them back with libogg to assert frame counts, granulepos
// progression, and packetno monotonicity — the bookkeeping the 20 ms / 960
// sample accumulator is responsible for.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <ogg/ogg.h>

extern "C" {
#include "radio-encoder.h"
#include "radio-output.h"
}

// ---- Stubs for radio-opus-encoder.c's OBS externals ----
extern "C" {
void obs_log(int log_level, const char *format, ...)
{
	(void)log_level;
	(void)format;
}
// bmalloc / brealloc / bfree are extern functions in libobs (which we don't
// link); stub them onto the C allocator.  bzalloc / bmemdup are static inline
// in util/bmem.h and resolve through bmalloc, so they must NOT be redefined.
void *bmalloc(size_t size)
{
	return malloc(size);
}
void *brealloc(void *ptr, size_t size)
{
	return realloc(ptr, size);
}
void bfree(void *ptr)
{
	free(ptr);
}
}

namespace {

constexpr int kFrameSize = 960; // 20 ms at 48 kHz

struct ParsedStream {
	int header_packets = 0;
	int audio_packets = 0;
	int64_t last_granulepos = 0;
	bool packetno_monotonic = true;
};

// Parse a buffer of Ogg bytes into packets using libogg, classifying the two
// Opus header packets vs. audio packets and tracking granulepos / packetno.
ParsedStream parse_ogg(const std::vector<uint8_t> &data)
{
	ParsedStream r;

	ogg_sync_state oy;
	ogg_sync_init(&oy);
	char *buf = ogg_sync_buffer(&oy, static_cast<long>(data.size()));
	std::memcpy(buf, data.data(), data.size());
	ogg_sync_wrote(&oy, static_cast<long>(data.size()));

	ogg_stream_state os;
	bool os_ready = false;
	ogg_page og;
	ogg_packet op;
	int64_t prev_packetno = -1;

	while (ogg_sync_pageout(&oy, &og) == 1) {
		if (!os_ready) {
			ogg_stream_init(&os, ogg_page_serialno(&og));
			os_ready = true;
		}
		ogg_stream_pagein(&os, &og);
		while (ogg_stream_packetout(&os, &op) == 1) {
			const bool is_header = op.bytes >= 8 && (std::memcmp(op.packet, "OpusHead", 8) == 0 ||
								 std::memcmp(op.packet, "OpusTags", 8) == 0);
			if (is_header) {
				r.header_packets++;
			} else {
				r.audio_packets++;
			}
			// granulepos is -1 on packets that aren't the last of a page;
			// keep the most recent valid value (the stream total at EOS).
			if (op.granulepos >= 0)
				r.last_granulepos = op.granulepos;
			if (op.packetno <= prev_packetno)
				r.packetno_monotonic = false;
			prev_packetno = op.packetno;
		}
	}

	if (os_ready)
		ogg_stream_clear(&os);
	ogg_sync_clear(&oy);
	return r;
}

// Drive the Opus encoder: init, feed `calls` batches of `frames_per_call`
// samples/channel of synthetic ramp PCM, optionally flush, and return every
// byte emitted (headers + audio + flush).
std::vector<uint8_t> drive_encoder(int channels, size_t frames_per_call, int calls, bool do_flush)
{
	struct radio_output ctx;
	std::memset(&ctx, 0, sizeof(ctx));
	ctx.bitrate = 128;

	uint8_t *headers = nullptr;
	size_t header_len = 0;
	REQUIRE(radio_encoder_opus.init(&ctx, 48000, channels, &headers, &header_len));

	std::vector<uint8_t> out;
	if (headers && header_len) {
		out.insert(out.end(), headers, headers + header_len);
		bfree(headers);
	}

	std::vector<float> left(frames_per_call);
	std::vector<float> right(frames_per_call);
	for (size_t i = 0; i < frames_per_call; i++) {
		// A quiet ramp — deterministic, non-silent so the encoder has signal.
		const float v = static_cast<float>(i % 480) / 480.0f * 0.25f;
		left[i] = v;
		right[i] = -v;
	}

	const size_t cap = radio_encoder_opus.max_output_for(&ctx, frames_per_call);
	std::vector<uint8_t> scratch(cap);
	for (int c = 0; c < calls; c++) {
		const int n = radio_encoder_opus.encode_frame(&ctx, left.data(), right.data(), frames_per_call,
							      scratch.data(), scratch.size());
		REQUIRE(n >= 0);
		out.insert(out.end(), scratch.begin(), scratch.begin() + n);
	}

	if (do_flush) {
		std::vector<uint8_t> fbuf(64 * 1024);
		const int n = radio_encoder_opus.flush(&ctx, fbuf.data(), fbuf.size());
		REQUIRE(n >= 0);
		out.insert(out.end(), fbuf.begin(), fbuf.begin() + n);
	}

	radio_encoder_opus.destroy(&ctx);
	return out;
}

} // namespace

TEST_CASE("a sub-frame batch is buffered, emitting no bytes yet", "[opus][accumulator]")
{
	struct radio_output ctx;
	std::memset(&ctx, 0, sizeof(ctx));
	ctx.bitrate = 128;
	uint8_t *headers = nullptr;
	size_t header_len = 0;
	REQUIRE(radio_encoder_opus.init(&ctx, 48000, 2, &headers, &header_len));
	bfree(headers);

	std::vector<float> left(480, 0.1f);
	std::vector<float> right(480, -0.1f);
	std::vector<uint8_t> scratch(radio_encoder_opus.max_output_for(&ctx, 480));
	// 480 < 960: the accumulator holds the samples and emits nothing.
	const int n =
		radio_encoder_opus.encode_frame(&ctx, left.data(), right.data(), 480, scratch.data(), scratch.size());
	REQUIRE(n == 0);
	radio_encoder_opus.destroy(&ctx);
}

TEST_CASE("N full frames plus flush produce N+1 packets with monotonic granulepos", "[opus][accumulator]")
{
	const auto bytes = drive_encoder(/*channels=*/2, /*frames_per_call=*/kFrameSize, /*calls=*/10,
					 /*do_flush=*/true);
	const ParsedStream s = parse_ogg(bytes);

	REQUIRE(s.header_packets == 2); // OpusHead + OpusTags
	REQUIRE(s.audio_packets == 11); // 10 fed frames + 1 zero-padded EOS frame from flush
	REQUIRE(s.last_granulepos == 11 * kFrameSize);
	REQUIRE(s.packetno_monotonic);
}

TEST_CASE("samples accumulate across sub-frame calls into whole frames", "[opus][accumulator]")
{
	// 4 calls x 480 samples = 1920 = exactly 2 frames during encode; flush adds 1.
	const auto bytes = drive_encoder(/*channels=*/2, /*frames_per_call=*/480, /*calls=*/4, /*do_flush=*/true);
	const ParsedStream s = parse_ogg(bytes);

	REQUIRE(s.audio_packets == 3);
	REQUIRE(s.last_granulepos == 3 * kFrameSize);
	REQUIRE(s.packetno_monotonic);
}

TEST_CASE("a partial batch is preserved and flushed as one final frame", "[opus][accumulator]")
{
	const auto bytes = drive_encoder(/*channels=*/2, /*frames_per_call=*/480, /*calls=*/1, /*do_flush=*/true);
	const ParsedStream s = parse_ogg(bytes);

	REQUIRE(s.audio_packets == 1); // the buffered 480 samples become the single padded EOS frame
	REQUIRE(s.last_granulepos == kFrameSize);
}

TEST_CASE("mono input encodes through the downmix path", "[opus][accumulator]")
{
	const auto bytes = drive_encoder(/*channels=*/1, /*frames_per_call=*/kFrameSize, /*calls=*/5,
					 /*do_flush=*/true);
	const ParsedStream s = parse_ogg(bytes);

	REQUIRE(s.header_packets == 2);
	REQUIRE(s.audio_packets == 6); // 5 + flush
	REQUIRE(s.last_granulepos == 6 * kFrameSize);
	REQUIRE(s.packetno_monotonic);
}
