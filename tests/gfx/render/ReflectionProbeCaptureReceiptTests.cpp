/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "ReflectionProbeCaptureReceipt.h"
#include "ReflectionProbeCaptureTestAdapter.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <type_traits>
#include <vector>

namespace {

using RoR::Render::Testing::ReflectionProbeCaptureTestAdapter;

static_assert(!std::is_default_constructible<
                  RoR::Render::ReflectionProbeCaptureReceipt>::value,
              "successful receipts must not expose a raw construction path");
static_assert(
    !std::is_convertible<RoR::Render::ReflectionProbeCaptureMeasurementResult,
                         RoR::Render::ReflectionProbeCaptureReceipt>::value,
    "a CPU measurement must not be authoritative native evidence");
static_assert(
    !std::is_constructible<
        RoR::Render::ReflectionProbeCaptureReceipt, std::uint64_t, std::size_t,
        const RoR::Render::ReflectionProbeUpdateRequest &, std::uint64_t,
        const RoR::Render::ReflectionProbeCaptureMeasurementResult &>::value,
    "generic callers must not submit raw native evidence or digest fields");

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "reflection-probe capture receipt test failed: " << message
              << '\n';
    std::exit(EXIT_FAILURE);
  }
}

RoR::Render::ReflectionProbeUpdateRequest MakeRequest(
    std::uint16_t resolution = 32U) {
  using namespace RoR::Render;
  ReflectionProbeRuntimeDescriptor descriptor;
  descriptor.probe_id = 42U;
  descriptor.content_revision = 7U;
  descriptor.absolute_world_position_meters = {1000000010.0, 2.0, -3.0};
  descriptor.influence_half_size = {3.0F, 2.0F, 1.0F};
  descriptor.correction_shape_half_size = {4.0F, 3.0F, 2.0F};
  descriptor.resolution = resolution;
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

RoR::Render::ReflectionProbeCaptureMeasurementResult Compute(
    const RoR::Render::ReflectionProbeUpdateRequest &request,
    const CaptureFixture &capture,
    RoR::Render::ReflectionProbeCaptureBackend backend =
        RoR::Render::ReflectionProbeCaptureBackend::OGRE_NEXT_METAL) {
  return RoR::Render::ComputeReflectionProbeCaptureMeasurement(
      request, backend,
      RoR::Render::ReflectionProbeCapturePixelFormat::RGBA16_FLOAT,
      capture.views);
}

void TestCanonicalMeasurementAndPaddingIndependence() {
  using namespace RoR::Render;
  const ReflectionProbeUpdateRequest request = MakeRequest();
  CaptureFixture capture = MakeCapture(request);
  const ReflectionProbeCaptureMeasurementResult first =
      Compute(request, capture);
  const ReflectionProbeCaptureMeasurementResult replay =
      Compute(request, capture);
  Require(first.ok() && replay.ok() &&
              first.canonical_capture_digest != 0U &&
              first.canonical_capture_digest ==
                  replay.canonical_capture_digest,
          "canonical measurement was zero or nondeterministic");
  Require(first.completed_face_count == 6U &&
              first.completed_mip_count == 2U &&
              first.canonical_payload_bytes == 61440U,
          "canonical six-face/PCC-filtered payload accounting drifted");
  Require(first.mip_metadata.contract ==
                  ReflectionProbeCaptureMipContract::
                      OGRE_NEXT_PCC_FILTERED_IBL &&
              first.mip_metadata.face_count == 6U &&
              first.mip_metadata.mip_count == 2U &&
              first.mip_metadata.widths[0U] == 32U &&
              first.mip_metadata.widths[1U] == 16U &&
              first.mip_metadata.heights[0U] == 32U &&
              first.mip_metadata.heights[1U] == 16U &&
              first.mip_metadata.widths[2U] == 0U,
          "32-pixel PCC filtered-IBL metadata drifted");

  capture.storage.front()[capture.views.front().row_pitch_bytes - 1U] ^= 0xFFU;
  const ReflectionProbeCaptureMeasurementResult padding =
      Compute(request, capture);
  Require(padding.ok() && padding.canonical_capture_digest ==
                              first.canonical_capture_digest,
          "row padding contaminated the canonical capture digest");

  capture.storage.front()[0U] ^= 0x01U;
  const ReflectionProbeCaptureMeasurementResult changed =
      Compute(request, capture);
  Require(changed.ok() && changed.canonical_capture_digest !=
                              first.canonical_capture_digest,
          "active native texel bytes did not change the capture digest");
}

void TestExactRequestBackendAndAdapterReceiptAreBound() {
  using namespace RoR::Render;
  ReflectionProbeUpdateRequest request = MakeRequest();
  const CaptureFixture capture = MakeCapture(request, 0U);
  const ReflectionProbeCaptureMeasurementResult baseline =
      Compute(request, capture);
  Require(baseline.ok(), "baseline lineage capture failed");

  ReflectionProbeUpdateRequest later_generation = request;
  ++later_generation.candidate_generation;
  later_generation.deterministic_seed = ComputeReflectionProbeCaptureSeed(
      later_generation.descriptor, later_generation.candidate_generation,
      later_generation.simulation_tick);
  Require(Compute(later_generation, capture).canonical_capture_digest !=
              baseline.canonical_capture_digest,
          "candidate generation was not bound to the receipt");

  ReflectionProbeUpdateRequest later_tick = request;
  ++later_tick.simulation_tick;
  later_tick.deterministic_seed = ComputeReflectionProbeCaptureSeed(
      later_tick.descriptor, later_tick.candidate_generation,
      later_tick.simulation_tick);
  Require(Compute(later_tick, capture).canonical_capture_digest !=
              baseline.canonical_capture_digest,
          "simulation tick was not bound to the receipt");

  ReflectionProbeUpdateRequest changed_descriptor = request;
  ++changed_descriptor.descriptor.priority;
  changed_descriptor.descriptor_fingerprint =
      ComputeReflectionProbeDescriptorFingerprint(
          changed_descriptor.descriptor);
  changed_descriptor.deterministic_seed = ComputeReflectionProbeCaptureSeed(
      changed_descriptor.descriptor,
      changed_descriptor.candidate_generation,
      changed_descriptor.simulation_tick);
  Require(Compute(changed_descriptor, capture).canonical_capture_digest !=
              baseline.canonical_capture_digest,
          "exact descriptor contents were not bound to the measurement");

  ReflectionProbeUpdateRequest changed_origin = request;
  changed_origin.absolute_world_origin_meters.x += 1.0;
  changed_origin.render_from_probe.elements[12U] -= 1.0F;
  Require(Compute(changed_origin, capture).canonical_capture_digest !=
              baseline.canonical_capture_digest,
          "exact origin and derived transform were not bound to the measurement");

  ReflectionProbeUpdateRequest changed_reason = request;
  changed_reason.reason = ReflectionProbeUpdateReason::PERIOD_ELAPSED;
  Require(Compute(changed_reason, capture).canonical_capture_digest !=
              baseline.canonical_capture_digest,
          "capture reason was not bound to the measurement");

  Require(Compute(request, capture,
                  ReflectionProbeCaptureBackend::OGRE_NEXT_VULKAN)
              .canonical_capture_digest != baseline.canonical_capture_digest,
          "native backend was not bound to the receipt");

  const ReflectionProbeCaptureReceipt first_receipt =
      ReflectionProbeCaptureTestAdapter::Issue(
          9U, 0U, request, baseline, UINT64_C(0xa11ce5eeda7a0001));
  const ReflectionProbeCaptureReceipt other_execution =
      ReflectionProbeCaptureTestAdapter::Issue(
          9U, 0U, request, baseline, UINT64_C(0xa11ce5eeda7a0002));
  const ReflectionProbeCaptureReceipt other_plan =
      ReflectionProbeCaptureTestAdapter::Issue(
          10U, 0U, request, baseline, UINT64_C(0xa11ce5eeda7a0001));
  const ReflectionProbeCaptureReceipt mismatched_measurement =
      ReflectionProbeCaptureTestAdapter::Issue(
          9U, 0U, changed_descriptor, baseline,
          UINT64_C(0xa11ce5eeda7a0003));
  const ReflectionProbeCaptureReceipt zero_native_evidence =
      ReflectionProbeCaptureTestAdapter::Issue(9U, 0U, request, baseline, 0U);
  ReflectionProbeCaptureMeasurementResult changed_mip_metadata = baseline;
  changed_mip_metadata.mip_metadata.widths[1U] = 8U;
  const ReflectionProbeCaptureReceipt mismatched_mip_metadata =
      ReflectionProbeCaptureTestAdapter::Issue(
          9U, 0U, request, changed_mip_metadata,
          UINT64_C(0xa11ce5eeda7a0004));
  Require(first_receipt.successful() && first_receipt.capture_digest() != 0U &&
              first_receipt.capture_digest() !=
                  other_execution.capture_digest() &&
              first_receipt.capture_digest() != other_plan.capture_digest(),
          "opaque receipt did not bind concrete adapter execution and plan");
  Require(mismatched_measurement.successful() &&
              !mismatched_measurement.authoritative() &&
              mismatched_measurement.capture_digest() == 0U,
          "measurement from another exact request became authoritative");
  Require(zero_native_evidence.successful() &&
              !zero_native_evidence.authoritative() &&
              zero_native_evidence.capture_digest() == 0U,
          "zero/fake native evidence became authoritative");
  Require(mismatched_mip_metadata.successful() &&
              !mismatched_mip_metadata.authoritative() &&
              mismatched_mip_metadata.capture_digest() == 0U,
          "receipt authority ignored altered exact mip metadata");
}

void TestOgrePccFilteredIblMipContractAt32And256() {
  using namespace RoR::Render;
  Require(ComputeReflectionProbeRequiredMipCount(32U) == 2U &&
              ComputeReflectionProbeRequiredMipCount(256U) == 5U,
          "Ogre PCC filtered-IBL mip-count formula drifted at 32/256");

  const ReflectionProbeUpdateRequest request_32 = MakeRequest(32U);
  const ReflectionProbeCaptureMeasurementResult measurement_32 =
      Compute(request_32, MakeCapture(request_32));
  Require(measurement_32.ok() &&
              measurement_32.mip_metadata.mip_count == 2U &&
              measurement_32.mip_metadata.widths[0U] == 32U &&
              measurement_32.mip_metadata.widths[1U] == 16U,
          "32-pixel receipt metadata does not match Ogre PCC output");

  const ReflectionProbeUpdateRequest request_256 = MakeRequest(256U);
  const ReflectionProbeCaptureMeasurementResult measurement_256 =
      Compute(request_256, MakeCapture(request_256));
  Require(measurement_256.ok() &&
              measurement_256.completed_mip_count == 5U &&
              measurement_256.canonical_payload_bytes == 4190208U &&
              measurement_256.mip_metadata.mip_count == 5U &&
              measurement_256.mip_metadata.widths[0U] == 256U &&
              measurement_256.mip_metadata.widths[1U] == 128U &&
              measurement_256.mip_metadata.widths[2U] == 64U &&
              measurement_256.mip_metadata.widths[3U] == 32U &&
              measurement_256.mip_metadata.widths[4U] == 16U &&
              measurement_256.mip_metadata.widths[5U] == 0U,
          "256-pixel receipt metadata does not match Ogre PCC output");

  const ReflectionProbeCaptureReceipt receipt =
      ReflectionProbeCaptureTestAdapter::Issue(
          17U, 0U, request_256, measurement_256,
          UINT64_C(0xa11ce5eeda7a0100));
  Require(receipt.authoritative() &&
              AreReflectionProbeCaptureMipMetadataEquivalent(
                  receipt.mip_metadata(), measurement_256.mip_metadata),
          "opaque receipt did not retain the exact 256-pixel mip metadata");
}

void TestMalformedCapturesFailTransactionally() {
  using namespace RoR::Render;
  ReflectionProbeUpdateRequest request = MakeRequest();
  const CaptureFixture valid = MakeCapture(request);
  const auto rejected = [](const ReflectionProbeCaptureMeasurementResult &result,
                           ValidationCode expected) {
    return !result.ok() && result.validation.code == expected &&
           result.canonical_capture_digest == 0U &&
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
  Require(rejected(ComputeReflectionProbeCaptureMeasurement(
                       request, ReflectionProbeCaptureBackend::OGRE_NEXT_METAL,
                       static_cast<ReflectionProbeCapturePixelFormat>(0U),
                       valid.views),
                   ValidationCode::INVALID_ENUM),
          "unknown pixel format did not fail transactionally");

  ReflectionProbeUpdateRequest partial_request = request;
  partial_request.expected_mip_count = 1U;
  CaptureFixture partial_capture = valid;
  partial_capture.views.resize(kReflectionProbeCubemapFaceCount);
  Require(rejected(Compute(partial_request, partial_capture),
                   ValidationCode::INVALID_DIMENSIONS),
          "caller-shortened mip chain was accepted as complete");

  ReflectionProbeUpdateRequest changed_transform = request;
  changed_transform.render_from_probe.elements[12U] += 1.0F;
  Require(rejected(Compute(changed_transform, valid),
                   ValidationCode::SEQUENCE_MISMATCH),
          "altered render transform was accepted");

  ReflectionProbeUpdateRequest changed_origin = request;
  changed_origin.absolute_world_origin_meters.x += 1.0;
  Require(rejected(Compute(changed_origin, valid),
                   ValidationCode::SEQUENCE_MISMATCH),
          "origin inconsistent with the capture transform was accepted");

  ReflectionProbeUpdateRequest invalid_reason = request;
  invalid_reason.reason = static_cast<ReflectionProbeUpdateReason>(255U);
  Require(rejected(Compute(invalid_reason, valid), ValidationCode::INVALID_ENUM),
          "unknown capture reason was accepted");

  ReflectionProbeUpdateRequest stale = request;
  ++stale.descriptor.content_revision;
  Require(rejected(Compute(stale, valid), ValidationCode::REVISION_MISMATCH),
          "stale descriptor fingerprint did not fail transactionally");
  ReflectionProbeUpdateRequest zero_generation = request;
  zero_generation.candidate_generation = 0U;
  Require(rejected(Compute(zero_generation, valid),
                   ValidationCode::SEQUENCE_MISMATCH),
          "zero candidate generation did not fail transactionally");

  ReflectionProbeUpdateRequest stale_seed = request;
  ++stale_seed.candidate_generation;
  Require(rejected(Compute(stale_seed, valid),
                   ValidationCode::SEQUENCE_MISMATCH),
          "stale deterministic seed was accepted for another generation");
}

} // namespace

int main() {
  TestCanonicalMeasurementAndPaddingIndependence();
  TestExactRequestBackendAndAdapterReceiptAreBound();
  TestOgrePccFilteredIblMipContractAt32And256();
  TestMalformedCapturesFailTransactionally();
  std::cout << "reflection-probe capture receipt tests passed\n";
  return EXIT_SUCCESS;
}
