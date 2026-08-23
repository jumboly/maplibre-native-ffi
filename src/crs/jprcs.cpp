#include <array>
#include <stdexcept>

#include "crs/jprcs.hpp"

#include "crs/tmerc.hpp"

namespace mln::crs {

namespace {

struct ZoneOrigin {
  double lat;
  double lng;
};

// Zone origins from MLIT public notice No. 1613 of 2011 (平成23年国土交通省
// 告示第1613号). Minute fractions stay written as divisions: hardcoding a
// rounded decimal would silently move an origin by up to the rounding error,
// and the compiler evaluates these exactly once at compile time anyway.
constexpr std::array<ZoneOrigin, kJprcsZoneCount> kZoneOrigins = {{
  {.lat = 33.0, .lng = 129.5},                  // I
  {.lat = 33.0, .lng = 131.0},                  // II
  {.lat = 36.0, .lng = 132.0 + (10.0 / 60.0)},  // III (132°10')
  {.lat = 33.0, .lng = 133.5},                  // IV
  {.lat = 36.0, .lng = 134.0 + (20.0 / 60.0)},  // V (134°20')
  {.lat = 36.0, .lng = 136.0},                  // VI
  {.lat = 36.0, .lng = 137.0 + (10.0 / 60.0)},  // VII (137°10')
  {.lat = 36.0, .lng = 138.5},                  // VIII
  {.lat = 36.0, .lng = 139.0 + (50.0 / 60.0)},  // IX (139°50')
  {.lat = 40.0, .lng = 140.0 + (50.0 / 60.0)},  // X (140°50')
  {.lat = 44.0, .lng = 140.25},                 // XI (140°15')
  {.lat = 44.0, .lng = 142.25},                 // XII (142°15')
  {.lat = 44.0, .lng = 144.25},                 // XIII (144°15')
  {.lat = 26.0, .lng = 142.0},                  // XIV
  {.lat = 26.0, .lng = 127.5},                  // XV
  {.lat = 26.0, .lng = 124.0},                  // XVI
  {.lat = 26.0, .lng = 131.0},                  // XVII
  {.lat = 20.0, .lng = 136.0},                  // XVIII
  {.lat = 26.0, .lng = 154.0},                  // XIX
}};

// Every zone shares the same scale factor on its central meridian.
constexpr double kJprcsScaleFactor = 0.9999;

auto validate_zone(int zone) -> void {
  if (zone < 1 || zone > kJprcsZoneCount) {
    throw std::invalid_argument{"JPRCS zone must be in 1..19"};
  }
}

}  // namespace

auto jprcs_epsg_code(int zone) -> int {
  validate_zone(zone);
  return 6668 + zone;
}

auto make_jprcs_projection(int zone) -> TransverseMercator {
  validate_zone(zone);
  const ZoneOrigin& origin =
    kZoneOrigins.at(static_cast<std::size_t>(zone) - 1);
  return TransverseMercator{kGrs80, origin.lat, origin.lng, kJprcsScaleFactor};
}

}  // namespace mln::crs
