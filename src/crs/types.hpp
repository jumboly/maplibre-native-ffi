#pragma once

// Vocabulary types for the CRS math layer (src/crs). This layer is pure math:
// nothing under src/crs may include mbgl headers, other src modules, or the
// public C API, so it stays independently testable on every target.
//
// Axis conventions differ across the sources this layer reconciles, so each
// coordinate kind gets its own named-field type instead of a bare pair:
//
//   * Projected coordinates here follow the proj/GIS order [easting,
//     northing]. Surveying practice and the official EPSG:6669-6687 axis
//     definitions put X = northing first; values quoted from those sources
//     must be swapped explicitly at the point of comparison.
//   * Geographic coordinates are [longitude, latitude] in degrees.

namespace mln::crs {

// Geographic coordinate in degrees.
struct LngLat {
  double lng;
  double lat;
};

// Projected coordinate in meters, ordered [easting, northing]. See the axis
// note above before comparing against surveying or EPSG-official values.
struct EastNorth {
  double easting;
  double northing;
};

// Dimensionless 2D point: tile-local coordinates, output pixels, or NDC.
struct Point2 {
  double x;
  double y;
};

}  // namespace mln::crs
