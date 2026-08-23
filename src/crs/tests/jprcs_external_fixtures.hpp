#pragma once

#include <array>

// External verification fixtures, transcribed mechanically from the source
// data below (auto-generated; do not edit by hand -- regenerate instead).
//
// - kGsiFixtures: official values computed by the GSI surveying calculation
//   API (bl2xy.pl, refFrame=2 = JGD2011). The gsi_x_north / gsi_y_east
//   fields keep the surveying axis convention (X = north, Y = east); tests
//   must swap axes when comparing against this layer's [easting, northing].
//   grid_conv_deg is the grid convergence (true north against grid north)
//   in degrees.
// - kProjFixtures: independent reference values from pyproj 3.x (PROJ, the
//   official EPSG:6669-6687 definitions). east / north in meters, rounded
//   to 0.1 mm by the generator.
//
// Retrieved 2026-08-22. Source of truth: the archived maplibre-vector-
// printer repository, src/projection/jprcs.external-fixtures.ts (its
// docs/05-poc-design.md documents the regeneration procedure); this header
// is a 1:1 mechanical conversion of that file.

namespace mln::crs::test_fixtures {

// Constructors instead of aggregates so the record-per-line tables below
// stay readable without designated initializers at every field.
struct GsiFixture {
  int zone;
  double lng;
  double lat;
  const char* name;
  double gsi_x_north;
  double gsi_y_east;
  double grid_conv_deg;

  constexpr GsiFixture(
    int zone_number, double lng_deg, double lat_deg, const char* place,
    double x_north_m, double y_east_m, double convergence_deg
  )
      : zone(zone_number),
        lng(lng_deg),
        lat(lat_deg),
        name(place),
        gsi_x_north(x_north_m),
        gsi_y_east(y_east_m),
        grid_conv_deg(convergence_deg) {}
};

struct ProjFixture {
  int zone;
  double lng;
  double lat;
  double east;
  double north;

  constexpr ProjFixture(
    int zone_number, double lng_deg, double lat_deg, double east_m,
    double north_m
  )
      : zone(zone_number),
        lng(lng_deg),
        lat(lat_deg),
        east(east_m),
        north(north_m) {}
};

inline constexpr std::array<GsiFixture, 8> kGsiFixtures = {{
  {1, 129.8779, 32.7503, "Nagasaki", -27626.3434, 35411.6784, -0.204438889},
  {2, 131.6126, 33.2382, "Oita", 26582.5951, 57089.8381, -0.335788889},
  {4, 133.5311, 33.5597, "Kochi", 62070.2452, 2887.6171, -0.017191667},
  {6, 135.7681, 35.0116, "Kyoto", -109627.3164, -21164.6418, 0.13305},
  {9, 139.6917, 35.6895, "Tokyo Metropolitan Government", -34439.1888,
   -12818.777, 0.082627778},
  {12, 142.365, 43.7708, "Asahikawa", -25457.439, 9258.22, -0.079555556},
  {13, 145.585, 43.33, "Nemuro", -73567.8159, 108262.5791, -0.916163889},
  {15, 127.6792, 26.2124, "Naha", 23541.7317, 17906.9355, -0.079152778},
}};

inline constexpr std::array<ProjFixture, 76> kProjFixtures = {{
  {1, 129.5, 33, 0.0, 0.0},
  {1, 130.0, 33.4, 46510.2061, 44470.4886},
  {1, 129.0, 32.6, -46931.8698, -44245.5959},
  {1, 130.4, 32.8, 84290.155, -21819.6819},
  {2, 131, 33, 0.0, 0.0},
  {2, 131.5, 33.4, 46510.2061, 44470.4886},
  {2, 130.5, 32.6, -46931.8698, -44245.5959},
  {2, 131.9, 32.8, 84290.155, -21819.6819},
  {3, 132.1666666667, 36, -0.0, 0.0},
  {3, 132.6666666667, 36.4, 44848.768, 44496.7747},
  {3, 131.6666666667, 35.6, -45304.0579, -44262.6078},
  {3, 133.0666666667, 35.8, 81344.6483, -21815.4752},
  {4, 133.5, 33, 0.0, 0.0},
  {4, 134.0, 33.4, 46510.2061, 44470.4886},
  {4, 133.0, 32.6, -46931.8698, -44245.5959},
  {4, 134.4, 32.8, 84290.155, -21819.6819},
  {5, 134.3333333333, 36, 0.0, 0.0},
  {5, 134.8333333333, 36.4, 44848.768, 44496.7747},
  {5, 133.8333333333, 35.6, -45304.0579, -44262.6078},
  {5, 135.2333333333, 35.8, 81344.6483, -21815.4752},
  {6, 136, 36, 0.0, 0.0},
  {6, 136.5, 36.4, 44848.768, 44496.7747},
  {6, 135.5, 35.6, -45304.0579, -44262.6078},
  {6, 136.9, 35.8, 81344.6483, -21815.4752},
  {7, 137.1666666667, 36, -0.0, 0.0},
  {7, 137.6666666667, 36.4, 44848.768, 44496.7747},
  {7, 136.6666666667, 35.6, -45304.0579, -44262.6078},
  {7, 138.0666666667, 35.8, 81344.6483, -21815.4752},
  {8, 138.5, 36, 0.0, 0.0},
  {8, 139.0, 36.4, 44848.768, 44496.7747},
  {8, 138.0, 35.6, -45304.0579, -44262.6078},
  {8, 139.4, 35.8, 81344.6483, -21815.4752},
  {9, 139.8333333333, 36, 0.0, 0.0},
  {9, 140.3333333333, 36.4, 44848.768, 44496.7747},
  {9, 139.3333333333, 35.6, -45304.0579, -44262.6078},
  {9, 140.7333333333, 35.8, 81344.6483, -21815.4752},
  {10, 140.8333333333, 40, 0.0, -0.0},
  {10, 141.3333333333, 40.4, 42442.5939, 44530.9776},
  {10, 140.3333333333, 39.6, -42940.8246, -44288.4433},
  {10, 141.7333333333, 39.8, 77071.093, -21816.8362},
  {11, 140.25, 44, 0.0, 0.0},
  {11, 140.75, 44.4, 39828.7169, 44563.6074},
  {11, 139.75, 43.6, -40367.5265, -44317.4221},
  {11, 141.15, 43.8, 72420.5021, -21826.1355},
  {12, 142.25, 44, 0.0, 0.0},
  {12, 142.75, 44.4, 39828.7169, 44563.6074},
  {12, 141.75, 43.6, -40367.5265, -44317.4221},
  {12, 143.15, 43.8, 72420.5021, -21826.1355},
  {13, 144.25, 44, 0.0, 0.0},
  {13, 144.75, 44.4, 39828.7169, 44563.6074},
  {13, 143.75, 43.6, -40367.5265, -44317.4221},
  {13, 145.15, 43.8, 72420.5021, -21826.1355},
  {14, 142, 26, 0.0, 0.0},
  {14, 142.5, 26.4, 49883.5092, 44408.7754},
  {14, 141.5, 25.6, -50222.5458, -44214.8569},
  {14, 142.9, 25.8, 90251.2662, -21846.5545},
  {15, 127.5, 26, 0.0, 0.0},
  {15, 128.0, 26.4, 49883.5092, 44408.7754},
  {15, 127.0, 25.6, -50222.5458, -44214.8569},
  {15, 128.4, 25.8, 90251.2662, -21846.5545},
  {16, 124, 26, 0.0, 0.0},
  {16, 124.5, 26.4, 49883.5092, 44408.7754},
  {16, 123.5, 25.6, -50222.5458, -44214.8569},
  {16, 124.9, 25.8, 90251.2662, -21846.5545},
  {17, 131, 26, 0.0, 0.0},
  {17, 131.5, 26.4, 49883.5092, 44408.7754},
  {17, 130.5, 25.6, -50222.5458, -44214.8569},
  {17, 131.9, 25.8, 90251.2662, -21846.5545},
  {18, 136, 20, 0.0, -0.0},
  {18, 136.5, 20.4, 52185.3923, 44357.6629},
  {18, 135.5, 19.6, -52449.7127, -44199.5231},
  {18, 136.9, 19.8, 94294.3344, -21887.5156},
  {19, 154, 26, 0.0, 0.0},
  {19, 154.5, 26.4, 49883.5092, 44408.7754},
  {19, 153.5, 25.6, -50222.5458, -44214.8569},
  {19, 154.9, 25.8, 90251.2662, -21846.5545},
}};

}  // namespace mln::crs::test_fixtures
