/**
 * @file maplibre_native_c/render_crs.h
 * Public C API declarations for rendering in an alternate coordinate
 * reference system.
 */

#ifndef MAPLIBRE_NATIVE_C_RENDER_CRS_H
#define MAPLIBRE_NATIVE_C_RENDER_CRS_H

#include <stdint.h>

#include "base.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Coordinate reference systems a map can render in. */
typedef enum mln_crs_kind : uint32_t {
  /**
   * The Japanese plane rectangular coordinate system (JGD2011,
   * EPSG:6669..6687), selected by zone number 1..19.
   */
  MLN_CRS_JAPAN_PLANE_RECTANGULAR = 1,
} mln_crs_kind;

/** Options describing one render-CRS output extent. */
typedef struct mln_render_crs_options {
  uint32_t size;
  /** The coordinate reference system family. See mln_crs_kind. */
  uint32_t crs_kind;
  /** Zone number within the CRS family; 1..19 for JGD2011. */
  uint32_t zone;
  /**
   * Output center easting, in the zone's projected meters. Note that the
   * surveying convention labels northing X and easting Y; this API uses the
   * GIS axis order (easting first).
   */
  double center_easting;
  /** Output center northing, in the zone's projected meters. */
  double center_northing;
  /**
   * Ground distance covered by one physical output pixel, in meters. Must be
   * positive and finite. A 1:5000 print at 300 dpi is
   * 5000 * 0.0254 / 300; scale and dpi vocabulary stays at the call site.
   */
  double meters_per_pixel;
  /**
   * Output rotation: the grid azimuth, in degrees clockwise from grid north,
   * that the image's up direction points at. 0 keeps grid north up. Callers
   * who want true north add the meridian convergence themselves.
   */
  double rotation;
} mln_render_crs_options;

/** Returns mln_render_crs_options with documented defaults. */
MLN_API mln_render_crs_options
mln_render_crs_options_default(void) MLN_NOEXCEPT;

/**
 * Renders the map in the projected coordinate reference system the options
 * describe instead of Web Mercator.
 *
 * Only rendering changes: tile loading, style evaluation, and query APIs
 * keep their Web Mercator semantics. The map must be in
 * MLN_MAP_MODE_STATIC with pitch 0; interactive use is out of scope.
 *
 * This call derives the camera center and zoom from the options and the
 * map's current dimensions and applies them, overwriting the current camera
 * (bearing and pitch become 0; rotation is carried by the render matrices,
 * not the camera). Changing the camera or the map size while a render CRS
 * is set leaves the rendering unspecified; call this function again after a
 * resize, and use mln_map_clear_render_crs() to return to Web Mercator.
 *
 * Returns:
 * - MLN_STATUS_OK on success.
 * - MLN_STATUS_INVALID_ARGUMENT when map is null or not live, options is
 *   null or its size is too small, or an option value is out of range.
 * - MLN_STATUS_WRONG_THREAD when called from a thread other than the map
 *   owner thread.
 * - MLN_STATUS_NATIVE_ERROR when an internal exception is converted to
 *   status.
 */
MLN_API mln_status mln_map_set_render_crs(
  mln_map map, const mln_render_crs_options* options
) MLN_NOEXCEPT;

/**
 * Restores Web Mercator rendering.
 *
 * The camera keeps the values mln_map_set_render_crs() derived; set a new
 * camera with mln_map_jump_to() if something else is wanted.
 *
 * Returns:
 * - MLN_STATUS_OK on success.
 * - MLN_STATUS_INVALID_ARGUMENT when map is null or not live.
 * - MLN_STATUS_WRONG_THREAD when called from a thread other than the map
 *   owner thread.
 * - MLN_STATUS_NATIVE_ERROR when an internal exception is converted to
 *   status.
 */
MLN_API mln_status mln_map_clear_render_crs(mln_map map) MLN_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif  // MAPLIBRE_NATIVE_C_RENDER_CRS_H
