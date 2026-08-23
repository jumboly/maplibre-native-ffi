#pragma once

#include <array>

#include "crs/types.hpp"

namespace mln::crs {

// Reference ellipsoid, described by its semi-major axis in meters and its
// flattening.
struct Ellipsoid {
  double semi_major;
  double flattening;
};

// GRS80, the ellipsoid every Japanese plane rectangular zone is defined on.
inline constexpr Ellipsoid kGrs80{
  .semi_major = 6378137.0, .flattening = 1.0 / 298.257222101
};

// Transverse Mercator projection after Poder & Engsager (1998): Gauss-
// Schreiber spherical transverse mercator wrapped in Krueger series of the
// sixth order in the third flattening n. This is the algorithm behind
// PROJ's default +proj=tmerc, proj4js's etmerc, and GeographicLib's
// TransverseMercator, so results agree with PROJ-derived reference values
// to well below the millimeter over a transverse Mercator zone's extent.
// The classic sixth-order-in-latitude tmerc series is NOT equivalent: its
// error at a zone edge exceeds this project's 5 mm verification tolerance.
//
// Series coefficients and evaluation order follow the PROJ / proj4js
// implementations (both MIT licensed); the constants are the published
// Poder-Engsager polynomials in n.
class TransverseMercator {
 public:
  // origin_lat/lng in degrees; scale_factor is the scale on the central
  // meridian (k0). False easting and northing are the caller's business:
  // every Japanese zone uses 0 and a UTM-style offset is a plain addition.
  TransverseMercator(
    const Ellipsoid& ellipsoid, double origin_lat_deg, double origin_lng_deg,
    double scale_factor
  );

  // Geographic (degrees) to projected meters relative to the origin.
  // A point farther than roughly 89.99 degrees of arc from the central
  // meridian has no transverse Mercator image; both fields come back
  // infinite there, matching PROJ's behavior.
  [[nodiscard]] auto forward(LngLat lng_lat) const -> EastNorth;

  // Projected meters back to geographic degrees, with the same infinity
  // convention for coordinates outside the projectable domain.
  [[nodiscard]] auto inverse(EastNorth east_north) const -> LngLat;

 private:
  std::array<double, 6> cgb_{};  // conformal-to-geodetic latitude series
  std::array<double, 6> cbg_{};  // geodetic-to-conformal latitude series
  std::array<double, 6> utg_{};  // ellipsoidal-to-spherical complex series
  std::array<double, 6> gtu_{};  // spherical-to-ellipsoidal complex series
  double qn_ = 0.0;              // normalized meridian arc scale (k0 applied)
  double zb_ = 0.0;              // origin latitude's arc offset
  double origin_lng_rad_ = 0.0;
  double semi_major_ = 0.0;
};

}  // namespace mln::crs
