/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererOgre14GameHostSession.h"
#include "RendererOgre14ProductSession.h"

#include "RenderBridgeSessionIdentity.h"
#include "RenderTransportStream.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#endif

namespace {

using namespace RoR;
using namespace RoR::Render;

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "renderer OGRE 14 game-host session test failed: " << message
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
  NativePipe pipe;
  Require(::CreatePipe(&pipe.read_handle, &pipe.write_handle, &security, 0U) !=
              FALSE,
          "CreatePipe failed");
  return pipe;
}

std::uint64_t NativeToken(NativeHandle handle) {
  return static_cast<std::uint64_t>(
      reinterpret_cast<std::uintptr_t>(handle));
}

void CloseNative(NativeHandle &handle) {
  if (handle == nullptr || handle == kInvalidNativeHandle) {
    return;
  }
  Require(::CloseHandle(handle) != FALSE, "CloseHandle failed");
  handle = kInvalidNativeHandle;
}

bool ReadNative(NativeHandle handle, std::uint8_t *bytes,
                std::size_t capacity, std::size_t &transferred) {
  DWORD count = 0U;
  const DWORD requested = static_cast<DWORD>(capacity);
  if (::ReadFile(handle, bytes, requested, &count, nullptr) == FALSE) {
    const DWORD error = ::GetLastError();
    transferred = 0U;
    return error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED;
  }
  transferred = static_cast<std::size_t>(count);
  return true;
}

bool WriteNative(NativeHandle handle, const std::uint8_t *bytes,
                 std::size_t size) {
  std::size_t total = 0U;
  while (total < size) {
    DWORD count = 0U;
    const DWORD requested = static_cast<DWORD>(
        (std::min)(size - total, static_cast<std::size_t>(1024U * 1024U)));
    if (::WriteFile(handle, bytes + total, requested, &count, nullptr) ==
            FALSE ||
        count == 0U) {
      return false;
    }
    total += static_cast<std::size_t>(count);
  }
  return true;
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
  Require(promoted >= 3 && ::close(descriptor) == 0,
          "could not promote reserved pipe descriptor");
  return promoted;
}

NativePipe MakePipe() {
  int descriptors[2] = {-1, -1};
  Require(::pipe(descriptors) == 0, "pipe failed");
  NativePipe pipe;
  pipe.read_handle = PromoteReservedDescriptor(descriptors[0]);
  pipe.write_handle = PromoteReservedDescriptor(descriptors[1]);
  return pipe;
}

std::uint64_t NativeToken(NativeHandle handle) {
  return static_cast<std::uint64_t>(handle);
}

void CloseNative(NativeHandle &handle) {
  if (handle < 0) {
    return;
  }
  int result = 0;
  do {
    errno = 0;
    result = ::close(handle);
  } while (result != 0 && errno == EINTR);
  Require(result == 0, "close failed");
  handle = kInvalidNativeHandle;
}

bool ReadNative(NativeHandle handle, std::uint8_t *bytes,
                std::size_t capacity, std::size_t &transferred) {
  ssize_t count = -1;
  do {
    errno = 0;
    count = ::read(handle, bytes, capacity);
  } while (count < 0 && errno == EINTR);
  if (count < 0) {
    transferred = 0U;
    return false;
  }
  transferred = static_cast<std::size_t>(count);
  return true;
}

bool WriteNative(NativeHandle handle, const std::uint8_t *bytes,
                 std::size_t size) {
  std::size_t total = 0U;
  while (total < size) {
    ssize_t count = -1;
    do {
      errno = 0;
      count = ::write(handle, bytes + total, size - total);
    } while (count < 0 && errno == EINTR);
    if (count <= 0) {
      return false;
    }
    total += static_cast<std::size_t>(count);
  }
  return true;
}

#endif

RendererBridgeSessionId SessionId() {
  RendererBridgeSessionId session{};
  for (std::size_t index = 0U; index < session.size(); ++index) {
    session[index] = static_cast<std::uint8_t>(0x40U + index);
  }
  return session;
}

RendererBridgeEndpoint MakeEndpoint(NativeHandle inbound,
                                    NativeHandle outbound) {
  RendererBridgeEndpoint endpoint;
  endpoint.platform = CurrentPlatform();
  endpoint.role = RendererBridgeRole::GAME_HOST;
  endpoint.session_id = SessionId();
  endpoint.inbound_native_handle = NativeToken(inbound);
  endpoint.outbound_native_handle = NativeToken(outbound);
  return endpoint;
}

RendererChildLauncherString NativeArgument(const std::string &value) {
  RendererChildLauncherString result;
  for (const char character : value) {
    result.push_back(static_cast<RendererChildLauncherChar>(
        static_cast<unsigned char>(character)));
  }
  return result;
}

std::vector<const RendererChildLauncherChar *> NativePointers(
    const std::vector<RendererChildLauncherString> &arguments) {
  std::vector<const RendererChildLauncherChar *> pointers;
  pointers.reserve(arguments.size());
  for (const RendererChildLauncherString &argument : arguments) {
    pointers.push_back(argument.c_str());
  }
  return pointers;
}

std::vector<std::string> NarrowArguments(
    const std::vector<RendererChildLauncherString> &arguments) {
  std::vector<std::string> narrowed;
  narrowed.reserve(arguments.size());
  for (const RendererChildLauncherString &argument : arguments) {
    std::string value;
    for (const RendererChildLauncherChar character : argument) {
#if defined(_WIN32)
      const std::uint32_t code = static_cast<std::uint32_t>(character);
      Require(code <= 0xffU, "bridge argument was not byte-valued");
      value.push_back(static_cast<char>(code));
#else
      value.push_back(static_cast<char>(
          static_cast<unsigned char>(character)));
#endif
    }
    narrowed.push_back(std::move(value));
  }
  return narrowed;
}

struct MutableArguments final {
  std::vector<std::vector<char>> storage;
  std::vector<char *> pointers;

  explicit MutableArguments(const RendererBridgeEndpoint &endpoint) {
    const std::vector<RendererChildLauncherString> game{
        NativeArgument("RoR-Ogre14"), NativeArgument("-map"),
        NativeArgument("City World")};
    const auto game_pointers = NativePointers(game);
    const RendererBridgeEndpointArgvEncoding encoded =
        EncodeRendererBridgeEndpoint(
            endpoint, static_cast<int>(game_pointers.size()),
            game_pointers.data());
    Require(encoded.accepted, "bridge endpoint did not encode");
    const std::vector<std::string> arguments =
        NarrowArguments(encoded.arguments);
    storage.reserve(arguments.size());
    for (const std::string &argument : arguments) {
      storage.emplace_back(argument.begin(), argument.end());
      storage.back().push_back('\0');
    }
    pointers.reserve(storage.size());
    for (std::vector<char> &argument : storage) {
      pointers.push_back(argument.data());
    }
  }
};

struct BridgeFixture final {
  NativePipe game_inbound = MakePipe();
  NativePipe game_outbound = MakePipe();
  MutableArguments arguments{MakeEndpoint(game_inbound.read_handle,
                                          game_outbound.write_handle)};
  RendererOgre14GameBridge bridge;

  BridgeFixture() {
    const RendererOgre14GameBridgeResult initialized = bridge.Initialize(
        static_cast<int>(arguments.pointers.size()), arguments.pointers.data());
    Require(initialized.accepted && initialized.active,
            "game bridge fixture did not adopt pipes");
    game_inbound.read_handle = kInvalidNativeHandle;
    game_outbound.write_handle = kInvalidNativeHandle;
  }

  ~BridgeFixture() {
    (void)bridge.Close();
    CloseNative(game_inbound.write_handle);
    CloseNative(game_outbound.read_handle);
  }
};

RenderAssetDelta EmptyAssetSnapshot(std::uint64_t registry_id,
                                    std::uint64_t sequence = 1U) {
  RenderAssetDelta delta;
  delta.registry_id = registry_id;
  delta.sequence = sequence;
  delta.full_snapshot = true;
  Require(ValidateRenderAssetDelta(delta).ok(),
          "empty asset snapshot fixture is invalid");
  return delta;
}

RenderAssetDelta LargeTextureSnapshot(std::uint64_t registry_id) {
  TextureResourceDescriptor texture;
  texture.debug_name = "bounded close texture";
  texture.format = TextureResourceFormat::RGBA8_UNORM;
  texture.color_space = TextureColorSpace::SRGB;
  texture.width = 2048U;
  texture.height = 1024U;
  TextureMipLevelDescriptor mip;
  mip.width = texture.width;
  mip.height = texture.height;
  mip.row_pitch_bytes = static_cast<std::uint64_t>(texture.width) * 4U;
  mip.layer_pitch_bytes = mip.row_pitch_bytes * texture.height;
  mip.bytes.assign(static_cast<std::size_t>(mip.layer_pitch_bytes), 0x5aU);
  texture.mip_levels.push_back(std::move(mip));
  Require(ValidateTextureResourceDescriptor(texture).ok(),
          "large texture fixture is invalid");

  RenderAssetMutation mutation;
  mutation.type = RenderAssetMutationType::UPSERT;
  mutation.asset = RenderAssetReference::Create(
      RenderAssetKind::TEXTURE, RenderAssetId::FromWords(0U, 1U), 1U);
  mutation.payload = std::move(texture);
  RenderAssetDelta delta = EmptyAssetSnapshot(registry_id);
  delta.mutations.push_back(std::move(mutation));
  Require(ValidateRenderAssetDelta(delta).ok(),
          "large asset snapshot fixture is invalid");
  return delta;
}

std::shared_ptr<const SceneSnapshot> EmptyScene(std::uint64_t registry_id,
                                                std::uint64_t asset_sequence,
                                                std::uint64_t snapshot_id,
                                                std::uint64_t tick) {
  SceneSnapshotDescriptor descriptor;
  descriptor.snapshot_id = snapshot_id;
  descriptor.asset_registry_id = registry_id;
  descriptor.asset_sequence = asset_sequence;
  descriptor.simulation_tick = tick;
  descriptor.simulation_time_seconds = static_cast<double>(tick) / 2000.0;
  SceneSnapshotCreateResult created = CreateSceneSnapshot(std::move(descriptor));
  Require(created.ok(), "empty scene fixture is invalid");
  return created.snapshot;
}

Matrix4x4 Perspective(float near_plane, float far_plane) {
  Matrix4x4 projection;
  projection.elements.fill(0.0F);
  projection.elements[0U] = 1.0F;
  projection.elements[5U] = 1.0F;
  const float depth_scale = far_plane / (near_plane - far_plane);
  projection.elements[10U] = depth_scale;
  projection.elements[11U] = -1.0F;
  projection.elements[14U] = near_plane * depth_scale;
  return projection;
}

CameraViewRequest Camera(std::uint32_t width = 1280U,
                         std::uint32_t height = 720U) {
  CameraViewRequest camera;
  camera.view_id = 1U;
  camera.width = width;
  camera.height = height;
  camera.clip_from_view = Perspective(camera.near_plane, camera.far_plane);
  camera.previous_clip_from_view = camera.clip_from_view;
  Require(ValidateCameraViewRequest(camera).ok(), "camera fixture is invalid");
  return camera;
}

RenderBridgeSurfaceState ActiveSurface(std::uint64_t revision,
                                       std::uint32_t logical_width,
                                       std::uint32_t logical_height,
                                       std::uint32_t drawable_width,
                                       std::uint32_t drawable_height) {
  RenderBridgeSurfaceState surface;
  surface.surface_revision = revision;
  surface.logical_width = logical_width;
  surface.logical_height = logical_height;
  surface.drawable_width = drawable_width;
  surface.drawable_height = drawable_height;
  return surface;
}

InputTransportBatch InputBatch() {
  InputTransportBatch batch;
  batch.clock_origin_id = 0x53455353494f4e31ULL;
  InputTransportEvent event;
  event.event_id = 1U;
  event.host_timestamp_ns = 100U;
  event.payload = InputTransportFocusEvent{InputTransportFocusState::GAINED};
  batch.events.push_back(event);
  batch.reconciliation.through_event_id = 1U;
  batch.reconciliation.host_timestamp_ns = 100U;
  batch.reconciliation.focus = InputTransportFocusState::GAINED;
  Require(ValidateInputTransportBatch(batch) == RenderTransportStatus::OK,
          "input batch fixture is invalid");
  return batch;
}

class ProductInputTarget final : public IRendererOgre14InputTarget {
public:
  bool ActivateTransport() noexcept override {
    ++activations;
    return activation_succeeds;
  }
  void KeyChanged(RendererOgre14LegacyKey, bool) noexcept override {}
  void MouseMoved(std::int32_t, std::int32_t, std::int32_t,
                  std::int32_t) noexcept override {}
  void MouseButtonChanged(RendererOgre14LegacyMouseButton,
                          bool) noexcept override {}
  void MouseWheel(float, float) noexcept override {}
  void TextInput(std::string_view) noexcept override {}
  void FocusChanged(bool value) noexcept override { focused = value; }
  void WindowCloseRequested() noexcept override { ++close_requests; }
  bool Reconcile(
      const RendererOgre14LegacyInputState &state) noexcept override {
    applied_through = state.through_event_id;
    ++reconciliations;
    return true;
  }

  int activations = 0;
  int reconciliations = 0;
  int close_requests = 0;
  bool focused = false;
  bool activation_succeeds = true;
  std::uint64_t applied_through = 0U;
};

class ProductSceneSource final : public IJoinedGraphicsSceneSource {
public:
  ProductSceneSource() {
    frame.simulation_tick = 41U;
    frame.simulation_time_seconds = 1.0;

    MeshResourceDescriptor mesh;
    mesh.debug_name = "pending joined deformable triangle";
    mesh.dynamic = true;
    mesh.local_bounds.minimum = {0.0F, 0.0F, 0.0F};
    mesh.local_bounds.maximum = {1.0F, 1.0F, 0.0F};
    mesh.positions = {
        {0.0F, 0.0F, 0.0F},
        {1.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F},
    };
    mesh.normals.assign(3U, Float3{0.0F, 0.0F, 1.0F});
    mesh.texture_coordinates_0 = {
        {0.0F, 0.0F}, {1.0F, 0.0F}, {0.0F, 1.0F}};
    mesh.indices = {0U, 1U, 2U};
    GraphicsSceneAssetInput mesh_asset;
    mesh_asset.source_asset_id = 50U;
    mesh_asset.payload = std::make_shared<const RenderAssetPayload>(
        std::move(mesh));
    frame.assets.push_back(std::move(mesh_asset));

    MaterialDescriptor material;
    material.debug_name = "pending joined deformable material";
    GraphicsSceneAssetInput material_asset;
    material_asset.source_asset_id = 20U;
    material_asset.payload = std::make_shared<const RenderAssetPayload>(
        std::move(material));
    frame.assets.push_back(std::move(material_asset));

    auto state = std::make_shared<GraphicsSceneDynamicMeshState>();
    state->deformation_revision = 2U;
    state->positions = {
        {0.0F, 0.0F, 0.0F},
        {1.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F},
    };
    state->normals.assign(3U, Float3{0.0F, 0.0F, 1.0F});
    state->updated_local_bounds.minimum = {0.0F, 0.0F, 0.0F};
    state->updated_local_bounds.maximum = {1.0F, 1.0F, 0.0F};
    GraphicsSceneDynamicMeshInput object;
    object.source_object_id = 150U;
    object.mesh_source_asset_id = 50U;
    object.material_source_asset_id = 20U;
    object.state = std::move(state);
    frame.dynamic_meshes.push_back(std::move(object));

    frame.camera.view_id = 1U;
    frame.camera.width = 1600U;
    frame.camera.height = 1200U;
    frame.camera.clip_from_view = Perspective(
        frame.camera.near_plane, frame.camera.far_plane);
  }

  ValidationResult CaptureJoinedGraphicsFrame(
      GraphicsSceneFrameInput &output) override {
    ++captures;
    output = frame;
    return ValidationResult::Success();
  }

  GraphicsSceneFrameInput frame;
  std::uint32_t captures = 0U;
};

bool ReadFrame(NativeHandle handle, RenderTransportStreamDecoder &stream,
               RenderTransportStreamFrameResult &frame) {
  std::uint8_t byte = 0U;
  for (;;) {
    std::size_t transferred = 0U;
    if (!ReadNative(handle, &byte, 1U, transferred)) {
      return false;
    }
    if (transferred == 0U) {
      return false;
    }
    const RenderTransportStreamResult accepted = stream.Accept(&byte, 1U);
    if (accepted.terminal || accepted.bytes_consumed != 1U) {
      return false;
    }
    if (accepted.status == RenderTransportStreamStatus::FRAME_READY) {
      frame = stream.TakeFrame();
      return frame.ok();
    }
  }
}

template <typename Predicate>
bool WaitUntil(Predicate predicate,
               std::chrono::milliseconds timeout =
                   std::chrono::milliseconds(5000)) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return predicate();
}

void TestStatusConfigurationAndLineageRejections() {
  for (unsigned int value = 0U; value <= 255U; ++value) {
    const auto status =
        static_cast<RendererOgre14GameHostSessionStatus>(value);
    Require(IsKnownRendererOgre14GameHostSessionStatus(status) ==
                (value <= 21U),
            "session status classifier changed");
  }
  Require(std::string(ToString(
              RendererOgre14GameHostSessionStatus::BACKPRESSURE)) ==
                  "backpressure" &&
              std::string(ToString(static_cast<
                              RendererOgre14GameHostSessionStatus>(255U))) ==
                  "invalid",
          "session status strings changed");

  BridgeFixture fixture;
  RendererOgre14GameHostSession invalid(fixture.bridge);
  RendererOgre14GameHostSessionConfig invalid_config;
  invalid_config.maximum_reverse_messages = 0U;
  Require(invalid.Start(invalid_config).status ==
              RendererOgre14GameHostSessionStatus::
                  REJECTED_INVALID_CONFIGURATION,
          "invalid bounded configuration was accepted");

  RendererOgre14GameHostSession session(fixture.bridge);
  RendererOgre14GameHostSessionConfig config;
  config.maximum_forward_messages = 1U;
  config.maximum_unacknowledged_forward_messages = 1U;
  Require(session.Start(config).ok(), "session did not start");
  const std::uint64_t registry_id = session.registry_id();
  Require(registry_id == DeriveRenderAssetRegistryIdFromBridgeSession(
                             SessionId()),
          "session registry identity was not endpoint-derived");

  RenderAssetDelta foreign = EmptyAssetSnapshot(registry_id + 1U);
  Require(session.Submit(foreign).status ==
              RendererOgre14GameHostSessionStatus::REJECTED_REGISTRY_ID &&
              session.next_forward_sequence() == 1U,
          "foreign asset registry advanced lineage");
  const auto premature_scene = EmptyScene(registry_id, 1U, 1U, 1U);
  Require(session.PostPhysics(*premature_scene, Camera()).status ==
              RendererOgre14GameHostSessionStatus::REJECTED_SURFACE_STATE &&
              session.next_forward_sequence() == 1U,
          "scene advanced before renderer surface readiness");

  Require(session.Submit(EmptyAssetSnapshot(registry_id)).ok(),
          "first asset snapshot was not queued");
  Require(session.PostPhysics(*premature_scene, Camera()).status ==
              RendererOgre14GameHostSessionStatus::REJECTED_SURFACE_STATE &&
              session.next_forward_sequence() == 2U,
          "scene advanced before PEER_READY after assets existed");
  RenderAssetDelta second = EmptyAssetSnapshot(registry_id, 2U);
  Require(session.Submit(second).status ==
              RendererOgre14GameHostSessionStatus::BACKPRESSURE &&
              session.next_forward_sequence() == 2U,
          "unacknowledged lineage did not exert deterministic backpressure");

  CloseNative(fixture.game_outbound.read_handle);
  CloseNative(fixture.game_inbound.write_handle);
  Require(WaitUntil([&session]() { return session.terminal(); }),
          "closed peer did not terminate bounded session");
  (void)session.Close();
}

void TestReverseCapacityPausesForwardWrites() {
  BridgeFixture fixture;
  RendererOgre14GameHostSession session(fixture.bridge);
  RendererOgre14GameHostSessionConfig config;
  config.maximum_forward_messages = 4U;
  config.maximum_unacknowledged_forward_messages = 4U;
  config.maximum_reverse_messages = 1U;
  Require(session.Start(config).ok(), "reverse-capacity session did not start");

  RenderBridgeControl ready;
  ready.kind = RenderBridgeControlKind::PEER_READY;
  ready.registry_id = session.registry_id();
  ready.command_id = 1U;
  ready.surface = ActiveSurface(1U, 800U, 600U, 1600U, 1200U);
  const RenderTransportEnvelopeEncodeResult ready_frame =
      EncodeRenderBridgeControlFrame(1U, ready);
  Require(ready_frame.ok() &&
              WriteNative(fixture.game_inbound.write_handle,
                          ready_frame.bytes.data(), ready_frame.bytes.size()) &&
              WaitUntil([&session]() {
                return session.peer_ready() || session.terminal();
              }) &&
              !session.terminal(),
          "reverse-capacity PEER_READY did not reach the session");
  Require(session.Submit(EmptyAssetSnapshot(session.registry_id())).ok(),
          "reverse-capacity asset did not enqueue");
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  Require(session.last_written_forward_sequence() == 0U &&
              session.queued_forward_bytes() != 0U,
          "forward writer ignored saturated reverse delivery capacity");

  const RendererOgre14GameHostPollResult polled = session.PollControl();
  Require(polled && polled.message.control.kind ==
                        RenderBridgeControlKind::PEER_READY,
          "reverse-capacity control was not available to the game loop");
  RenderTransportStreamDecoder stream(
      kRenderTransportStreamAbsoluteMaximumPayloadBytes);
  RenderTransportStreamFrameResult frame;
  Require(ReadFrame(fixture.game_outbound.read_handle, stream, frame) &&
              frame.sequence == 1U &&
              WaitUntil([&session]() {
                return session.last_written_forward_sequence() == 1U ||
                       session.terminal();
              }) &&
              !session.terminal(),
          "forward writer did not resume after reverse capacity was released");
  (void)session.Close();
}

void TestCloseIsBoundedWhenPeerDoesNotDrainForwardPipe() {
  BridgeFixture fixture;
  RendererOgre14GameHostSession session(fixture.bridge);
  RendererOgre14GameHostSessionConfig config;
  config.maximum_forward_queue_bytes = 32U * 1024U * 1024U;
  Require(session.Start(config).ok(), "bounded-close session did not start");
  const RenderAssetDelta large = LargeTextureSnapshot(session.registry_id());
  Require(session.Submit(large).ok(), "bounded-close asset did not enqueue");
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  Require(session.last_written_forward_sequence() == 0U &&
              session.queued_forward_bytes() != 0U,
          "large frame unexpectedly fit in the undrained native pipe");

  const auto started = std::chrono::steady_clock::now();
  const RendererOgre14GameHostSessionResult closed = session.Close();
  const auto elapsed = std::chrono::steady_clock::now() - started;
  Require(closed.status == RendererOgre14GameHostSessionStatus::CLOSED &&
              closed.accepted && !closed.terminal &&
              elapsed < std::chrono::seconds(2) &&
              session.queued_forward_bytes() == 0U,
          "Close blocked on an open peer that stopped draining forward bytes");
}

void TestTwoEndedAssetSceneInputAckControlAndHalfClose() {
  BridgeFixture fixture;
  RendererOgre14GameHostSession session(fixture.bridge);
  RendererOgre14GameHostSessionConfig config;
  config.maximum_forward_messages = 8U;
  config.maximum_unacknowledged_forward_messages = 8U;
  config.maximum_reverse_messages = 8U;
  Require(session.Start(config).ok(), "stream session did not start");
  const std::uint64_t registry_id = session.registry_id();

  std::atomic<bool> peer_ok{false};
  std::atomic<bool> peer_done{false};
  std::thread peer([&]() {
    RenderBridgeControl ready;
    ready.kind = RenderBridgeControlKind::PEER_READY;
    ready.registry_id = registry_id;
    ready.command_id = 1U;
    ready.surface = ActiveSurface(7U, 1280U, 720U, 2560U, 1440U);
    const RenderTransportEnvelopeEncodeResult ready_frame =
        EncodeRenderBridgeControlFrame(1U, ready);
    bool ok = ready_frame.ok() &&
              WriteNative(fixture.game_inbound.write_handle,
                          ready_frame.bytes.data(), ready_frame.bytes.size());

    RenderTransportStreamDecoder stream(
        kRenderTransportStreamAbsoluteMaximumPayloadBytes);
    RenderTransportSequenceState forward_sequence;
    RenderAssetDeltaTransportDecoder asset_decoder(registry_id,
                                                    forward_sequence);
    SceneSnapshotTransportDecoder scene_decoder(forward_sequence);
    RenderTransportStreamFrameResult asset_frame;
    RenderTransportStreamFrameResult scene_frame;
    ok = ok && ReadFrame(fixture.game_outbound.read_handle, stream,
                         asset_frame) &&
              asset_frame.kind ==
                  RenderTransportMessageKind::RENDER_ASSET_DELTA_V1 &&
              asset_decoder.Accept(asset_frame.bytes).ok() &&
              asset_decoder.registry().registry_id() == registry_id &&
              asset_decoder.registry().sequence() == 1U &&
              ReadFrame(fixture.game_outbound.read_handle, stream,
                        scene_frame) &&
              scene_frame.kind == RenderTransportMessageKind::
                                      SCENE_SNAPSHOT_V4_CAMERA_V2;
    SceneSnapshotTransportDecodeResult decoded_scene;
    if (ok) {
      decoded_scene = scene_decoder.Accept(scene_frame.bytes);
      ok = decoded_scene.ok() &&
           decoded_scene.message->scene_snapshot()->snapshot_id() == 1U &&
           decoded_scene.message->scene_snapshot()->simulation_tick() == 41U;
    }

    const InputEventTransportEncodeResult input =
        EncodeInputEventTransportFrame(2U, InputBatch());
    RenderBridgeAcknowledgement asset_acknowledgement;
    asset_acknowledgement.registry_id = registry_id;
    asset_acknowledgement.through_forward_sequence = asset_frame.sequence;
    const RenderTransportEnvelopeEncodeResult asset_ack =
        EncodeRenderBridgeAcknowledgementFrame(3U, asset_acknowledgement);
    RenderBridgeControl control;
    control.kind = RenderBridgeControlKind::HEARTBEAT;
    control.registry_id = registry_id;
    control.command_id = 2U;
    const RenderTransportEnvelopeEncodeResult control_frame =
        EncodeRenderBridgeControlFrame(4U, control);
    RenderBridgeAcknowledgement scene_acknowledgement;
    scene_acknowledgement.registry_id = registry_id;
    scene_acknowledgement.through_forward_sequence = scene_frame.sequence;
    scene_acknowledgement.presented_scene_sequence = scene_frame.sequence;
    scene_acknowledgement.presented_snapshot_id = 1U;
    const RenderTransportEnvelopeEncodeResult scene_ack =
        EncodeRenderBridgeAcknowledgementFrame(5U, scene_acknowledgement);
    ok = ok && input.ok() && asset_ack.ok() && control_frame.ok() &&
         scene_ack.ok() &&
         WriteNative(fixture.game_inbound.write_handle, input.bytes.data(),
                     input.bytes.size()) &&
         WriteNative(fixture.game_inbound.write_handle,
                     asset_ack.bytes.data(), asset_ack.bytes.size()) &&
         WriteNative(fixture.game_inbound.write_handle,
                     control_frame.bytes.data(), control_frame.bytes.size()) &&
         WriteNative(fixture.game_inbound.write_handle,
                     scene_ack.bytes.data(), scene_ack.bytes.size());
    peer_ok.store(ok, std::memory_order_release);

    std::uint8_t byte = 0U;
    std::size_t transferred = 1U;
    while (ok && transferred != 0U) {
      ok = ReadNative(fixture.game_outbound.read_handle, &byte, 1U,
                      transferred);
    }
    CloseNative(fixture.game_outbound.read_handle);
    CloseNative(fixture.game_inbound.write_handle);
    peer_ok.store(ok, std::memory_order_release);
    peer_done.store(true, std::memory_order_release);
  });

  Require(session.Submit(EmptyAssetSnapshot(registry_id)).ok(),
          "asset delta did not enqueue");
  Require(WaitUntil([&session]() { return session.peer_ready(); }),
          "PEER_READY surface did not unblock scene publication");
  const RenderBridgeSurfaceState ready_surface =
      session.current_surface_state();
  Require(ready_surface.surface_revision == 7U &&
              ready_surface.logical_width == 1280U &&
              ready_surface.drawable_width == 2560U &&
              ready_surface.content_scale_x() == 2.0 &&
              ready_surface.content_scale_y() == 2.0,
          "Retina logical/drawable readiness state changed");
  const std::shared_ptr<const SceneSnapshot> scene =
      EmptyScene(registry_id, 1U, 1U, 41U);
  const RendererOgre14GameHostSessionResult posted =
      session.PostPhysics(*scene, Camera(2560U, 1440U));
  Require(posted.ok() && posted.surface_revision == 7U,
          "post-physics scene did not enqueue");

  std::vector<RendererOgre14GameHostReverseMessage> reverse;
  Require(WaitUntil([&]() {
            const RendererOgre14GameHostPollResult polled =
                session.PollReverse();
            if (polled) {
              reverse.push_back(polled.message);
            }
            return reverse.size() == 5U || session.terminal();
          }),
          "reverse input/ack/control did not arrive");
  Require(!session.terminal() && reverse.size() == 5U &&
              reverse[0U].kind ==
                  RenderTransportMessageKind::RENDER_BRIDGE_CONTROL_V1 &&
              reverse[0U].reverse_sequence == 1U &&
              reverse[0U].control.kind ==
                  RenderBridgeControlKind::PEER_READY &&
              reverse[0U].control.command_id == 1U &&
              reverse[0U].control.surface.surface_revision == 7U &&
              reverse[1U].kind ==
                  RenderTransportMessageKind::INPUT_EVENT_BATCH_V1 &&
              reverse[1U].reverse_sequence == 2U &&
              reverse[1U].issued_first_event_id == 1U &&
              reverse[1U].issued_last_event_id == 1U &&
              reverse[1U].resolved_through_event_id == 1U &&
              reverse[2U].kind == RenderTransportMessageKind::
                                      RENDER_BRIDGE_ACKNOWLEDGEMENT_V1 &&
              reverse[2U].reverse_sequence == 3U &&
              reverse[2U].acknowledgement.through_forward_sequence == 1U &&
              reverse[2U].acknowledgement.presented_scene_sequence == 0U &&
              reverse[3U].kind ==
                  RenderTransportMessageKind::RENDER_BRIDGE_CONTROL_V1 &&
              reverse[3U].reverse_sequence == 4U &&
              reverse[3U].control.kind ==
                  RenderBridgeControlKind::HEARTBEAT &&
              reverse[3U].control.command_id == 2U &&
              reverse[4U].kind == RenderTransportMessageKind::
                                      RENDER_BRIDGE_ACKNOWLEDGEMENT_V1 &&
              reverse[4U].reverse_sequence == 5U &&
              reverse[4U].acknowledgement.through_forward_sequence == 2U &&
              reverse[4U].acknowledgement.presented_scene_sequence == 2U &&
              reverse[4U].acknowledgement.presented_snapshot_id == 1U &&
              session.last_written_forward_sequence() == 2U &&
              session.last_acknowledged_forward_sequence() == 2U,
          "shared reverse sequence, cumulative ACK, or input lineage changed");

  Require(session.FinishForward().ok(),
          "outbound half-close was not requested");
  Require(WaitUntil([&peer_done]() {
            return peer_done.load(std::memory_order_acquire);
          }) &&
              peer_ok.load(std::memory_order_acquire),
          "peer did not observe drained forward EOF");
  peer.join();
  const RendererOgre14GameHostSessionResult closed = session.Close();
  Require(closed.status == RendererOgre14GameHostSessionStatus::CLOSED &&
              closed.accepted && !closed.terminal,
          "orderly two-ended half-close did not finish cleanly");
}

void TestReverseControlAndAcknowledgementLineageFailuresAreTerminal() {
  {
    BridgeFixture fixture;
    RendererOgre14GameHostSession session(fixture.bridge);
    Require(session.Start().ok(), "control-lineage session did not start");
    RenderBridgeControl control;
    control.kind = RenderBridgeControlKind::PEER_READY;
    control.registry_id = session.registry_id();
    control.command_id = 2U;
    control.surface = ActiveSurface(1U, 800U, 600U, 1600U, 1200U);
    const RenderTransportEnvelopeEncodeResult encoded =
        EncodeRenderBridgeControlFrame(1U, control);
    Require(encoded.ok() &&
                WriteNative(fixture.game_inbound.write_handle,
                            encoded.bytes.data(), encoded.bytes.size()),
            "invalid initial control fixture did not reach the session");
    Require(WaitUntil([&session]() { return session.terminal(); }) &&
                session.terminal_cause() ==
                    RendererOgre14GameHostSessionStatus::
                        FAILED_REVERSE_LINEAGE,
            "PEER_READY control lineage did not require command ID one");
    (void)session.Close();
  }

  {
    BridgeFixture fixture;
    RendererOgre14GameHostSession session(fixture.bridge);
    Require(session.Start().ok(), "ack-lineage session did not start");
    Require(session.Submit(EmptyAssetSnapshot(session.registry_id())).ok() &&
                WaitUntil([&session]() {
                  return session.last_written_forward_sequence() == 1U ||
                         session.terminal();
                }) &&
                !session.terminal(),
            "ack-lineage asset did not reach the pipe");
    RenderBridgeControl ready;
    ready.kind = RenderBridgeControlKind::PEER_READY;
    ready.registry_id = session.registry_id();
    ready.command_id = 1U;
    ready.surface = ActiveSurface(1U, 800U, 600U, 1600U, 1200U);
    const RenderTransportEnvelopeEncodeResult ready_frame =
        EncodeRenderBridgeControlFrame(1U, ready);
    Require(ready_frame.ok() &&
                WriteNative(fixture.game_inbound.write_handle,
                            ready_frame.bytes.data(), ready_frame.bytes.size()) &&
                WaitUntil([&session]() {
                  return session.peer_ready() || session.terminal();
                }) &&
                !session.terminal(),
            "ack-lineage PEER_READY did not reach the session");
    RenderBridgeAcknowledgement acknowledgement;
    acknowledgement.registry_id = session.registry_id();
    acknowledgement.through_forward_sequence = 2U;
    const RenderTransportEnvelopeEncodeResult encoded =
        EncodeRenderBridgeAcknowledgementFrame(2U, acknowledgement);
    Require(encoded.ok() &&
                WriteNative(fixture.game_inbound.write_handle,
                            encoded.bytes.data(), encoded.bytes.size()),
            "future cumulative ACK fixture did not reach the session");
    Require(WaitUntil([&session]() { return session.terminal(); }) &&
                session.terminal_cause() ==
                    RendererOgre14GameHostSessionStatus::
                        FAILED_REVERSE_LINEAGE &&
                session.last_acknowledged_forward_sequence() == 0U,
            "ACK advanced beyond written forward lineage");
    (void)session.Close();
  }
}

void TestAcknowledgedSceneCanBePresentedByALaterAck() {
  BridgeFixture fixture;
  RendererOgre14GameHostSession session(fixture.bridge);
  Require(session.Start().ok(), "delayed-presentation session did not start");
  const std::uint64_t registry_id = session.registry_id();

  RenderBridgeControl ready;
  ready.kind = RenderBridgeControlKind::PEER_READY;
  ready.registry_id = registry_id;
  ready.command_id = 1U;
  ready.surface = ActiveSurface(1U, 800U, 600U, 1600U, 1200U);
  const RenderTransportEnvelopeEncodeResult ready_frame =
      EncodeRenderBridgeControlFrame(1U, ready);
  Require(ready_frame.ok() &&
              WriteNative(fixture.game_inbound.write_handle,
                          ready_frame.bytes.data(), ready_frame.bytes.size()) &&
              WaitUntil([&session]() {
                return session.peer_ready() || session.terminal();
              }) &&
              !session.terminal(),
          "delayed-presentation PEER_READY did not reach the session");

  Require(session.Submit(EmptyAssetSnapshot(registry_id)).ok(),
          "delayed-presentation asset did not enqueue");
  const std::shared_ptr<const SceneSnapshot> scene =
      EmptyScene(registry_id, 1U, 1U, 41U);
  Require(session.PostPhysics(*scene, Camera(1600U, 1200U)).ok(),
          "delayed-presentation scene did not enqueue");
  Require(session.Submit(EmptyAssetSnapshot(registry_id, 2U)).ok(),
          "delayed-presentation following asset did not enqueue");

  RenderTransportStreamDecoder stream(
      kRenderTransportStreamAbsoluteMaximumPayloadBytes);
  RenderTransportStreamFrameResult frames[3U];
  for (auto &frame : frames) {
    Require(ReadFrame(fixture.game_outbound.read_handle, stream, frame),
            "delayed-presentation forward frame did not arrive");
  }
  Require(frames[0U].sequence == 1U && frames[1U].sequence == 2U &&
              frames[1U].kind == RenderTransportMessageKind::
                                      SCENE_SNAPSHOT_V4_CAMERA_V2 &&
              frames[2U].sequence == 3U &&
              WaitUntil([&session]() {
                return session.last_written_forward_sequence() == 3U ||
                       session.terminal();
              }) &&
              !session.terminal(),
          "delayed-presentation forward lineage changed");

  RenderBridgeAcknowledgement consumed_scene;
  consumed_scene.registry_id = registry_id;
  consumed_scene.through_forward_sequence = 2U;
  const RenderTransportEnvelopeEncodeResult consumed_frame =
      EncodeRenderBridgeAcknowledgementFrame(2U, consumed_scene);
  Require(consumed_frame.ok() &&
              WriteNative(fixture.game_inbound.write_handle,
                          consumed_frame.bytes.data(),
                          consumed_frame.bytes.size()) &&
              WaitUntil([&session]() {
                return session.last_acknowledged_forward_sequence() == 2U ||
                       session.terminal();
              }) &&
              !session.terminal(),
          "scene consumption without presentation was rejected");

  RenderBridgeAcknowledgement presented_scene;
  presented_scene.registry_id = registry_id;
  presented_scene.through_forward_sequence = 3U;
  presented_scene.presented_scene_sequence = 2U;
  presented_scene.presented_snapshot_id = 1U;
  const RenderTransportEnvelopeEncodeResult presented_frame =
      EncodeRenderBridgeAcknowledgementFrame(3U, presented_scene);
  Require(presented_frame.ok() &&
              WriteNative(fixture.game_inbound.write_handle,
                          presented_frame.bytes.data(),
                          presented_frame.bytes.size()) &&
              WaitUntil([&session]() {
                return session.last_acknowledged_forward_sequence() == 3U ||
                       session.terminal();
              }) &&
              !session.terminal(),
          "later ACK could not prove an already-consumed scene presentation");
  (void)session.Close();
}

void TestSurfaceReadinessSuspendResumeAndStaleRevision() {
  BridgeFixture fixture;
  RendererOgre14GameHostSession session(fixture.bridge);
  Require(session.Start().ok(), "surface lifecycle session did not start");
  const std::uint64_t registry_id = session.registry_id();
  Require(session.Submit(EmptyAssetSnapshot(registry_id)).ok(),
          "surface lifecycle asset did not enqueue");
  const std::shared_ptr<const SceneSnapshot> scene1 =
      EmptyScene(registry_id, 1U, 1U, 41U);
  Require(session.PostPhysics(*scene1, Camera(1600U, 1200U)).status ==
              RendererOgre14GameHostSessionStatus::REJECTED_SURFACE_STATE &&
              session.next_forward_sequence() == 2U,
          "scene published before initial surface readiness");

  RenderBridgeControl control;
  control.kind = RenderBridgeControlKind::PEER_READY;
  control.registry_id = registry_id;
  control.command_id = 1U;
  control.surface = ActiveSurface(10U, 800U, 600U, 1600U, 1200U);
  RenderTransportEnvelopeEncodeResult encoded =
      EncodeRenderBridgeControlFrame(1U, control);
  Require(encoded.ok() &&
              WriteNative(fixture.game_inbound.write_handle,
                          encoded.bytes.data(), encoded.bytes.size()) &&
              WaitUntil([&session]() {
                return session.current_surface_state().surface_revision ==
                           10U ||
                       session.terminal();
              }) &&
              !session.terminal(),
          "active PEER_READY was not consumed");
  const RendererOgre14GameHostSessionResult posted1 =
      session.PostPhysics(*scene1, Camera(1600U, 1200U));
  Require(posted1.ok() && posted1.surface_revision == 10U &&
              posted1.forward_sequence == 2U,
          "active ready surface did not admit matching scene pixels");

  control.kind = RenderBridgeControlKind::SURFACE_CHANGED;
  control.command_id = 2U;
  control.surface.surface_revision = 11U;
  control.surface.logical_width = 1000U;
  control.surface.logical_height = 700U;
  control.surface.drawable_width = 0U;
  control.surface.drawable_height = 0U;
  control.surface.suspended = true;
  encoded = EncodeRenderBridgeControlFrame(2U, control);
  Require(encoded.ok() &&
              WriteNative(fixture.game_inbound.write_handle,
                          encoded.bytes.data(), encoded.bytes.size()) &&
              WaitUntil([&session]() {
                return session.current_surface_state().surface_revision ==
                           11U ||
                       session.terminal();
              }) &&
              session.current_surface_state().suspended &&
              !session.terminal(),
          "suspended 0x0 surface revision was not consumed");
  const std::shared_ptr<const SceneSnapshot> scene2 =
      EmptyScene(registry_id, 1U, 2U, 83U);
  Require(session.PostPhysics(*scene2, Camera(1600U, 1200U)).status ==
              RendererOgre14GameHostSessionStatus::REJECTED_SURFACE_STATE &&
              session.next_forward_sequence() == 3U,
          "scene published while presentation was suspended");

  control.command_id = 3U;
  control.surface = ActiveSurface(12U, 1000U, 700U, 2000U, 1400U);
  encoded = EncodeRenderBridgeControlFrame(3U, control);
  Require(encoded.ok() &&
              WriteNative(fixture.game_inbound.write_handle,
                          encoded.bytes.data(), encoded.bytes.size()) &&
              WaitUntil([&session]() {
                return session.current_surface_state().surface_revision ==
                           12U ||
                       session.terminal();
              }) &&
              !session.current_surface_state().suspended &&
              !session.terminal(),
          "active resume surface revision was not consumed");
  Require(session.PostPhysics(*scene2, Camera(1600U, 1200U)).status ==
              RendererOgre14GameHostSessionStatus::REJECTED_SURFACE_STATE &&
              session.next_forward_sequence() == 3U,
          "stale drawable dimensions published after resize");
  const RendererOgre14GameHostSessionResult posted2 =
      session.PostPhysics(*scene2, Camera(2000U, 1400U));
  Require(posted2.ok() && posted2.surface_revision == 12U &&
              posted2.forward_sequence == 3U,
          "resumed drawable dimensions did not admit the next scene");

  control.command_id = 4U;
  encoded = EncodeRenderBridgeControlFrame(4U, control);
  Require(encoded.ok() &&
              WriteNative(fixture.game_inbound.write_handle,
                          encoded.bytes.data(), encoded.bytes.size()) &&
              WaitUntil([&session]() { return session.terminal(); }) &&
              session.terminal_cause() ==
                  RendererOgre14GameHostSessionStatus::
                      FAILED_REVERSE_LINEAGE,
          "duplicate surface revision did not fail closed");
  (void)session.Close();
}

void TestQueuedSceneRetiresAcrossSurfaceBarrier() {
  BridgeFixture fixture;
  RendererOgre14GameHostSession session(fixture.bridge);
  RendererOgre14GameHostSessionConfig config;
  config.maximum_forward_messages = 8U;
  config.maximum_unacknowledged_forward_messages = 8U;
  config.maximum_reverse_messages = 1U;
  Require(session.Start(config).ok(), "surface-barrier session did not start");
  const std::uint64_t registry_id = session.registry_id();

  RenderBridgeControl control;
  control.kind = RenderBridgeControlKind::PEER_READY;
  control.registry_id = registry_id;
  control.command_id = 1U;
  control.surface = ActiveSurface(20U, 800U, 600U, 1600U, 1200U);
  RenderTransportEnvelopeEncodeResult encoded =
      EncodeRenderBridgeControlFrame(1U, control);
  Require(encoded.ok() &&
              WriteNative(fixture.game_inbound.write_handle,
                          encoded.bytes.data(), encoded.bytes.size()) &&
              WaitUntil([&session]() {
                return (session.peer_ready() &&
                        session.queued_reverse_messages() == 1U) ||
                       session.terminal();
              }) &&
              !session.terminal() && session.PollControl(),
          "surface-barrier PEER_READY did not reach the game loop");

  Require(session.Submit(EmptyAssetSnapshot(registry_id)).ok(),
          "surface-barrier asset did not enqueue");
  RenderTransportStreamDecoder stream(
      kRenderTransportStreamAbsoluteMaximumPayloadBytes);
  RenderTransportStreamFrameResult asset_frame;
  Require(ReadFrame(fixture.game_outbound.read_handle, stream, asset_frame) &&
              asset_frame.sequence == 1U &&
              WaitUntil([&session]() {
                return session.last_written_forward_sequence() == 1U ||
                       session.terminal();
              }) &&
              !session.terminal(),
          "surface-barrier asset did not cross the forward stream");

  control.kind = RenderBridgeControlKind::HEARTBEAT;
  control.command_id = 2U;
  control.surface = {};
  encoded = EncodeRenderBridgeControlFrame(2U, control);
  Require(encoded.ok() &&
              WriteNative(fixture.game_inbound.write_handle,
                          encoded.bytes.data(), encoded.bytes.size()) &&
              WaitUntil([&session]() {
                return session.queued_reverse_messages() == 1U ||
                       session.terminal();
              }) &&
              !session.terminal(),
          "surface-barrier heartbeat did not saturate reverse delivery");

  const std::shared_ptr<const SceneSnapshot> stale_scene =
      EmptyScene(registry_id, 1U, 1U, 41U);
  Require(session.PostPhysics(*stale_scene, Camera(1600U, 1200U)).ok(),
          "pre-resize scene did not enter exact forward lineage");
  control.kind = RenderBridgeControlKind::SURFACE_CHANGED;
  control.command_id = 3U;
  control.surface = ActiveSurface(21U, 1000U, 700U, 2000U, 1400U);
  encoded = EncodeRenderBridgeControlFrame(3U, control);
  Require(encoded.ok() &&
              WriteNative(fixture.game_inbound.write_handle,
                          encoded.bytes.data(), encoded.bytes.size()) &&
              session.PollControl(),
          "surface-barrier resize fixture did not reach the reverse pipe");
  Require(WaitUntil([&session]() {
            return session.current_surface_state().surface_revision == 21U ||
                   session.terminal();
          }) &&
              !session.terminal() &&
              session.queued_reverse_messages() == 1U &&
              session.last_written_forward_sequence() == 1U,
          "queued stale scene crossed before SURFACE_CHANGED delivery");
  const RendererOgre14GameHostPollResult resized = session.PollControl();
  Require(resized && resized.message.control.kind ==
                         RenderBridgeControlKind::SURFACE_CHANGED,
          "surface-barrier resize was not delivered to the game loop");

  RenderTransportStreamFrameResult retired_frame;
  Require(ReadFrame(fixture.game_outbound.read_handle, stream, retired_frame) &&
              retired_frame.sequence == 2U &&
              retired_frame.kind == RenderTransportMessageKind::
                                        SCENE_SNAPSHOT_V4_CAMERA_V2 &&
              WaitUntil([&session]() {
                return session.last_written_forward_sequence() == 2U ||
                       session.terminal();
              }) &&
              !session.terminal(),
          "already-sequenced stale scene was not preserved for retirement");

  RenderBridgeAcknowledgement retired;
  retired.registry_id = registry_id;
  retired.through_forward_sequence = 2U;
  const RenderTransportEnvelopeEncodeResult retired_ack =
      EncodeRenderBridgeAcknowledgementFrame(4U, retired);
  Require(retired_ack.ok() &&
              WriteNative(fixture.game_inbound.write_handle,
                          retired_ack.bytes.data(), retired_ack.bytes.size()) &&
              WaitUntil([&session]() {
                return session.last_acknowledged_forward_sequence() == 2U ||
                       session.terminal();
              }) &&
              !session.terminal() && session.PollAcknowledgement(),
          "stale scene could not retire without becoming presented");

  const std::shared_ptr<const SceneSnapshot> resized_scene =
      EmptyScene(registry_id, 1U, 2U, 83U);
  Require(session.PostPhysics(*resized_scene, Camera(1600U, 1200U)).status ==
                  RendererOgre14GameHostSessionStatus::REJECTED_SURFACE_STATE &&
              session.PostPhysics(*resized_scene, Camera(2000U, 1400U)).ok(),
          "post-resize scene admission did not require current drawable pixels");
  RenderTransportStreamFrameResult resized_frame;
  Require(ReadFrame(fixture.game_outbound.read_handle, stream, resized_frame) &&
              resized_frame.sequence == 3U &&
              resized_frame.kind == RenderTransportMessageKind::
                                        SCENE_SNAPSHOT_V4_CAMERA_V2,
          "resized scene did not continue exact forward lineage");
  (void)session.Close();
}

void TestProductLifecycleRetainsPendingFrameAcrossBackpressureAndResize() {
  BridgeFixture fixture;
  ProductInputTarget input_target;
  RendererOgre14ProductSession product(fixture.bridge, input_target);
  RendererOgre14ProductSessionConfig config;
  config.host.maximum_forward_messages = 1U;
  config.host.maximum_unacknowledged_forward_messages = 1U;
  config.host.maximum_reverse_messages = 8U;
  config.shutdown_drain_timeout_milliseconds = 1000U;
  Require(product.Start(config).ok(), "product lifecycle did not start");
  const std::uint64_t registry_id = product.host().registry_id();

  RenderBridgeControl ready;
  ready.kind = RenderBridgeControlKind::PEER_READY;
  ready.registry_id = registry_id;
  ready.command_id = 1U;
  ready.surface = ActiveSurface(10U, 800U, 600U, 1600U, 1200U);
  RenderTransportEnvelopeEncodeResult encoded_control =
      EncodeRenderBridgeControlFrame(1U, ready);
  Require(encoded_control.ok() &&
              WriteNative(fixture.game_inbound.write_handle,
                          encoded_control.bytes.data(),
                          encoded_control.bytes.size()) &&
              WaitUntil([&product]() {
                return product.host().queued_reverse_messages() == 1U ||
                       product.host().terminal();
              }) &&
              product.PumpReverse().ok() && product.host().peer_ready(),
          "product PEER_READY was not drained on the game thread");

  const InputEventTransportEncodeResult input =
      EncodeInputEventTransportFrame(2U, InputBatch());
  Require(input.ok() &&
              WriteNative(fixture.game_inbound.write_handle,
                          input.bytes.data(), input.bytes.size()) &&
              WaitUntil([&product]() {
                return product.host().queued_reverse_messages() == 1U ||
                       product.host().terminal();
              }),
          "product input fixture did not reach reverse delivery");
  const RendererOgre14ProductSessionResult input_drain =
      product.PumpReverse();
  Require(input_drain.ok() && input_drain.input_batches == 1U &&
              input_target.activations == 1 &&
              input_target.reconciliations == 1 && input_target.focused &&
              input_target.applied_through == 1U,
          "product did not apply decoded renderer input exactly once");

  ProductSceneSource source;
  const RendererOgre14ProductSessionResult first =
      product.PostUpdatedScene(source);
  Require(first.status ==
                  RendererOgre14ProductSessionStatus::PENDING_BACKPRESSURE &&
              first.pending_frame && source.captures == 1U &&
              product.has_pending_frame(),
          "asset-first bounded lineage did not retain the produced scene");
  for (int retry = 0; retry < 3; ++retry) {
    const RendererOgre14ProductSessionResult pending =
        product.PostUpdatedScene(source);
    Require(pending.status == RendererOgre14ProductSessionStatus::
                                  PENDING_BACKPRESSURE &&
                source.captures == 1U,
            "backpressure advanced or recaptured producer lineage");
  }

  RenderBridgeControl resized;
  resized.kind = RenderBridgeControlKind::SURFACE_CHANGED;
  resized.registry_id = registry_id;
  resized.command_id = 2U;
  resized.surface = ActiveSurface(11U, 1000U, 700U, 2000U, 1400U);
  encoded_control = EncodeRenderBridgeControlFrame(3U, resized);
  Require(encoded_control.ok() &&
              WriteNative(fixture.game_inbound.write_handle,
                          encoded_control.bytes.data(),
                          encoded_control.bytes.size()) &&
              WaitUntil([&product]() {
                return product.host().current_surface_state().surface_revision ==
                           11U ||
                       product.host().terminal();
              }) &&
              product.PumpReverse().ok(),
          "resize did not overtake the pending captured frame");

  RenderTransportStreamDecoder stream(
      kRenderTransportStreamAbsoluteMaximumPayloadBytes);
  RenderTransportStreamFrameResult asset_frame;
  Require(ReadFrame(fixture.game_outbound.read_handle, stream, asset_frame) &&
              asset_frame.sequence == 1U &&
              asset_frame.kind ==
                  RenderTransportMessageKind::RENDER_ASSET_DELTA_V1,
          "product initial asset frame did not preserve forward sequence");
  RenderBridgeAcknowledgement asset_ack;
  asset_ack.registry_id = registry_id;
  asset_ack.through_forward_sequence = 1U;
  const RenderTransportEnvelopeEncodeResult encoded_asset_ack =
      EncodeRenderBridgeAcknowledgementFrame(4U, asset_ack);
  Require(encoded_asset_ack.ok() &&
              WriteNative(fixture.game_inbound.write_handle,
                          encoded_asset_ack.bytes.data(),
                          encoded_asset_ack.bytes.size()) &&
              WaitUntil([&product]() {
                return product.host().last_acknowledged_forward_sequence() ==
                           1U ||
                       product.host().terminal();
              }) &&
              product.PumpReverse().ok(),
          "product asset ACK did not release bounded lineage");

  const RendererOgre14ProductSessionResult posted =
      product.PostUpdatedScene(source);
  Require(posted.status == RendererOgre14ProductSessionStatus::FRAME_QUEUED &&
              posted.accepted && !posted.pending_frame &&
              source.captures == 1U && !product.has_pending_frame(),
          "pre-resize immutable scene was not sequenced for child retirement");
  RenderTransportStreamFrameResult scene_frame;
  Require(ReadFrame(fixture.game_outbound.read_handle, stream, scene_frame) &&
              scene_frame.sequence == 2U &&
              scene_frame.kind == RenderTransportMessageKind::
                                      SCENE_SNAPSHOT_V4_CAMERA_V2,
          "retained scene did not follow its asset without a lineage gap");
  SceneSnapshotTransportDecoder scene_decoder(2U);
  const SceneSnapshotTransportDecodeResult decoded_scene =
      scene_decoder.Accept(scene_frame.bytes);
  Require(decoded_scene.ok() &&
              decoded_scene.message->scene_snapshot()
                      ->dynamic_mesh_updates().size() == 1U &&
              decoded_scene.message->scene_snapshot()
                      ->dynamic_mesh_updates().front().update_sequence == 1U &&
              decoded_scene.message->scene_snapshot()
                      ->dynamic_mesh_updates().front().instance_id == 150U,
          "backpressure/resize changed or dropped the immutable deformable "
          "update");

  RenderBridgeAcknowledgement scene_ack;
  scene_ack.registry_id = registry_id;
  scene_ack.through_forward_sequence = 2U;
  const RenderTransportEnvelopeEncodeResult encoded_scene_ack =
      EncodeRenderBridgeAcknowledgementFrame(5U, scene_ack);
  Require(encoded_scene_ack.ok() &&
              WriteNative(fixture.game_inbound.write_handle,
                          encoded_scene_ack.bytes.data(),
                          encoded_scene_ack.bytes.size()) &&
              WaitUntil([&product]() {
                return product.host().last_acknowledged_forward_sequence() ==
                           2U ||
                       product.host().terminal();
              }) &&
              product.PumpReverse().ok(),
          "retired product scene was not cumulatively acknowledged");

  NativeHandle reverse_writer = fixture.game_inbound.write_handle;
  NativeHandle forward_reader = fixture.game_outbound.read_handle;
  std::atomic<bool> peer_saw_eof{false};
  std::thread peer([reverse_writer, forward_reader, &peer_saw_eof]() mutable {
    std::uint8_t byte = 0U;
    for (;;) {
      std::size_t transferred = 0U;
      if (!ReadNative(forward_reader, &byte, 1U, transferred))
        break;
      if (transferred == 0U) {
        peer_saw_eof = true;
        break;
      }
    }
    CloseNative(reverse_writer);
  });
  const RendererOgre14ProductSessionResult shutdown = product.Shutdown();
  peer.join();
  fixture.game_inbound.write_handle = kInvalidNativeHandle;
  Require(shutdown.status == RendererOgre14ProductSessionStatus::CLOSED &&
              shutdown.ok() && peer_saw_eof &&
              product.host().shutdown_complete(),
          "product shutdown did not drain, half-close, and join in order");
}

void TestProductStartFailsClosedBeforeInputAuthority() {
  BridgeFixture fixture;
  ProductInputTarget input_target;
  input_target.activation_succeeds = false;
  RendererOgre14ProductSession product(fixture.bridge, input_target);
  RendererOgre14ProductSessionConfig config;
  config.host.maximum_forward_messages = 1U;
  config.host.maximum_unacknowledged_forward_messages = 1U;
  const RendererOgre14ProductSessionResult result = product.Start(config);
  Require(result.status == RendererOgre14ProductSessionStatus::FAILED_INPUT &&
              result.input_status ==
                  RendererOgre14InputApplyStatus::FAILED_TARGET &&
              result.terminal && !result.accepted &&
              input_target.activations == 1 && !product.active(),
          "product startup crossed failed input authority");
}

void TestAbruptPeerTeardownIsTerminal() {
  BridgeFixture fixture;
  RendererOgre14GameHostSession session(fixture.bridge);
  Require(session.Start().ok(), "teardown session did not start");
  CloseNative(fixture.game_outbound.read_handle);
  CloseNative(fixture.game_inbound.write_handle);
  Require(WaitUntil([&session]() { return session.terminal(); }) &&
              session.terminal_cause() ==
                  RendererOgre14GameHostSessionStatus::PEER_CLOSED,
          "abrupt peer teardown did not become terminal");
  const RendererOgre14GameHostSessionResult closed = session.Close();
  Require(closed.terminal &&
              closed.terminal_cause ==
                  RendererOgre14GameHostSessionStatus::PEER_CLOSED,
          "terminal peer cause was not stable through close");
}

} // namespace

int main() {
#if !defined(_WIN32)
  Require(::signal(SIGPIPE, SIG_IGN) != SIG_ERR,
          "could not ignore test-peer SIGPIPE");
#endif
  Require(CurrentPlatform() != HostRenderPlatform::UNKNOWN,
          "test host platform is unsupported");
  TestStatusConfigurationAndLineageRejections();
  TestReverseCapacityPausesForwardWrites();
  TestCloseIsBoundedWhenPeerDoesNotDrainForwardPipe();
  TestTwoEndedAssetSceneInputAckControlAndHalfClose();
  TestReverseControlAndAcknowledgementLineageFailuresAreTerminal();
  TestAcknowledgedSceneCanBePresentedByALaterAck();
  TestSurfaceReadinessSuspendResumeAndStaleRevision();
  TestQueuedSceneRetiresAcrossSurfaceBarrier();
  TestProductStartFailsClosedBeforeInputAuthority();
  TestProductLifecycleRetainsPendingFrameAcrossBackpressureAndResize();
  TestAbruptPeerTeardownIsTerminal();
  return EXIT_SUCCESS;
}
