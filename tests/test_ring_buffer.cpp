// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ring-buffer wraparound + FIFO drop-oldest tests (issue #78) for the send-buf
// module extracted in #77.  send-buf.c is pure (no OBS/libshout), so this links
// it directly with no stubs.  Small capacities make the boundary math explicit.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

extern "C" {
#include "send-buf.h"
}

namespace {
std::vector<uint8_t> seq(uint8_t start, size_t n)
{
	std::vector<uint8_t> v(n);
	for (size_t i = 0; i < n; i++)
		v[i] = static_cast<uint8_t>(start + i);
	return v;
}

// Push then read everything currently buffered; returns the bytes read back.
std::vector<uint8_t> read_all(radio_send_buf *sb)
{
	std::vector<uint8_t> out(radio_send_buf_used(sb));
	const size_t n = radio_send_buf_read(sb, out.data(), out.size());
	out.resize(n);
	return out;
}
} // namespace

TEST_CASE("a freshly initialized buffer is empty", "[ring]")
{
	radio_send_buf sb;
	REQUIRE(radio_send_buf_init(&sb, 8));
	REQUIRE(radio_send_buf_used(&sb) == 0);
	uint8_t dst[4];
	REQUIRE(radio_send_buf_read(&sb, dst, sizeof(dst)) == 0);
	radio_send_buf_free(&sb);
}

TEST_CASE("a write that does not wrap round-trips intact", "[ring]")
{
	radio_send_buf sb;
	REQUIRE(radio_send_buf_init(&sb, 8));
	const auto in = seq(1, 4);
	REQUIRE(radio_send_buf_push(&sb, in.data(), in.size()) == 0);
	REQUIRE(radio_send_buf_used(&sb) == 4);
	REQUIRE(read_all(&sb) == in);
	radio_send_buf_free(&sb);
}

TEST_CASE("a write that wraps mid-payload reassembles in order", "[ring]")
{
	radio_send_buf sb;
	REQUIRE(radio_send_buf_init(&sb, 8));
	// Advance read/write positions to 6 so the next write straddles the end.
	const auto warmup = seq(100, 6);
	radio_send_buf_push(&sb, warmup.data(), warmup.size());
	(void)read_all(&sb); // drain; rpos == wpos == 6
	REQUIRE(radio_send_buf_used(&sb) == 0);

	const auto in = seq(1, 4); // writes at indices 6,7,0,1 — wraps
	REQUIRE(radio_send_buf_push(&sb, in.data(), in.size()) == 0);
	REQUIRE(radio_send_buf_used(&sb) == 4);
	REQUIRE(read_all(&sb) == in);
	radio_send_buf_free(&sb);
}

TEST_CASE("a write that exactly fills the buffer keeps all bytes", "[ring]")
{
	radio_send_buf sb;
	REQUIRE(radio_send_buf_init(&sb, 8));
	const auto in = seq(1, 8);
	REQUIRE(radio_send_buf_push(&sb, in.data(), in.size()) == 0);
	REQUIRE(radio_send_buf_used(&sb) == 8);
	REQUIRE(read_all(&sb) == in);
	radio_send_buf_free(&sb);
}

TEST_CASE("overflow by one byte drops exactly the oldest byte", "[ring]")
{
	radio_send_buf sb;
	REQUIRE(radio_send_buf_init(&sb, 8));
	const auto first = seq(1, 8); // fills
	REQUIRE(radio_send_buf_push(&sb, first.data(), first.size()) == 0);

	const uint8_t extra = 99;
	REQUIRE(radio_send_buf_push(&sb, &extra, 1) == 1); // one oldest byte dropped
	REQUIRE(radio_send_buf_used(&sb) == 8);

	// Oldest (1) is gone; buffer now holds 2..8 then 99.
	std::vector<uint8_t> expected = {2, 3, 4, 5, 6, 7, 8, 99};
	REQUIRE(read_all(&sb) == expected);
	radio_send_buf_free(&sb);
}

TEST_CASE("a payload larger than capacity keeps only the newest capacity bytes", "[ring]")
{
	radio_send_buf sb;
	REQUIRE(radio_send_buf_init(&sb, 8));
	const auto fill = seq(1, 8);
	radio_send_buf_push(&sb, fill.data(), fill.size());

	// 12-byte payload into an 8-byte buffer: drop = used(8)+len(12)-cap(8) = 12.
	const auto big = seq(20, 12); // 20..31
	REQUIRE(radio_send_buf_push(&sb, big.data(), big.size()) == 12);
	REQUIRE(radio_send_buf_used(&sb) == 8);

	// Only the newest 8 bytes of the 12-byte payload survive (24..31).
	REQUIRE(read_all(&sb) == seq(24, 8));
	radio_send_buf_free(&sb);
}

TEST_CASE("a partial read advances the read position", "[ring]")
{
	radio_send_buf sb;
	REQUIRE(radio_send_buf_init(&sb, 8));
	const auto in = seq(1, 6);
	radio_send_buf_push(&sb, in.data(), in.size());

	uint8_t dst[2];
	REQUIRE(radio_send_buf_read(&sb, dst, 2) == 2);
	REQUIRE(dst[0] == 1);
	REQUIRE(dst[1] == 2);
	REQUIRE(radio_send_buf_used(&sb) == 4);
	REQUIRE(read_all(&sb) == seq(3, 4));
	radio_send_buf_free(&sb);
}
