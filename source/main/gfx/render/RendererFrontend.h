/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Abstract frontend, native interop, and native RT contracts.

#pragma once

#include "MaterialDescriptor.h"
#include "RenderAssetRegistry.h"
#include "RenderFrame.h"
#include "RenderResourceDescriptors.h"

#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace RoR::Render {

constexpr std::uint32_t kRendererFrontendContractVersion = 2U;
/// Native image exchange evolves independently from the established geometry
/// contract. A caller must reject any image request/export whose version is
/// not exactly this value; native handles are process-local, not serializable.
constexpr std::uint32_t kNativeImageInteropContractVersion = 1U;
constexpr std::uint64_t kInfiniteRenderTimeoutNanoseconds =
    (std::numeric_limits<std::uint64_t>::max)();

enum class RendererFrontendKind : std::uint8_t {
  OGRE14 = 0,
  OGRE_NEXT = 1,
  CUSTOM = 2,
};

/// Raster API used to produce ordinary frontend frames. This is deliberately
/// independent of NativeGraphicsApi: a D3D11 frontend can render normally yet
/// must report native interop NONE when it cannot share a D3D12/DXR device.
enum class RasterGraphicsApi : std::uint8_t {
  NONE = 0,
  OPENGL = 1,
  METAL = 2,
  DIRECT3D11 = 3,
  DIRECT3D12 = 4,
  VULKAN = 5,
};

enum class NativeGraphicsApi : std::uint8_t {
  NONE = 0,
  METAL = 1,
  DIRECT3D12 = 2,
  VULKAN = 3,
};

enum class NativeWindowSystem : std::uint8_t {
  NONE = 0,
  COCOA = 1,
  WINDOWS = 2,
  X11 = 3,
  WAYLAND = 4,
};

enum class RenderOperationCode : std::uint8_t {
  OK = 0,
  INVALID_ARGUMENT,
  UNSUPPORTED,
  NOT_INITIALIZED,
  RESOURCE_STALE,
  OUT_OF_MEMORY,
  OUTSTANDING_LEASES,
  TIMEOUT,
  DEVICE_LOST,
  BACKEND_FAILURE,
};

struct RenderOperationResult {
  RenderOperationCode code = RenderOperationCode::OK;
  std::string detail;

  [[nodiscard]] bool ok() const noexcept {
    return code == RenderOperationCode::OK;
  }
  explicit operator bool() const noexcept { return ok(); }

  static RenderOperationResult Success() { return {}; }
  static RenderOperationResult Failure(RenderOperationCode failure_code,
                                       std::string failure_detail) {
    RenderOperationResult result;
    result.code = failure_code;
    result.detail = std::move(failure_detail);
    return result;
  }
};

enum class NativeObjectKind : std::uint8_t {
  INVALID = 0,
  DEVICE,
  PHYSICAL_DEVICE,
  QUEUE,
  BUFFER,
  /// MTLTexture, ID3D12Resource image, or VkImage. The token never owns the
  /// image; its matching NativeImageExport lease controls the borrow.
  IMAGE,
  /// Valued cross-queue primitive: MTLSharedEvent, ID3D12Fence, or a Vulkan
  /// timeline VkSemaphore. It never represents MTLFence or VkFence.
  TIMELINE_SYNC,
};

/// Opaque process-local object identity. Adapters alone may decode `value`.
/// `context_id` binds the token to one NativeContextExport; the generation
/// changes whenever an adapter recycles the native identity.
struct NativeObjectToken {
  NativeGraphicsApi api = NativeGraphicsApi::NONE;
  NativeObjectKind kind = NativeObjectKind::INVALID;
  std::uint64_t context_id = 0U;
  std::uint64_t value = 0U;
  std::uint64_t generation = 0U;

  [[nodiscard]] bool valid() const noexcept {
    const bool concrete_api = api == NativeGraphicsApi::METAL ||
                              api == NativeGraphicsApi::DIRECT3D12 ||
                              api == NativeGraphicsApi::VULKAN;
    const bool concrete_kind = kind == NativeObjectKind::DEVICE ||
                               kind == NativeObjectKind::PHYSICAL_DEVICE ||
                               kind == NativeObjectKind::QUEUE ||
                               kind == NativeObjectKind::BUFFER ||
                               kind == NativeObjectKind::IMAGE ||
                               kind == NativeObjectKind::TIMELINE_SYNC;
    return concrete_api && concrete_kind && context_id != 0U && value != 0U &&
           generation != 0U;
  }
};

/// Borrowed native-window identity. `connection` carries X11 Display* or
/// Wayland wl_display*; `surface` carries X11 Window, wl_surface*, NSView*, or
/// HWND. Cocoa and Windows require only `surface`. The producer retains the
/// initialized values until a successful replacement UpdateSurface() returns
/// or Shutdown() returns.
struct NativeWindowHandle {
  NativeWindowSystem system = NativeWindowSystem::NONE;
  std::uint64_t connection = 0U;
  std::uint64_t surface = 0U;
  std::uint64_t generation = 0U;

  [[nodiscard]] bool valid() const noexcept {
    if (generation == 0U || surface == 0U) {
      return false;
    }
    if (system == NativeWindowSystem::COCOA ||
        system == NativeWindowSystem::WINDOWS) {
      return connection == 0U;
    }
    if (system == NativeWindowSystem::X11 ||
        system == NativeWindowSystem::WAYLAND) {
      return connection != 0U;
    }
    return false;
  }
};

struct FrontendInitializationRequest {
  std::uint32_t version = kRendererFrontendContractVersion;
  /// Initial presentation-surface identity. Later updates must be strictly
  /// greater. Headless frontends retain this revision for offscreen resizes.
  std::uint64_t initial_surface_revision = 1U;
  NativeWindowHandle window;
  /// Initial drawable pixel extent, never logical points.
  std::uint32_t initial_width = 0U;
  std::uint32_t initial_height = 0U;
  /// Drawable pixels per host logical unit on each axis.
  Float2 initial_content_scale{1.0F, 1.0F};
  std::uint32_t maximum_frames_in_flight = 2U;
  bool headless = false;
  bool vertical_sync = true;
};

/// Post-platform-event surface state. The host completes Cocoa/Win32/X11 or
/// Wayland configure/ack handling first, then submits revisions strictly
/// greater than the current revision. Replacing a native surface increments
/// NativeWindowHandle's generation. A suspended/minimized surface has a zero
/// extent. A successful update borrows its window until the next successful
/// replacement or Shutdown; after that call returns, the host may destroy the
/// superseded handle. A failed update leaves the previous borrow and state
/// unchanged.
struct FrontendSurfaceUpdate {
  std::uint32_t version = kRendererFrontendContractVersion;
  std::uint64_t surface_revision = 0U;
  NativeWindowHandle window;
  std::uint32_t pixel_width = 0U;
  std::uint32_t pixel_height = 0U;
  Float2 content_scale{1.0F, 1.0F};
  bool suspended = false;
};

/// Normalized capabilities used by policy and acceptance gates.
///
/// Ray-tracing fields default false and are independent proofs. In particular,
/// API or probe support never implies geometry interop readiness.
struct FrontendCapabilityReport {
  std::uint32_t version = kRendererFrontendContractVersion;
  std::uint32_t scene_snapshot_version = kSceneSnapshotVersion;
  std::uint32_t asset_registry_contract_version =
      kRenderAssetRegistryContractVersion;
  RendererFrontendKind frontend_kind = RendererFrontendKind::CUSTOM;
  RasterGraphicsApi raster_api = RasterGraphicsApi::NONE;
  /// Native API exported for same-device interop. A concrete value must match
  /// the Metal, D3D12, or Vulkan raster API exactly; D3D11/OpenGL use NONE.
  NativeGraphicsApi native_api = NativeGraphicsApi::NONE;
  std::string frontend_name;
  std::string frontend_version;
  std::uint32_t maximum_texture_dimension_2d = 0U;
  std::uint32_t maximum_views = 0U;
  std::uint32_t maximum_frames_in_flight = 0U;
  FrameOutputMask supported_outputs = FrameOutputMask::NONE;
  bool raster_ready = false;
  bool supports_hdr_output = false;
  bool supports_compute = false;
  bool supports_async_compute = false;
  bool supports_dynamic_mesh_updates = false;
  bool supports_particle_events = false;
  bool supports_native_interop = false;
  bool supports_native_ray_tracing_api = false;
  bool native_ray_tracing_hardware_accelerated = false;
  bool native_ray_tracing_probe_passed = false;
  bool native_ray_tracing_geometry_interop_ready = false;
};

struct NativeInteropCapabilityReport {
  std::uint32_t version = kRendererFrontendContractVersion;
  NativeGraphicsApi native_api = NativeGraphicsApi::NONE;
  bool exports_native_context = false;
  bool exports_vertex_buffers = false;
  bool exports_index_buffers = false;
  bool exports_deformed_meshes = false;
  bool provides_explicit_frame_synchronization = false;
  bool preserves_resource_generations = false;
  bool geometry_interop_proven = false;
  /// Exact frontend render targets can be borrowed as native images. Version
  /// 1 exposes colour only and requires the canonical read/write/copy usage.
  bool exports_color_images = false;
  bool supports_read_write_color_images = false;
};

struct NativeRayTracingCapabilityReport {
  std::uint32_t version = kRendererFrontendContractVersion;
  NativeGraphicsApi native_api = NativeGraphicsApi::NONE;
  bool backend_compiled = false;
  bool api_supported = false;
  bool hardware_accelerated = false;
  bool dispatch_readback_probe_passed = false;
  bool geometry_interop_ready = false;
  /// Becomes true only after Render() has produced and read back a real
  /// view-dependent native image contribution.
  bool view_dependent_output_ready = false;
  /// Becomes true only after that contribution was GPU-composited into the
  /// exact frontend-owned HDR colour target.
  bool hybrid_composite_ready = false;
  std::uint32_t maximum_instances = 0U;
};

class NativeRenderInterop;
class INativeRayTracingBackend;
class IRendererFrontend;

enum class NativeIndexFormat : std::uint8_t {
  UINT16 = 0,
  UINT32 = 1,
};

enum class NativeVertexPositionFormat : std::uint8_t {
  FLOAT32_XYZ = 0,
};

enum class NativeGeometryBufferState : std::uint8_t {
  INVALID = 0,
  /// Read-only vertex/index input suitable for acceleration-structure builds.
  READ_ONLY_ACCELERATION_STRUCTURE_BUILD = 1,
};

/// Canonical image usage exported by version 1. The frontend first renders to
/// the image, external work reads and writes it as shader storage, and either
/// side may copy it to a readback buffer while the lease is live.
enum class NativeImageUsage : std::uint8_t {
  INVALID = 0,
  COLOR_ATTACHMENT_SHADER_READ_WRITE_COPY_SOURCE = 1,
};

/// Cross-API state at the queue handoff. Metal has no explicit layout here;
/// Vulkan/D3D12 adapters must map this to their general/UAV equivalent and
/// restore the same state before signaling external completion.
enum class NativeImageState : std::uint8_t {
  INVALID = 0,
  GENERAL_READ_WRITE = 1,
};

constexpr std::uint32_t kInvalidNativeQueueFamily =
    (std::numeric_limits<std::uint32_t>::max)();

/// Borrowed device/queue identities required by an external renderer to build
/// and dispatch work against exported geometry. All tokens remain valid while
/// the owning NativeRenderInterop is initialized and until external frames and
/// geometry exports using them have been released.
struct NativeContextExport {
  std::uint32_t version = kRendererFrontendContractVersion;
  NativeGraphicsApi native_api = NativeGraphicsApi::NONE;
  /// Process-unique identity that is never reused for another initialized
  /// device context during this process.
  std::uint64_t context_id = 0U;
  NativeObjectToken device;
  NativeObjectToken physical_device;
  NativeObjectToken graphics_queue;
  NativeObjectToken compute_queue;
  std::uint32_t graphics_queue_family = kInvalidNativeQueueFamily;
  std::uint32_t compute_queue_family = kInvalidNativeQueueFamily;
};

struct NativeBufferSlice {
  NativeObjectToken buffer;
  std::uint64_t offset_bytes = 0U;
  std::uint64_t size_bytes = 0U;
  std::uint32_t stride_bytes = 0U;
};

/// Identifies the exact deformable geometry state requested from one submitted
/// scene. `deformation_revision` starts at one for base mesh contents and
/// advances whenever that instance's vertices change.
struct NativeGeometryExportRequest {
  std::uint32_t version = kRendererFrontendContractVersion;
  std::uint64_t frame_id = 0U;
  std::uint64_t snapshot_id = 0U;
  std::uint64_t instance_id = 0U;
  RenderAssetReference mesh;
  std::uint64_t topology_revision = 0U;
  std::uint64_t deformation_revision = 0U;
};

/// Borrowed geometry export. Its exact revision bytes remain immutable through
/// both ReleaseGeometry(export_id) and the matching external frame completion;
/// later deformation uploads rename storage or wait. The owning interop object
/// controls native-token lifetime.
struct NativeGeometryExport {
  std::uint32_t version = kRendererFrontendContractVersion;
  std::uint64_t export_id = 0U;
  std::uint64_t frame_id = 0U;
  std::uint64_t snapshot_id = 0U;
  std::uint64_t instance_id = 0U;
  RenderAssetReference mesh;
  std::uint64_t topology_revision = 0U;
  std::uint64_t deformation_revision = 0U;
  MeshPrimitiveTopology topology = MeshPrimitiveTopology::TRIANGLE_LIST;
  NativeBufferSlice positions;
  NativeBufferSlice indices;
  NativeVertexPositionFormat position_format =
      NativeVertexPositionFormat::FLOAT32_XYZ;
  NativeIndexFormat index_format = NativeIndexFormat::UINT32;
  std::uint32_t vertex_count = 0U;
  std::uint32_t index_count = 0U;
};

/// Identifies one exact frontend output image. The dimensions and format are
/// repeated deliberately so stale resize/reformat requests fail closed before
/// a native token is decoded.
struct NativeImageExportRequest {
  std::uint32_t version = kNativeImageInteropContractVersion;
  std::uint64_t frame_id = 0U;
  std::uint64_t snapshot_id = 0U;
  std::uint64_t view_id = 0U;
  FrameOutputMask output = FrameOutputMask::NONE;
  PixelFormat format = PixelFormat::INVALID;
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
};

/// Borrowed native image. The owning frontend keeps the exact allocation alive
/// and immutable in identity until ReleaseImage(export_id) and the matching
/// external frame have both ended. Version 1 exports one non-MSAA mip/slice.
struct NativeImageExport {
  std::uint32_t version = kNativeImageInteropContractVersion;
  std::uint64_t export_id = 0U;
  std::uint64_t frame_id = 0U;
  std::uint64_t snapshot_id = 0U;
  std::uint64_t view_id = 0U;
  FrameOutputMask output = FrameOutputMask::NONE;
  PixelFormat format = PixelFormat::INVALID;
  NativeImageUsage usage = NativeImageUsage::INVALID;
  NativeObjectToken image;
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  std::uint32_t mip_level = 0U;
  std::uint32_t array_slice = 0U;
  std::uint32_t sample_count = 0U;
};

struct NativeFrameSynchronization {
  std::uint32_t version = kRendererFrontendContractVersion;
  /// The frontend signals frontend_complete_timeline/value only after every
  /// exported buffer tagged with this frame and snapshot contains its final
  /// vertex data. The external backend waits for that value before reading,
  /// then supplies a timeline token/value and signals it after its last read.
  /// EndExternalFrame registers that wait before resources may be recycled.
  ///
  /// Version 1 always uses the exported graphics queue (and its Vulkan family)
  /// as the interop queue. Before signaling frontend completion, the frontend
  /// makes every exported slice visible in
  /// READ_ONLY_ACCELERATION_STRUCTURE_BUILD state. The external backend waits
  /// at its AS-build stage, reads only on this queue, leaves the same state,
  /// and signals external completion. EndExternalFrame waits and restores
  /// frontend-private states before writes. Its live frame lease also
  /// serializes host access to APIs such as VkQueue.
  std::uint64_t frame_id = 0U;
  std::uint64_t snapshot_id = 0U;
  NativeObjectToken interop_queue;
  std::uint32_t interop_queue_family = kInvalidNativeQueueFamily;
  NativeGeometryBufferState frontend_release_state =
      NativeGeometryBufferState::INVALID;
  NativeGeometryBufferState external_return_state =
      NativeGeometryBufferState::INVALID;
  /// INVALID/INVALID means this external frame has geometry only. When an
  /// image lease is present both values are GENERAL_READ_WRITE.
  NativeImageState frontend_image_release_state = NativeImageState::INVALID;
  NativeImageState external_image_return_state = NativeImageState::INVALID;
  NativeObjectToken frontend_complete_timeline;
  std::uint64_t frontend_complete_value = 0U;
  NativeObjectToken external_complete_timeline;
  std::uint64_t external_complete_value = 0U;
};

struct NativeRayTracingFrameRequest {
  std::uint32_t version = kRendererFrontendContractVersion;
  RenderFrameRequest frame;
  std::uint32_t samples_per_pixel = 1U;
  std::uint32_t maximum_bounces = 1U;
  bool denoise = true;
};

constexpr std::uint32_t kMaximumNativeRayTracingSamplesPerPixel = 64U;
constexpr std::uint32_t kMaximumNativeRayTracingBounces = 32U;

/// Fail-closed cross-report geometry-interoperability proof used by readiness
/// policy gates. Validation succeeds only for complete live evidence.
/// Individual capability flags are insufficient: the validator queries the
/// live interop object, acquires its context, and validates concrete deformable
/// geometry, synchronization, native dispatch, and readback evidence. This
/// payload deliberately does not prove material/texture shading parity.
struct NativeGeometryInteropProofSet {
  std::uint32_t version = kRendererFrontendContractVersion;
  FrontendCapabilityReport frontend;
  NativeInteropCapabilityReport interop;
  NativeRayTracingCapabilityReport ray_tracing;
  IRendererFrontend *frontend_object = nullptr;
  NativeRenderInterop *native_interop_object = nullptr;
  INativeRayTracingBackend *native_ray_tracing_backend = nullptr;
  NativeContextExport native_context;
  NativeGeometryExportRequest geometry_request;
  NativeGeometryExport geometry_export;
  NativeFrameSynchronization frame_synchronization;
};

[[nodiscard]] bool
IsKnownRendererFrontendKind(RendererFrontendKind kind) noexcept;
[[nodiscard]] bool IsKnownRasterGraphicsApi(RasterGraphicsApi api) noexcept;
[[nodiscard]] bool IsKnownNativeGraphicsApi(NativeGraphicsApi api) noexcept;
[[nodiscard]] bool
IsKnownNativeWindowSystem(NativeWindowSystem system) noexcept;
[[nodiscard]] bool IsKnownNativeObjectKind(NativeObjectKind kind) noexcept;
[[nodiscard]] bool IsKnownNativeIndexFormat(NativeIndexFormat format) noexcept;
[[nodiscard]] bool
IsKnownNativeVertexPositionFormat(NativeVertexPositionFormat format) noexcept;
[[nodiscard]] bool
IsKnownNativeGeometryBufferState(NativeGeometryBufferState state) noexcept;
[[nodiscard]] bool IsKnownNativeImageUsage(NativeImageUsage usage) noexcept;
[[nodiscard]] bool IsKnownNativeImageState(NativeImageState state) noexcept;
[[nodiscard]] ValidationResult
ValidateFrontendCapabilityReport(const FrontendCapabilityReport &report);
/// Validates one exact frame against the selected live frontend. Unsupported
/// output formats/features fail closed before backend work is submitted.
[[nodiscard]] ValidationResult ValidateRenderFrameRequestAgainstCapabilities(
    const RenderFrameRequest &request,
    const FrontendCapabilityReport &capabilities);
[[nodiscard]] ValidationResult ValidateNativeInteropCapabilityReport(
    const NativeInteropCapabilityReport &report);
[[nodiscard]] ValidationResult ValidateNativeRayTracingCapabilityReport(
    const NativeRayTracingCapabilityReport &report);
[[nodiscard]] ValidationResult ValidateFrontendInitializationRequest(
    const FrontendInitializationRequest &request);
[[nodiscard]] ValidationResult
ValidateFrontendSurfaceUpdate(const FrontendSurfaceUpdate &update,
                              bool headless);
/// Adds stateful monotonicity, native-window generation, and old-surface
/// frame-drain checks required by UpdateSurface(). A uint64-maximum revision is
/// terminal until reinitialize. Keeping the same native identity requires the
/// same generation; replacing it requires a strictly newer generation.
[[nodiscard]] ValidationResult
ValidateFrontendSurfaceTransition(const FrontendSurfaceUpdate &current_surface,
                                  const FrontendSurfaceUpdate &next_surface,
                                  bool headless,
                                  bool prior_surface_frames_complete);
/// Validates a presented request against the exact current windowed surface,
/// including its revision and 1:1 drawable pixel extent.
[[nodiscard]] ValidationResult
ValidateRenderFramePresentation(const RenderFrameRequest &request,
                                const FrontendSurfaceUpdate &current_surface);
[[nodiscard]] ValidationResult
ValidateNativeContextExport(const NativeContextExport &context);
[[nodiscard]] ValidationResult
ValidateNativeGeometryExportRequest(const NativeGeometryExportRequest &request);
[[nodiscard]] ValidationResult
ValidateNativeGeometryExport(const NativeGeometryExport &geometry,
                             NativeGraphicsApi expected_api,
                             std::uint64_t expected_context_id);
[[nodiscard]] ValidationResult
ValidateNativeGeometryExport(const NativeGeometryExportRequest &request,
                             const NativeGeometryExport &geometry,
                             NativeGraphicsApi expected_api,
                             std::uint64_t expected_context_id);
[[nodiscard]] ValidationResult
ValidateNativeImageExportRequest(const NativeImageExportRequest &request);
[[nodiscard]] ValidationResult
ValidateNativeImageExport(const NativeImageExport &image,
                          NativeGraphicsApi expected_api,
                          std::uint64_t expected_context_id);
[[nodiscard]] ValidationResult
ValidateNativeImageExport(const NativeImageExportRequest &request,
                          const NativeImageExport &image,
                          NativeGraphicsApi expected_api,
                          std::uint64_t expected_context_id);
[[nodiscard]] ValidationResult ValidateNativeFrameSynchronization(
    const NativeFrameSynchronization &synchronization,
    const NativeContextExport &context, bool require_external_completion);
[[nodiscard]] ValidationResult ValidateNativeRayTracingFrameRequest(
    const NativeRayTracingFrameRequest &request);
/// Version 1 native RT is an offscreen CPU-readback path. This validator also
/// rejects frontend-owned GPU handles or presentation claims in its output.
[[nodiscard]] ValidationResult
ValidateNativeRayTracingFrameOutput(const NativeRayTracingFrameRequest &request,
                                    const RenderFrameOutput &output);
[[nodiscard]] ValidationResult ValidateNativeGeometryInteropProofSet(
    const NativeGeometryInteropProofSet &proof);

class NativeRenderInterop {
public:
  /// All calls are serialized on the owning frontend thread. The successful
  /// frontend Initialize() call establishes that thread; no method in this
  /// interface is independently thread-safe.
  virtual ~NativeRenderInterop() = default;

  [[nodiscard]] virtual NativeInteropCapabilityReport
  QueryCapabilities() const = 0;
  /// Returns the stable borrowed context used by all subsequent exports.
  virtual RenderOperationResult AcquireContext(NativeContextExport &output) = 0;
  virtual RenderOperationResult
  AcquireGeometry(const NativeGeometryExportRequest &request,
                  NativeGeometryExport &output) = 0;
  virtual RenderOperationResult
  AcquireImage(const NativeImageExportRequest &request,
               NativeImageExport &output) = 0;
  virtual RenderOperationResult
  BeginExternalFrame(std::uint64_t frame_id, std::uint64_t snapshot_id,
                     NativeFrameSynchronization &synchronization) = 0;
  /// Transactionally registers the external completion wait and the eventual
  /// return transition. Success ends the serialized interop-queue lease;
  /// failure leaves it live so the caller can correct/retry or shut down.
  virtual RenderOperationResult
  EndExternalFrame(const NativeFrameSynchronization &synchronization) = 0;
  /// Succeeds only when the complete payload identifies a currently live
  /// lease owned by this interop object.
  [[nodiscard]] virtual RenderOperationResult
  ValidateGeometryLease(const NativeGeometryExport &geometry) const = 0;
  [[nodiscard]] virtual RenderOperationResult
  ValidateImageLease(const NativeImageExport &image) const = 0;
  [[nodiscard]] virtual RenderOperationResult ValidateFrameLease(
      const NativeFrameSynchronization &synchronization) const = 0;
  virtual void ReleaseGeometry(std::uint64_t export_id) noexcept = 0;
  virtual void ReleaseImage(std::uint64_t export_id) noexcept = 0;
};

class INativeRayTracingBackend {
public:
  /// Initialize and every later call are serialized on the owning frontend
  /// thread. Native command recording may use private workers, but queue
  /// submission and these public calls remain owner-thread operations.
  virtual ~INativeRayTracingBackend() = default;

  [[nodiscard]] virtual NativeRayTracingCapabilityReport
  QueryCapabilities() const = 0;
  virtual RenderOperationResult Initialize(NativeRenderInterop &interop) = 0;
  /// Version 1 accepts only offscreen requests and returns CPU-readback-only
  /// attachments accepted by ValidateNativeRayTracingFrameOutput(). Native
  /// presentation/output-image import is intentionally deferred to a later
  /// contract because this interface cannot mint frontend resource handles.
  virtual RenderOperationResult
  Render(const NativeRayTracingFrameRequest &request,
         RenderFrameOutput &output) = 0;
  /// Succeeds only when this live backend consumed the supplied interop leases
  /// during its dispatch/readback geometry-interop acceptance probe.
  [[nodiscard]] virtual RenderOperationResult ValidateInteropEvidence(
      const NativeGeometryExport &geometry,
      const NativeFrameSynchronization &synchronization) const = 0;
  /// Stops submissions, waits up to the timeout, ends external frames, and
  /// releases every geometry export. Call this before frontend Shutdown(). A
  /// timeout leaves the backend initialized so teardown can be retried.
  virtual RenderOperationResult Shutdown(std::uint64_t timeout_nanoseconds) = 0;
};

class IRendererFrontend {
public:
  /// Threading contract: Initialize() establishes the owner/render thread.
  /// Every later public call, including const queries and waits, is serialized
  /// on that same thread through successful Shutdown(); none is independently
  /// thread-safe. The host converts main-thread platform window callbacks into
  /// immutable FrontendSurfaceUpdate values and queues them to the owner thread
  /// while preserving the documented borrowed-handle lifetime. On platforms
  /// requiring main-thread presentation (notably some Cocoa configurations),
  /// the host chooses the application main thread as this owner.
  virtual ~IRendererFrontend() = default;

  [[nodiscard]] virtual FrontendCapabilityReport QueryCapabilities() const = 0;
  virtual RenderOperationResult
  Initialize(const FrontendInitializationRequest &request) = 0;
  /// Recreates/resizes the presentation surface without invalidating portable
  /// resources. The headless argument must match initialization. Suspended
  /// surfaces skip presentation until a later active revision. Before success,
  /// it waits up to timeout for every presented frame targeting the prior
  /// revision, then adopts the new handle and releases the prior borrow.
  /// TIMEOUT/failure is transactional and retains the old handle/revision. It
  /// must reject equal/decreasing revisions via the transition validator;
  /// terminal revision exhaustion requires Shutdown()/Initialize().
  virtual RenderOperationResult
  UpdateSurface(const FrontendSurfaceUpdate &update, bool headless,
                std::uint64_t timeout_nanoseconds) = 0;
  /// Transactionally maps renderer-neutral logical assets into this frontend's
  /// private ResourceHandle domain. Incremental updates must continue the
  /// applied sequence exactly. A full snapshot can initialize a new frontend
  /// or rebuild native allocations after device recovery; replaying the same
  /// sequence is permitted only when all logical contents are identical.
  virtual RenderOperationResult
  SynchronizeAssets(const RenderAssetDelta &delta) = 0;
  /// Releases a frontend-owned output handle transferred in FrameAttachment.
  /// Scene assets are retired only through SynchronizeAssets tombstones.
  virtual RenderOperationResult ReleaseResource(ResourceHandle resource) = 0;
  /// A successful submission retains the resolved transitive asset graph
  /// internally until IsFrameComplete(frame_id) is true. Asset updates and
  /// tombstones never overwrite storage leased by an older submitted frame.
  /// The exact topology/deformation revision bytes observed by a frame remain
  /// immutable through frame completion. Later updates must rename/copy/ring
  /// buffer storage or wait; they may never overwrite an in-flight revision.
  /// The frontend tracks first-seen snapshot/event IDs for this initialized
  /// lifetime. A new snapshot ID must exceed the prior first-seen ID and every
  /// particle event must exceed the prior consumed event ID. Only a successful
  /// first submission consumes/emits its events once; repeat submissions emit
  /// none, and failed submissions consume nothing. Shutdown resets this state.
  /// Render rejects every stale/missing asset revision and an
  /// environment texture that is not a live linear floating-point texture.
  /// Its paired sampler and texture must pass
  /// ValidateEnvironmentTextureCompatibility().
  /// The snapshot registry ID and sequence must equal the synchronized asset
  /// catalog. It resolves each live mesh/material pair and requires
  /// ValidateMaterialMeshCompatibility() to succeed; adapters never invent
  /// normals, tangents, or UVs.
  /// Each instance/update must also pass ValidateMeshInstanceCompatibility()
  /// against the live mesh so static buffers, stale topology, absent streams,
  /// bounds disagreements, and out-of-range writes fail.
  /// Frame IDs must be strictly increasing and never reused during one
  /// initialized frontend lifetime. A presented request must name the current
  /// active surface revision and one view whose extent exactly equals that
  /// surface's pixel extent; implicit presentation scaling is forbidden.
  /// Before consuming any frame/snapshot/event identity, Render calls
  /// ValidateRenderFrameRequestAgainstCapabilities() against its current
  /// QueryCapabilities() result and maps UNSUPPORTED_FEATURE to UNSUPPORTED.
  /// Implementations must return an output accepted by the request-correlated
  /// ValidateRenderFrameOutput(request, output) overload.
  virtual RenderOperationResult Render(const RenderFrameRequest &request,
                                       RenderFrameOutput &output) = 0;
  [[nodiscard]] virtual bool
  IsFrameComplete(std::uint64_t frame_id) const noexcept = 0;
  /// A zero timeout polls; kInfiniteRenderTimeoutNanoseconds waits without a
  /// deadline. Unknown frame IDs return INVALID_ARGUMENT.
  virtual RenderOperationResult
  WaitForFrame(std::uint64_t frame_id, std::uint64_t timeout_nanoseconds) = 0;
  /// Returns a frontend-owned object while initialized. A capability report
  /// with supports_native_interop=true requires a non-null result.
  [[nodiscard]] virtual NativeRenderInterop *GetNativeInterop() noexcept = 0;
  /// Call only after every native RT backend has shut down. Outstanding
  /// BeginExternalFrame/AcquireGeometry leases return OUTSTANDING_LEASES
  /// without destroying the device. Internal render frames are drained up to
  /// the timeout; TIMEOUT leaves the frontend initialized for a safe retry.
  /// On success all borrowed context/window/native tokens are revoked.
  virtual RenderOperationResult Shutdown(std::uint64_t timeout_nanoseconds) = 0;
};

} // namespace RoR::Render
