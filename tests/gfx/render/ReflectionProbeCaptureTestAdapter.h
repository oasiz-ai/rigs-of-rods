/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#pragma once

#include "ReflectionProbeCaptureReceipt.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace RoR::Render::Testing {

/// Concrete adapter available only to portable contract tests. Shipping code
/// has no successful receipt issuer until a real native adapter is reviewed.
class ReflectionProbeCaptureTestAdapter final {
public:
  [[nodiscard]] static ReflectionProbeCaptureReceipt Issue(
      std::uint64_t plan_id, std::size_t request_index,
      const ReflectionProbeUpdateRequest &request,
      const ReflectionProbeCaptureMeasurementResult &measurement,
      std::uint64_t execution_id = UINT64_C(0x746573745f69626c)) {
    if (!measurement.ok()) {
      std::abort();
    }
    return ReflectionProbeCaptureReceipt::IssueFromConcreteAdapter(
        plan_id, request_index, request, execution_id, measurement);
  }

  [[nodiscard]] static ReflectionProbeCaptureReceipt CaptureSynthetic(
      std::uint64_t plan_id, std::size_t request_index,
      const ReflectionProbeUpdateRequest &request,
      ReflectionProbeCaptureBackend backend =
          ReflectionProbeCaptureBackend::OGRE_NEXT_METAL,
      std::uint64_t execution_id = UINT64_C(0x746573745f69626c)) {
    CaptureFixture fixture = MakeFixture(request);
    const ReflectionProbeCaptureMeasurementResult measurement =
        ComputeReflectionProbeCaptureMeasurement(
            request, backend,
            ReflectionProbeCapturePixelFormat::RGBA16_FLOAT, fixture.views);
    return Issue(plan_id, request_index, request, measurement, execution_id);
  }

private:
  struct CaptureFixture {
    std::vector<std::vector<std::uint8_t>> storage;
    std::vector<ReflectionProbeCapturedSubresourceView> views;
  };

  [[nodiscard]] static CaptureFixture
  MakeFixture(const ReflectionProbeUpdateRequest &request) {
    CaptureFixture fixture;
    const std::size_t count = static_cast<std::size_t>(
        request.expected_face_count * request.expected_mip_count);
    fixture.storage.resize(count);
    fixture.views.resize(count);
    for (std::size_t index = 0U; index < count; ++index) {
      const std::uint16_t mip = static_cast<std::uint16_t>(
          index / request.expected_face_count);
      const std::uint32_t face = static_cast<std::uint32_t>(
          index % request.expected_face_count);
      const std::uint32_t dimension =
          (std::max)(1U,
                     static_cast<std::uint32_t>(request.resolution) >> mip);
      const std::uint64_t row_pitch =
          static_cast<std::uint64_t>(dimension) * 8U;
      std::vector<std::uint8_t> &bytes = fixture.storage[index];
      bytes.resize(static_cast<std::size_t>(row_pitch * dimension));
      for (std::size_t byte = 0U; byte < bytes.size(); ++byte) {
        bytes[byte] = static_cast<std::uint8_t>(
            (index * 29U + byte * 7U + request.candidate_generation) & 0xFFU);
      }
      ReflectionProbeCapturedSubresourceView &view = fixture.views[index];
      view.face_index = face;
      view.mip_level = mip;
      view.width = dimension;
      view.height = dimension;
      view.row_pitch_bytes = row_pitch;
      view.bytes = bytes.data();
      view.byte_count = bytes.size();
    }
    return fixture;
  }
};

} // namespace RoR::Render::Testing
