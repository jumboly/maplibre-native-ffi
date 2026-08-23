#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

#include "crs/extent.hpp"

#include "crs/types.hpp"

namespace mln::crs {

namespace {

constexpr double kDegToRad = std::numbers::pi_v<double> / 180.0;

// The reference CSS tile size zoom semantics are defined against.
constexpr double kTileSizeCssPx = 512.0;

struct Rotation {
  double sin;
  double cos;
};

auto rotation_of(const RenderExtent& extent) -> Rotation {
  const double angle_rad = extent.rotation_deg * kDegToRad;
  return Rotation{.sin = std::sin(angle_rad), .cos = std::cos(angle_rad)};
}

// Offset from the extent center to the paper frame: paper right and paper
// up in meters. With rotation_deg = 0 this is the identity on (dE, dN), so
// every formula below degenerates to the unrotated original.
auto to_paper_frame(const Rotation& rotation, double delta_e, double delta_n)
  -> Point2 {
  return Point2{
    .x = (delta_e * rotation.cos) - (delta_n * rotation.sin),
    .y = (delta_e * rotation.sin) + (delta_n * rotation.cos),
  };
}

}  // namespace

auto half_extent_meters(const RenderExtent& extent) -> Point2 {
  return Point2{
    .x = extent.meters_per_pixel * static_cast<double>(extent.width_px) / 2.0,
    .y = extent.meters_per_pixel * static_cast<double>(extent.height_px) / 2.0,
  };
}

auto east_north_to_paper_px(const RenderExtent& extent, EastNorth east_north)
  -> Point2 {
  const Point2 paper = to_paper_frame(
    rotation_of(extent), east_north.easting - extent.center.easting,
    east_north.northing - extent.center.northing
  );
  return Point2{
    .x = (static_cast<double>(extent.width_px) / 2.0) +
         (paper.x / extent.meters_per_pixel),
    .y = (static_cast<double>(extent.height_px) / 2.0) -
         (paper.y / extent.meters_per_pixel),
  };
}

auto paper_px_to_east_north(const RenderExtent& extent, Point2 paper_px)
  -> EastNorth {
  const Rotation rotation = rotation_of(extent);
  const double paper_right =
    (paper_px.x - (static_cast<double>(extent.width_px) / 2.0)) *
    extent.meters_per_pixel;
  const double paper_up =
    ((static_cast<double>(extent.height_px) / 2.0) - paper_px.y) *
    extent.meters_per_pixel;
  // Inverse of to_paper_frame (a rotation's inverse is its transpose).
  return EastNorth{
    .easting = extent.center.easting + (paper_right * rotation.cos) +
               (paper_up * rotation.sin),
    .northing = extent.center.northing - (paper_right * rotation.sin) +
                (paper_up * rotation.cos),
  };
}

auto east_north_to_clip(const RenderExtent& extent, EastNorth east_north)
  -> Point2 {
  const Point2 half = half_extent_meters(extent);
  const Point2 paper = to_paper_frame(
    rotation_of(extent), east_north.easting - extent.center.easting,
    east_north.northing - extent.center.northing
  );
  return Point2{.x = paper.x / half.x, .y = paper.y / half.y};
}

auto boundary_east_north(const RenderExtent& extent, int segments_per_edge)
  -> std::vector<EastNorth> {
  const auto width = static_cast<double>(extent.width_px);
  const auto height = static_cast<double>(extent.height_px);
  // Image-order corners in paper pixels: top-left, top-right, bottom-right,
  // bottom-left. Sampling happens in paper space, where edges are straight;
  // the projected ring picks up the rotation through the inverse transform.
  const std::array<Point2, 4> corners = {
    Point2{.x = 0.0, .y = 0.0},
    Point2{.x = width, .y = 0.0},
    Point2{.x = width, .y = height},
    Point2{.x = 0.0, .y = height},
  };
  std::vector<EastNorth> ring;
  ring.reserve((4 * static_cast<std::size_t>(segments_per_edge)) + 1);
  for (int edge = 0; edge < 4; ++edge) {
    const Point2& from = corners.at(static_cast<std::size_t>(edge));
    const Point2& next = corners.at(static_cast<std::size_t>((edge + 1) % 4));
    for (int step = 0; step < segments_per_edge; ++step) {
      const double along =
        static_cast<double>(step) / static_cast<double>(segments_per_edge);
      ring.push_back(paper_px_to_east_north(
        extent, Point2{
                  .x = from.x + ((next.x - from.x) * along),
                  .y = from.y + ((next.y - from.y) * along),
                }
      ));
    }
  }
  ring.push_back(ring.front());
  return ring;
}

auto equivalent_zoom(
  double meters_per_pixel, double pixel_ratio, double center_lat_deg
) -> double {
  const double meters_per_css_pixel = meters_per_pixel * pixel_ratio;
  const double cos_lat = std::cos(center_lat_deg * kDegToRad);
  return std::log2(
    kEarthCircumferenceMeters * cos_lat /
    (kTileSizeCssPx * meters_per_css_pixel)
  );
}

}  // namespace mln::crs
