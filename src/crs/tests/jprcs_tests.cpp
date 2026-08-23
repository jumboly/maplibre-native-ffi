#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

#include "crs/jprcs.hpp"
#include "crs/tmerc.hpp"
#include "crs/types.hpp"
#include "crs_tests.hpp"
#include "jprcs_external_fixtures.hpp"
#include "unity.h"

namespace {

using mln::crs::EastNorth;
using mln::crs::jprcs_epsg_code;
using mln::crs::kJprcsZoneCount;
using mln::crs::LngLat;
using mln::crs::make_jprcs_projection;
using mln::crs::TransverseMercator;
using mln::crs::test_fixtures::kGsiFixtures;
using mln::crs::test_fixtures::kProjFixtures;

// External agreement contract: 5 mm against independently computed official
// values. At 1:2500 -- the largest scale this project targets -- one printed
// dot covers about 21 cm of ground, so this bound has three orders of
// magnitude of headroom. The fixtures themselves are rounded to 0.1 mm,
// which is why tightening the bound further would not add information.
constexpr double kToleranceMeters = 0.005;

void test_epsg_codes_cover_the_zone_range() {
  TEST_ASSERT_EQUAL_INT(6669, jprcs_epsg_code(1));
  TEST_ASSERT_EQUAL_INT(6677, jprcs_epsg_code(9));
  TEST_ASSERT_EQUAL_INT(6687, jprcs_epsg_code(19));
}

void test_out_of_range_zones_throw() {
  // The math layer owns this precondition (the C API layer catches the
  // exception at the boundary), so the contract is pinned here.
  for (const int zone : {0, 20}) {
    bool threw = false;
    try {
      static_cast<void>(make_jprcs_projection(zone));
    } catch (const std::invalid_argument&) {
      threw = true;
    }
    TEST_ASSERT_TRUE(threw);
  }
}

void test_tokyo_metropolitan_office_lands_near_its_known_coordinates() {
  // Coarse sanity separate from the exact GSI comparison below: when the
  // 5 mm test fails, this one tells apart "slightly off" from "wrong zone,
  // wrong axes, or wrong units".
  const TransverseMercator projection = make_jprcs_projection(9);
  const EastNorth projected =
    projection.forward(LngLat{.lng = 139.6917, .lat = 35.6895});
  TEST_ASSERT_DOUBLE_WITHIN(50.0, -34439.0, projected.northing);
  TEST_ASSERT_DOUBLE_WITHIN(50.0, -12819.0, projected.easting);
}

void test_round_trips_are_identities_across_all_zones() {
  // The PROJ fixtures provide four spread-out probe points per zone.
  for (const auto& fixture : kProjFixtures) {
    const TransverseMercator projection = make_jprcs_projection(fixture.zone);
    const LngLat probe{.lng = fixture.lng, .lat = fixture.lat};
    const LngLat restored = projection.inverse(projection.forward(probe));
    TEST_ASSERT_DOUBLE_WITHIN(1e-11, probe.lng, restored.lng);
    TEST_ASSERT_DOUBLE_WITHIN(1e-11, probe.lat, restored.lat);
  }
}

void test_matches_gsi_official_values() {
  for (const auto& fixture : kGsiFixtures) {
    const TransverseMercator projection = make_jprcs_projection(fixture.zone);
    const EastNorth projected =
      projection.forward(LngLat{.lng = fixture.lng, .lat = fixture.lat});
    // The GSI fixture keeps the surveying axis order: X = north, Y = east.
    TEST_ASSERT_DOUBLE_WITHIN(
      kToleranceMeters, fixture.gsi_x_north, projected.northing
    );
    TEST_ASSERT_DOUBLE_WITHIN(
      kToleranceMeters, fixture.gsi_y_east, projected.easting
    );
  }
}

void test_grid_convergence_matches_gsi() {
  // Numerical differentiation: project two points 0.001 degrees apart along
  // the meridian and measure the grid azimuth of the true-north direction.
  constexpr double kLatStepDeg = 0.001;
  constexpr double kRadToDeg = 180.0 / std::numbers::pi_v<double>;
  for (const auto& fixture : kGsiFixtures) {
    const TransverseMercator projection = make_jprcs_projection(fixture.zone);
    const EastNorth base =
      projection.forward(LngLat{.lng = fixture.lng, .lat = fixture.lat});
    const EastNorth north = projection.forward(
      LngLat{.lng = fixture.lng, .lat = fixture.lat + kLatStepDeg}
    );
    const double convergence_deg =
      std::atan2(north.easting - base.easting, north.northing - base.northing) *
      kRadToDeg;
    TEST_ASSERT_DOUBLE_WITHIN(5e-4, fixture.grid_conv_deg, convergence_deg);
  }
}

void test_matches_proj_reference_values() {
  double max_diff = 0.0;
  for (const auto& fixture : kProjFixtures) {
    const TransverseMercator projection = make_jprcs_projection(fixture.zone);
    const EastNorth projected =
      projection.forward(LngLat{.lng = fixture.lng, .lat = fixture.lat});
    max_diff = std::max(max_diff, std::abs(projected.easting - fixture.east));
    max_diff = std::max(max_diff, std::abs(projected.northing - fixture.north));
  }
  TEST_ASSERT_DOUBLE_WITHIN(kToleranceMeters, 0.0, max_diff);
}

}  // namespace

void run_jprcs_tests() {
  UnitySetTestFile(__FILE__);
  RUN_TEST(test_epsg_codes_cover_the_zone_range);
  RUN_TEST(test_out_of_range_zones_throw);
  RUN_TEST(test_tokyo_metropolitan_office_lands_near_its_known_coordinates);
  RUN_TEST(test_round_trips_are_identities_across_all_zones);
  RUN_TEST(test_matches_gsi_official_values);
  RUN_TEST(test_grid_convergence_matches_gsi);
  RUN_TEST(test_matches_proj_reference_values);
}
