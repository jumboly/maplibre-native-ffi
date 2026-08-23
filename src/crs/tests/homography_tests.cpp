#include <array>
#include <cmath>
#include <cstddef>
#include <optional>

#include "crs/homography.hpp"
#include "crs/types.hpp"
#include "crs_tests.hpp"
#include "unity.h"

namespace {

using mln::crs::apply_homography;
using mln::crs::Homography;
using mln::crs::homography_to_mat4;
using mln::crs::Mat4;
using mln::crs::Point2;
using mln::crs::solve_homography_4pt;

using Quad = std::array<Point2, 4>;

// MVT tile-local corner coordinates (extent 8192), the src quad every
// per-tile homography in this project is solved against.
constexpr Quad kTileCorners = {
  Point2{0.0, 0.0}, Point2{8192.0, 0.0}, Point2{8192.0, 8192.0},
  Point2{0.0, 8192.0}
};

void test_reproduces_the_four_corners_exactly() {
  const Quad dst = {
    Point2{-0.51, 0.32}, Point2{-0.29, 0.335}, Point2{-0.28, 0.11},
    Point2{-0.52, 0.12}
  };
  const auto homography = solve_homography_4pt(kTileCorners, dst);
  TEST_ASSERT_TRUE(homography.has_value());
  for (std::size_t i = 0; i < 4; ++i) {
    const Point2 mapped = apply_homography(*homography, kTileCorners.at(i));
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, dst.at(i).x, mapped.x);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, dst.at(i).y, mapped.y);
  }
}

auto affine_reference(Point2 point) -> Point2 {
  return Point2{
    .x = 0.5 + (2e-5 * point.x) + (1e-6 * point.y),
    .y = -0.25 + (-1e-6 * point.x) + (2e-5 * point.y),
  };
}

void test_affine_correspondence_yields_a_linear_map() {
  // When dst = A * src + t the projective row must vanish: h[6] == h[7] == 0.
  Quad dst{};
  for (std::size_t i = 0; i < 4; ++i) {
    dst.at(i) = affine_reference(kTileCorners.at(i));
  }
  const auto homography = solve_homography_4pt(kTileCorners, dst);
  TEST_ASSERT_TRUE(homography.has_value());
  TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.0, (*homography)[6]);
  TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.0, (*homography)[7]);
  const Point2 probe{1234.0, 5678.0};
  const Point2 mapped = apply_homography(*homography, probe);
  const Point2 expected = affine_reference(probe);
  TEST_ASSERT_DOUBLE_WITHIN(1e-12, expected.x, mapped.x);
  TEST_ASSERT_DOUBLE_WITHIN(1e-12, expected.y, mapped.y);
}

void test_collinear_points_are_rejected_as_degenerate() {
  const Quad collinear = {
    Point2{0.0, 0.0}, Point2{1.0, 1.0}, Point2{2.0, 2.0}, Point2{3.0, 3.0}
  };
  TEST_ASSERT_FALSE(solve_homography_4pt(collinear, collinear).has_value());
  // A degenerate dst quad alone must also be rejected: it maps the tile onto
  // a line, which no invertible projective transform can represent.
  TEST_ASSERT_FALSE(solve_homography_4pt(kTileCorners, collinear).has_value());
}

void test_degeneracy_detection_is_scale_invariant() {
  // The pivot threshold is relative to the input magnitude, so rescaling a
  // degenerate correspondence must not change the verdict (an absolute
  // threshold passes the blown-up variant and rejects valid tiny inputs).
  for (const double scale : {1e6, 1e-6}) {
    Quad scaled_collinear{};
    Quad scaled_corners{};
    for (std::size_t i = 0; i < 4; ++i) {
      const double along = static_cast<double>(i);
      scaled_collinear.at(i) = Point2{along * scale, along * scale};
      scaled_corners.at(i) =
        Point2{kTileCorners.at(i).x * scale, kTileCorners.at(i).y * scale};
    }
    TEST_ASSERT_FALSE(
      solve_homography_4pt(scaled_corners, scaled_collinear).has_value()
    );
    TEST_ASSERT_TRUE(
      solve_homography_4pt(scaled_corners, kTileCorners).has_value()
    );
  }
}

auto smooth_reference(Point2 point) -> Point2 {
  // A smooth nonlinear map standing in for the mercator-to-projected-CRS
  // composition, evaluated on global (multi-tile) coordinates.
  const double gx = point.x / 8192.0;
  const double gy = point.y / 8192.0;
  return Point2{
    .x = gx + (0.001 * gy * gy) + (0.0005 * gx * gy),
    .y = gy + (0.002 * gx * gx) - (0.0003 * gx * gy),
  };
}

void test_adjacent_tiles_map_their_shared_edge_onto_one_line() {
  // Tile A spans x in [0, 8192], tile B spans x in [8192, 16384] (global
  // coordinates). Both are solved from the same tile-local corner quad.
  const Quad global_corners_b = {
    Point2{8192.0, 0.0}, Point2{16384.0, 0.0}, Point2{16384.0, 8192.0},
    Point2{8192.0, 8192.0}
  };
  Quad dst_a{};
  Quad dst_b{};
  for (std::size_t i = 0; i < 4; ++i) {
    dst_a.at(i) = smooth_reference(kTileCorners.at(i));
    dst_b.at(i) = smooth_reference(global_corners_b.at(i));
  }
  const auto homography_a = solve_homography_4pt(kTileCorners, dst_a);
  const auto homography_b = solve_homography_4pt(kTileCorners, dst_b);
  TEST_ASSERT_TRUE(homography_a.has_value());
  TEST_ASSERT_TRUE(homography_b.has_value());

  // The shared corners (A's right edge equals B's left edge) match exactly.
  const Point2 top_a = apply_homography(*homography_a, Point2{8192.0, 0.0});
  const Point2 top_b = apply_homography(*homography_b, Point2{0.0, 0.0});
  const Point2 bot_a = apply_homography(*homography_a, Point2{8192.0, 8192.0});
  const Point2 bot_b = apply_homography(*homography_b, Point2{0.0, 8192.0});
  TEST_ASSERT_DOUBLE_WITHIN(1e-12, top_b.x, top_a.x);
  TEST_ASSERT_DOUBLE_WITHIN(1e-12, top_b.y, top_a.y);
  TEST_ASSERT_DOUBLE_WITHIN(1e-12, bot_b.x, bot_a.x);
  TEST_ASSERT_DOUBLE_WITHIN(1e-12, bot_b.y, bot_a.y);

  // Interior edge points may be parameterized differently by the two
  // transforms, but both land on the same line, so fill and line geometry
  // meeting at the tile boundary leaves no gap or overlap.
  for (const double along : {0.25, 0.5, 0.75}) {
    const std::array<Point2, 2> probes = {
      apply_homography(*homography_a, Point2{8192.0, 8192.0 * along}),
      apply_homography(*homography_b, Point2{0.0, 8192.0 * along}),
    };
    for (const Point2& probe : probes) {
      const double cross = ((bot_a.x - top_a.x) * (probe.y - top_a.y)) -
                           ((bot_a.y - top_a.y) * (probe.x - top_a.x));
      TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.0, cross);
    }
  }
}

void test_mat4_embedding_matches_the_homography_and_pins_z_to_zero() {
  const Quad dst = {
    Point2{-0.9, 0.8}, Point2{0.7, 0.85}, Point2{0.75, -0.6},
    Point2{-0.85, -0.65}
  };
  const auto homography = solve_homography_4pt(kTileCorners, dst);
  TEST_ASSERT_TRUE(homography.has_value());
  const Mat4 mat = homography_to_mat4(*homography);
  // Column-major application of mat * vec4(x, y, 0, 1).
  const Point2 probe{3000.0, 5000.0};
  const double out_x = (mat[0] * probe.x) + (mat[4] * probe.y) + mat[12];
  const double out_y = (mat[1] * probe.x) + (mat[5] * probe.y) + mat[13];
  const double out_z = (mat[2] * probe.x) + (mat[6] * probe.y) + mat[14];
  const double out_w = (mat[3] * probe.x) + (mat[7] * probe.y) + mat[15];
  const Point2 expected = apply_homography(*homography, probe);
  // The matrix carries the coefficients in double (the GL JS original went
  // through Float32Array), so the agreement is exact up to rounding.
  TEST_ASSERT_DOUBLE_WITHIN(1e-12, expected.x, out_x / out_w);
  TEST_ASSERT_DOUBLE_WITHIN(1e-12, expected.y, out_y / out_w);
  TEST_ASSERT_TRUE(out_z == 0.0);
}

}  // namespace

void run_homography_tests() {
  UnitySetTestFile(__FILE__);
  RUN_TEST(test_reproduces_the_four_corners_exactly);
  RUN_TEST(test_affine_correspondence_yields_a_linear_map);
  RUN_TEST(test_collinear_points_are_rejected_as_degenerate);
  RUN_TEST(test_degeneracy_detection_is_scale_invariant);
  RUN_TEST(test_adjacent_tiles_map_their_shared_edge_onto_one_line);
  RUN_TEST(test_mat4_embedding_matches_the_homography_and_pins_z_to_zero);
}
