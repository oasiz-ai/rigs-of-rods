/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Renderer-neutral math storage types and validation helpers.

#pragma once

#include <array>
#include <cmath>

namespace RoR::Render {

/// Canonical renderer-boundary coordinates and clip convention:
///
/// - right-handed world space measured in meters;
/// - +X points right, +Y points up, and a camera looks along local -Z;
/// - each snapshot chooses a float-precision render origin in absolute
///   simulation space; submitted positions are relative to that origin;
/// - column vectors are transformed as `clip = clip_from_render * render`;
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
