/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "ReflectionProbeCaptureReceipt.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "reflection-probe capture receipt test failed: " << message
              << '\n';
    std::exit(EXIT_FAILURE);
  }
}

RoR::Render::ReflectionProbeUpdateRequest MakeRequest() {
  using namespace RoR::Render;
  ReflectionProbeRuntimeDescriptor descriptor;
  descriptor.probe_id = 42U;
  descriptor.content_revision = 7U;
  descriptor.absolute_world_position_meters = {1000000010.0, 2.0, -3.0};
  descriptor.influence_half_size = {3.0F, 2.0F, 1.0F};
  descriptor.correction_shape_half_size = {4.0F, 3.0F, 2.0F};
  descriptor.resolution = 16U;
  descriptor.capture_far_meters = 16.0F;

  ReflectionProbeUpdateScheduler scheduler;
  ReflectionProbePlanResult planned = scheduler.BeginFrame(
      11U, 101U, {1000000000.0, 0.0, 0.0}, {descriptor});
  Require(planned.ok() && planned.plan.requests.size() == 1U,
          "valid scheduler request was not produced");
  return planned.plan.requests.front();
}

struct CaptureFixture {
  std::vector<std::vector<std::uint8_t>> storage;
  std::vector<RoR::Render::ReflectionProbeCapturedSubresourceView> views;
};

CaptureFixture MakeCapture(
    const RoR::Render::ReflectionProbeUpdateRequest &request,
    std::uint32_t padding_bytes = 8U) {
  using namespace RoR::Render;
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
        (std::max)(1U, static_cast<std::uint32_t>(request.resolution) >> mip);
    const std::uint64_t active_row_bytes =
        static_cast<std::uint64_t>(dimension) * 8U;
    const std::uint64_t row_pitch = active_row_bytes + padding_bytes;
    std::vector<std::uint8_t> &bytes = fixture.storage[index];
    bytes.resize(static_cast<std::size_t>(row_pitch * dimension), 0xA5U);
    for (std::uint32_t row = 0U; row < dimension; ++row) {
      for (std::uint64_t column = 0U; column < active_row_bytes; ++column) {
        bytes[static_cast<std::size_t>(row_pitch * row + column)] =
            static_cast<std::uint8_t>((mip * 53U + face * 17U + row * 3U +
                                       column) &
                                      0xFFU);
      }
    }
    fixture.views[index].face_index = face;
    fixture.views[index].mip_level = mip;
    fixture.views[index].width = dimension;
    fixture.views[index].height = dimension;
    fixture.views[index].row_pitch_bytes = row_pitch;
    fixture.views[index].bytes = bytes.data();
    fixture.views[index].byte_count = bytes.size();
  }
  return fixture;
}

RoR::Render::ReflectionProbeCaptureReceiptResult Compute(
    const RoR::Render::ReflectionProbeUpdateRequest &request,
    const CaptureFixture &capture,
    RoR::Render::ReflectionProbeCaptureBackend backend =
        RoR::Render::ReflectionProbeCaptureBackend::OGRE_NEXT_METAL,
    std::uint64_t native_receipt = UINT64_C(0xa11ce5eeda7a0001)) {
  return RoR::Render::ComputeReflectionProbeCaptureReceipt(
      request, backend,
      RoR::Render::ReflectionProbeCapturePixelFormat::RGBA16_FLOAT,
      native_receipt, capture.views);
}

void TestCanonicalReceiptAndPaddingIndependence() {
  using namespace RoR::Render;
  const ReflectionProbeUpdateRequest request = MakeRequest();
  CaptureFixture capture = MakeCapture(request);
  const ReflectionProbeCaptureReceiptResult first = Compute(request, capture);
  const ReflectionProbeCaptureReceiptResult replay = Compute(request, capture);
  Require(first.ok() && replay.ok() && first.capture_digest != 0U &&
              first.capture_digest == replay.capture_digest,
          "canonical receipt was zero or nondeterministic");
  Require(first.capture_digest == UINT64_C(10194573715730044615),
          "canonical receipt golden changed");
  Require(first.completed_face_count == 6U &&
              first.completed_mip_count == 5U &&
              first.canonical_payload_bytes == 16368U,
          "canonical six-face/full-mip payload accounting drifted");

  capture.storage.front()[capture.views.front().row_pitch_bytes - 1U] ^= 0xFFU;
  const ReflectionProbeCaptureReceiptResult padding = Compute(request, capture);
  Require(padding.ok() && padding.capture_digest == first.capture_digest,
          "row padding contaminated the canonical capture digest");

  capture.storage.front()[0U] ^= 0x01U;
  const ReflectionProbeCaptureReceiptResult changed = Compute(request, capture);
  Require(changed.ok() && changed.capture_digest != first.capture_digest,
          "active native texel bytes did not change the capture digest");
}

void TestLineageBackendAndNativeReceiptAreBound() {
  using namespace RoR::Render;
  ReflectionProbeUpdateRequest request = MakeRequest();
  const CaptureFixture capture = MakeCapture(request, 0U);
  const ReflectionProbeCaptureReceiptResult baseline = Compute(request, capture);
  Require(baseline.ok(), "baseline lineage capture failed");

  ReflectionProbeUpdateRequest later_generation = request;
  ++later_generation.candidate_generation;
  Require(Compute(later_generation, capture).capture_digest !=
              baseline.capture_digest,
          "candidate generation was not bound to the receipt");

  ReflectionProbeUpdateRequest later_tick = request;
  ++later_tick.simulation_tick;
  Require(Compute(later_tick, capture).capture_digest !=
              baseline.capture_digest,
          "simulation tick was not bound to the receipt");

  Require(Compute(request, capture,
                  ReflectionProbeCaptureBackend::OGRE_NEXT_VULKAN)
              .capture_digest != baseline.capture_digest,
          "native backend was not bound to the receipt");
  Require(Compute(request, capture,
                  ReflectionProbeCaptureBackend::OGRE_NEXT_METAL,
                  UINT64_C(0xa11ce5eeda7a0002))
              .capture_digest != baseline.capture_digest,
          "native IBL execution receipt was not bound to the digest");
}

void TestMalformedCapturesFailTransactionally() {
  using namespace RoR::Render;
  ReflectionProbeUpdateRequest request = MakeRequest();
  const CaptureFixture valid = MakeCapture(request);
  const auto rejected = [](const ReflectionProbeCaptureReceiptResult &result,
                           ValidationCode expected) {
    return !result.ok() && result.validation.code == expected &&
           result.capture_digest == 0U &&
           result.completed_face_count == 0U &&
           result.completed_mip_count == 0U &&
           result.canonical_payload_bytes == 0U;
  };

  CaptureFixture missing = valid;
  missing.views.pop_back();
  Require(rejected(Compute(request, missing), ValidationCode::SIZE_MISMATCH),
          "missing face/mip did not fail transactionally");

  CaptureFixture reordered = valid;
  std::swap(reordered.views[0U], reordered.views[1U]);
  Require(rejected(Compute(request, reordered),
                   ValidationCode::NON_DETERMINISTIC_ORDER),
          "reordered face/mip did not fail transactionally");

  CaptureFixture wrong_dimension = valid;
  --wrong_dimension.views[0U].width;
  Require(rejected(Compute(request, wrong_dimension),
                   ValidationCode::INVALID_DIMENSIONS),
          "wrong mip dimensions did not fail transactionally");

  CaptureFixture short_row = valid;
  short_row.views[0U].row_pitch_bytes = 1U;
  Require(rejected(Compute(request, short_row),
                   ValidationCode::SIZE_MISMATCH),
          "under-pitched face did not fail transactionally");

  CaptureFixture truncated = valid;
  truncated.views[0U].byte_count = 1U;
  Require(rejected(Compute(request, truncated), ValidationCode::SIZE_MISMATCH),
          "truncated face did not fail transactionally");

  CaptureFixture null_bytes = valid;
  null_bytes.views[0U].bytes = nullptr;
  Require(rejected(Compute(request, null_bytes), ValidationCode::SIZE_MISMATCH),
          "null face bytes did not fail transactionally");

  CaptureFixture old_view = valid;
  old_view.views[0U].version = 0U;
  Require(rejected(Compute(request, old_view),
                   ValidationCode::UNSUPPORTED_VERSION),
          "old subresource version did not fail transactionally");

  Require(rejected(Compute(request, valid,
                           static_cast<ReflectionProbeCaptureBackend>(0U)),
                   ValidationCode::INVALID_ENUM),
          "unknown native backend did not fail transactionally");
  Require(rejected(ComputeReflectionProbeCaptureReceipt(
                       request, ReflectionProbeCaptureBackend::OGRE_NEXT_METAL,
                       static_cast<ReflectionProbeCapturePixelFormat>(0U),
                       UINT64_C(0xa11ce5eeda7a0001), valid.views),
                   ValidationCode::INVALID_ENUM),
          "unknown pixel format did not fail transactionally");
  Require(rejected(Compute(request, valid,
                           ReflectionProbeCaptureBackend::OGRE_NEXT_METAL, 0U),
                   ValidationCode::EMPTY_PAYLOAD),
          "zero native execution receipt did not fail transactionally");

  ReflectionProbeUpdateRequest stale = request;
  ++stale.descriptor.content_revision;
  Require(rejected(Compute(stale, valid), ValidationCode::REVISION_MISMATCH),
          "stale descriptor fingerprint did not fail transactionally");
  ReflectionProbeUpdateRequest zero_generation = request;
  zero_generation.candidate_generation = 0U;
  Require(rejected(Compute(zero_generation, valid),
                   ValidationCode::SEQUENCE_MISMATCH),
          "zero candidate generation did not fail transactionally");
}

} // namespace

int main() {
  TestCanonicalReceiptAndPaddingIndependence();
  TestLineageBackendAndNativeReceiptAreBound();
  TestMalformedCapturesFailTransactionally();
  std::cout << "reflection-probe capture receipt tests passed\n";
  return EXIT_SUCCESS;
}
