/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Ogre-Next Metal same-device geometry interop.

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "OgreNextN1NativeInterop.h"
#include "OgreNextN2InteropState.h"

#include "OgreMetalDevice.h"
#include "OgreMetalRenderSystem.h"
#include "OgreMetalTextureGpu.h"
#include "OgreTextureGpu.h"
#include "Vao/OgreBufferPacked.h"
#include "Vao/OgreIndexBufferPacked.h"
#include "Vao/OgreMetalBufferInterface.h"
#include "Vao/OgreVertexBufferPacked.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <limits>
#include <new>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

namespace RoR::Render {
namespace {

std::atomic<std::uint64_t> g_next_context_id{1U};

std::uint64_t NextContextId() noexcept {
  std::uint64_t candidate =
      g_next_context_id.load(std::memory_order_relaxed);
  while (candidate != 0U &&
         candidate != (std::numeric_limits<std::uint64_t>::max)()) {
    if (g_next_context_id.compare_exchange_weak(
            candidate, candidate + 1U, std::memory_order_relaxed,
            std::memory_order_relaxed)) {
      return candidate;
    }
  }
  return 0U;
}

std::uint64_t NativeIdentity(id object) noexcept {
  return static_cast<std::uint64_t>(
      reinterpret_cast<std::uintptr_t>((__bridge void *)object));
}

NativeObjectToken Token(NativeObjectKind kind, id object,
                        std::uint64_t context_id,
                        std::uint64_t generation) noexcept {
  NativeObjectToken token;
  token.api = NativeGraphicsApi::METAL;
  token.kind = kind;
  token.context_id = context_id;
  token.value = NativeIdentity(object);
  token.generation = generation;
  return token;
}

RenderOperationResult WrongThread() {
  return RenderOperationResult::Failure(
      RenderOperationCode::INVALID_ARGUMENT,
      "Ogre-Next Metal interop call was made off its owner thread");
}

RenderOperationResult Faulted() {
  return RenderOperationResult::Failure(
      RenderOperationCode::BACKEND_FAILURE,
      "Ogre-Next Metal interop is fault-latched after an ambiguous native queue transition");
}

RenderOperationResult BackendFailure(const std::string &detail) {
  return RenderOperationResult::Failure(RenderOperationCode::BACKEND_FAILURE,
                                        detail);
}

bool CheckedMultiply(std::uint64_t lhs, std::uint64_t rhs,
                     std::uint64_t &output) noexcept {
  if (lhs != 0U && rhs >
                       (std::numeric_limits<std::uint64_t>::max)() / lhs) {
    return false;
  }
  output = lhs * rhs;
  return true;
}

bool CheckedAdd(std::uint64_t lhs, std::uint64_t rhs,
                std::uint64_t &output) noexcept {
  if (rhs > (std::numeric_limits<std::uint64_t>::max)() - lhs) {
    return false;
  }
  output = lhs + rhs;
  return true;
}

std::string MetalError(id<MTLCommandBuffer> command_buffer,
                       const char *prefix) {
  std::ostringstream detail;
  detail << prefix;
  if (command_buffer != nil && command_buffer.error != nil) {
    const char *message = command_buffer.error.localizedDescription.UTF8String;
    if (message != nullptr) {
      detail << ": " << message;
    }
  }
  return detail.str();
}

class OgreNextMetalInterop final : public OgreNextN1NativeInteropBridge {
public:
  OgreNextMetalInterop(Ogre::MetalDevice *metal_device,
                       id<MTLSharedEvent> timeline,
                       NativeContextExport context,
                       bool ray_tracing_api_supported,
                       bool apple_family_9_supported,
                       OgreNextNativeFeatureTier native_feature_tier)
      : metal_device_(metal_device), device_(metal_device->mDevice),
        queue_(metal_device->mMainCommandQueue), timeline_(timeline),
        context_(std::move(context)),
        ray_tracing_api_supported_(ray_tracing_api_supported),
        apple_family_9_supported_(apple_family_9_supported),
        native_feature_tier_(native_feature_tier),
        image_exports_enabled_(
            native_feature_tier ==
                OgreNextNativeFeatureTier::METAL_RAY_TRACING_N3 ||
            native_feature_tier == OgreNextNativeFeatureTier::
                                       METAL_RAY_TRACING_N4_DIRECTIONAL_HARD_SHADOW),
        owner_thread_(std::this_thread::get_id()) {
    const RenderOperationResult initialized = state_.Initialize(
        context_, Token(NativeObjectKind::TIMELINE_SYNC, timeline_,
                        context_.context_id, context_.device.generation));
    if (!initialized) {
      throw std::runtime_error(initialized.detail);
    }
  }

  ~OgreNextMetalInterop() override { RevokeFrontend(); }

  NativeInteropCapabilityReport QueryCapabilities() const override {
    NativeInteropCapabilityReport report;
    if (!valid_ || faulted_ || frontend_revoked_) {
      return report;
    }
    report.native_api = NativeGraphicsApi::METAL;
    report.exports_native_context = true;
    report.exports_vertex_buffers = true;
    report.exports_index_buffers = true;
    report.exports_deformed_meshes = true;
    report.provides_explicit_frame_synchronization = true;
    report.preserves_resource_generations = true;
    report.geometry_interop_proven = geometry_interop_passed_;
    report.exports_color_images = image_exports_enabled_;
    report.supports_read_write_color_images = image_exports_enabled_;
    return report;
  }

  void DecorateFrontendCapabilities(
      FrontendCapabilityReport &report) const override {
    if (!valid_ || faulted_ || frontend_revoked_) {
      return;
    }
    report.native_api = NativeGraphicsApi::METAL;
    report.supports_compute = true;
    report.supports_dynamic_mesh_updates = true;
    report.supports_native_interop = true;
    report.supports_native_ray_tracing_api = ray_tracing_api_supported_;
    report.native_ray_tracing_hardware_accelerated =
        ray_tracing_api_supported_ && apple_family_9_supported_;
    report.native_ray_tracing_probe_passed = dispatch_readback_passed_;
    report.native_ray_tracing_geometry_interop_ready =
        geometry_interop_passed_;
  }

  OgreNextNativeFeatureTier ConfiguredNativeFeatureTier() const noexcept
      override {
    return native_feature_tier_;
  }

  RenderOperationResult AcquireContext(NativeContextExport &output) override {
    const RenderOperationResult ready = Ready();
    if (!ready) {
      return ready;
    }
    output = context_;
    return RenderOperationResult::Success();
  }

  RenderOperationResult
  AcquireGeometry(const NativeGeometryExportRequest &request,
                  NativeGeometryExport &output) override {
    const RenderOperationResult ready = Ready();
    if (!ready) {
      return ready;
    }
    return state_.AcquireGeometry(request, output);
  }

  RenderOperationResult AcquireImage(const NativeImageExportRequest &request,
                                     NativeImageExport &output) override {
    const RenderOperationResult ready = Ready();
    if (!ready) {
      return ready;
    }
    if (!image_exports_enabled_) {
      return RenderOperationResult::Failure(
          RenderOperationCode::UNSUPPORTED,
          "this Ogre-Next Metal feature tier does not export colour images");
    }
    return state_.AcquireImage(request, output);
  }

  RenderOperationResult BeginExternalFrame(
      std::uint64_t frame_id, std::uint64_t snapshot_id,
      NativeFrameSynchronization &synchronization) override {
    const RenderOperationResult ready = Ready();
    if (!ready) {
      return ready;
    }
    NativeFrameSynchronization candidate;
    RenderOperationResult result =
        state_.BeginExternalFrame(frame_id, snapshot_id, candidate);
    if (!result) {
      return result;
    }

    try {
      // This is the sole interop boundary: no Ogre encoder may remain active
      // when the timeline signal is appended to Ogre's current command buffer.
      metal_device_->endAllEncoders();
      id<MTLCommandBuffer> command_buffer =
          metal_device_->mCurrentCommandBuffer;
      if (command_buffer == nil ||
          command_buffer.status == MTLCommandBufferStatusError) {
        static_cast<void>(
            state_.AbortExternalFrameBeforeSubmission(candidate));
        faulted_ = true;
        return BackendFailure(
            MetalError(command_buffer,
                       "Ogre did not expose a usable current Metal command buffer"));
      }
      [command_buffer encodeSignalEvent:timeline_
                                  value:candidate.frontend_complete_value];
      metal_device_->commitAndNextCommandBuffer();
      synchronization = candidate;
      return RenderOperationResult::Success();
    } catch (const std::exception &error) {
      static_cast<void>(state_.AbortExternalFrameBeforeSubmission(candidate));
      faulted_ = true;
      return BackendFailure(std::string("Ogre Metal signal submission failed: ") +
                            error.what());
    } catch (...) {
      static_cast<void>(state_.AbortExternalFrameBeforeSubmission(candidate));
      faulted_ = true;
      return BackendFailure("Ogre Metal signal submission failed");
    }
  }

  RenderOperationResult EndExternalFrame(
      const NativeFrameSynchronization &synchronization) override {
    const RenderOperationResult ready = LifecycleReady();
    if (!ready) {
      return ready;
    }
    RenderOperationResult result = state_.ValidateFrameLease(synchronization);
    if (!result) {
      return result;
    }
    if (frontend_revoked_) {
      return state_.EndExternalFrame(synchronization);
    }

    try {
      metal_device_->endAllEncoders();
      id<MTLCommandBuffer> command_buffer =
          metal_device_->mCurrentCommandBuffer;
      if (command_buffer == nil ||
          command_buffer.status == MTLCommandBufferStatusError) {
        faulted_ = true;
        return BackendFailure(
            MetalError(command_buffer,
                       "Ogre did not expose a usable return command buffer"));
      }
      [command_buffer encodeWaitForEvent:timeline_
                                   value:synchronization.external_complete_value];
      dispatch_semaphore_t completion = dispatch_semaphore_create(0);
      [command_buffer addCompletedHandler:^(id<MTLCommandBuffer>) {
        dispatch_semaphore_signal(completion);
      }];
      last_return_command_buffer_ = command_buffer;
      last_return_completion_ = completion;
      last_return_completion_waited_ = false;
      metal_device_->commitAndNextCommandBuffer();
      result = state_.EndExternalFrame(synchronization);
      if (!result) {
        faulted_ = true;
        return BackendFailure(
            "native lifecycle rejected a committed Ogre return dependency");
      }
      return RenderOperationResult::Success();
    } catch (const std::exception &error) {
      faulted_ = true;
      return BackendFailure(std::string("Ogre Metal return submission failed: ") +
                            error.what());
    } catch (...) {
      faulted_ = true;
      return BackendFailure("Ogre Metal return submission failed");
    }
  }

  RenderOperationResult ValidateGeometryLease(
      const NativeGeometryExport &geometry) const override {
    const RenderOperationResult ready = Ready();
    if (!ready) {
      return ready;
    }
    return state_.ValidateGeometryLease(geometry);
  }

  RenderOperationResult ValidateImageLease(
      const NativeImageExport &image) const override {
    const RenderOperationResult ready = Ready();
    if (!ready) {
      return ready;
    }
    return state_.ValidateImageLease(image);
  }

  RenderOperationResult ValidateFrameLease(
      const NativeFrameSynchronization &synchronization) const override {
    const RenderOperationResult ready = Ready();
    if (!ready) {
      return ready;
    }
    return state_.ValidateFrameLease(synchronization);
  }

  void ReleaseGeometry(std::uint64_t export_id) noexcept override {
    if (state_.initialized() && OnOwnerThread()) {
      state_.ReleaseGeometry(export_id);
    }
  }

  void ReleaseImage(std::uint64_t export_id) noexcept override {
    if (state_.initialized() && OnOwnerThread()) {
      state_.ReleaseImage(export_id);
    }
  }

  RenderOperationResult CanPublishFrame() const override {
    const RenderOperationResult ready = Ready();
    if (!ready) {
      return ready;
    }
    return state_.CanPublishFrame();
  }

  RenderOperationResult PublishFrame(
      std::uint64_t frame_id, std::uint64_t snapshot_id,
      const std::vector<OgreNextN2FrameGeometryBinding> &geometry,
      const std::vector<OgreNextN3FrameImageBinding> &images) override {
    const RenderOperationResult prepared =
        PreparePublishFrame(frame_id, snapshot_id, geometry, images);
    if (!prepared) {
      return prepared;
    }
    if (!CanCommitPreparedFrame(frame_id, snapshot_id)) {
      AbortPreparedFrame();
      return BackendFailure(
          "prepared Metal frame changed before publication");
    }
    CommitPreparedFrame();
    return RenderOperationResult::Success();
  }

  RenderOperationResult PreparePublishFrame(
      std::uint64_t frame_id, std::uint64_t snapshot_id,
      const std::vector<OgreNextN2FrameGeometryBinding> &geometry,
      const std::vector<OgreNextN3FrameImageBinding> &images) override {
    const RenderOperationResult ready = Ready();
    if (!ready) {
      return ready;
    }
    if (geometry.empty()) {
      return RenderOperationResult::Failure(
          RenderOperationCode::INVALID_ARGUMENT,
          "Ogre-Next Metal interop requires at least one raster geometry binding");
    }

    std::vector<OgreNextN2PublishedGeometry> published;
    std::vector<OgreNextN3PublishedImage> published_images;
    try {
      published.reserve(geometry.size());
      for (const OgreNextN2FrameGeometryBinding &binding : geometry) {
        OgreNextN2PublishedGeometry converted;
        RenderOperationResult conversion = ConvertBinding(binding, converted);
        if (!conversion) {
          return conversion;
        }
        published.push_back(std::move(converted));
      }
      if (!images.empty() && !image_exports_enabled_) {
        return RenderOperationResult::Failure(
            RenderOperationCode::UNSUPPORTED,
            "this Ogre-Next Metal feature tier cannot publish colour images");
      }
      published_images.reserve(images.size());
      for (const OgreNextN3FrameImageBinding &binding : images) {
        OgreNextN3PublishedImage converted;
        RenderOperationResult conversion = ConvertImageBinding(binding,
                                                                converted);
        if (!conversion) {
          return conversion;
        }
        published_images.push_back(std::move(converted));
      }
    } catch (const std::bad_alloc &) {
      return RenderOperationResult::Failure(
          RenderOperationCode::OUT_OF_MEMORY,
          "Metal geometry publication allocation failed");
    }
    return state_.PreparePublishFrame(frame_id, snapshot_id, published,
                                      published_images);
  }

  bool CanCommitPreparedFrame(
      std::uint64_t frame_id,
      std::uint64_t snapshot_id) const noexcept override {
    return valid_ && !faulted_ && !frontend_revoked_ && OnOwnerThread() &&
           state_.CanCommitPreparedFrame(frame_id, snapshot_id);
  }

  void CommitPreparedFrame() noexcept override {
    state_.CommitPreparedFrame();
  }

  void AbortPreparedFrame() noexcept override {
    state_.AbortPreparedFrame();
  }

  RenderOperationResult DiscardPublishedFrame() override {
    const RenderOperationResult ready = Ready();
    if (!ready) {
      return ready;
    }
    return state_.DiscardPublishedFrame();
  }

  RenderOperationResult ArmExternalCompletion(
      NativeFrameSynchronization &synchronization) override {
    const RenderOperationResult ready = Ready();
    return ready ? state_.ArmExternalCompletion(synchronization) : ready;
  }

  RenderOperationResult MarkExternalSubmitted(
      const NativeFrameSynchronization &synchronization) override {
    const RenderOperationResult ready = LifecycleReady();
    return ready ? state_.MarkExternalSubmitted(synchronization) : ready;
  }

  RenderOperationResult MarkExternalCompleted(
      const NativeFrameSynchronization &synchronization) override {
    const RenderOperationResult ready = LifecycleReady();
    return ready ? state_.MarkExternalCompleted(synchronization) : ready;
  }

  RenderOperationResult AbortExternalFrameBeforeSubmission(
      const NativeFrameSynchronization &synchronization) override {
    const RenderOperationResult ready = LifecycleReady();
    return ready ? state_.AbortExternalFrameBeforeSubmission(synchronization)
                 : ready;
  }

  RenderOperationResult RegisterRayTracingBackend() override {
    const RenderOperationResult ready = Ready();
    if (!ready) {
      return ready;
    }
    if (!ray_tracing_api_supported_) {
      return RenderOperationResult::Failure(
          RenderOperationCode::UNSUPPORTED,
          "the live Ogre Metal device does not support Metal ray tracing");
    }
    if (!apple_family_9_supported_) {
      return RenderOperationResult::Failure(
          RenderOperationCode::UNSUPPORTED,
          "the live Ogre Metal device does not meet the Apple family 9 hardware floor");
    }
    return state_.RegisterRayTracingBackend();
  }

  RenderOperationResult UnregisterRayTracingBackend() override {
    const RenderOperationResult ready = LifecycleReady();
    return ready ? state_.UnregisterRayTracingBackend() : ready;
  }

  RenderOperationResult AbandonRayTracingBackendAfterFault() override {
    const RenderOperationResult ready = LifecycleReady();
    if (!ready) {
      return ready;
    }
    faulted_ = true;
    dispatch_readback_passed_ = false;
    geometry_interop_passed_ = false;
    return state_.AbandonRayTracingBackendAfterFault();
  }

  void SetRayTracingProof(bool dispatch_readback_passed,
                          bool geometry_interop_passed) override {
    if (!valid_ || faulted_ || frontend_revoked_ || !OnOwnerThread() ||
        !ray_tracing_api_supported_ || !apple_family_9_supported_ ||
        (geometry_interop_passed && !dispatch_readback_passed)) {
      dispatch_readback_passed_ = false;
      geometry_interop_passed_ = false;
      return;
    }
    dispatch_readback_passed_ = dispatch_readback_passed;
    geometry_interop_passed_ = geometry_interop_passed;
  }

  RenderOperationResult PrepareFrontendShutdown(
      std::uint64_t timeout_nanoseconds) override {
    const RenderOperationResult ready = LifecycleReady();
    if (!ready) {
      return ready;
    }
    RenderOperationResult result = state_.CanShutdown();
    if (!result) {
      return result;
    }
    if (last_return_completion_ != nullptr &&
        !last_return_completion_waited_) {
      dispatch_time_t deadline = DISPATCH_TIME_FOREVER;
      if (timeout_nanoseconds != kInfiniteRenderTimeoutNanoseconds) {
        const std::uint64_t bounded = std::min<std::uint64_t>(
            timeout_nanoseconds,
            static_cast<std::uint64_t>((std::numeric_limits<int64_t>::max)()));
        deadline = dispatch_time(DISPATCH_TIME_NOW,
                                 static_cast<int64_t>(bounded));
      }
      if (dispatch_semaphore_wait(last_return_completion_, deadline) != 0) {
        return RenderOperationResult::Failure(
            RenderOperationCode::TIMEOUT,
            "timed out waiting for Ogre's submitted Metal return dependency");
      }
      last_return_completion_waited_ = true;
    }
    const bool return_dependency_failed =
        last_return_command_buffer_ != nil &&
        last_return_command_buffer_.status != MTLCommandBufferStatusCompleted;
    const std::string return_dependency_error =
        return_dependency_failed
            ? MetalError(last_return_command_buffer_,
                         "Ogre Metal return dependency did not complete")
            : std::string();
    result = state_.Reset();
    if (!result) {
      return result;
    }
    valid_ = false;
    last_return_command_buffer_ = nil;
    last_return_completion_ = nullptr;
    timeline_ = nil;
    queue_ = nil;
    device_ = nil;
    metal_device_ = nullptr;
    frontend_revoked_ = true;
    if (return_dependency_failed) {
      return RenderOperationResult::Failure(RenderOperationCode::DEVICE_LOST,
                                            return_dependency_error);
    }
    return RenderOperationResult::Success();
  }

  void RevokeFrontend() noexcept override {
    valid_ = false;
    frontend_revoked_ = true;
    dispatch_readback_passed_ = false;
    geometry_interop_passed_ = false;
    last_return_command_buffer_ = nil;
    last_return_completion_ = nullptr;
    timeline_ = nil;
    queue_ = nil;
    device_ = nil;
    metal_device_ = nullptr;
  }

private:
  bool OnOwnerThread() const noexcept {
    return std::this_thread::get_id() == owner_thread_;
  }

  RenderOperationResult Ready() const {
    if (!valid_) {
      return RenderOperationResult::Failure(
          RenderOperationCode::NOT_INITIALIZED,
          "Ogre-Next Metal interop context is not live");
    }
    if (!OnOwnerThread()) {
      return WrongThread();
    }
    if (faulted_) {
      return Faulted();
    }
    return RenderOperationResult::Success();
  }

  RenderOperationResult LifecycleReady() const {
    if (!OnOwnerThread()) {
      return WrongThread();
    }
    if (!state_.initialized()) {
      return RenderOperationResult::Failure(
          RenderOperationCode::NOT_INITIALIZED,
          "Ogre-Next Metal interop lifecycle is not live");
    }
    return RenderOperationResult::Success();
  }

  RenderOperationResult ConvertBinding(
      const OgreNextN2FrameGeometryBinding &binding,
      OgreNextN2PublishedGeometry &output) const {
    if (binding.ogre_vertex_buffer == 0U ||
        binding.ogre_index_buffer == 0U || binding.frame_id == 0U ||
        binding.snapshot_id == 0U || binding.instance_id == 0U ||
        binding.topology_revision == 0U ||
        binding.deformation_revision == 0U || !binding.mesh.valid() ||
        binding.mesh.kind != RenderAssetKind::MESH ||
        binding.topology != MeshPrimitiveTopology::TRIANGLE_LIST ||
        binding.vertex_count == 0U || binding.index_count == 0U) {
      return RenderOperationResult::Failure(
          RenderOperationCode::INVALID_ARGUMENT,
          "Ogre frame geometry binding is incomplete");
    }
    auto *vertex_buffer = reinterpret_cast<Ogre::VertexBufferPacked *>(
        binding.ogre_vertex_buffer);
    auto *index_buffer = reinterpret_cast<Ogre::IndexBufferPacked *>(
        binding.ogre_index_buffer);
    if (vertex_buffer->getBufferPackedType() != Ogre::BP_TYPE_VERTEX ||
        index_buffer->getBufferPackedType() != Ogre::BP_TYPE_INDEX ||
        vertex_buffer->getNumElements() != binding.vertex_count ||
        index_buffer->getNumElements() != binding.index_count ||
        vertex_buffer->getBytesPerElement() <
            binding.position_offset_bytes + 12U) {
      return BackendFailure(
          "Ogre v2 buffers disagree with the published geometry layout");
    }
    const Ogre::VertexElement2Vec &elements =
        vertex_buffer->getVertexElements();
    const auto exact_element = [&](std::size_t index,
                                   Ogre::VertexElementType type,
                                   Ogre::VertexElementSemantic semantic) {
      return index < elements.size() && elements[index].mType == type &&
             elements[index].mSemantic == semantic &&
             elements[index].mInstancingStepRate == 0U;
    };
    bool reviewed_layout = false;
    std::uint32_t expected_stride = 0U;
    switch (binding.vertex_layout) {
    case OgreNextNativeVertexLayout::POSITION_NORMAL_FLOAT32_24:
      expected_stride = kOgreNextPositionNormalVertexStrideBytes;
      reviewed_layout =
          elements.size() == 2U &&
          exact_element(0U, Ogre::VET_FLOAT3, Ogre::VES_POSITION) &&
          exact_element(1U, Ogre::VET_FLOAT3, Ogre::VES_NORMAL);
      break;
    case OgreNextNativeVertexLayout::
        POSITION_NORMAL_TANGENT_UV0_FLOAT32_48:
      expected_stride =
          kOgreNextPositionNormalTangentUv0VertexStrideBytes;
      reviewed_layout =
          elements.size() == 4U &&
          exact_element(0U, Ogre::VET_FLOAT3, Ogre::VES_POSITION) &&
          exact_element(1U, Ogre::VET_FLOAT3, Ogre::VES_NORMAL) &&
          exact_element(2U, Ogre::VET_FLOAT4, Ogre::VES_TANGENT) &&
          exact_element(3U, Ogre::VET_FLOAT2,
                        Ogre::VES_TEXTURE_COORDINATES);
      break;
    case OgreNextNativeVertexLayout::INVALID:
      break;
    }
    if (!reviewed_layout || binding.position_offset_bytes != 0U ||
        binding.vertex_stride_bytes != expected_stride ||
        vertex_buffer->getBytesPerElement() != expected_stride) {
      return BackendFailure(
          "Ogre v2 vertex layout is not an exact reviewed interop layout");
    }
    const NativeIndexFormat actual_index_format =
        index_buffer->getIndexType() == Ogre::IndexBufferPacked::IT_16BIT
            ? NativeIndexFormat::UINT16
            : NativeIndexFormat::UINT32;
    if (actual_index_format != binding.index_format) {
      return BackendFailure(
          "Ogre v2 index type disagrees with the renderer-neutral mesh");
    }

    auto *vertex_interface = dynamic_cast<Ogre::MetalBufferInterface *>(
        vertex_buffer->getBufferInterface());
    auto *index_interface = dynamic_cast<Ogre::MetalBufferInterface *>(
        index_buffer->getBufferInterface());
    if (vertex_interface == nullptr || index_interface == nullptr) {
      return BackendFailure(
          "Ogre v2 buffers are not backed by the live Metal allocator");
    }
    id<MTLBuffer> metal_vertex_buffer = vertex_interface->getVboName();
    id<MTLBuffer> metal_index_buffer = index_interface->getVboName();
    if (metal_vertex_buffer == nil || metal_index_buffer == nil ||
        metal_vertex_buffer.device != device_ ||
        metal_index_buffer.device != device_) {
      return BackendFailure(
          "Ogre v2 buffers do not belong to the exported Metal device");
    }

    const std::uint64_t vertex_stride =
        vertex_buffer->getBytesPerElement();
    const std::uint64_t index_stride = index_buffer->getBytesPerElement();
    std::uint64_t vertex_pool_offset = 0U;
    std::uint64_t index_pool_offset = 0U;
    if (!CheckedMultiply(vertex_buffer->_getFinalBufferStart(), vertex_stride,
                         vertex_pool_offset) ||
        !CheckedMultiply(index_buffer->_getFinalBufferStart(), index_stride,
                         index_pool_offset)) {
      return BackendFailure("Ogre pooled buffer offset overflowed");
    }
    std::uint64_t position_offset = 0U;
    if (!CheckedAdd(vertex_pool_offset, binding.position_offset_bytes,
                    position_offset)) {
      return BackendFailure("Ogre position buffer offset overflowed");
    }
    std::uint64_t vertex_span = 0U;
    std::uint64_t index_span = 0U;
    if (!CheckedMultiply(binding.vertex_count - 1U, vertex_stride,
                         vertex_span) ||
        !CheckedAdd(vertex_span, 12U, vertex_span) ||
        !CheckedMultiply(binding.index_count, index_stride, index_span)) {
      return BackendFailure("Ogre geometry slice size overflowed");
    }
    std::uint64_t vertex_end = 0U;
    std::uint64_t index_end = 0U;
    if (!CheckedAdd(position_offset, vertex_span, vertex_end) ||
        !CheckedAdd(index_pool_offset, index_span, index_end) ||
        vertex_end > metal_vertex_buffer.length ||
        index_end > metal_index_buffer.length) {
      return BackendFailure(
          "Ogre pooled geometry slice exceeds its exact MTLBuffer length");
    }

    NativeGeometryExport &geometry = output.geometry;
    geometry.frame_id = binding.frame_id;
    geometry.snapshot_id = binding.snapshot_id;
    geometry.instance_id = binding.instance_id;
    geometry.mesh = binding.mesh;
    geometry.topology_revision = binding.topology_revision;
    geometry.deformation_revision = binding.deformation_revision;
    geometry.topology = binding.topology;
    geometry.positions.buffer =
        Token(NativeObjectKind::BUFFER, metal_vertex_buffer,
              context_.context_id, binding.frame_id);
    geometry.positions.offset_bytes = position_offset;
    geometry.positions.size_bytes = vertex_span;
    geometry.positions.stride_bytes =
        static_cast<std::uint32_t>(vertex_stride);
    geometry.indices.buffer = Token(NativeObjectKind::BUFFER,
                                    metal_index_buffer, context_.context_id,
                                    binding.frame_id);
    geometry.indices.offset_bytes = index_pool_offset;
    geometry.indices.size_bytes = index_span;
    geometry.indices.stride_bytes =
        static_cast<std::uint32_t>(index_stride);
    geometry.position_format = NativeVertexPositionFormat::FLOAT32_XYZ;
    geometry.index_format = actual_index_format;
    geometry.vertex_count = binding.vertex_count;
    geometry.index_count = binding.index_count;
    return RenderOperationResult::Success();
  }

  RenderOperationResult ConvertImageBinding(
      const OgreNextN3FrameImageBinding &binding,
      OgreNextN3PublishedImage &output) const {
    if (binding.frame_id == 0U || binding.snapshot_id == 0U ||
        binding.view_id == 0U || binding.output != FrameOutputMask::COLOR ||
        binding.format != PixelFormat::RGBA16_FLOAT ||
        binding.ogre_texture == 0U || binding.width == 0U ||
        binding.height == 0U || !binding.scene_snapshot ||
        binding.scene_snapshot->snapshot_id() != binding.snapshot_id ||
        binding.view.view_id != binding.view_id ||
        binding.view.width != binding.width ||
        binding.view.height != binding.height ||
        !ValidateCameraViewRequest(binding.view)) {
      return RenderOperationResult::Failure(
          RenderOperationCode::INVALID_ARGUMENT,
          "Ogre frame image binding is incomplete or is not linear HDR colour");
    }
    auto *ogre_texture = reinterpret_cast<Ogre::TextureGpu *>(
        binding.ogre_texture);
    auto *metal_texture = dynamic_cast<Ogre::MetalTextureGpu *>(ogre_texture);
    if (metal_texture == nullptr || !ogre_texture->isRenderToTexture() ||
        !ogre_texture->isUav() ||
        ogre_texture->getPixelFormat() != Ogre::PFG_RGBA16_FLOAT ||
        ogre_texture->getWidth() != binding.width ||
        ogre_texture->getHeight() != binding.height ||
        ogre_texture->getNumMipmaps() != 1U ||
        ogre_texture->getNumSlices() != 1U ||
        ogre_texture->getSampleDescription().getColourSamples() != 1U) {
      return BackendFailure(
          "Ogre render target does not match the reviewed N3 HDR UAV contract");
    }
    id<MTLTexture> texture = metal_texture->getFinalTextureName();
    const MTLTextureUsage required_usage =
        MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead |
        MTLTextureUsageShaderWrite;
    if (texture == nil || texture.device != device_ ||
        texture.pixelFormat != MTLPixelFormatRGBA16Float ||
        texture.width != binding.width || texture.height != binding.height ||
        texture.mipmapLevelCount != 1U || texture.arrayLength != 1U ||
        texture.sampleCount != 1U ||
        (texture.usage & required_usage) != required_usage ||
        texture.storageMode == MTLStorageModeMemoryless) {
      return BackendFailure(
        "native MTLTexture does not match Ogre's exact N3/N4 HDR target");
    }

    NativeImageExport &image = output.image;
    image.frame_id = binding.frame_id;
    image.snapshot_id = binding.snapshot_id;
    image.view_id = binding.view_id;
    image.scene_snapshot = binding.scene_snapshot;
    image.view = binding.view;
    image.output = binding.output;
    image.format = binding.format;
    image.usage =
        NativeImageUsage::COLOR_ATTACHMENT_SHADER_READ_WRITE_COPY_SOURCE;
    image.image = Token(NativeObjectKind::IMAGE, texture,
                        context_.context_id, binding.frame_id);
    image.width = binding.width;
    image.height = binding.height;
    image.sample_count = 1U;
    return RenderOperationResult::Success();
  }

  Ogre::MetalDevice *metal_device_ = nullptr;
  id<MTLDevice> device_ = nil;
  id<MTLCommandQueue> queue_ = nil;
  id<MTLSharedEvent> timeline_ = nil;
  id<MTLCommandBuffer> last_return_command_buffer_ = nil;
  dispatch_semaphore_t last_return_completion_ = nullptr;
  NativeContextExport context_;
  OgreNextN2InteropState state_;
  bool ray_tracing_api_supported_ = false;
  bool apple_family_9_supported_ = false;
  OgreNextNativeFeatureTier native_feature_tier_ =
      OgreNextNativeFeatureTier::RASTER_N1;
  bool image_exports_enabled_ = false;
  bool dispatch_readback_passed_ = false;
  bool geometry_interop_passed_ = false;
  bool last_return_completion_waited_ = false;
  bool valid_ = true;
  bool faulted_ = false;
  bool frontend_revoked_ = false;
  std::thread::id owner_thread_;
};

} // namespace

RenderOperationResult CreateOgreNextMetalInterop(
    std::uintptr_t ogre_render_system,
    OgreNextNativeFeatureTier native_feature_tier,
    std::shared_ptr<OgreNextN1NativeInteropBridge> &output) {
  output.reset();
  if (ogre_render_system == 0U) {
    return RenderOperationResult::Failure(
        RenderOperationCode::INVALID_ARGUMENT,
        "Ogre Metal interop requires a live render system");
  }
  if (native_feature_tier == OgreNextNativeFeatureTier::RASTER_N1) {
    return RenderOperationResult::Failure(
        RenderOperationCode::INVALID_ARGUMENT,
        "Ogre Metal interop requires an explicit native feature tier");
  }
  auto *render_system = reinterpret_cast<Ogre::MetalRenderSystem *>(
      ogre_render_system);
  Ogre::MetalDevice *metal_device = render_system->getActiveDevice();
  if (metal_device == nullptr || metal_device->mDevice == nil ||
      metal_device->mMainCommandQueue == nil ||
      metal_device->mCurrentCommandBuffer == nil) {
    return BackendFailure(
        "the initialized Ogre hidden window did not expose a live Metal device, queue, and command buffer");
  }

  if (@available(macOS 10.14, *)) {
    // Continue below. Keeping the availability branch explicit prevents this
    // deployment-floor guard from being optimized into compile-time presence.
  } else {
    return RenderOperationResult::Failure(
        RenderOperationCode::UNSUPPORTED,
        "MTLSharedEvent is unavailable on this macOS runtime");
  }
  id<MTLSharedEvent> timeline = [metal_device->mDevice newSharedEvent];
  if (timeline == nil) {
    return BackendFailure("the live Ogre Metal device could not create MTLSharedEvent");
  }

  bool ray_tracing_api_supported = false;
  if (@available(macOS 11.0, *)) {
    ray_tracing_api_supported = metal_device->mDevice.supportsRaytracing;
  }
  bool apple_family_9_supported = false;
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 140000
  if (@available(macOS 14.0, *)) {
    apple_family_9_supported =
        [metal_device->mDevice supportsFamily:MTLGPUFamilyApple9];
  }
#endif

  const std::uint64_t context_id = NextContextId();
  if (context_id == 0U) {
    return BackendFailure("native Metal context identifier space is exhausted");
  }
  NativeContextExport context;
  context.native_api = NativeGraphicsApi::METAL;
  context.context_id = context_id;
  context.device = Token(NativeObjectKind::DEVICE, metal_device->mDevice,
                         context_id, context_id);
  context.graphics_queue =
      Token(NativeObjectKind::QUEUE, metal_device->mMainCommandQueue,
            context_id, context_id);

  try {
    output = std::make_shared<OgreNextMetalInterop>(
        metal_device, timeline, context, ray_tracing_api_supported,
        apple_family_9_supported, native_feature_tier);
    return RenderOperationResult::Success();
  } catch (const std::bad_alloc &) {
    return RenderOperationResult::Failure(
        RenderOperationCode::OUT_OF_MEMORY,
        "Metal interop allocation failed");
  } catch (const std::exception &error) {
    return BackendFailure(std::string("Metal interop initialization failed: ") +
                          error.what());
  }
}

} // namespace RoR::Render
