/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Renderer-neutral math storage types and validation helpers.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace RoR::Render {

constexpr float kCanonicalProjectionTolerance =
    16.0F * (std::numeric_limits<float>::epsilon)();

/// Canonical renderer-boundary coordinates and clip convention:
///
/// - right-handed world space measured in meters;
/// - +X points right, +Y points up, and a camera looks along local -Z;
/// - each snapshot chooses a float-precision render origin in absolute
///   simulation space; submitted positions are relative to that origin;
/// - column vectors are transformed as
///   `clip = clip_from_view * view_from_render * render`;
/// - matrices use column-major storage (element = column * 4 + row);
/// - normalized device X/Y/Z ranges are [-1, 1], [-1, 1], and [0, 1];
/// - depth is non-reversed: the near plane maps to 0 and far maps to 1;
/// - framebuffer/readback pixel (0, 0) is the upper-left pixel.
///
/// A backend adapter owns any required OpenGL, Vulkan, Metal, or D3D
/// projection/depth/Y remapping. Producers always submit this convention.

struct Float2 {
  float x = 0.0F;
  float y = 0.0F;
};

struct Float3 {
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
};

struct Float4 {
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
  float w = 0.0F;
};

struct Double3 {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

/// Column-major transform with translation in elements 12, 13, and 14.
struct Matrix4x4 {
  std::array<float, 16> elements{{
      1.0F,
      0.0F,
      0.0F,
      0.0F,
      0.0F,
      1.0F,
      0.0F,
      0.0F,
      0.0F,
      0.0F,
      1.0F,
      0.0F,
      0.0F,
      0.0F,
      0.0F,
      1.0F,
  }};
};

struct Bounds3 {
  Float3 minimum{};
  Float3 maximum{};
};

constexpr bool operator==(const Float2 &lhs, const Float2 &rhs) noexcept {
  return lhs.x == rhs.x && lhs.y == rhs.y;
}
constexpr bool operator!=(const Float2 &lhs, const Float2 &rhs) noexcept {
  return !(lhs == rhs);
}
constexpr bool operator==(const Float3 &lhs, const Float3 &rhs) noexcept {
  return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}
constexpr bool operator!=(const Float3 &lhs, const Float3 &rhs) noexcept {
  return !(lhs == rhs);
}
constexpr bool operator==(const Float4 &lhs, const Float4 &rhs) noexcept {
  return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z &&
         lhs.w == rhs.w;
}
constexpr bool operator!=(const Float4 &lhs, const Float4 &rhs) noexcept {
  return !(lhs == rhs);
}
constexpr bool operator==(const Double3 &lhs, const Double3 &rhs) noexcept {
  return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}
constexpr bool operator!=(const Double3 &lhs, const Double3 &rhs) noexcept {
  return !(lhs == rhs);
}
inline bool operator==(const Matrix4x4 &lhs,
                       const Matrix4x4 &rhs) noexcept {
  return lhs.elements == rhs.elements;
}
inline bool operator!=(const Matrix4x4 &lhs,
                       const Matrix4x4 &rhs) noexcept {
  return !(lhs == rhs);
}
constexpr bool operator==(const Bounds3 &lhs, const Bounds3 &rhs) noexcept {
  return lhs.minimum == rhs.minimum && lhs.maximum == rhs.maximum;
}
constexpr bool operator!=(const Bounds3 &lhs, const Bounds3 &rhs) noexcept {
  return !(lhs == rhs);
}

inline bool IsFinite(float value) noexcept { return std::isfinite(value); }

inline bool IsFinite(double value) noexcept { return std::isfinite(value); }

inline bool IsFinite(const Float2 &value) noexcept {
  return IsFinite(value.x) && IsFinite(value.y);
}

inline bool IsFinite(const Float3 &value) noexcept {
  return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
}

inline bool IsFinite(const Float4 &value) noexcept {
  return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z) &&
         IsFinite(value.w);
}

inline bool IsFinite(const Double3 &value) noexcept {
  return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
}

inline bool IsFinite(const Matrix4x4 &value) noexcept {
  for (const float element : value.elements) {
    if (!IsFinite(element)) {
      return false;
    }
  }
  return true;
}

/// Determinant of the upper-left object-to-render linear 3x3 transform.
inline float LinearDeterminant(const Matrix4x4 &value) noexcept {
  const float m00 = value.elements[0U];
  const float m01 = value.elements[4U];
  const float m02 = value.elements[8U];
  const float m10 = value.elements[1U];
  const float m11 = value.elements[5U];
  const float m12 = value.elements[9U];
  const float m20 = value.elements[2U];
  const float m21 = value.elements[6U];
  const float m22 = value.elements[10U];
  return m00 * (m11 * m22 - m12 * m21) - m01 * (m10 * m22 - m12 * m20) +
         m02 * (m10 * m21 - m11 * m20);
}

inline bool HasInvertibleLinearTransform(const Matrix4x4 &value) noexcept {
  constexpr float kMinimumAbsoluteLinearDeterminant = 1.0e-8F;
  if (!IsFinite(value)) {
    return false;
  }
  const float determinant = LinearDeterminant(value);
  return IsFinite(determinant) &&
         std::fabs(determinant) > kMinimumAbsoluteLinearDeterminant;
}

inline bool IsCanonicalAffineTransform(const Matrix4x4 &value) noexcept {
  return IsFinite(value) && value.elements[3U] == 0.0F &&
         value.elements[7U] == 0.0F && value.elements[11U] == 0.0F &&
         value.elements[15U] == 1.0F;
}

inline bool HasInvertibleAffineTransform(const Matrix4x4 &value) noexcept {
  return IsCanonicalAffineTransform(value) &&
         HasInvertibleLinearTransform(value);
}

/// True when the upper-left 3x3 scales all three axes by the same factor.
///
/// A composed float rotation can leave mathematically identical column lengths
/// a handful of ULPs apart. This bound admits that representation noise while
/// rejecting a material scale difference. The pinned PBS vertex path
/// multiplies both authored normals and tangents by worldViewMat; it does not
/// enable accurate_non_uniform_scaled_normals, and its tangent path has no
/// inverse-transpose equivalent, so a non-uniformly scaled instance cannot
/// carry a correct tangent frame.
///
/// Shared by the producer, which filters such an instance out of the capture,
/// and by the presenter, which skips it if one arrives anyway. Both count the
/// drop; neither may fail the frame over it.
inline bool HasEffectivelyUniformLinearScale(const Matrix4x4 &value) noexcept {
  const Float3 columns[] = {
      {value.elements[0U], value.elements[1U], value.elements[2U]},
      {value.elements[4U], value.elements[5U], value.elements[6U]},
      {value.elements[8U], value.elements[9U], value.elements[10U]},
  };
  const auto length_squared = [](const Float3 &axis) noexcept {
    return axis.x * axis.x + axis.y * axis.y + axis.z * axis.z;
  };
  const float lengths_squared[] = {length_squared(columns[0U]),
                                   length_squared(columns[1U]),
                                   length_squared(columns[2U])};
  const float largest = (std::max)(
      lengths_squared[0U],
      (std::max)(lengths_squared[1U], lengths_squared[2U]));
  constexpr float kRelativeUniformScaleTolerance =
      64.0F * (std::numeric_limits<float>::epsilon)();
  if (!IsFinite(largest) || largest <= 0.0F) {
    return false;
  }
  for (const float candidate : lengths_squared) {
    if (!IsFinite(candidate) ||
        std::fabs(candidate - largest) >
            kRelativeUniformScaleTolerance * largest) {
      return false;
    }
  }
  return true;
}

/// A camera view transform must preserve metric distances and handedness.
/// Translation is unrestricted; the upper-left 3x3 must be an orthonormal
/// right-handed basis. This rejects scale, shear, and reflection before they
/// can corrupt camera-space depth, lighting, or motion vectors.
inline bool HasRigidRightHandedAffineTransform(
    const Matrix4x4 &value, float tolerance = 1.0e-3F) noexcept {
  if (!IsCanonicalAffineTransform(value) || !IsFinite(tolerance) ||
      tolerance < 0.0F) {
    return false;
  }
  const Float3 x{value.elements[0U], value.elements[1U],
                 value.elements[2U]};
  const Float3 y{value.elements[4U], value.elements[5U],
                 value.elements[6U]};
  const Float3 z{value.elements[8U], value.elements[9U],
                 value.elements[10U]};
  const auto length_squared = [](const Float3 &axis) noexcept {
    return axis.x * axis.x + axis.y * axis.y + axis.z * axis.z;
  };
  const auto dot = [](const Float3 &lhs, const Float3 &rhs) noexcept {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
  };
  return std::fabs(length_squared(x) - 1.0F) <= tolerance &&
         std::fabs(length_squared(y) - 1.0F) <= tolerance &&
         std::fabs(length_squared(z) - 1.0F) <= tolerance &&
         std::fabs(dot(x, y)) <= tolerance &&
         std::fabs(dot(x, z)) <= tolerance &&
         std::fabs(dot(y, z)) <= tolerance &&
         std::fabs(LinearDeterminant(value) - 1.0F) <= 4.0F * tolerance;
}

inline bool NearlyEqualRelative(float lhs, float rhs,
                                float tolerance =
                                    kCanonicalProjectionTolerance) noexcept {
  if (!IsFinite(lhs) || !IsFinite(rhs) || !IsFinite(tolerance) ||
      tolerance < 0.0F) {
    return false;
  }
  const float scale = (std::max)(std::fabs(lhs), std::fabs(rhs));
  const float subnormal_floor =
      16.0F * (std::numeric_limits<float>::denorm_min)();
  return std::fabs(lhs - rhs) <=
         (std::max)(tolerance * scale, subnormal_floor);
}

/// Validates one of the two canonical renderer-boundary projection forms:
/// right-handed perspective or orthographic, camera-forward -Z, non-reversed
/// depth [0, 1]. Perspective off-center lens terms live in m02/m12;
/// orthographic off-center lens terms live in m03/m13. Temporal jitter is a
/// separate CameraViewRequest value and must not be baked into this matrix.
inline bool IsCanonicalProjection(const Matrix4x4 &value, float near_plane,
                                  float far_plane,
                                  float tolerance =
                                      kCanonicalProjectionTolerance) noexcept {
  if (!IsFinite(value) || !IsFinite(near_plane) || !IsFinite(far_plane) ||
      near_plane <= 0.0F || far_plane <= near_plane ||
      !IsFinite(tolerance) || tolerance < 0.0F || value.elements[0U] <= 0.0F ||
      value.elements[5U] <= 0.0F) {
    return false;
  }

  const auto zero = [](float element) noexcept { return element == 0.0F; };
  const float depth_scale = far_plane / (near_plane - far_plane);
  const float depth_offset = near_plane * depth_scale;
  const bool perspective =
      zero(value.elements[1U]) && zero(value.elements[2U]) &&
      zero(value.elements[3U]) && zero(value.elements[4U]) &&
      zero(value.elements[6U]) && zero(value.elements[7U]) &&
      zero(value.elements[12U]) && zero(value.elements[13U]) &&
      value.elements[10U] < 0.0F && value.elements[14U] < 0.0F &&
      NearlyEqualRelative(value.elements[10U], depth_scale, tolerance) &&
      value.elements[11U] == -1.0F &&
      NearlyEqualRelative(value.elements[14U], depth_offset, tolerance) &&
      zero(value.elements[15U]);
  if (perspective) {
    return true;
  }

  const float ortho_depth_scale = 1.0F / (near_plane - far_plane);
  const float ortho_depth_offset = near_plane * ortho_depth_scale;
  return zero(value.elements[1U]) && zero(value.elements[2U]) &&
         zero(value.elements[3U]) && zero(value.elements[4U]) &&
         zero(value.elements[6U]) && zero(value.elements[7U]) &&
         zero(value.elements[8U]) && zero(value.elements[9U]) &&
         zero(value.elements[11U]) && value.elements[10U] < 0.0F &&
         value.elements[14U] < 0.0F &&
         NearlyEqualRelative(value.elements[10U], ortho_depth_scale,
                             tolerance) &&
         NearlyEqualRelative(value.elements[14U], ortho_depth_offset,
                             tolerance) &&
         value.elements[15U] == 1.0F;
}

inline bool IsValid(const Bounds3 &bounds) noexcept {
  return IsFinite(bounds.minimum) && IsFinite(bounds.maximum) &&
         bounds.minimum.x <= bounds.maximum.x &&
         bounds.minimum.y <= bounds.maximum.y &&
         bounds.minimum.z <= bounds.maximum.z;
}

inline bool IsNonNegative(const Float3 &value) noexcept {
  return IsFinite(value) && value.x >= 0.0F && value.y >= 0.0F &&
         value.z >= 0.0F;
}

inline bool IsNormalizedColor(const Float4 &value) noexcept {
  return IsFinite(value) && value.x >= 0.0F && value.x <= 1.0F &&
         value.y >= 0.0F && value.y <= 1.0F && value.z >= 0.0F &&
         value.z <= 1.0F && value.w >= 0.0F && value.w <= 1.0F;
}

inline float LengthSquared(const Float3 &value) noexcept {
  return value.x * value.x + value.y * value.y + value.z * value.z;
}

inline float Dot(const Float3 &lhs, const Float3 &rhs) noexcept {
  return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

inline bool IsNormalized(const Float3 &value,
                         float tolerance = 1.0e-3F) noexcept {
  if (!IsFinite(value) || !IsFinite(tolerance) || tolerance < 0.0F) {
    return false;
  }
  return std::fabs(LengthSquared(value) - 1.0F) <= tolerance;
}

inline bool IsNormalizedTangent(const Float4 &value,
                                float tolerance = 1.0e-3F) noexcept {
  return IsFinite(value) &&
         IsNormalized(Float3{value.x, value.y, value.z}, tolerance) &&
         std::fabs(std::fabs(value.w) - 1.0F) <= tolerance;
}

} // namespace RoR::Render
