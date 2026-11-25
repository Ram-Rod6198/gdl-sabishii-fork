#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/render/CrtFilter.h"

using namespace gdl;

TEST_CASE("the picture-tube filter covers the composed frame and leaves the letterbox bars",
          "[crt]") {
    // The shipped window is the frame doubled: the tube is the whole framebuffer.
    const CrtFilter whole = CrtFilter::of({640, 448}, {1280, 896});
    CHECK(whole.x == 0.0f);
    CHECK(whole.y == 0.0f);
    CHECK(whole.width == 1280.0f);
    CHECK(whole.height == 896.0f);
    CHECK(whole.columns == 640.0f);
    CHECK(whole.rows == 448.0f);
    // A wide screen pillarboxes the frame; the tube is the frame between the bars.
    const CrtFilter wide = CrtFilter::of({640, 448}, {1920, 1080});
    CHECK(wide.height == 1080.0f);
    CHECK(wide.width == Catch::Approx(640.0f * 1080.0f / 448.0f));
    CHECK(wide.x == Catch::Approx((1920.0f - wide.width) / 2.0f));
    CHECK(wide.y == 0.0f);
    // A tall one letterboxes it.
    const CrtFilter tall = CrtFilter::of({640, 448}, {1280, 1280});
    CHECK(tall.width == 1280.0f);
    CHECK(tall.height == 896.0f);
    CHECK(tall.x == 0.0f);
    CHECK(tall.y == 192.0f);
}

TEST_CASE("scan lines are whole from two framebuffer rows a line and left out under one and a "
          "half",
          "[crt]") {
    CHECK(CrtFilter::of({640, 448}, {1280, 896}).scanlineStrength() == CrtFilter::kScanlines);
    CHECK(CrtFilter::of({640, 448}, {1920, 1080}).scanlineStrength() == CrtFilter::kScanlines);
    CHECK(CrtFilter::of({640, 448}, {3840, 2160}).scanlineStrength() == CrtFilter::kScanlines);
    // One and three quarter rows a line: half way in.
    CHECK(CrtFilter::of({640, 448}, {1120, 784}).scanlineStrength() ==
          Catch::Approx(CrtFilter::kScanlines / 2.0f));
    // At the frame's own size, or smaller, a line and its gap share a row: none are drawn.
    CHECK(CrtFilter::of({640, 448}, {960, 672}).scanlineStrength() == 0.0f);
    CHECK(CrtFilter::of({640, 448}, {640, 448}).scanlineStrength() == 0.0f);
    CHECK(CrtFilter::of({640, 448}, {320, 224}).scanlineStrength() == 0.0f);
}

TEST_CASE("a minimised window or an unset frame gives the filter nothing to draw", "[crt]") {
    CHECK(CrtFilter::of({640, 448}, {1280, 896}).drawable());
    CHECK_FALSE(CrtFilter::of({640, 448}, {0, 0}).drawable());
    CHECK_FALSE(CrtFilter::of({640, 448}, {0, 0}).scanlineStrength() > 0.0f);
    CHECK_FALSE(CrtFilter{}.drawable());
}
