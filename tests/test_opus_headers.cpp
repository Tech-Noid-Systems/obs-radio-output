// SPDX-License-Identifier: GPL-2.0-or-later
//
// Byte-exact tests for the Opus container header serializers (RFC 7845
// §5.1/§5.2), issue #75.  The serializers were split out of
// radio-opus-encoder.c into src/ogg-opus-headers.c precisely so they can be
// tested here with no libopus / libogg / OBS dependency — this target compiles
// only that one pure C file.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>

extern "C" {
#include "ogg-opus-headers.h"
}

namespace {
uint16_t le16(const uint8_t *p)
{
	return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
uint32_t le32(const uint8_t *p)
{
	return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
	       (static_cast<uint32_t>(p[3]) << 24);
}
} // namespace

TEST_CASE("OpusHead matches RFC 7845 §5.1 byte layout (stereo)", "[opus][headers]")
{
	uint8_t head[OPUS_HEAD_SIZE];
	opus_format_head(head, /*channels=*/2, /*pre_skip=*/312, /*input_sample_rate=*/48000);

	REQUIRE(std::memcmp(head, "OpusHead", 8) == 0);
	REQUIRE(head[8] == 1);              // version
	REQUIRE(head[9] == 2);              // channel count
	REQUIRE(le16(head + 10) == 312);    // pre-skip, little-endian
	REQUIRE(le32(head + 12) == 48000u); // input sample rate, little-endian
	REQUIRE(le16(head + 16) == 0);      // output gain (unity)
	REQUIRE(head[18] == 0);             // channel mapping family 0
}

TEST_CASE("OpusHead encodes mono channel count", "[opus][headers]")
{
	uint8_t head[OPUS_HEAD_SIZE];
	opus_format_head(head, /*channels=*/1, /*pre_skip=*/0, /*input_sample_rate=*/48000);
	REQUIRE(head[9] == 1);
	REQUIRE(le16(head + 10) == 0);
}

TEST_CASE("OpusHead writes pre-skip and sample rate little-endian", "[opus][headers]")
{
	uint8_t head[OPUS_HEAD_SIZE];
	// Values with distinct bytes per position catch endianness/offset slips.
	opus_format_head(head, 2, 0x1234, 0x0A0B0C0Du);
	REQUIRE(head[10] == 0x34);
	REQUIRE(head[11] == 0x12);
	REQUIRE(head[12] == 0x0D);
	REQUIRE(head[13] == 0x0C);
	REQUIRE(head[14] == 0x0B);
	REQUIRE(head[15] == 0x0A);
}

TEST_CASE("OpusTags matches RFC 7845 §5.2 byte layout", "[opus][headers]")
{
	const char *vendor = "obs-radio-output";
	const size_t vendor_len = std::strlen(vendor);

	uint8_t tags[64];
	const size_t n = opus_format_tags(tags, sizeof(tags), vendor);

	REQUIRE(n == 8 + 4 + vendor_len + 4);
	REQUIRE(std::memcmp(tags, "OpusTags", 8) == 0);
	REQUIRE(le32(tags + 8) == static_cast<uint32_t>(vendor_len));
	REQUIRE(std::memcmp(tags + 12, vendor, vendor_len) == 0);
	REQUIRE(le32(tags + 12 + vendor_len) == 0u); // user comment count
}

TEST_CASE("OpusTags reports 0 when the buffer is too small", "[opus][headers]")
{
	uint8_t tiny[4];
	REQUIRE(opus_format_tags(tiny, sizeof(tiny), "obs-radio-output") == 0);
}
