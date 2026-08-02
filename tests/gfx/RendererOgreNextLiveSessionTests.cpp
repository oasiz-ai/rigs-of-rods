/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererOgreNextLiveSession.h"

#include "RenderAssetDeltaTransport.h"
#include "SceneSnapshotTransport.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(_WIN32)
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {

using namespace RoR;
using namespace RoR::Render;

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "renderer Ogre-Next live session test failed: " << message
              << '\n';
    std::exit(EXIT_FAILURE);
  }
}

HostRenderPlatform CurrentPlatform() {
#if defined(_WIN32)
  return HostRenderPlatform::WINDOWS;
#elif defined(__APPLE__)
  return HostRenderPlatform::MACOS;
#elif defined(__linux__)
  return HostRenderPlatform::LINUX;
#else
  return HostRenderPlatform::UNKNOWN;
#endif
}

#if defined(_WIN32)

using NativeHandle = HANDLE;
const NativeHandle kInvalidNativeHandle = INVALID_HANDLE_VALUE;

struct NativePipe final {
  NativeHandle read_handle = kInvalidNativeHandle;
  NativeHandle write_handle = kInvalidNativeHandle;
};

NativePipe MakePipe() {
  SECURITY_ATTRIBUTES security{};
  security.nLength = sizeof(security);
  security.bInheritHandle = TRUE;
  NativePipe result;
  Require(::CreatePipe(&result.read_handle, &result.write_handle, &security,
                       0U) != FALSE,
          "CreatePipe failed");
  return result;
}

std::uint64_t NativeToken(NativeHandle handle) {
  return static_cast<std::uint64_t>(
      reinterpret_cast<std::uintptr_t>(handle));
}

bool IsNativeOpen(NativeHandle handle) {
  DWORD flags = 0U;
  return handle != nullptr && handle != kInvalidNativeHandle &&
         ::GetHandleInformation(handle, &flags) != FALSE;
}

void CloseNative(NativeHandle &handle) {
  if (handle == nullptr || handle == kInvalidNativeHandle) {
    return;
  }
  Require(::CloseHandle(handle) != FALSE, "CloseHandle failed");
  handle = kInvalidNativeHandle;
}

void WriteNative(NativeHandle handle, const std::uint8_t *bytes,
                 std::size_t size) {
  std::size_t total = 0U;
  while (total < size) {
    const DWORD requested = static_cast<DWORD>((std::min)(
        size - total, static_cast<std::size_t>(1024U * 1024U)));
    DWORD transferred = 0U;
    Require(::WriteFile(handle, bytes + total, requested, &transferred,
                        nullptr) != FALSE &&
                transferred != 0U,
            "WriteFile failed");
    total += static_cast<std::size_t>(transferred);
  }
}

std::vector<std::uint8_t> ReadNativeExact(NativeHandle handle,
                                          std::size_t size) {
  std::vector<std::uint8_t> result(size);
  std::size_t total = 0U;
  while (total < size) {
    DWORD transferred = 0U;
    Require(::ReadFile(handle, result.data() + total,
                       static_cast<DWORD>(size - total), &transferred,
                       nullptr) != FALSE &&
                transferred != 0U,
            "ReadFile failed");
    total += static_cast<std::size_t>(transferred);
  }
  return result;
}

#else

using NativeHandle = int;
const NativeHandle kInvalidNativeHandle = -1;

struct NativePipe final {
  NativeHandle read_handle = kInvalidNativeHandle;
  NativeHandle write_handle = kInvalidNativeHandle;
};

int PromoteReservedDescriptor(int descriptor) {
  if (descriptor >= 3) {
    return descriptor;
  }
  const int promoted = ::fcntl(descriptor, F_DUPFD, 3);
  Require(promoted >= 3, "could not promote reserved descriptor");
  Require(::close(descriptor) == 0, "could not close reserved descriptor");
  return promoted;
}

NativePipe MakePipe() {
  int descriptors[2] = {-1, -1};
  Require(::pipe(descriptors) == 0, "pipe failed");
  NativePipe result;
  result.read_handle = PromoteReservedDescriptor(descriptors[0]);
  result.write_handle = PromoteReservedDescriptor(descriptors[1]);
  return result;
}

std::uint64_t NativeToken(NativeHandle handle) {
  return static_cast<std::uint64_t>(handle);
}

bool IsNativeOpen(NativeHandle handle) {
  return handle >= 0 && ::fcntl(handle, F_GETFD) >= 0;
}

void CloseNative(NativeHandle &handle) {
  if (handle < 0) {
    return;
  }
  Require(::close(handle) == 0, "close failed");
  handle = kInvalidNativeHandle;
}

void WriteNative(NativeHandle handle, const std::uint8_t *bytes,
                 std::size_t size) {
  std::size_t total = 0U;
  while (total < size) {
    errno = 0;
    const ssize_t transferred = ::write(handle, bytes + total, size - total);
    if (transferred < 0 && errno == EINTR) {
      continue;
    }
    Require(transferred > 0, "write failed");
    total += static_cast<std::size_t>(transferred);
  }
}

std::vector<std::uint8_t> ReadNativeExact(NativeHandle handle,
                                          std::size_t size) {
  std::vector<std::uint8_t> result(size);
  std::size_t total = 0U;
  while (total < size) {
    errno = 0;
    const ssize_t transferred =
        ::read(handle, result.data() + total, size - total);
    if (transferred < 0 && errno == EINTR) {
      continue;
    }
    Require(transferred > 0, "read failed");
    total += static_cast<std::size_t>(transferred);
  }
  return result;
}

#endif

std::uint64_t ReadU64(const std::uint8_t *bytes) {
  std::uint64_t value = 0U;
  for (std::size_t index = 0U; index < 8U; ++index) {
    value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8U);
  }
  return value;
}

std::vector<std::uint8_t> ReadNativeFrame(NativeHandle handle) {
  std::vector<std::uint8_t> frame =
      ReadNativeExact(handle, kRenderTransportEnvelopeHeaderBytes);
  const std::uint64_t payload_size = ReadU64(frame.data() + 24U);
  Require(payload_size <= kRenderTransportStreamAbsoluteMaximumPayloadBytes,
          "reverse frame exceeded absolute payload bound");
  std::vector<std::uint8_t> payload =
      ReadNativeExact(handle, static_cast<std::size_t>(payload_size));
  frame.insert(frame.end(), payload.begin(), payload.end());
  return frame;
}

RendererBridgeEndpoint MakeEndpoint(NativeHandle inbound,
                                    NativeHandle outbound,
                                    std::uint8_t seed = 1U) {
  RendererBridgeEndpoint endpoint;
  endpoint.platform = CurrentPlatform();
  endpoint.role = RendererBridgeRole::PRESENTATION_FRONTEND;
  for (std::size_t index = 0U; index < endpoint.session_id.size(); ++index) {
    endpoint.session_id[index] = static_cast<std::uint8_t>(
        static_cast<unsigned>(seed) + static_cast<unsigned>(index));
  }
  endpoint.inbound_native_handle = NativeToken(inbound);
  endpoint.outbound_native_handle = NativeToken(outbound);
  return endpoint;
}

Matrix4x4 Perspective() {
  constexpr float near_plane = 0.1F;
  constexpr float far_plane = 1000.0F;
  Matrix4x4 projection;
  projection.elements.fill(0.0F);
  projection.elements[0U] = 1.0F;
  projection.elements[5U] = 1.0F;
  projection.elements[10U] = far_plane / (near_plane - far_plane);
  projection.elements[11U] = -1.0F;
  projection.elements[14U] =
      near_plane * far_plane / (near_plane - far_plane);
  return projection;
}

CameraViewRequest Camera() {
  CameraViewRequest camera;
  camera.view_id = 7U;
  camera.width = 96U;
  camera.height = 64U;
  camera.near_plane = 0.1F;
  camera.far_plane = 1000.0F;
  camera.clip_from_view = Perspective();
  camera.previous_clip_from_view = camera.clip_from_view;
  Require(ValidateCameraViewRequest(camera).ok(), "camera fixture invalid");
  return camera;
}

RenderBridgeSurfaceState ActiveSurface(std::uint64_t revision = 9U) {
  RenderBridgeSurfaceState surface;
  surface.surface_revision = revision;
  surface.logical_width = 96U;
  surface.logical_height = 64U;
  surface.drawable_width = 96U;
  surface.drawable_height = 64U;
  Require(IsValidRenderBridgeSurfaceState(surface, false),
          "active surface fixture invalid");
  return surface;
}

RenderBridgeSurfaceState SuspendedSurface(std::uint64_t revision) {
  RenderBridgeSurfaceState surface = ActiveSurface(revision);
  surface.drawable_width = 0U;
  surface.drawable_height = 0U;
  surface.suspended = true;
  Require(IsValidRenderBridgeSurfaceState(surface, true),
          "suspended surface fixture invalid");
  return surface;
}

std::shared_ptr<const SceneSnapshot> Scene(std::uint64_t registry_id) {
  SceneSnapshotDescriptor descriptor;
  descriptor.snapshot_id = 1U;
  descriptor.asset_registry_id = registry_id;
  descriptor.asset_sequence = 1U;
  descriptor.simulation_tick = 1U;
  descriptor.simulation_time_seconds = 1.0 / 48.0;
  SceneSnapshotCreateResult created =
      CreateSceneSnapshot(std::move(descriptor));
  Require(created.ok(), "scene fixture invalid");
  return created.snapshot;
}

std::vector<std::uint8_t> AssetFrame(std::uint64_t registry_id) {
  RenderAssetDelta delta;
  delta.registry_id = registry_id;
  delta.sequence = 1U;
  delta.full_snapshot = true;
  const RenderAssetDeltaTransportEncodeResult encoded =
      EncodeRenderAssetDeltaTransportFrame(1U, delta);
  Require(encoded.ok(), "asset frame fixture did not encode");
  return encoded.bytes;
}

std::vector<std::uint8_t> SceneFrame(std::uint64_t registry_id) {
  const SceneSnapshotTransportEncodeResult encoded =
      EncodeSceneSnapshotTransportFrame(2U, *Scene(registry_id), Camera());
  Require(encoded.ok(), "scene frame fixture did not encode");
  return encoded.bytes;
}

class FakeFrontend final : public IRendererFrontend {
public:
  FakeFrontend() {
    capabilities.frontend_kind = RendererFrontendKind::CUSTOM;
    capabilities.raster_api = RasterGraphicsApi::VULKAN;
    capabilities.native_api = NativeGraphicsApi::VULKAN;
    capabilities.frontend_name = "live-session-fake";
    capabilities.frontend_version = "1";
    capabilities.maximum_texture_dimension_2d = 8192U;
    capabilities.maximum_views = 1U;
    capabilities.maximum_frames_in_flight = 1U;
    capabilities.supported_outputs = FrameOutputMask::COLOR;
    capabilities.raster_ready = true;
    capabilities.supports_dynamic_mesh_updates = true;
    capabilities.supports_particle_events = true;
    Require(ValidateFrontendCapabilityReport(capabilities).ok(),
            "fake capabilities invalid");
  }

  FrontendCapabilityReport QueryCapabilities() const override {
    return capabilities;
  }
  RenderOperationResult Initialize(const FrontendInitializationRequest &) override {
    return RenderOperationResult::Success();
  }
  RenderOperationResult UpdateSurface(const FrontendSurfaceUpdate &, bool,
                                      std::uint64_t) override {
    return RenderOperationResult::Success();
  }
  RenderOperationResult SynchronizeAssets(const RenderAssetDelta &) override {
    ++asset_calls;
    return RenderOperationResult::Success();
  }
  RenderOperationResult ReleaseResource(ResourceHandle resource) override {
    Require(resource.valid(), "dispatcher released invalid fake resource");
    ++release_calls;
    return RenderOperationResult::Success();
  }
  RenderOperationResult Render(const RenderFrameRequest &request,
                               RenderFrameOutput &output) override {
    ++render_calls;
    frame_ids.push_back(request.frame_id);
    presented.push_back(request.present);
    surface_revisions.push_back(request.presentation_surface_revision);
    output.frame_id = request.frame_id;
    output.snapshot_id = request.scene_snapshot->snapshot_id();
    output.status = RenderFrameStatus::RENDERED;
    output.presented = request.present;
    output.presented_view_id = request.presentation_view_id;
    FrameAttachment color;
    color.view_id = request.views.front().view_id;
    color.output = FrameOutputMask::COLOR;
    color.format = request.color_format;
    color.width = request.views.front().width;
    color.height = request.views.front().height;
    color.gpu_resource = ResourceHandle::Create(
        ResourceKind::RENDER_TARGET, 41U,
        static_cast<std::uint32_t>(render_calls - 1U), 1U);
    output.attachments.push_back(color);
    return RenderOperationResult::Success();
  }
  bool IsFrameComplete(std::uint64_t) const noexcept override { return true; }
  RenderOperationResult WaitForFrame(std::uint64_t, std::uint64_t) override {
    ++wait_calls;
    return RenderOperationResult::Success();
  }
  NativeRenderInterop *GetNativeInterop() noexcept override { return nullptr; }
  RenderOperationResult Shutdown(std::uint64_t) override {
    return RenderOperationResult::Success();
  }

  FrontendCapabilityReport capabilities;
  std::uint64_t asset_calls = 0U;
  std::uint64_t render_calls = 0U;
  std::uint64_t wait_calls = 0U;
  std::uint64_t release_calls = 0U;
  std::vector<std::uint64_t> frame_ids;
  std::vector<bool> presented;
  std::vector<std::uint64_t> surface_revisions;
};

struct PollContext final {
  std::vector<std::uint64_t> observed_forward_sequences;
  std::vector<RenderBridgeSurfaceState> surfaces;
  std::uint64_t calls = 0U;
  std::uint64_t close_on_call = 0U;
  bool invalid_batch = false;
};

bool Poll(void *opaque, std::uint64_t observed_forward_sequence,
          RendererOgreNextLiveSessionObservation *observation) {
  auto &context = *static_cast<PollContext *>(opaque);
  if (observation == nullptr) {
    return false;
  }
  ++context.calls;
  context.observed_forward_sequences.push_back(observed_forward_sequence);
  *observation = RendererOgreNextLiveSessionObservation{};
  observation->surface = context.surfaces.empty()
                             ? ActiveSurface()
                             : context.surfaces[(std::min)(
                                   static_cast<std::size_t>(context.calls - 1U),
                                   context.surfaces.size() - 1U)];
  observation->response.clock_origin_id = context.invalid_batch ? 0U : 77U;
  observation->response.reconciliation.host_timestamp_ns = context.calls;
  observation->response.reconciliation.focus =
      InputTransportFocusState::GAINED;
  if (context.close_on_call == context.calls) {
    InputTransportEvent close;
    close.event_id = 1U;
    close.host_timestamp_ns = context.calls;
    close.payload = InputTransportWindowCloseEvent{};
    observation->response.events.push_back(std::move(close));
    observation->response.reconciliation.through_event_id = 1U;
    observation->response.reconciliation.window_close_requested = true;
    observation->window_close_requested = true;
  }
  return true;
}

RendererOgreNextLiveSessionRuntime Runtime(FakeFrontend &frontend,
                                           PollContext &poll) {
  RendererOgreNextLiveSessionRuntime runtime;
  runtime.frontend = &frontend;
  runtime.context = &poll;
  runtime.poll = &Poll;
  runtime.initial_surface = ActiveSurface();
  return runtime;
}

void TestStatusDomainAndRejections() {
  Require(kRendererOgreNextLiveSessionContractVersion == 3U,
          "live session contract version changed");
  for (unsigned value = 0U; value <= 14U; ++value) {
    const auto status =
        static_cast<RendererOgreNextLiveSessionStatus>(value);
    Require(IsKnownRendererOgreNextLiveSessionStatus(status),
            "known live session status rejected");
    Require(std::string(ToString(status)) != "invalid",
            "known live session status lacks text");
  }
  Require(!IsKnownRendererOgreNextLiveSessionStatus(
              static_cast<RendererOgreNextLiveSessionStatus>(15U)),
          "unknown live session status accepted");

  RendererBridgeEndpoint invalid;
  FakeFrontend frontend;
  PollContext poll;
  const RendererOgreNextLiveSessionResult endpoint_rejected =
      RunRendererOgreNextLiveSession(invalid, Runtime(frontend, poll));
  Require(endpoint_rejected.status ==
              RendererOgreNextLiveSessionStatus::REJECTED_INVALID_ENDPOINT &&
              !endpoint_rejected.channel_adopted,
          "invalid endpoint crossed adoption boundary");

  RendererOgreNextLiveSessionRuntime invalid_runtime;
  invalid_runtime.frontend = &frontend;
  RendererBridgeEndpoint structurally_valid;
  structurally_valid.platform = CurrentPlatform();
  structurally_valid.role = RendererBridgeRole::PRESENTATION_FRONTEND;
  structurally_valid.session_id[0U] = 1U;
  structurally_valid.inbound_native_handle = 10U;
  structurally_valid.outbound_native_handle = 11U;
  const RendererOgreNextLiveSessionResult runtime_rejected =
      RunRendererOgreNextLiveSession(structurally_valid, invalid_runtime);
  Require(runtime_rejected.status ==
              RendererOgreNextLiveSessionStatus::REJECTED_INVALID_RUNTIME &&
              !runtime_rejected.channel_adopted,
          "invalid runtime crossed adoption boundary");

  RendererOgreNextLiveSessionRuntime suspended_initial =
      Runtime(frontend, poll);
  suspended_initial.initial_surface = SuspendedSurface(10U);
  const RendererOgreNextLiveSessionResult surface_rejected =
      RunRendererOgreNextLiveSession(structurally_valid, suspended_initial);
  Require(surface_rejected.status ==
              RendererOgreNextLiveSessionStatus::REJECTED_INVALID_RUNTIME &&
              !surface_rejected.channel_adopted,
          "suspended PEER_READY surface crossed adoption boundary");
}

void TestCompleteMonotonicFramesAndResponses() {
  NativePipe forward = MakePipe();
  NativePipe reverse = MakePipe();
  const RendererBridgeEndpoint endpoint =
      MakeEndpoint(forward.read_handle, reverse.write_handle, 7U);
  const std::uint64_t registry_id =
      DeriveRenderAssetRegistryIdFromBridgeSession(endpoint.session_id);
  Require(registry_id != 0U, "valid session derived zero registry");
  const std::vector<std::uint8_t> asset = AssetFrame(registry_id);
  const std::vector<std::uint8_t> scene = SceneFrame(registry_id);

  FakeFrontend frontend;
  PollContext poll;
  RendererOgreNextLiveSessionResult session;
  std::thread worker([&]() {
    session = RunRendererOgreNextLiveSession(endpoint, Runtime(frontend, poll));
  });

  const std::vector<std::uint8_t> ready_frame =
      ReadNativeFrame(reverse.read_handle);
  WriteNative(forward.write_handle, asset.data(), 17U);
  WriteNative(forward.write_handle, asset.data() + 17U, asset.size() - 17U);
  const std::vector<std::uint8_t> asset_input_frame =
      ReadNativeFrame(reverse.read_handle);
  const std::vector<std::uint8_t> asset_ack_frame =
      ReadNativeFrame(reverse.read_handle);
  WriteNative(forward.write_handle, scene.data(), scene.size());
  const std::vector<std::uint8_t> scene_input_frame =
      ReadNativeFrame(reverse.read_handle);
  const std::vector<std::uint8_t> scene_ack_frame =
      ReadNativeFrame(reverse.read_handle);
  CloseNative(forward.write_handle);
  worker.join();

  RenderTransportSequenceState reverse_sequence(1U);
  InputEventTransportDecoder input_decoder(reverse_sequence);
  RenderBridgeControlTransportDecoder control_decoder(registry_id,
                                                       reverse_sequence);
  const RenderBridgeControlTransportDecodeResult ready =
      control_decoder.Accept(ready_frame);
  const InputEventTransportDecodeResult asset_input =
      input_decoder.Accept(asset_input_frame);
  const RenderBridgeControlTransportDecodeResult asset_ack =
      control_decoder.Accept(asset_ack_frame);
  const InputEventTransportDecodeResult scene_input =
      input_decoder.Accept(scene_input_frame);
  const RenderBridgeControlTransportDecodeResult scene_ack =
      control_decoder.Accept(scene_ack_frame);
  Require(ready.ok() &&
              ready.kind == RenderTransportMessageKind::
                                RENDER_BRIDGE_CONTROL_V1 &&
              ready.sequence == 1U &&
              ready.control.kind == RenderBridgeControlKind::PEER_READY &&
              ready.control.command_id == 1U &&
              ready.control.surface.surface_revision == 9U &&
              ready.control.surface.logical_width == 96U &&
              ready.control.surface.drawable_width == 96U &&
              !ready.control.surface.suspended && asset_input.ok() &&
              asset_input.message->sequence() == 2U && asset_ack.ok() &&
              asset_ack.sequence == 3U &&
              asset_ack.acknowledgement.through_forward_sequence == 1U &&
              asset_ack.acknowledgement.presented_scene_sequence == 0U &&
              asset_ack.acknowledgement.presented_snapshot_id == 0U &&
              scene_input.ok() && scene_input.message->sequence() == 4U &&
              scene_ack.ok() && scene_ack.sequence == 5U &&
              scene_ack.acknowledgement.through_forward_sequence == 2U &&
              scene_ack.acknowledgement.presented_scene_sequence == 2U &&
              scene_ack.acknowledgement.presented_snapshot_id == 1U &&
              reverse_sequence.next_expected_sequence() == 6U,
          "shared reverse input/control/ACK lineage changed");
  Require(session.status ==
              RendererOgreNextLiveSessionStatus::COMPLETED_PEER_EOF &&
              session.completed && session.channel_adopted &&
              session.asset_frames == 1U && session.scene_frames == 1U &&
              session.presented_scene_frames == 1U &&
              session.responses_sent == 5U &&
              session.controls_sent == 1U &&
              session.surface_changes_sent == 0U &&
              session.last_announced_surface_revision == 9U &&
              session.peer_ready_sent &&
              session.input_batches_sent == 2U &&
              session.acknowledgements_sent == 2U &&
              session.last_forward_sequence == 2U &&
              session.last_acknowledged_forward_sequence == 2U &&
              session.last_presented_scene_sequence == 2U &&
              session.last_presented_snapshot_id == 1U &&
              session.last_reverse_sequence == 5U,
          "live session completion audit changed");
  Require(frontend.asset_calls == 1U && frontend.render_calls == 1U &&
              frontend.wait_calls == 1U && frontend.release_calls == 1U &&
              frontend.frame_ids == std::vector<std::uint64_t>{2U} &&
              frontend.presented == std::vector<bool>{true} &&
              frontend.surface_revisions == std::vector<std::uint64_t>{9U},
          "complete frames did not reach the fake frontend exactly once");
  Require(poll.observed_forward_sequences ==
              std::vector<std::uint64_t>({1U, 2U}),
          "frame-driven polling lineage changed");
  Require(!IsNativeOpen(forward.read_handle) &&
              !IsNativeOpen(reverse.write_handle),
          "session did not close adopted native handles");
  CloseNative(reverse.read_handle);
}

void TestWindowCloseControlCompletesCleanly() {
  NativePipe forward = MakePipe();
  NativePipe reverse = MakePipe();
  const RendererBridgeEndpoint endpoint =
      MakeEndpoint(forward.read_handle, reverse.write_handle, 33U);
  const std::uint64_t registry_id =
      DeriveRenderAssetRegistryIdFromBridgeSession(endpoint.session_id);
  const std::vector<std::uint8_t> asset = AssetFrame(registry_id);
  FakeFrontend frontend;
  PollContext poll;
  poll.close_on_call = 1U;
  RenderBridgeSurfaceState closing_surface = ActiveSurface(10U);
  closing_surface.logical_width = 48U;
  closing_surface.logical_height = 32U;
  poll.surfaces.push_back(closing_surface);
  RendererOgreNextLiveSessionResult session;
  std::thread worker([&]() {
    session = RunRendererOgreNextLiveSession(endpoint, Runtime(frontend, poll));
  });
  const std::vector<std::uint8_t> ready_frame =
      ReadNativeFrame(reverse.read_handle);
  WriteNative(forward.write_handle, asset.data(), asset.size());
  const std::vector<std::uint8_t> surface_frame =
      ReadNativeFrame(reverse.read_handle);
  const std::vector<std::uint8_t> input_frame =
      ReadNativeFrame(reverse.read_handle);
  const std::vector<std::uint8_t> shutdown_frame =
      ReadNativeFrame(reverse.read_handle);
  worker.join();

  RenderTransportSequenceState reverse_sequence(1U);
  InputEventTransportDecoder input_decoder(reverse_sequence);
  RenderBridgeControlTransportDecoder control_decoder(registry_id,
                                                       reverse_sequence);
  const RenderBridgeControlTransportDecodeResult ready =
      control_decoder.Accept(ready_frame);
  const RenderBridgeControlTransportDecodeResult surface =
      control_decoder.Accept(surface_frame);
  const InputEventTransportDecodeResult input =
      input_decoder.Accept(input_frame);
  const RenderBridgeControlTransportDecodeResult shutdown =
      control_decoder.Accept(shutdown_frame);
  Require(ready.ok() && ready.sequence == 1U &&
              ready.control.kind == RenderBridgeControlKind::PEER_READY &&
              ready.control.surface.surface_revision == 9U &&
              surface.ok() && surface.sequence == 2U &&
              surface.control.kind ==
                  RenderBridgeControlKind::SURFACE_CHANGED &&
              surface.control.command_id == 2U && input.ok() &&
              input.message->sequence() == 3U &&
              input.message->batch()->events.size() == 1U &&
              input.message->batch()->reconciliation.window_close_requested &&
              shutdown.ok() && shutdown.sequence == 4U &&
              shutdown.control.kind ==
                  RenderBridgeControlKind::REQUEST_GRACEFUL_SHUTDOWN &&
              shutdown.control.command_id == 3U &&
              reverse_sequence.next_expected_sequence() == 5U,
          "window-close input/control lineage did not round-trip");
  Require(session.status ==
              RendererOgreNextLiveSessionStatus::COMPLETED_WINDOW_CLOSE &&
              session.completed && session.responses_sent == 4U &&
              session.controls_sent == 3U &&
              session.surface_changes_sent == 1U &&
              session.peer_ready_sent &&
              session.input_batches_sent == 1U &&
              session.acknowledgements_sent == 0U &&
              session.last_acknowledged_forward_sequence == 0U &&
              frontend.asset_calls == 0U && frontend.render_calls == 0U,
          "window close dispatched or fabricated an acknowledgement");
  CloseNative(forward.write_handle);
  CloseNative(reverse.read_handle);
}

void TestEmptyPeerEofDoesNotFabricateAcknowledgement() {
  NativePipe forward = MakePipe();
  NativePipe reverse = MakePipe();
  const RendererBridgeEndpoint endpoint =
      MakeEndpoint(forward.read_handle, reverse.write_handle, 43U);
  const std::uint64_t registry_id =
      DeriveRenderAssetRegistryIdFromBridgeSession(endpoint.session_id);
  FakeFrontend frontend;
  PollContext poll;
  RendererOgreNextLiveSessionResult session;
  std::thread worker([&]() {
    session = RunRendererOgreNextLiveSession(endpoint, Runtime(frontend, poll));
  });
  const std::vector<std::uint8_t> ready_frame =
      ReadNativeFrame(reverse.read_handle);
  CloseNative(forward.write_handle);
  worker.join();

  RenderTransportSequenceState reverse_sequence(1U);
  RenderBridgeControlTransportDecoder control_decoder(registry_id,
                                                       reverse_sequence);
  const RenderBridgeControlTransportDecodeResult ready =
      control_decoder.Accept(ready_frame);
  Require(ready.ok() &&
              ready.control.kind == RenderBridgeControlKind::PEER_READY &&
              reverse_sequence.next_expected_sequence() == 2U &&
              session.status ==
                  RendererOgreNextLiveSessionStatus::COMPLETED_PEER_EOF &&
              session.completed && session.responses_sent == 1U &&
              session.controls_sent == 1U &&
              session.peer_ready_sent &&
              session.input_batches_sent == 0U &&
              session.acknowledgements_sent == 0U && poll.calls == 0U &&
              frontend.asset_calls == 0U && frontend.render_calls == 0U,
          "empty peer EOF fabricated reverse input or acknowledgement");
  CloseNative(reverse.read_handle);
}

void TestSurfaceChangePrecedesAffectedFrameResponse() {
  NativePipe forward = MakePipe();
  NativePipe reverse = MakePipe();
  const RendererBridgeEndpoint endpoint =
      MakeEndpoint(forward.read_handle, reverse.write_handle, 45U);
  const std::uint64_t registry_id =
      DeriveRenderAssetRegistryIdFromBridgeSession(endpoint.session_id);
  const std::vector<std::uint8_t> asset = AssetFrame(registry_id);
  FakeFrontend frontend;
  PollContext poll;
  RenderBridgeSurfaceState scaled = ActiveSurface(10U);
  scaled.logical_width = 48U;
  scaled.logical_height = 32U;
  Require(IsValidRenderBridgeSurfaceState(scaled, false),
          "scaled surface fixture invalid");
  poll.surfaces.push_back(scaled);
  RendererOgreNextLiveSessionResult session;
  std::thread worker([&]() {
    session = RunRendererOgreNextLiveSession(endpoint, Runtime(frontend, poll));
  });

  const std::vector<std::uint8_t> ready_frame =
      ReadNativeFrame(reverse.read_handle);
  WriteNative(forward.write_handle, asset.data(), asset.size());
  const std::vector<std::uint8_t> surface_frame =
      ReadNativeFrame(reverse.read_handle);
  const std::vector<std::uint8_t> input_frame =
      ReadNativeFrame(reverse.read_handle);
  const std::vector<std::uint8_t> acknowledgement_frame =
      ReadNativeFrame(reverse.read_handle);
  CloseNative(forward.write_handle);
  worker.join();

  RenderTransportSequenceState reverse_sequence(1U);
  InputEventTransportDecoder input_decoder(reverse_sequence);
  RenderBridgeControlTransportDecoder control_decoder(registry_id,
                                                       reverse_sequence);
  const auto ready = control_decoder.Accept(ready_frame);
  const auto surface = control_decoder.Accept(surface_frame);
  const auto input = input_decoder.Accept(input_frame);
  const auto acknowledgement =
      control_decoder.Accept(acknowledgement_frame);
  Require(ready.ok() && ready.sequence == 1U &&
              ready.control.kind == RenderBridgeControlKind::PEER_READY &&
              surface.ok() && surface.sequence == 2U &&
              surface.control.kind ==
                  RenderBridgeControlKind::SURFACE_CHANGED &&
              surface.control.command_id == 2U &&
              surface.control.surface.surface_revision == 10U &&
              surface.control.surface.logical_width == 48U &&
              surface.control.surface.drawable_width == 96U && input.ok() &&
              input.message->sequence() == 3U && acknowledgement.ok() &&
              acknowledgement.sequence == 4U &&
              acknowledgement.acknowledgement.through_forward_sequence == 1U,
          "surface change did not precede the affected input/ACK response");
  Require(session.completed && session.peer_ready_sent &&
              session.controls_sent == 2U &&
              session.surface_changes_sent == 1U &&
              session.last_announced_surface_revision == 10U &&
              session.responses_sent == 4U && frontend.asset_calls == 1U &&
              frontend.render_calls == 0U,
          "active surface change audit changed");
  CloseNative(reverse.read_handle);
}

void TestIdlePumpPublishesSurfaceWithoutForwardEnvelope() {
  NativePipe forward = MakePipe();
  NativePipe reverse = MakePipe();
  const RendererBridgeEndpoint endpoint =
      MakeEndpoint(forward.read_handle, reverse.write_handle, 46U);
  const std::uint64_t registry_id =
      DeriveRenderAssetRegistryIdFromBridgeSession(endpoint.session_id);
  FakeFrontend frontend;
  PollContext poll;
  RenderBridgeSurfaceState scaled = ActiveSurface(10U);
  scaled.logical_width = 48U;
  scaled.logical_height = 32U;
  poll.surfaces.push_back(scaled);
  RendererOgreNextLiveSessionRuntime runtime = Runtime(frontend, poll);
  runtime.idle_poll_interval_milliseconds = 1U;
  RendererOgreNextLiveSessionResult session;
  std::thread worker([&]() {
    session = RunRendererOgreNextLiveSession(endpoint, runtime);
  });

  const std::vector<std::uint8_t> ready_frame =
      ReadNativeFrame(reverse.read_handle);
  const std::vector<std::uint8_t> surface_frame =
      ReadNativeFrame(reverse.read_handle);
  CloseNative(forward.write_handle);
  worker.join();

  RenderTransportSequenceState reverse_sequence(1U);
  RenderBridgeControlTransportDecoder control_decoder(registry_id,
                                                       reverse_sequence);
  const auto ready = control_decoder.Accept(ready_frame);
  const auto surface = control_decoder.Accept(surface_frame);
  Require(ready.ok() && ready.sequence == 1U && surface.ok() &&
              surface.sequence == 2U &&
              surface.control.kind ==
                  RenderBridgeControlKind::SURFACE_CHANGED &&
              surface.control.surface.surface_revision == 10U &&
              reverse_sequence.next_expected_sequence() == 3U,
          "idle surface control did not preserve reverse lineage");
  Require(session.completed && session.peer_ready_sent &&
              session.idle_polls >= 1U && session.responses_sent == 2U &&
              session.controls_sent == 2U &&
              session.surface_changes_sent == 1U &&
              session.input_batches_sent == 0U &&
              session.acknowledgements_sent == 0U &&
              session.last_forward_sequence == 0U &&
              session.last_announced_surface_revision == 10U &&
              !poll.observed_forward_sequences.empty() &&
              poll.observed_forward_sequences.front() == 0U &&
              frontend.asset_calls == 0U && frontend.render_calls == 0U,
          "idle pump required a forward envelope or fabricated a response");
  CloseNative(reverse.read_handle);
}

void TestIdleResizeRetiresLaterPreResizeScene() {
  NativePipe forward = MakePipe();
  NativePipe reverse = MakePipe();
  const RendererBridgeEndpoint endpoint =
      MakeEndpoint(forward.read_handle, reverse.write_handle, 56U);
  const std::uint64_t registry_id =
      DeriveRenderAssetRegistryIdFromBridgeSession(endpoint.session_id);
  const std::vector<std::uint8_t> asset = AssetFrame(registry_id);
  const std::vector<std::uint8_t> pre_resize_scene = SceneFrame(registry_id);
  FakeFrontend frontend;
  PollContext poll;
  RenderBridgeSurfaceState resized = ActiveSurface(10U);
  resized.drawable_width = 192U;
  resized.drawable_height = 128U;
  Require(IsValidRenderBridgeSurfaceState(resized, false),
          "idle-resize stale-scene fixture invalid");
  poll.surfaces.push_back(resized);
  RendererOgreNextLiveSessionRuntime runtime = Runtime(frontend, poll);
  runtime.idle_poll_interval_milliseconds = 1U;
  RendererOgreNextLiveSessionResult session;
  std::thread worker([&]() {
    session = RunRendererOgreNextLiveSession(endpoint, runtime);
  });

  const std::vector<std::uint8_t> ready_frame =
      ReadNativeFrame(reverse.read_handle);
  const std::vector<std::uint8_t> surface_frame =
      ReadNativeFrame(reverse.read_handle);
  WriteNative(forward.write_handle, asset.data(), asset.size());
  const std::vector<std::uint8_t> asset_input_frame =
      ReadNativeFrame(reverse.read_handle);
  const std::vector<std::uint8_t> asset_ack_frame =
      ReadNativeFrame(reverse.read_handle);
  WriteNative(forward.write_handle, pre_resize_scene.data(),
              pre_resize_scene.size());
  const std::vector<std::uint8_t> scene_input_frame =
      ReadNativeFrame(reverse.read_handle);
  const std::vector<std::uint8_t> scene_ack_frame =
      ReadNativeFrame(reverse.read_handle);
  CloseNative(forward.write_handle);
  worker.join();

  RenderTransportSequenceState reverse_sequence(1U);
  InputEventTransportDecoder input_decoder(reverse_sequence);
  RenderBridgeControlTransportDecoder control_decoder(registry_id,
                                                       reverse_sequence);
  const auto ready = control_decoder.Accept(ready_frame);
  const auto surface = control_decoder.Accept(surface_frame);
  const auto asset_input = input_decoder.Accept(asset_input_frame);
  const auto asset_ack = control_decoder.Accept(asset_ack_frame);
  const auto scene_input = input_decoder.Accept(scene_input_frame);
  const auto scene_ack = control_decoder.Accept(scene_ack_frame);
  Require(ready.ok() && surface.ok() &&
              surface.control.kind ==
                  RenderBridgeControlKind::SURFACE_CHANGED &&
              surface.control.surface.surface_revision == 10U &&
              asset_input.ok() && asset_ack.ok() && scene_input.ok() &&
              scene_ack.ok() &&
              scene_ack.acknowledgement.through_forward_sequence == 2U &&
              scene_ack.acknowledgement.presented_scene_sequence == 0U &&
              scene_ack.acknowledgement.presented_snapshot_id == 0U &&
              reverse_sequence.next_expected_sequence() == 7U,
          "idle-resize retirement response lineage changed");
  Require(session.completed && session.asset_frames == 1U &&
              session.scene_frames == 1U &&
              session.retired_scene_frames == 1U &&
              session.presented_scene_frames == 0U &&
              session.last_announced_surface_revision == 10U &&
              session.dispatch_status ==
                  RendererFrontendTransportDispatchStatus::
                      SCENE_FRAME_RETIRED &&
              frontend.asset_calls == 1U && frontend.render_calls == 0U &&
              frontend.wait_calls == 0U && frontend.release_calls == 0U,
          "pre-resize scene reached the frontend after idle resize poll");
  CloseNative(reverse.read_handle);
}

void RunChangedSurfaceRetirementCase(
    const RenderBridgeSurfaceState &changed_surface, std::uint8_t seed) {
  NativePipe forward = MakePipe();
  NativePipe reverse = MakePipe();
  const RendererBridgeEndpoint endpoint =
      MakeEndpoint(forward.read_handle, reverse.write_handle, seed);
  const std::uint64_t registry_id =
      DeriveRenderAssetRegistryIdFromBridgeSession(endpoint.session_id);
  const std::vector<std::uint8_t> asset = AssetFrame(registry_id);
  const std::vector<std::uint8_t> scene = SceneFrame(registry_id);
  FakeFrontend frontend;
  PollContext poll;
  poll.surfaces = {ActiveSurface(), changed_surface};
  RendererOgreNextLiveSessionResult session;
  std::thread worker([&]() {
    session = RunRendererOgreNextLiveSession(endpoint, Runtime(frontend, poll));
  });

  const std::vector<std::uint8_t> ready_frame =
      ReadNativeFrame(reverse.read_handle);
  WriteNative(forward.write_handle, asset.data(), asset.size());
  const std::vector<std::uint8_t> asset_input_frame =
      ReadNativeFrame(reverse.read_handle);
  const std::vector<std::uint8_t> asset_ack_frame =
      ReadNativeFrame(reverse.read_handle);
  WriteNative(forward.write_handle, scene.data(), scene.size());
  const std::vector<std::uint8_t> suspended_frame =
      ReadNativeFrame(reverse.read_handle);
  const std::vector<std::uint8_t> retired_input_frame =
      ReadNativeFrame(reverse.read_handle);
  const std::vector<std::uint8_t> retired_ack_frame =
      ReadNativeFrame(reverse.read_handle);
  CloseNative(forward.write_handle);
  worker.join();

  RenderTransportSequenceState reverse_sequence(1U);
  InputEventTransportDecoder input_decoder(reverse_sequence);
  RenderBridgeControlTransportDecoder control_decoder(registry_id,
                                                       reverse_sequence);
  const auto ready = control_decoder.Accept(ready_frame);
  const auto asset_input = input_decoder.Accept(asset_input_frame);
  const auto asset_ack = control_decoder.Accept(asset_ack_frame);
  const auto suspended = control_decoder.Accept(suspended_frame);
  const auto retired_input = input_decoder.Accept(retired_input_frame);
  const auto retired_ack = control_decoder.Accept(retired_ack_frame);
  Require(ready.ok() && asset_input.ok() && asset_ack.ok() &&
              suspended.ok() && suspended.sequence == 4U &&
              suspended.control.kind ==
                  RenderBridgeControlKind::SURFACE_CHANGED &&
              suspended.control.surface.suspended ==
                  changed_surface.suspended &&
              suspended.control.surface.surface_revision == 10U &&
              suspended.control.surface.drawable_width ==
                  changed_surface.drawable_width &&
              retired_input.ok() && retired_input.message->sequence() == 5U &&
              retired_ack.ok() && retired_ack.sequence == 6U &&
              retired_ack.acknowledgement.through_forward_sequence == 2U &&
              retired_ack.acknowledgement.presented_scene_sequence == 0U &&
              retired_ack.acknowledgement.presented_snapshot_id == 0U &&
              reverse_sequence.next_expected_sequence() == 7U,
          "changed surface retirement lineage changed");
  Require(session.status ==
              RendererOgreNextLiveSessionStatus::COMPLETED_PEER_EOF &&
              session.dispatch_status ==
                  RendererFrontendTransportDispatchStatus::
                      SCENE_FRAME_RETIRED &&
              session.completed && session.peer_ready_sent &&
              session.asset_frames == 1U && session.scene_frames == 1U &&
              session.retired_scene_frames == 1U &&
              session.presented_scene_frames == 0U &&
              session.last_forward_sequence == 2U &&
              session.last_acknowledged_forward_sequence == 2U &&
              session.surface_changes_sent == 1U &&
              session.input_batches_sent == 2U &&
              session.acknowledgements_sent == 2U &&
              frontend.asset_calls == 1U && frontend.render_calls == 0U,
          "changed scene was not retired without frontend dispatch");
  CloseNative(reverse.read_handle);
}

void TestChangedOrSuspendedSurfaceRetiresSceneWithoutDispatch() {
  RenderBridgeSurfaceState resized = ActiveSurface(10U);
  resized.drawable_width = 192U;
  resized.drawable_height = 128U;
  Require(IsValidRenderBridgeSurfaceState(resized, false),
          "resized stale-scene fixture invalid");
  RunChangedSurfaceRetirementCase(resized, 47U);
  RunChangedSurfaceRetirementCase(SuspendedSurface(10U), 48U);
}

void TestPreReadyPeerCloseAndSameRevisionMutationFailClosed() {
  {
    NativePipe forward = MakePipe();
    NativePipe reverse = MakePipe();
    CloseNative(reverse.read_handle);
    const RendererBridgeEndpoint endpoint =
        MakeEndpoint(forward.read_handle, reverse.write_handle, 49U);
    FakeFrontend frontend;
    PollContext poll;
    const RendererOgreNextLiveSessionResult session =
        RunRendererOgreNextLiveSession(endpoint, Runtime(frontend, poll));
    Require(session.status == RendererOgreNextLiveSessionStatus::
                                  FAILED_PEER_CLOSED_BEFORE_READY &&
                !session.completed && session.channel_adopted &&
                !session.peer_ready_sent && session.responses_sent == 0U &&
                session.controls_sent == 0U &&
                session.last_announced_surface_revision == 0U,
            "pre-ready reverse close was not classified as startup failure");
    CloseNative(forward.write_handle);
  }

  {
    NativePipe forward = MakePipe();
    NativePipe reverse = MakePipe();
    const RendererBridgeEndpoint endpoint =
        MakeEndpoint(forward.read_handle, reverse.write_handle, 50U);
    const std::uint64_t registry_id =
        DeriveRenderAssetRegistryIdFromBridgeSession(endpoint.session_id);
    const std::vector<std::uint8_t> asset = AssetFrame(registry_id);
    FakeFrontend frontend;
    PollContext poll;
    RenderBridgeSurfaceState mutated = ActiveSurface();
    mutated.logical_width = 95U;
    poll.surfaces.push_back(mutated);
    RendererOgreNextLiveSessionResult session;
    std::thread worker([&]() {
      session =
          RunRendererOgreNextLiveSession(endpoint, Runtime(frontend, poll));
    });
    const std::vector<std::uint8_t> ready_frame =
        ReadNativeFrame(reverse.read_handle);
    WriteNative(forward.write_handle, asset.data(), asset.size());
    worker.join();
    RenderTransportSequenceState reverse_sequence(1U);
    RenderBridgeControlTransportDecoder control_decoder(registry_id,
                                                         reverse_sequence);
    Require(control_decoder.Accept(ready_frame).ok() &&
                session.status ==
                    RendererOgreNextLiveSessionStatus::FAILED_EVENT_POLL &&
                session.peer_ready_sent && !session.completed &&
                session.responses_sent == 1U &&
                session.surface_changes_sent == 0U &&
                frontend.asset_calls == 0U && frontend.render_calls == 0U,
            "same-revision surface mutation crossed the event boundary");
    CloseNative(forward.write_handle);
    CloseNative(reverse.read_handle);
  }
}

void TestTruncatedEofAndInvalidResponseFailClosed() {
  {
    NativePipe forward = MakePipe();
    NativePipe reverse = MakePipe();
    const RendererBridgeEndpoint endpoint =
        MakeEndpoint(forward.read_handle, reverse.write_handle, 51U);
    FakeFrontend frontend;
    PollContext poll;
    RendererOgreNextLiveSessionResult session;
    std::thread worker([&]() {
      session =
          RunRendererOgreNextLiveSession(endpoint, Runtime(frontend, poll));
    });
    const std::vector<std::uint8_t> ready_frame =
        ReadNativeFrame(reverse.read_handle);
    const std::array<std::uint8_t, 10U> partial{{'R', 'O', 'R', 'T', 'R',
                                                'N', 'S', 'P', 1U, 0U}};
    WriteNative(forward.write_handle, partial.data(), partial.size());
    CloseNative(forward.write_handle);
    worker.join();
    const std::uint64_t registry_id =
        DeriveRenderAssetRegistryIdFromBridgeSession(endpoint.session_id);
    RenderTransportSequenceState reverse_sequence(1U);
    RenderBridgeControlTransportDecoder control_decoder(registry_id,
                                                         reverse_sequence);
    const RenderBridgeControlTransportDecodeResult ready =
        control_decoder.Accept(ready_frame);
    Require(session.status ==
                RendererOgreNextLiveSessionStatus::FAILED_STREAM &&
                !session.completed && frontend.asset_calls == 0U &&
                frontend.render_calls == 0U && ready.ok() &&
                ready.control.kind == RenderBridgeControlKind::PEER_READY &&
                session.controls_sent == 1U &&
                session.input_batches_sent == 0U &&
                session.acknowledgements_sent == 0U,
            "truncated EOF reached frontend or fabricated an acknowledgement");
    CloseNative(reverse.read_handle);
  }

  {
    NativePipe forward = MakePipe();
    NativePipe reverse = MakePipe();
    const RendererBridgeEndpoint endpoint =
        MakeEndpoint(forward.read_handle, reverse.write_handle, 61U);
    const std::uint64_t registry_id =
        DeriveRenderAssetRegistryIdFromBridgeSession(endpoint.session_id);
    const std::vector<std::uint8_t> asset = AssetFrame(registry_id);
    FakeFrontend frontend;
    PollContext poll;
    poll.invalid_batch = true;
    RendererOgreNextLiveSessionResult session;
    std::thread worker([&]() {
      session =
          RunRendererOgreNextLiveSession(endpoint, Runtime(frontend, poll));
    });
    const std::vector<std::uint8_t> ready_frame =
        ReadNativeFrame(reverse.read_handle);
    WriteNative(forward.write_handle, asset.data(), asset.size());
    worker.join();
    RenderTransportSequenceState reverse_sequence(1U);
    RenderBridgeControlTransportDecoder control_decoder(registry_id,
                                                         reverse_sequence);
    const RenderBridgeControlTransportDecodeResult ready =
        control_decoder.Accept(ready_frame);
    Require(session.status ==
                RendererOgreNextLiveSessionStatus::FAILED_RESPONSE_ENCODE &&
                !session.completed && frontend.asset_calls == 1U &&
                ready.ok() && session.controls_sent == 1U &&
                session.input_batches_sent == 0U &&
                session.acknowledgements_sent == 0U &&
                session.last_forward_sequence == 1U &&
                session.last_acknowledged_forward_sequence == 0U,
            "invalid reverse response fabricated an acknowledgement after dispatch");
    CloseNative(forward.write_handle);
    CloseNative(reverse.read_handle);
  }
}

} // namespace

int main() {
  static_assert(
      !std::is_copy_constructible_v<RendererOgreNextLiveSessionRuntime> ||
      std::is_trivially_copy_constructible_v<RendererOgreNextLiveSessionRuntime>);
  TestStatusDomainAndRejections();
  TestCompleteMonotonicFramesAndResponses();
  TestWindowCloseControlCompletesCleanly();
  TestEmptyPeerEofDoesNotFabricateAcknowledgement();
  TestSurfaceChangePrecedesAffectedFrameResponse();
  TestIdlePumpPublishesSurfaceWithoutForwardEnvelope();
  TestIdleResizeRetiresLaterPreResizeScene();
  TestChangedOrSuspendedSurfaceRetiresSceneWithoutDispatch();
  TestPreReadyPeerCloseAndSameRevisionMutationFailClosed();
  TestTruncatedEofAndInvalidResponseFailClosed();
  std::cout << "Renderer Ogre-Next live session tests passed\n";
  return EXIT_SUCCESS;
}
