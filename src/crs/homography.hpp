#pragma once

#include <array>
#include <optional>

#include "crs/types.hpp"

namespace mln::crs {

// Row-major 3x3 projective transform, normalized so that h[8] == 1:
//
//   u = (h[0]*x + h[1]*y + h[2]) / (h[6]*x + h[7]*y + 1)
//   v = (h[3]*x + h[4]*y + h[5]) / (h[6]*x + h[7]*y + 1)
using Homography = std::array<double, 9>;

// Column-major 4x4 matrix (element (row, col) lives at m[col * 4 + row]).
// Layout-compatible with MapLibre Native's mat4; kept as a local alias so
// this layer carries no mbgl include (the integration layer asserts the
// layouts match before handing matrices across).
using Mat4 = std::array<double, 16>;

// Solves the projective transform mapping src[i] to dst[i] by direct linear
// transformation: a projective transform has 8 degrees of freedom, so four
// point correspondences (8 constraints) determine it uniquely. Returns
// std::nullopt for a degenerate correspondence (for example four collinear
// points), which callers must treat as "no transform exists".
[[nodiscard]] auto solve_homography_4pt(
  const std::array<Point2, 4>& src, const std::array<Point2, 4>& dst
) -> std::optional<Homography>;

// Applies the projective transform to a point.
[[nodiscard]] auto apply_homography(const Homography& homography, Point2 point)
  -> Point2;

// Embeds the homography into a column-major mat4 for consumption by
// mercator-style vertex shaders, which compute matrix * vec4(x, y, z, 1).
// The input z (elevation; always 0 in this layer's pitch = 0 scope) is
// ignored and the output is [u*w, v*w, 0, w]: NDC z stays 0 because a flat
// 2D rendering needs no depth ordering.
[[nodiscard]] auto homography_to_mat4(const Homography& homography) -> Mat4;

}  // namespace mln::crs
