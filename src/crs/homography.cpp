#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>
#include <utility>

#include "crs/homography.hpp"

#include "crs/types.hpp"

namespace mln::crs {

namespace {

// Degeneracy threshold for the normalized system. Hartley normalization
// (below) brings every coefficient to order 1 first, so this relative bound
// separates genuine rank loss from harmless input scaling.
constexpr double kRelativePivotEpsilon = 1e-13;

using Matrix8 = std::array<std::array<double, 8>, 8>;
using Vector8 = std::array<double, 8>;
// Row-major 3x3, same layout as Homography.
using Mat3 = std::array<double, 9>;

// Gaussian elimination with partial pivoting; solves matrix * x = rhs.
// Consumes its arguments (they are elimination workspace). Element access
// uses at(): the indices are loop-bounded, but the checked accessor keeps
// clang-tidy's constant-index rule satisfied without NOLINT noise, and the
// checks fold away under optimization for these fixed 8x8 bounds.
auto solve_linear_system(Matrix8& matrix, Vector8& rhs)
  -> std::optional<Vector8> {
  constexpr std::size_t size = 8;
  double max_abs = 0.0;
  for (const auto& row : matrix) {
    for (const double value : row) {
      max_abs = std::max(max_abs, std::abs(value));
    }
  }
  const double pivot_threshold = max_abs * kRelativePivotEpsilon;
  for (std::size_t col = 0; col < size; ++col) {
    std::size_t pivot = col;
    for (std::size_t row = col + 1; row < size; ++row) {
      if (
        std::abs(matrix.at(row).at(col)) > std::abs(matrix.at(pivot).at(col))
      ) {
        pivot = row;
      }
    }
    if (std::abs(matrix.at(pivot).at(col)) <= pivot_threshold) {
      return std::nullopt;
    }
    std::swap(matrix.at(col), matrix.at(pivot));
    std::swap(rhs.at(col), rhs.at(pivot));
    for (std::size_t row = col + 1; row < size; ++row) {
      const double factor = matrix.at(row).at(col) / matrix.at(col).at(col);
      for (std::size_t term = col; term < size; ++term) {
        matrix.at(row).at(term) -= factor * matrix.at(col).at(term);
      }
      rhs.at(row) -= factor * rhs.at(col);
    }
  }
  auto solution = Vector8{};
  for (std::size_t row = size; row-- > 0;) {
    double acc = rhs.at(row);
    for (std::size_t term = row + 1; term < size; ++term) {
      acc -= matrix.at(row).at(term) * solution.at(term);
    }
    solution.at(row) = acc / matrix.at(row).at(row);
  }
  return solution;
}

// Similarity transform of the Hartley normalization: p -> (p - center) *
// scale. Solving the DLT on raw coordinates mixes columns of wildly
// different magnitude (tile coordinates ~1e4 against the constant 1), which
// makes any single pivot threshold either miss degeneracies or reject valid
// systems depending on the input scale. Normalizing both quads to centroid 0
// and mean distance sqrt(2) makes every coefficient order 1, so conditioning
// no longer depends on the caller's units.
struct Similarity {
  double scale;
  Point2 center;
};

auto normalizing_similarity(const std::array<Point2, 4>& points)
  -> std::optional<Similarity> {
  auto center = Point2{.x = 0.0, .y = 0.0};
  for (const Point2& point : points) {
    center.x += point.x;
    center.y += point.y;
  }
  center.x /= 4.0;
  center.y /= 4.0;
  double mean_distance = 0.0;
  for (const Point2& point : points) {
    mean_distance += std::hypot(point.x - center.x, point.y - center.y);
  }
  mean_distance /= 4.0;
  if (mean_distance <= 0.0) {
    // All four points coincide; no transform can be recovered.
    return std::nullopt;
  }
  return Similarity{
    .scale = std::numbers::sqrt2_v<double> / mean_distance, .center = center
  };
}

auto normalize_point(const Similarity& similarity, Point2 point) -> Point2 {
  return Point2{
    .x = (point.x - similarity.center.x) * similarity.scale,
    .y = (point.y - similarity.center.y) * similarity.scale,
  };
}

auto multiply_3x3(const Mat3& lhs, const Mat3& rhs) -> Mat3 {
  auto out = Mat3{};
  for (std::size_t row = 0; row < 3; ++row) {
    for (std::size_t col = 0; col < 3; ++col) {
      double acc = 0.0;
      for (std::size_t term = 0; term < 3; ++term) {
        acc += lhs.at((3 * row) + term) * rhs.at((3 * term) + col);
      }
      out.at((3 * row) + col) = acc;
    }
  }
  return out;
}

}  // namespace

auto solve_homography_4pt(
  const std::array<Point2, 4>& src, const std::array<Point2, 4>& dst
) -> std::optional<Homography> {
  const auto src_similarity = normalizing_similarity(src);
  const auto dst_similarity = normalizing_similarity(dst);
  if (!src_similarity || !dst_similarity) {
    return std::nullopt;
  }

  // Each normalized correspondence (x, y) -> (u, v) contributes two rows to
  // the 8x8 linear system A * h = b in the unknowns h[0..7] (h[8] fixed at 1).
  auto matrix = Matrix8{};
  auto rhs = Vector8{};
  for (std::size_t i = 0; i < 4; ++i) {
    const Point2 source = normalize_point(*src_similarity, src.at(i));
    const Point2 target = normalize_point(*dst_similarity, dst.at(i));
    const double x = source.x;
    const double y = source.y;
    matrix.at(2 * i) = {x, y, 1.0, 0.0, 0.0, 0.0, -target.x * x, -target.x * y};
    rhs.at(2 * i) = target.x;
    matrix.at((2 * i) + 1) = {0.0, 0.0, 0.0,           x,
                              y,   1.0, -target.y * x, -target.y * y};
    rhs.at((2 * i) + 1) = target.y;
  }
  const auto solution = solve_linear_system(matrix, rhs);
  if (!solution) {
    return std::nullopt;
  }
  auto normalized = Mat3{};
  std::copy(solution->begin(), solution->end(), normalized.begin());
  normalized[8] = 1.0;

  // Undo the normalization: H = inv(T_dst) * H_normalized * T_src, where
  // T maps raw points to normalized ones.
  const Mat3 t_src = {
    src_similarity->scale,
    0.0,
    -src_similarity->scale * src_similarity->center.x,
    0.0,
    src_similarity->scale,
    -src_similarity->scale * src_similarity->center.y,
    0.0,
    0.0,
    1.0
  };
  const Mat3 t_dst_inverse = {
    1.0 / dst_similarity->scale,
    0.0,
    dst_similarity->center.x,
    0.0,
    1.0 / dst_similarity->scale,
    dst_similarity->center.y,
    0.0,
    0.0,
    1.0
  };
  auto homography =
    multiply_3x3(t_dst_inverse, multiply_3x3(normalized, t_src));

  // Restore the h[8] == 1 convention. A vanishing h[8] would mean the src
  // centroid maps to infinity, which no finite output quad produces.
  double max_coefficient = 0.0;
  for (const double value : homography) {
    max_coefficient = std::max(max_coefficient, std::abs(value));
  }
  const double divisor = homography[8];
  if (std::abs(divisor) <= max_coefficient * kRelativePivotEpsilon) {
    return std::nullopt;
  }
  for (double& value : homography) {
    value /= divisor;
  }
  homography[8] = 1.0;
  return homography;
}

auto apply_homography(const Homography& homography, Point2 point) -> Point2 {
  const double weight =
    (homography[6] * point.x) + (homography[7] * point.y) + homography[8];
  return Point2{
    .x =
      ((homography[0] * point.x) + (homography[1] * point.y) + homography[2]) /
      weight,
    .y =
      ((homography[3] * point.x) + (homography[4] * point.y) + homography[5]) /
      weight,
  };
}

auto homography_to_mat4(const Homography& homography) -> Mat4 {
  auto mat = Mat4{};
  mat[0] = homography[0];
  mat[4] = homography[1];
  mat[12] = homography[2];
  mat[1] = homography[3];
  mat[5] = homography[4];
  mat[13] = homography[5];
  // The z row (mat[2], mat[6], mat[10], mat[14]) stays 0; see the header.
  mat[3] = homography[6];
  mat[7] = homography[7];
  mat[15] = homography[8];
  return mat;
}

}  // namespace mln::crs
