#pragma once

#include <cstdint>
#include <vector>

#include "crs/types.hpp"

namespace mln::crs {

// Used by equivalent_zoom and shared with any mercator-side arithmetic so
// both sides of a comparison round identically. Kept at the GL JS
// predecessor's value: MapLibre's 2*pi*6378137 differs only in the 1e-9
// relative range, but mixing the two spellings would move test expectations.
inline constexpr double kEarthCircumferenceMeters = 40075016.686;

// The output extent: what one rendered image covers, in projected-CRS terms.
// Deliberately free of print vocabulary -- scale denominators and dpi reduce
// to meters_per_pixel and the map's pixel ratio at the call site (a 1:5000
// print at 300 dpi is meters_per_pixel = 5000 * 0.0254 / 300).
struct RenderExtent {
  // Center of the output, in the target CRS.
  EastNorth center;
  // Ground distance covered by one output (physical) pixel.
  double meters_per_pixel;
  // Rotation of the output: the grid azimuth, in degrees clockwise from
  // grid north, that the image's up direction points at. 0 keeps north up;
  // 90 puts east up. This matches the observable meaning of MapLibre's
  // bearing (bearing 90 renders east up); whether the two signs agree is
  // confirmed against the core in the integration phase, and a mismatch is
  // absorbed there rather than by changing this layer's convention.
  double rotation_deg;
  // Output size in physical pixels.
  std::uint32_t width_px;
  std::uint32_t height_px;
};

// Half the extent's footprint, in meters: {half width, half height} of the
// rotated output rectangle measured in its own axes.
[[nodiscard]] auto half_extent_meters(const RenderExtent& extent) -> Point2;

// Projected coordinate to output pixel coordinate (origin at the top-left
// corner, y grows downward).
[[nodiscard]] auto east_north_to_paper_px(
  const RenderExtent& extent, EastNorth east_north
) -> Point2;

// Output pixel coordinate back to the projected coordinate.
[[nodiscard]] auto paper_px_to_east_north(
  const RenderExtent& extent, Point2 paper_px
) -> EastNorth;

// Projected coordinate to clip space (NDC: x right, y up, the output
// rectangle spanning [-1, 1] on both axes).
[[nodiscard]] auto east_north_to_clip(
  const RenderExtent& extent, EastNorth east_north
) -> Point2;

// The extent's outline as a closed ring of 4 * segments_per_edge + 1
// projected points, starting at the image's top-left corner and walking the
// edges in image order. Each edge is sampled because a straight paper edge
// is a gentle curve in other coordinate spaces; the caller composes this
// with a projection's inverse to get geographic coverage (tile-cover input,
// preview polygons).
[[nodiscard]] auto boundary_east_north(
  const RenderExtent& extent, int segments_per_edge
) -> std::vector<EastNorth>;

// The Web Mercator zoom whose resolution at center_lat_deg matches
// meters_per_pixel, in CSS pixel terms: pixel_ratio is the map's physical-
// to-CSS pixel factor (mln_map_options scale_factor; dpi / 96 in print
// terms). Zoom controls tile selection, style evaluation, and symbol
// scaling, so this keeps zoom-dependent styling natural for the output
// resolution while the render matrices come from the extent. dpi never
// appears here by contract -- it reaches the map only through pixel_ratio.
[[nodiscard]] auto equivalent_zoom(
  double meters_per_pixel, double pixel_ratio, double center_lat_deg
) -> double;

}  // namespace mln::crs
