#pragma once

#include <cstdint>

#include <mbgl/map/tile_matrix_hook.hpp>
#include <mbgl/util/geo.hpp>

#include "crs/types.hpp"

namespace mln::crs {

// The integration layer between the pure math in src/crs/ and the hooks the
// maplibre-native patches add (patches/maplibre-native/0005, 0006). Unlike
// the math layer, this header may depend on mbgl types.
struct RenderCrsSetup {
  // Per-tile homography provider for Map::setTileMatrixHook. The hook is
  // immutable after construction and safe to call from the state copies that
  // reach placement, collision, and tile cover.
  TileMatrixHook hook;
  // Padded geographic coverage for Map::setTileCoverBoundsOverride. Tile
  // selection reads only the inverse projection matrix and cannot follow the
  // hook, so the covered region is named explicitly.
  LatLngBounds cover_bounds;
  // Derived camera: zoom keeps tile selection, style evaluation, and symbol
  // scaling consistent with the output resolution (equivalent_zoom), and
  // center is the extent center back-projected to geographic coordinates.
  // Bearing stays 0 -- rotation is carried by the homography, not the camera.
  LatLng camera_center;
  double camera_zoom;
};

// Builds the render setup for one Japanese plane rectangular zone output.
// center is in the zone's projected meters, meters_per_pixel and
// width/height describe physical output pixels, and pixel_ratio is the
// map's physical-to-CSS pixel factor (mln_map_options scale_factor).
// rotation_deg follows RenderExtent's convention (degrees clockwise from
// grid north for the image's up direction). Throws std::invalid_argument
// for a zone outside 1..19, matching make_jprcs_projection.
[[nodiscard]] auto make_jprcs_render_crs(
  int zone, EastNorth center, double meters_per_pixel, double rotation_deg,
  std::uint32_t width_px, std::uint32_t height_px, double pixel_ratio
) -> RenderCrsSetup;

}  // namespace mln::crs
