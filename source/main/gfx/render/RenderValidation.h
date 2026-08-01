/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Shared validation result for renderer-neutral data contracts.

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace RoR::Render {

enum class ValidationCode : std::uint8_t {
  OK = 0,
  UNSUPPORTED_VERSION,
  INVALID_ENUM,
  INVALID_IDENTIFIER,
  DUPLICATE_IDENTIFIER,
  NON_DETERMINISTIC_ORDER,
  INVALID_HANDLE,
  WRONG_RESOURCE_KIND,
  NON_FINITE_VALUE,
  VALUE_OUT_OF_RANGE,
  INVALID_BOUNDS,
  INVALID_DIMENSIONS,
  EMPTY_PAYLOAD,
  SIZE_MISMATCH,
  MISSING_REFERENCE,
  INVALID_OUTPUT_MASK,
};

struct ValidationResult {
  static constexpr std::size_t kNoElement =
      (std::numeric_limits<std::size_t>::max)();

  ValidationCode code = ValidationCode::OK;
  std::size_t element_index = kNoElement;
  std::string field;
  std::string detail;

  [[nodiscard]] bool ok() const noexcept { return code == ValidationCode::OK; }

  explicit operator bool() const noexcept { return ok(); }

  static ValidationResult Success() { return {}; }

  static ValidationResult Failure(ValidationCode failure_code,
                                  const char *failure_field,
                                  const char *failure_detail,
                                  std::size_t failure_element = kNoElement) {
    ValidationResult result;
    result.code = failure_code;
    result.element_index = failure_element;
    result.field = failure_field != nullptr ? failure_field : "";
    result.detail = failure_detail != nullptr ? failure_detail : "";
    return result;
  }
};

} // namespace RoR::Render
