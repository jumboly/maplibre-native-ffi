#include <cmath>
#include <numbers>
#include <vector>

#include "crs/extent.hpp"
#include "crs/types.hpp"
#include "crs_tests.hpp"
#include "unity.h"

namespace {

using mln::crs::boundary_east_north;
using mln::crs::east_north_to_clip;
using mln::crs::east_north_to_paper_px;
using mln::crs::EastNorth;
using mln::crs::equivalent_zoom;
using mln::crs::half_extent_meters;
using mln::crs::kEarthCircumferenceMeters;
using mln::crs::paper_px_to_east_north;
using mln::crs::Point2;
using mln::crs::RenderExtent;

constexpr double kPi = std::numbers::pi_v<double>;

// A3 sheet at 300 dpi and 1:5000, the GL JS predecessor's test extent
// (meters_per_pixel = 5000 * 0.0254 / 300).
auto a3_extent(double rotation_deg) -> RenderExtent {
  return RenderExtent{
    .center = EastNorth{.easting = -6556.0, .northing = -34626.0},
    .meters_per_pixel = 5000.0 * 0.0254 / 300.0,
    .rotation_deg = rotation_deg,
    .width_px = 4961,
    .height_px = 3508,
  };
}

void test_equivalent_zoom_matches_its_defining_property() {
  // Definition: C * cos(lat) / (512 * 2^z) equals the ground distance one
  // CSS pixel covers.
  for (const double scale : {2500.0, 5000.0, 25000.0}) {
    const double meters_per_css_px = scale * 0.0254 / 96.0;
    const double zoom = equivalent_zoom(meters_per_css_px, 1.0, 35.0);
    const double mercator_meters_per_css_px = kEarthCircumferenceMeters *
                                              std::cos(35.0 * kPi / 180.0) /
                                              (512.0 * std::exp2(zoom));
    TEST_ASSERT_DOUBLE_WITHIN(
      1e-9, meters_per_css_px, mercator_meters_per_css_px
    );
  }
  // Spot value: 1:2500 at 35 degrees is roughly zoom 16.56.
  TEST_ASSERT_DOUBLE_WITHIN(
    0.05, 16.56, equivalent_zoom(2500.0 * 0.0254 / 96.0, 1.0, 35.0)
  );
}

void test_equivalent_zoom_drops_one_level_per_doubled_resolution() {
  const double zoom_5000 = equivalent_zoom(5000.0 * 0.0254 / 96.0, 1.0, 35.0);
  const double zoom_10000 = equivalent_zoom(10000.0 * 0.0254 / 96.0, 1.0, 35.0);
  TEST_ASSERT_DOUBLE_WITHIN(1e-10, 1.0, zoom_5000 - zoom_10000);
}

void test_equivalent_zoom_is_independent_of_dpi() {
  // The old contract "dpi never appears in the zoom formula", restated for
  // the meters_per_pixel parameterization: the same print (one scale, any
  // dpi) yields one zoom, because pixel_ratio absorbs exactly the dpi
  // factor that meters_per_pixel carries.
  const double zoom_96dpi = equivalent_zoom(5000.0 * 0.0254 / 96.0, 1.0, 35.0);
  const double zoom_300dpi =
    equivalent_zoom(5000.0 * 0.0254 / 300.0, 300.0 / 96.0, 35.0);
  TEST_ASSERT_DOUBLE_WITHIN(1e-12, zoom_96dpi, zoom_300dpi);
}

void test_center_maps_to_the_paper_center() {
  const RenderExtent extent = a3_extent(0.0);
  const Point2 paper = east_north_to_paper_px(extent, extent.center);
  TEST_ASSERT_DOUBLE_WITHIN(1e-10, 4961.0 / 2.0, paper.x);
  TEST_ASSERT_DOUBLE_WITHIN(1e-10, 3508.0 / 2.0, paper.y);
}

void test_north_up_moves_paper_y_upward() {
  const RenderExtent extent = a3_extent(0.0);
  const Point2 paper = east_north_to_paper_px(
    extent, EastNorth{
              .easting = extent.center.easting,
              .northing = extent.center.northing + 100.0,
            }
  );
  TEST_ASSERT_TRUE(paper.y < 3508.0 / 2.0);
  TEST_ASSERT_DOUBLE_WITHIN(1e-10, 4961.0 / 2.0, paper.x);
}

void test_paper_round_trip_is_identity() {
  for (const double rotation_deg : {0.0, 30.0}) {
    const RenderExtent extent = a3_extent(rotation_deg);
    const Point2 probe{.x = 123.4, .y = 567.8};
    const Point2 restored =
      east_north_to_paper_px(extent, paper_px_to_east_north(extent, probe));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, probe.x, restored.x);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, probe.y, restored.y);
  }
}

void test_paper_corners_map_to_clip_extremes() {
  const RenderExtent extent = a3_extent(0.0);
  const Point2 half = half_extent_meters(extent);
  const Point2 clip = east_north_to_clip(
    extent, EastNorth{
              .easting = extent.center.easting + half.x,
              .northing = extent.center.northing + half.y,
            }
  );
  TEST_ASSERT_DOUBLE_WITHIN(1e-10, 1.0, clip.x);
  TEST_ASSERT_DOUBLE_WITHIN(1e-10, 1.0, clip.y);
}

void test_rotation_by_90_degrees_puts_east_up() {
  // rotation_deg is the grid azimuth the image's up direction points at, so
  // at 90 degrees a point due east of the center appears straight above it.
  const RenderExtent extent = a3_extent(90.0);
  const Point2 paper = east_north_to_paper_px(
    extent, EastNorth{
              .easting = extent.center.easting + 100.0,
              .northing = extent.center.northing,
            }
  );
  TEST_ASSERT_DOUBLE_WITHIN(1e-10, 4961.0 / 2.0, paper.x);
  TEST_ASSERT_TRUE(paper.y < 3508.0 / 2.0);
}

void test_rotation_preserves_center_and_lengths() {
  const RenderExtent extent = a3_extent(30.0);
  const Point2 center_px = east_north_to_paper_px(extent, extent.center);
  TEST_ASSERT_DOUBLE_WITHIN(1e-10, 4961.0 / 2.0, center_px.x);
  TEST_ASSERT_DOUBLE_WITHIN(1e-10, 3508.0 / 2.0, center_px.y);
  // A rotation is an isometry: paper distance times meters_per_pixel equals
  // ground distance for any offset.
  const EastNorth probe{
    .easting = extent.center.easting + 300.0,
    .northing = extent.center.northing - 400.0,
  };
  const Point2 paper = east_north_to_paper_px(extent, probe);
  const double paper_distance_m =
    std::hypot(paper.x - center_px.x, paper.y - center_px.y) *
    extent.meters_per_pixel;
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 500.0, paper_distance_m);
}

void test_rotated_clip_right_edge_lies_at_the_expected_bearing() {
  // The clip (1, 0) point sits half the extent's width away from the
  // center, at 90 degrees clockwise from the image's up azimuth: offset
  // (cos(theta), -sin(theta)) scaled by the half width.
  const double rotation_deg = 30.0;
  const RenderExtent extent = a3_extent(rotation_deg);
  const double rotation_rad = rotation_deg * kPi / 180.0;
  const Point2 half = half_extent_meters(extent);
  const Point2 clip = east_north_to_clip(
    extent,
    EastNorth{
      .easting = extent.center.easting + (half.x * std::cos(rotation_rad)),
      .northing = extent.center.northing - (half.x * std::sin(rotation_rad)),
    }
  );
  TEST_ASSERT_DOUBLE_WITHIN(1e-10, 1.0, clip.x);
  TEST_ASSERT_DOUBLE_WITHIN(1e-10, 0.0, clip.y);
}

void test_boundary_ring_is_closed_and_anchored_at_the_top_left() {
  const RenderExtent extent = a3_extent(0.0);
  const std::vector<EastNorth> ring = boundary_east_north(extent, 8);
  TEST_ASSERT_EQUAL_size_t((8 * 4) + 1, ring.size());
  TEST_ASSERT_TRUE(ring.front().easting == ring.back().easting);
  TEST_ASSERT_TRUE(ring.front().northing == ring.back().northing);
  const Point2 half = half_extent_meters(extent);
  TEST_ASSERT_DOUBLE_WITHIN(
    1e-9, extent.center.easting - half.x, ring.front().easting
  );
  TEST_ASSERT_DOUBLE_WITHIN(
    1e-9, extent.center.northing + half.y, ring.front().northing
  );
}

void test_boundary_ring_follows_the_rotated_frame() {
  // Every boundary sample must agree with the paper-space inverse transform
  // it is defined through, rotation included.
  const RenderExtent extent = a3_extent(30.0);
  const std::vector<EastNorth> ring = boundary_east_north(extent, 4);
  const EastNorth top_left =
    paper_px_to_east_north(extent, Point2{.x = 0.0, .y = 0.0});
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, top_left.easting, ring.front().easting);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, top_left.northing, ring.front().northing);
  // All samples of a rectangle's outline sit on the clip-space unit square.
  for (const EastNorth& sample : ring) {
    const Point2 clip = east_north_to_clip(extent, sample);
    const double edge_distance = std::max(std::abs(clip.x), std::abs(clip.y));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 1.0, edge_distance);
  }
}

}  // namespace

void run_extent_tests() {
  UnitySetTestFile(__FILE__);
  RUN_TEST(test_equivalent_zoom_matches_its_defining_property);
  RUN_TEST(test_equivalent_zoom_drops_one_level_per_doubled_resolution);
  RUN_TEST(test_equivalent_zoom_is_independent_of_dpi);
  RUN_TEST(test_center_maps_to_the_paper_center);
  RUN_TEST(test_north_up_moves_paper_y_upward);
  RUN_TEST(test_paper_round_trip_is_identity);
  RUN_TEST(test_paper_corners_map_to_clip_extremes);
  RUN_TEST(test_rotation_by_90_degrees_puts_east_up);
  RUN_TEST(test_rotation_preserves_center_and_lengths);
  RUN_TEST(test_rotated_clip_right_edge_lies_at_the_expected_bearing);
  RUN_TEST(test_boundary_ring_is_closed_and_anchored_at_the_top_left);
  RUN_TEST(test_boundary_ring_follows_the_rotated_frame);
}
