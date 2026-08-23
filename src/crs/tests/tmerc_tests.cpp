#include <array>
#include <cmath>
#include <numbers>

#include "crs/tmerc.hpp"
#include "crs/types.hpp"
#include "crs_tests.hpp"
#include "unity.h"

namespace {

using mln::crs::EastNorth;
using mln::crs::kGrs80;
using mln::crs::LngLat;
using mln::crs::TransverseMercator;

// Zone IX (Tokyo) parameters, the workhorse configuration of this project.
constexpr double kOriginLatDeg = 36.0;
constexpr double kOriginLngDeg = 139.0 + (50.0 / 60.0);
constexpr double kScaleFactor = 0.9999;

auto zone_ix() -> TransverseMercator {
  return TransverseMercator{kGrs80, kOriginLatDeg, kOriginLngDeg, kScaleFactor};
}

void test_origin_projects_to_zero() {
  const EastNorth projected =
    zone_ix().forward(LngLat{.lng = kOriginLngDeg, .lat = kOriginLatDeg});
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 0.0, projected.easting);
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 0.0, projected.northing);
}

void test_round_trip_is_identity() {
  const TransverseMercator projection = zone_ix();
  // The metropolitan office, a point near the zone edge, and one south of
  // the origin; roughly the coordinate spread a zone serves.
  const std::array<LngLat, 3> probes = {
    LngLat{.lng = 139.6917, .lat = 35.6895},
    LngLat{.lng = 141.0, .lat = 36.5},
    LngLat{.lng = 138.5, .lat = 34.9},
  };
  for (const LngLat& probe : probes) {
    const LngLat restored = projection.inverse(projection.forward(probe));
    TEST_ASSERT_DOUBLE_WITHIN(1e-11, probe.lng, restored.lng);
    TEST_ASSERT_DOUBLE_WITHIN(1e-11, probe.lat, restored.lat);
  }
}

void test_scale_factor_applies_on_the_central_meridian() {
  const TransverseMercator projection = zone_ix();
  // Along the central meridian the projection scale is exactly k0, so a
  // small latitude step must advance northing by k0 times the meridian arc.
  const double step_deg = 0.01;
  const double step_rad = step_deg * std::numbers::pi_v<double> / 180.0;
  const EastNorth north_point = projection.forward(
    LngLat{.lng = kOriginLngDeg, .lat = kOriginLatDeg + (step_deg / 2.0)}
  );
  const EastNorth south_point = projection.forward(
    LngLat{.lng = kOriginLngDeg, .lat = kOriginLatDeg - (step_deg / 2.0)}
  );
  const double eccentricity_sq = kGrs80.flattening * (2.0 - kGrs80.flattening);
  const double sin_lat =
    std::sin(kOriginLatDeg * std::numbers::pi_v<double> / 180.0);
  const double meridian_radius =
    kGrs80.semi_major * (1.0 - eccentricity_sq) /
    std::pow(1.0 - (eccentricity_sq * sin_lat * sin_lat), 1.5);
  const double expected = kScaleFactor * meridian_radius * step_rad;
  const double actual = north_point.northing - south_point.northing;
  // 1 mm tolerance over a ~1.1 km arc: tight enough that dropping the k0
  // factor (a 0.11 m effect) or using the wrong radius fails loudly.
  TEST_ASSERT_DOUBLE_WITHIN(1e-3, expected, actual);
}

}  // namespace

void run_tmerc_tests() {
  UnitySetTestFile(__FILE__);
  RUN_TEST(test_origin_projects_to_zero);
  RUN_TEST(test_round_trip_is_identity);
  RUN_TEST(test_scale_factor_applies_on_the_central_meridian);
}
