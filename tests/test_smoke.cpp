#include <catch2/catch_test_macros.hpp>

// Smoke test for the Tier 1 unit-test harness (issue #73).  It deliberately
// exercises no production code — its only job is to prove that Catch2 + CTest
// build, link, discover, and run on every CI platform before any real test
// logic lands in the follow-up tickets (#74-#78).
TEST_CASE("harness smoke test", "[smoke]")
{
	REQUIRE(1 + 1 == 2);
}
