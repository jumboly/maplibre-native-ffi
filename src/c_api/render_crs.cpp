#define MLN_BUILDING_C

#include "c_api/boundary.hpp"
#include "map/map.hpp"
#include "maplibre_native_c.h"

auto mln_render_crs_options_default(void) noexcept -> mln_render_crs_options {
  return mln::core::render_crs_options_default();
}

auto mln_map_set_render_crs(
  mln_map map, const mln_render_crs_options* options
) noexcept -> mln_status {
  return mln::c_api::status_boundary([&]() -> mln_status {
    return mln::core::map_set_render_crs(map, options);
  });
}

auto mln_map_clear_render_crs(mln_map map) noexcept -> mln_status {
  return mln::c_api::status_boundary([&]() -> mln_status {
    return mln::core::map_clear_render_crs(map);
  });
}
