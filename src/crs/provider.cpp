#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <limits>
#include <memory>
#include <numbers>
#include <type_traits>

#include <mbgl/map/transform_state.hpp>
#include <mbgl/tile/tile_id.hpp>
#include <mbgl/util/constants.hpp>
#include <mbgl/util/logging.hpp>
#include <mbgl/util/mat4.hpp>

#include "crs/provider.hpp"

#include "crs/extent.hpp"
#include "crs/homography.hpp"
#include "crs/jprcs.hpp"
#include "crs/tmerc.hpp"

namespace mln::crs {
namespace {

// The math layer keeps its own Mat4 alias so it carries no mbgl include;
// this integration layer is where the two meet, so pin the layout
// equivalence down before handing matrices across.
static_assert(std::is_same_v<mln::mat4, std::array<double, 16>>);
static_assert(std::is_same_v<Mat4, std::array<double, 16>>);

// Normalized Web Mercator ([0, 1] on both axes, y growing south), the
// coordinate space tile IDs subdivide.
auto lng_lat_to_mercator(LngLat lng_lat) -> Point2 {
  const double lat_rad = lng_lat.lat * (std::numbers::pi / 180.0);
  return {
    .x = (lng_lat.lng / 360.0) + 0.5,
    .y = 0.5 - (std::log(std::tan((std::numbers::pi / 4.0) + (lat_rad / 2.0))) /
                (2.0 * std::numbers::pi)),
  };
}

auto mercator_to_lng_lat(Point2 mercator) -> LngLat {
  const double lat_rad =
    (2.0 * std::atan(std::exp((0.5 - mercator.y) * 2.0 * std::numbers::pi))) -
    (std::numbers::pi / 2.0);
  return {
    .lng = (mercator.x - 0.5) * 360.0,
    .lat = lat_rad * (180.0 / std::numbers::pi),
  };
}

struct MercatorAabb {
  double min_x;
  double min_y;
  double max_x;
  double max_y;
};

// Everything the hook needs, immutable after construction: TransformState
// copies on several threads share the one callable, so thread safety comes
// from never mutating this.
struct ProviderState {
  TransverseMercator projection;
  RenderExtent extent;
  MercatorAabb coverage;
};

// Clamps [tile_min, tile_max] to the coverage interval. The homography is
// exact at its four sample corners and the sampling window only picks where
// the (tiny) interior linearization error lands, so sampling inside the
// covered region keeps the approximation best where pixels are, and keeps
// tmerc away from its divergence domain on low-zoom tiles: Metal's stencil
// clear runs the tile matrix for tile 0/0/0, whose corners lie far outside
// any Japanese zone.
auto clamp_to_coverage(
  double tile_min, double tile_max, double coverage_min, double coverage_max
) -> std::array<double, 2> {
  // 1e-3 of the tile keeps the window non-degenerate for the DLT while
  // staying far above double noise at any zoom that can occur here.
  const double min_span = (tile_max - tile_min) * 1e-3;
  double low = std::max(tile_min, coverage_min);
  double high = std::min(tile_max, coverage_max);
  if (high - low < min_span) {
    const double mid = std::clamp(
      (low + high) / 2.0, tile_min + (min_span / 2.0),
      tile_max - (min_span / 2.0)
    );
    low = mid - (min_span / 2.0);
    high = mid + (min_span / 2.0);
  }
  return {low, high};
}

// The hook body: solve the tile's homography (tile-local coordinates to
// output clip space) and overwrite the default matrix with invP * H so the
// chokepoints' projMatrix left-multiplication cancels to exactly H. The
// whole homography is scaled by cameraToCenterDistance first: symbol
// shaders divide that constant by gl_Position.w for perspective size
// compensation, and the uniform scale (invisible to the projection) pins
// the ratio at 1.
void apply_tile_homography(
  const ProviderState& provider, mat4& matrix, const UnwrappedTileID& tile_id,
  const TransformState& transform
) {
  const double tile_scale = std::exp2(static_cast<double>(tile_id.canonical.z));
  const double tile_min_x = (static_cast<double>(tile_id.canonical.x) +
                             static_cast<double>(tile_id.wrap) * tile_scale) /
                            tile_scale;
  const double tile_min_y =
    static_cast<double>(tile_id.canonical.y) / tile_scale;
  const double tile_span = 1.0 / tile_scale;

  const auto [win_min_x, win_max_x] = clamp_to_coverage(
    tile_min_x, tile_min_x + tile_span, provider.coverage.min_x,
    provider.coverage.max_x
  );
  const auto [win_min_y, win_max_y] = clamp_to_coverage(
    tile_min_y, tile_min_y + tile_span, provider.coverage.min_y,
    provider.coverage.max_y
  );

  // Window corners in image order (TL, TR, BR, BL); mercator y and
  // tile-local y both grow south, so the correspondence is direct.
  const std::array<Point2, 4> corners_mercator{{
    {.x = win_min_x, .y = win_min_y},
    {.x = win_max_x, .y = win_min_y},
    {.x = win_max_x, .y = win_max_y},
    {.x = win_min_x, .y = win_max_y},
  }};

  std::array<Point2, 4> src{};
  std::array<Point2, 4> dst{};
  for (std::size_t idx = 0; idx < corners_mercator.size(); ++idx) {
    const Point2 corner = corners_mercator.at(idx);
    src.at(idx) = {
      .x = (corner.x - tile_min_x) / tile_span * util::EXTENT,
      .y = (corner.y - tile_min_y) / tile_span * util::EXTENT,
    };
    const EastNorth projected =
      provider.projection.forward(mercator_to_lng_lat(corner));
    dst.at(idx) = east_north_to_clip(provider.extent, projected);
  }

  auto homography = solve_homography_4pt(src, dst);
  if (!homography) {
    // Leaving the default matrix keeps the tile renderable (as mercator)
    // instead of vanishing; the seam it would cause is preferable to a hole
    // and this cannot happen for a finite, non-degenerate window.
    Log::Error(
      Event::General,
      "render_crs: degenerate tile homography; keeping the default matrix"
    );
    return;
  }

  const double camera_distance = transform.getCameraToCenterDistance();
  assert(camera_distance > 0);
  for (double& element : *homography) {
    element *= camera_distance;
  }

  const Mat4 homography_mat = homography_to_mat4(*homography);
  mat4 homography_mbgl;
  std::ranges::copy(homography_mat, homography_mbgl.begin());

  // homography_to_mat4 leaves NDC z at 0, but Metal clips z outside [0, w]
  // and the layer tweakers shift z down by a per-sublayer epsilon times w
  // for depth ordering -- at z = 0 that shift clips every depth-tested
  // drawable. Pin the flat output at mid-depth (z = 0.5 * w) instead: it
  // sits inside every backend's clip range and, being the same fraction for
  // every tile and layer, leaves the epsilon ordering unchanged.
  homography_mbgl[2] = 0.5 * homography_mbgl[3];
  homography_mbgl[6] = 0.5 * homography_mbgl[7];
  homography_mbgl[10] = 0.5 * homography_mbgl[11];
  homography_mbgl[14] = 0.5 * homography_mbgl[15];

  matrix::multiply(matrix, transform.getInvProjectionMatrix(), homography_mbgl);
}

}  // namespace

auto make_jprcs_render_crs(
  int zone, EastNorth center, double meters_per_pixel, double rotation_deg,
  std::uint32_t width_px, std::uint32_t height_px, double pixel_ratio
) -> RenderCrsSetup {
  auto projection = make_jprcs_projection(zone);
  const RenderExtent extent{
    .center = center,
    .meters_per_pixel = meters_per_pixel,
    .rotation_deg = rotation_deg,
    .width_px = width_px,
    .height_px = height_px,
  };

  // Geographic bounding box of the output outline. The paper edges are
  // gentle curves in geographic space, so the outline is sampled rather
  // than taken from the corners alone.
  double south = std::numeric_limits<double>::infinity();
  double north = -std::numeric_limits<double>::infinity();
  double west = std::numeric_limits<double>::infinity();
  double east = -std::numeric_limits<double>::infinity();
  for (const EastNorth& point : boundary_east_north(extent, 8)) {
    const LngLat geographic = projection.inverse(point);
    south = std::min(south, geographic.lat);
    north = std::max(north, geographic.lat);
    west = std::min(west, geographic.lng);
    east = std::max(east, geographic.lng);
  }

  // 2% of the span on each side, the GL JS predecessor's margin: covers
  // symbols anchored just outside the output and off-by-one tile effects
  // without materially growing the tile count.
  const double pad_lat = (north - south) * 0.02;
  const double pad_lng = (east - west) * 0.02;
  south -= pad_lat;
  north += pad_lat;
  west -= pad_lng;
  east += pad_lng;

  const Point2 mercator_nw = lng_lat_to_mercator({.lng = west, .lat = north});
  const Point2 mercator_se = lng_lat_to_mercator({.lng = east, .lat = south});

  auto state = std::make_shared<const ProviderState>(ProviderState{
    .projection = projection,
    .extent = extent,
    .coverage = {
      .min_x = mercator_nw.x,
      .min_y = mercator_nw.y,
      .max_x = mercator_se.x,
      .max_y = mercator_se.y,
    },
  });

  const LngLat center_geographic = projection.inverse(center);
  return RenderCrsSetup{
    .hook = [state](
              mat4& matrix, const UnwrappedTileID& tile_id,
              const TransformState& transform
            ) -> void {
      apply_tile_homography(*state, matrix, tile_id, transform);
    },
    .cover_bounds =
      LatLngBounds::hull(LatLng(south, west), LatLng(north, east)),
    .camera_center = LatLng(center_geographic.lat, center_geographic.lng),
    .camera_zoom =
      equivalent_zoom(meters_per_pixel, pixel_ratio, center_geographic.lat),
  };
}

}  // namespace mln::crs
