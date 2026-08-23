#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <utility>

#include "crs/tmerc.hpp"

#include "crs/types.hpp"

namespace mln::crs {

namespace {

constexpr double kDegToRad = std::numbers::pi_v<double> / 180.0;
constexpr double kRadToDeg = 180.0 / std::numbers::pi_v<double>;

// PROJ's bound on the eastward Gauss-Schreiber angle: beyond roughly 89.99
// degrees of arc from the central meridian the series diverges and the
// point has no transverse Mercator image.
constexpr double kMaxEastwardAngle = 2.623395162778;

using Series6 = std::array<double, 6>;

// Clenshaw summation of angle + sum(coefficients[k] * sin(2*(k+1)*angle)),
// the shared shape of the geodetic<->conformal latitude conversions.
auto gauss_latitude(const Series6& coefficients, double angle) -> double {
  const double two_cos = 2.0 * std::cos(2.0 * angle);
  double back2 = 0.0;
  double back1 = coefficients[5];
  double head = back1;
  for (std::size_t index = 5; index-- > 0;) {
    head = -back2 + (two_cos * back1) + coefficients.at(index);
    back2 = back1;
    back1 = head;
  }
  return angle + (head * std::sin(2.0 * angle));
}

// Real Clenshaw summation of sum(coefficients[k] * sin((k+1)*arg)).
auto clenshaw(const Series6& coefficients, double arg) -> double {
  const double two_cos = 2.0 * std::cos(arg);
  double back2 = 0.0;
  double back1 = coefficients[5];
  double head = back1;
  for (std::size_t index = 5; index-- > 0;) {
    head = -back2 + (two_cos * back1) + coefficients.at(index);
    back2 = back1;
    back1 = head;
  }
  return std::sin(arg) * head;
}

// Complex Clenshaw summation of sum(coefficients[k] * sin((k+1)*z)) for
// z = arg_north + i*arg_east; returns {real, imaginary}.
auto clenshaw_complex(
  const Series6& coefficients, double arg_north, double arg_east
) -> std::pair<double, double> {
  const double sin_north = std::sin(arg_north);
  const double cos_north = std::cos(arg_north);
  const double sinh_east = std::sinh(arg_east);
  const double cosh_east = std::cosh(arg_east);
  const double two_re = 2.0 * cos_north * cosh_east;
  const double two_im = -2.0 * sin_north * sinh_east;
  double head_re = coefficients[5];
  double head_im = 0.0;
  double back1_re = 0.0;
  double back1_im = 0.0;
  for (std::size_t index = 5; index-- > 0;) {
    const double back2_re = back1_re;
    const double back2_im = back1_im;
    back1_re = head_re;
    back1_im = head_im;
    head_re = -back2_re + (two_re * back1_re) - (two_im * back1_im) +
              coefficients.at(index);
    head_im = -back2_im + (two_im * back1_re) + (two_re * back1_im);
  }
  const double sin_re = sin_north * cosh_east;
  const double sin_im = cos_north * sinh_east;
  return {
    (sin_re * head_re) - (sin_im * head_im),
    (sin_re * head_im) + (sin_im * head_re)
  };
}

// Wraps a longitude difference into [-pi, pi].
auto adjust_lng(double lng_rad) -> double {
  constexpr double two_pi = 2.0 * std::numbers::pi_v<double>;
  if (std::abs(lng_rad) <= std::numbers::pi_v<double>) {
    return lng_rad;
  }
  return lng_rad - std::copysign(two_pi, lng_rad);
}

}  // namespace

TransverseMercator::TransverseMercator(
  const Ellipsoid& ellipsoid, double origin_lat_deg, double origin_lng_deg,
  double scale_factor
)
    : origin_lng_rad_(origin_lng_deg * kDegToRad),
      semi_major_(ellipsoid.semi_major) {
  // Third flattening; every series below is a polynomial in it. The
  // polynomials are the Poder-Engsager coefficients, transcribed 1:1 from
  // PROJ / proj4js (etmerc) so each line can be checked against the source.
  const double third_flattening =
    ellipsoid.flattening / (2.0 - ellipsoid.flattening);
  const double flat = third_flattening;

  double flat_pow = flat;
  cgb_[0] =
    flat *
    (2 + flat *
           (-2.0 / 3 + flat * (-2 + flat * (116.0 / 45 +
                                            flat * (26.0 / 45 +
                                                    flat * (-2854.0 / 675))))));
  cbg_[0] =
    flat *
    (-2 + flat * (2.0 / 3 +
                  flat * (4.0 / 3 + flat * (-82.0 / 45 +
                                            flat * (32.0 / 45 +
                                                    flat * (4642.0 / 4725))))));

  flat_pow *= flat;
  cgb_[1] =
    flat_pow *
    (7.0 / 3 +
     flat * (-8.0 / 5 + flat * (-227.0 / 45 + flat * (2704.0 / 315 +
                                                      flat * (2323.0 / 945)))));
  cbg_[1] =
    flat_pow *
    (5.0 / 3 + flat * (-16.0 / 15 +
                       flat * (-13.0 / 9 +
                               flat * (904.0 / 315 + flat * (-1522.0 / 945)))));

  flat_pow *= flat;
  cgb_[2] =
    flat_pow *
    (56.0 / 15 +
     flat * (-136.0 / 35 + flat * (-1262.0 / 105 + flat * (73814.0 / 2835))));
  cbg_[2] = flat_pow *
            (-26.0 / 15 +
             flat * (34.0 / 21 + flat * (8.0 / 5 + flat * (-12686.0 / 2835))));

  flat_pow *= flat;
  cgb_[3] = flat_pow *
            (4279.0 / 630 + flat * (-332.0 / 35 + flat * (-399572.0 / 14175)));
  cbg_[3] =
    flat_pow * (1237.0 / 630 + flat * (-12.0 / 5 + flat * (-24832.0 / 14175)));

  flat_pow *= flat;
  cgb_[4] = flat_pow * (4174.0 / 315 + flat * (-144838.0 / 6237));
  cbg_[4] = flat_pow * (-734.0 / 315 + flat * (109598.0 / 31185));

  flat_pow *= flat;
  cgb_[5] = flat_pow * (601676.0 / 22275);
  cbg_[5] = flat_pow * (444337.0 / 155925);

  const double flat_sq = flat * flat;
  qn_ = scale_factor / (1.0 + flat) *
        (1.0 + flat_sq * (1.0 / 4 + flat_sq * (1.0 / 64 + flat_sq / 256)));

  utg_[0] =
    flat *
    (-0.5 +
     flat * (2.0 / 3 + flat * (-37.0 / 96 +
                               flat * (1.0 / 360 +
                                       flat * (81.0 / 512 +
                                               flat * (-96199.0 / 604800))))));
  gtu_[0] =
    flat *
    (0.5 +
     flat * (-2.0 / 3 +
             flat * (5.0 / 16 +
                     flat * (41.0 / 180 + flat * (-127.0 / 288 +
                                                  flat * (7891.0 / 37800))))));

  utg_[1] =
    flat_sq *
    (-1.0 / 48 +
     flat * (-1.0 / 15 +
             flat * (437.0 / 1440 +
                     flat * (-46.0 / 105 + flat * (1118711.0 / 3870720)))));
  gtu_[1] =
    flat_sq *
    (13.0 / 48 +
     flat * (-3.0 / 5 +
             flat * (557.0 / 1440 +
                     flat * (281.0 / 630 + flat * (-1983433.0 / 1935360)))));

  flat_pow = flat_sq * flat;
  utg_[2] =
    flat_pow *
    (-17.0 / 480 +
     flat * (37.0 / 840 + flat * (209.0 / 4480 + flat * (-5569.0 / 90720))));
  gtu_[2] =
    flat_pow * (61.0 / 240 +
                flat * (-103.0 / 140 +
                        flat * (15061.0 / 26880 + flat * (167603.0 / 181440))));

  flat_pow *= flat;
  utg_[3] = flat_pow * (-4397.0 / 161280 +
                        flat * (11.0 / 504 + flat * (830251.0 / 7257600)));
  gtu_[3] = flat_pow * (49561.0 / 161280 +
                        flat * (-179.0 / 168 + flat * (6601661.0 / 7257600)));

  flat_pow *= flat;
  utg_[4] = flat_pow * (-4583.0 / 161280 + flat * (108847.0 / 3991680));
  gtu_[4] = flat_pow * (34729.0 / 80640 + flat * (-3418889.0 / 1995840));

  flat_pow *= flat;
  utg_[5] = flat_pow * (-20648693.0 / 638668800);
  gtu_[5] = flat_pow * (212378941.0 / 319334400);

  const double origin_conformal =
    gauss_latitude(cbg_, origin_lat_deg * kDegToRad);
  zb_ = -qn_ * (origin_conformal + clenshaw(gtu_, 2.0 * origin_conformal));
}

auto TransverseMercator::forward(LngLat lng_lat) const -> EastNorth {
  double gauss_e = adjust_lng((lng_lat.lng * kDegToRad) - origin_lng_rad_);
  double gauss_n = gauss_latitude(cbg_, lng_lat.lat * kDegToRad);

  const double sin_n = std::sin(gauss_n);
  const double cos_n = std::cos(gauss_n);
  const double sin_e = std::sin(gauss_e);
  const double cos_e = std::cos(gauss_e);

  // Gauss-Schreiber spherical transverse mercator of the conformal latitude.
  gauss_n = std::atan2(sin_n, cos_e * cos_n);
  gauss_e = std::atan2(sin_e * cos_n, std::hypot(sin_n, cos_n * cos_e));
  gauss_e = std::asinh(std::tan(gauss_e));

  const auto [delta_n, delta_e] =
    clenshaw_complex(gtu_, 2.0 * gauss_n, 2.0 * gauss_e);
  gauss_n += delta_n;
  gauss_e += delta_e;

  if (std::abs(gauss_e) > kMaxEastwardAngle) {
    constexpr double infinity = std::numeric_limits<double>::infinity();
    return EastNorth{.easting = infinity, .northing = infinity};
  }
  return EastNorth{
    .easting = semi_major_ * (qn_ * gauss_e),
    .northing = semi_major_ * ((qn_ * gauss_n) + zb_),
  };
}

auto TransverseMercator::inverse(EastNorth east_north) const -> LngLat {
  double gauss_e = east_north.easting / semi_major_ / qn_;
  double gauss_n = ((east_north.northing / semi_major_) - zb_) / qn_;

  if (std::abs(gauss_e) > kMaxEastwardAngle) {
    constexpr double infinity = std::numeric_limits<double>::infinity();
    return LngLat{.lng = infinity, .lat = infinity};
  }

  const auto [delta_n, delta_e] =
    clenshaw_complex(utg_, 2.0 * gauss_n, 2.0 * gauss_e);
  gauss_n += delta_n;
  gauss_e += delta_e;
  gauss_e = std::atan(std::sinh(gauss_e));

  const double sin_n = std::sin(gauss_n);
  const double cos_n = std::cos(gauss_n);
  const double sin_e = std::sin(gauss_e);
  const double cos_e = std::cos(gauss_e);

  const double conformal_lat =
    std::atan2(sin_n * cos_e, std::hypot(sin_e, cos_e * cos_n));
  const double lng_offset = std::atan2(sin_e, cos_e * cos_n);

  return LngLat{
    .lng = adjust_lng(lng_offset + origin_lng_rad_) * kRadToDeg,
    .lat = gauss_latitude(cgb_, conformal_lat) * kRadToDeg,
  };
}

}  // namespace mln::crs
