#pragma once

#include "crs/tmerc.hpp"

namespace mln::crs {

// The Japanese plane rectangular coordinate system (JGD2011) has 19 zones,
// numbered 1 through 19 (traditionally I through XIX).
inline constexpr int kJprcsZoneCount = 19;

// Zone number to EPSG code: zone 1 is EPSG:6669, zone 19 is EPSG:6687.
// Throws std::invalid_argument for a zone outside 1..19.
[[nodiscard]] auto jprcs_epsg_code(int zone) -> int;

// Builds the projection for a zone: transverse Mercator on GRS80 with scale
// factor 0.9999 at the zone's legally defined origin. Conversions treat
// input longitude/latitude as JGD2011; the JGD2011-to-WGS84 datum shift is
// centimeter level and deliberately omitted, which the 5 mm external
// verification tolerance absorbs. Throws std::invalid_argument for a zone
// outside 1..19.
[[nodiscard]] auto make_jprcs_projection(int zone) -> TransverseMercator;

}  // namespace mln::crs
