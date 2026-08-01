/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "OgreNextD3D12DxrBootstrap.h"

#if !defined(_WIN32) || !defined(ROR_OGRE_NEXT_N1_D3D11)
#error "The DXR7 bootstrap is reviewed only for Windows x64 D3D11On12"
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "OgreRenderSystem.h"

#include <d3d11.h>
#include <d3d11on12.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace RoR::Render {
namespace {

using Microsoft::WRL::ComPtr;

constexpr std::uint32_t kClosestHitValue = 0xd1ceb00bU;
constexpr DWORD kFenceTimeoutMilliseconds = 15'000U;

Dxr7BootstrapResult Ready() {
  return {Dxr7BootstrapCode::READY, {}};
}

Dxr7BootstrapResult Unsupported(std::string message) {
  return {Dxr7BootstrapCode::UNSUPPORTED, std::move(message)};
}

Dxr7BootstrapResult Failure(std::string message) {
  return {Dxr7BootstrapCode::FAILURE, std::move(message)};
}

std::string HresultFailure(const char* operation, HRESULT result) {
  std::ostringstream message;
  message << operation << " failed with HRESULT 0x" << std::hex
          << std::setw(8) << std::setfill('0')
          << static_cast<unsigned long>(result);
  return message.str();
}

std::string Utf8Name(const wchar_t* value) {
  if (value == nullptr || *value == L'\0') {
    return {};
  }
  const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value,
                                        -1, nullptr, 0, nullptr, nullptr);
  if (bytes <= 1) {
    return {};
  }
  std::string result(static_cast<std::size_t>(bytes), '\0');
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1,
                          result.data(), bytes, nullptr, nullptr) == 0) {
    return {};
  }
  result.resize(static_cast<std::size_t>(bytes - 1));
  return result;
}

std::string LuidString(LUID luid) {
  const std::uint64_t value =
      (static_cast<std::uint64_t>(static_cast<std::uint32_t>(luid.HighPart))
       << 32U) |
      static_cast<std::uint64_t>(luid.LowPart);
  std::ostringstream text;
  text << std::hex << std::setw(16) << std::setfill('0') << value;
  return text.str();
}

bool EqualLuid(LUID lhs, LUID rhs) noexcept {
  return lhs.HighPart == rhs.HighPart && lhs.LowPart == rhs.LowPart;
}

template <typename Left, typename Right>
bool SameComIdentity(Left* lhs, Right* rhs) noexcept {
  if (lhs == nullptr || rhs == nullptr) {
    return false;
  }
  ComPtr<IUnknown> lhs_identity;
  ComPtr<IUnknown> rhs_identity;
  if (FAILED(lhs->QueryInterface(IID_PPV_ARGS(&lhs_identity))) ||
      FAILED(rhs->QueryInterface(IID_PPV_ARGS(&rhs_identity)))) {
    return false;
  }
  return lhs_identity.Get() == rhs_identity.Get();
}

std::uint64_t AlignUp(std::uint64_t value, std::uint64_t alignment) {
  return (value + alignment - 1U) & ~(alignment - 1U);
}

std::vector<std::uint8_t> ReadBinary(
    const std::filesystem::path& path) {
  std::ifstream source(path, std::ios::binary | std::ios::ate);
  if (!source) {
    throw std::runtime_error("could not open the compiled DXR library");
  }
  const std::streamoff end = source.tellg();
  if (end <= 0 || end > static_cast<std::streamoff>(64U * 1024U * 1024U)) {
    throw std::runtime_error("compiled DXR library size is invalid");
  }
  source.seekg(0, std::ios::beg);
  std::vector<std::uint8_t> contents(static_cast<std::size_t>(end));
  source.read(reinterpret_cast<char*>(contents.data()), end);
  if (!source) {
    throw std::runtime_error("could not read the complete DXR library");
  }
  return contents;
}

D3D12_RESOURCE_DESC BufferDescription(std::uint64_t bytes,
                                      D3D12_RESOURCE_FLAGS flags) {
  D3D12_RESOURCE_DESC description{};
  description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  description.Alignment = 0U;
  description.Width = bytes;
  description.Height = 1U;
  description.DepthOrArraySize = 1U;
  description.MipLevels = 1U;
  description.Format = DXGI_FORMAT_UNKNOWN;
  description.SampleDesc.Count = 1U;
  description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  description.Flags = flags;
  return description;
}

ComPtr<ID3D12Resource> CreateBuffer(ID3D12Device* device,
                                    std::uint64_t bytes,
                                    D3D12_HEAP_TYPE heap_type,
                                    D3D12_RESOURCE_FLAGS flags,
                                    D3D12_RESOURCE_STATES state) {
  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = heap_type;
  heap.CreationNodeMask = 1U;
  heap.VisibleNodeMask = 1U;
  const D3D12_RESOURCE_DESC description = BufferDescription(bytes, flags);
  ComPtr<ID3D12Resource> resource;
  const HRESULT result = device->CreateCommittedResource(
      &heap, D3D12_HEAP_FLAG_NONE, &description, state, nullptr,
      IID_PPV_ARGS(&resource));
  if (FAILED(result)) {
    throw std::runtime_error(HresultFailure("CreateCommittedResource",
                                            result));
  }
  return resource;
}

template <typename Value>
void UploadValue(ID3D12Resource* resource, const Value& value) {
  void* mapped = nullptr;
  const D3D12_RANGE no_read{0U, 0U};
  const HRESULT result = resource->Map(0U, &no_read, &mapped);
  if (FAILED(result) || mapped == nullptr) {
    throw std::runtime_error(HresultFailure("ID3D12Resource::Map", result));
  }
  std::memcpy(mapped, &value, sizeof(value));
  resource->Unmap(0U, nullptr);
}

void UploadBytes(ID3D12Resource* resource, const void* source,
                 std::size_t bytes) {
  void* mapped = nullptr;
  const D3D12_RANGE no_read{0U, 0U};
  const HRESULT result = resource->Map(0U, &no_read, &mapped);
  if (FAILED(result) || mapped == nullptr) {
    throw std::runtime_error(HresultFailure("ID3D12Resource::Map", result));
  }
  std::memcpy(mapped, source, bytes);
  resource->Unmap(0U, nullptr);
}

}  // namespace

struct OgreNextD3D12DxrBootstrap::Impl {
  ComPtr<IDXGIFactory6> factory;
  ComPtr<IDXGIAdapter1> adapter;
  ComPtr<ID3D12Device5> d3d12_device;
  ComPtr<ID3D12CommandQueue> direct_queue;
  ComPtr<ID3D12Fence> fence;
  HANDLE fence_event = nullptr;
  ComPtr<ID3D11Device> d3d11_device;
  ComPtr<ID3D11DeviceContext> d3d11_context;
  ComPtr<ID3D11On12Device1> d3d11on12_device;
  Dxr7BootstrapEvidence evidence;
  bool ogre_attached = false;

  void RecordAdapter(IDXGIAdapter1* candidate_adapter,
                     const DXGI_ADAPTER_DESC1& description,
                     ID3D12Device5* candidate_device,
                     std::uint32_t raytracing_tier) {
    evidence.adapter_name = Utf8Name(description.Description);
    evidence.adapter_luid = LuidString(description.AdapterLuid);
    evidence.vendor_id = description.VendorId;
    evidence.device_id = description.DeviceId;
    evidence.software_adapter =
        (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0U;
    evidence.candidate.hardware_adapter = !evidence.software_adapter;
    evidence.candidate.d3d12_device_available = candidate_device != nullptr;
    evidence.candidate.raytracing_tier = raytracing_tier;
    if (candidate_device != nullptr) {
      evidence.d3d12_feature_level =
          static_cast<std::uint32_t>(D3D_FEATURE_LEVEL_12_0);
    }
    static_cast<void>(candidate_adapter);
  }

  Dxr7BootstrapResult WaitForFence(std::uint64_t value) {
    const HRESULT signal_result = direct_queue->Signal(fence.Get(), value);
    if (FAILED(signal_result)) {
      return Failure(HresultFailure("ID3D12CommandQueue::Signal",
                                    signal_result));
    }
    auto completion = EvaluateDxr7FenceCompletion(
        fence->GetCompletedValue(), value);
    if (completion == Dxr7FenceCompletionDecision::DEVICE_REMOVED) {
      return Failure(HresultFailure(
          "ID3D12Fence::GetCompletedValue reported device removal; "
          "ID3D12Device::GetDeviceRemovedReason",
          d3d12_device->GetDeviceRemovedReason()));
    }
    if (completion == Dxr7FenceCompletionDecision::WAIT) {
      ResetEvent(fence_event);
      const HRESULT event_result =
          fence->SetEventOnCompletion(value, fence_event);
      if (FAILED(event_result)) {
        return Failure(HresultFailure("ID3D12Fence::SetEventOnCompletion",
                                      event_result));
      }
      const DWORD wait_result =
          WaitForSingleObject(fence_event, kFenceTimeoutMilliseconds);
      if (wait_result != WAIT_OBJECT_0) {
        return Failure(wait_result == WAIT_TIMEOUT
                           ? "D3D12 fence wait timed out"
                           : "D3D12 fence wait failed");
      }
    }
    completion = EvaluateDxr7FenceCompletion(
        fence->GetCompletedValue(), value);
    if (completion == Dxr7FenceCompletionDecision::DEVICE_REMOVED) {
      return Failure(HresultFailure(
          "ID3D12Fence::GetCompletedValue reported device removal; "
          "ID3D12Device::GetDeviceRemovedReason",
          d3d12_device->GetDeviceRemovedReason()));
    }
    if (completion != Dxr7FenceCompletionDecision::COMPLETE) {
      return Failure("D3D12 fence completion regressed");
    }
    return Ready();
  }

  Dxr7BootstrapResult DestroyOwnedObjects() noexcept {
    if (d3d11_context) {
      d3d11_context->ClearState();
      d3d11_context->Flush();
      evidence.d3d11_context_flushed_before_release = true;
    }
    d3d11on12_device.Reset();
    d3d11_context.Reset();
    d3d11_device.Reset();
    evidence.d3d11_released_before_d3d12_queue = direct_queue != nullptr;

    if (fence_event != nullptr) {
      CloseHandle(fence_event);
      fence_event = nullptr;
    }
    fence.Reset();
    direct_queue.Reset();
    evidence.d3d12_queue_released_before_device = d3d12_device != nullptr;
    d3d12_device.Reset();
    adapter.Reset();
    factory.Reset();
    evidence.shutdown_completed = true;
    return Ready();
  }
};

OgreNextD3D12DxrBootstrap::OgreNextD3D12DxrBootstrap()
    : impl_(std::make_unique<Impl>()) {}

OgreNextD3D12DxrBootstrap::~OgreNextD3D12DxrBootstrap() {
  if (impl_ && !impl_->ogre_attached && !impl_->evidence.shutdown_completed) {
    static_cast<void>(impl_->DestroyOwnedObjects());
  }
}

Dxr7BootstrapResult OgreNextD3D12DxrBootstrap::Initialize() {
  if (impl_->factory || impl_->d3d12_device ||
      impl_->evidence.shutdown_completed) {
    return Failure("DXR7 bootstrap cannot be initialized twice");
  }

  HRESULT result = CreateDXGIFactory2(0U, IID_PPV_ARGS(&impl_->factory));
  if (FAILED(result)) {
    return Failure(HresultFailure("CreateDXGIFactory2", result));
  }

  bool saw_hardware = false;
  bool saw_d3d12 = false;
  for (UINT index = 0U;; ++index) {
    ComPtr<IDXGIAdapter1> candidate_adapter;
    result = impl_->factory->EnumAdapterByGpuPreference(
        index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
        IID_PPV_ARGS(&candidate_adapter));
    if (result == DXGI_ERROR_NOT_FOUND) {
      break;
    }
    if (FAILED(result)) {
      return Failure(HresultFailure("EnumAdapterByGpuPreference", result));
    }

    DXGI_ADAPTER_DESC1 description{};
    result = candidate_adapter->GetDesc1(&description);
    if (FAILED(result)) {
      return Failure(HresultFailure("IDXGIAdapter1::GetDesc1", result));
    }
    if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0U) {
      continue;
    }
    saw_hardware = true;

    ComPtr<ID3D12Device5> candidate_device;
    result = D3D12CreateDevice(candidate_adapter.Get(),
                               D3D_FEATURE_LEVEL_12_0,
                               IID_PPV_ARGS(&candidate_device));
    if (FAILED(result)) {
      if (!saw_d3d12) {
        impl_->RecordAdapter(candidate_adapter.Get(), description, nullptr,
                             0U);
      }
      continue;
    }
    saw_d3d12 = true;

    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};
    result = candidate_device->CheckFeatureSupport(
        D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5));
    if (FAILED(result)) {
      return Failure(HresultFailure(
          "CheckFeatureSupport(D3D12_OPTIONS5)", result));
    }
    const std::uint32_t tier =
        static_cast<std::uint32_t>(options5.RaytracingTier);
    impl_->RecordAdapter(candidate_adapter.Get(), description,
                         candidate_device.Get(), tier);
    if (options5.RaytracingTier < D3D12_RAYTRACING_TIER_1_1) {
      continue;
    }
    impl_->adapter = candidate_adapter;
    impl_->d3d12_device = candidate_device;
    break;
  }

  if (!impl_->d3d12_device) {
    if (!saw_hardware) {
      impl_->evidence.candidate = {};
    } else if (!saw_d3d12) {
      impl_->evidence.candidate.hardware_adapter = true;
      impl_->evidence.candidate.d3d12_device_available = false;
    }
    impl_->evidence.candidate_decision =
        EvaluateDxr7Candidate(impl_->evidence.candidate);
    const std::string reason = std::string("no attested DXR7 adapter: ") +
                               Dxr7CandidateDecisionName(
                                   impl_->evidence.candidate_decision);
    static_cast<void>(impl_->DestroyOwnedObjects());
    return Unsupported(reason);
  }

  D3D12_COMMAND_QUEUE_DESC queue_description{};
  queue_description.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  result = impl_->d3d12_device->CreateCommandQueue(
      &queue_description, IID_PPV_ARGS(&impl_->direct_queue));
  if (FAILED(result)) {
    return Failure(HresultFailure("CreateCommandQueue(DIRECT)", result));
  }
  impl_->evidence.candidate.direct_queue_available = true;
  result = impl_->d3d12_device->CreateFence(
      0U, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&impl_->fence));
  if (FAILED(result)) {
    return Failure(HresultFailure("ID3D12Device::CreateFence", result));
  }
  impl_->evidence.candidate.fence_available = true;
  impl_->fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (impl_->fence_event == nullptr) {
    return Failure("CreateEventW for the D3D12 fence failed");
  }

  impl_->evidence.candidate_decision =
      EvaluateDxr7Candidate(impl_->evidence.candidate);
  if (impl_->evidence.candidate_decision !=
      Dxr7CandidateDecision::ACCEPT) {
    return Failure("selected DXR7 adapter violated the admission contract");
  }
  impl_->evidence.app_owned_d3d12_device = true;
  impl_->evidence.app_owned_direct_queue = true;
  impl_->evidence.app_owned_fence = true;

  const D3D_FEATURE_LEVEL feature_levels[] = {
      D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
  IUnknown* queues[] = {impl_->direct_queue.Get()};
  D3D_FEATURE_LEVEL selected_d3d11_feature_level = D3D_FEATURE_LEVEL_11_0;
  result = D3D11On12CreateDevice(
      impl_->d3d12_device.Get(), D3D11_CREATE_DEVICE_BGRA_SUPPORT,
      feature_levels, static_cast<UINT>(std::size(feature_levels)), queues,
      static_cast<UINT>(std::size(queues)), 0U,
      impl_->d3d11_device.GetAddressOf(),
      impl_->d3d11_context.GetAddressOf(),
      &selected_d3d11_feature_level);
  if (FAILED(result)) {
    return Failure(HresultFailure("D3D11On12CreateDevice", result));
  }
  impl_->evidence.d3d11_feature_level =
      static_cast<std::uint32_t>(selected_d3d11_feature_level);
  impl_->evidence.d3d11on12_device_created = true;
  // D3D11On12 has no queue getter. The exact in-process pointer supplied in
  // the one-element direct-queue array is retained as provenance, then the
  // same owner queue is fenced before DXR and after Ogre's context flush.
  impl_->evidence.d3d11on12_created_with_exact_direct_queue =
      queues[0] == impl_->direct_queue.Get();
  result = impl_->d3d11_device.As(&impl_->d3d11on12_device);
  if (FAILED(result)) {
    return Failure(HresultFailure("QueryInterface(ID3D11On12Device1)",
                                  result));
  }
  ComPtr<ID3D12Device> underlying_device;
  result = impl_->d3d11on12_device->GetD3D12Device(
      IID_PPV_ARGS(&underlying_device));
  if (FAILED(result)) {
    return Failure(HresultFailure("ID3D11On12Device1::GetD3D12Device",
                                  result));
  }
  impl_->evidence.d3d11on12_underlying_d3d12_device_exact =
      SameComIdentity(underlying_device.Get(), impl_->d3d12_device.Get());

  ComPtr<IDXGIDevice> dxgi_device;
  ComPtr<IDXGIAdapter> d3d11_adapter;
  DXGI_ADAPTER_DESC d3d11_description{};
  result = impl_->d3d11_device.As(&dxgi_device);
  if (SUCCEEDED(result)) {
    result = dxgi_device->GetAdapter(d3d11_adapter.GetAddressOf());
  }
  if (SUCCEEDED(result)) {
    result = d3d11_adapter->GetDesc(&d3d11_description);
  }
  if (FAILED(result)) {
    return Failure(HresultFailure("D3D11On12 adapter identity query", result));
  }
  impl_->evidence.d3d11on12_adapter_luid_exact = EqualLuid(
      d3d11_description.AdapterLuid, impl_->d3d12_device->GetAdapterLuid());
  if (!impl_->evidence.d3d11on12_underlying_d3d12_device_exact ||
      !impl_->evidence.d3d11on12_adapter_luid_exact) {
    return Failure("D3D11On12 did not retain the exact D3D12 device/adapter");
  }
  return Ready();
}

Dxr7BootstrapResult
OgreNextD3D12DxrBootstrap::ProveFenceBeforeDispatch() {
  if (!impl_->direct_queue || impl_->evidence.fence_before_dispatch != 0U) {
    return Failure("DXR7 pre-dispatch fence proof is out of order");
  }
  Dxr7BootstrapResult result = impl_->WaitForFence(1U);
  if (result.ready()) {
    impl_->evidence.fence_before_dispatch = 1U;
  }
  return result;
}

Dxr7BootstrapResult OgreNextD3D12DxrBootstrap::DispatchProbe(
    const std::filesystem::path& dxil_library) {
  if (!impl_->d3d12_device || impl_->evidence.fence_before_dispatch != 1U ||
      impl_->evidence.dispatch_rays_called) {
    return Failure("DXR7 DispatchRays proof is out of order");
  }
  try {
    const std::vector<std::uint8_t> dxil = ReadBinary(dxil_library);

    ComPtr<ID3D12CommandAllocator> allocator;
    HRESULT result = impl_->d3d12_device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
    if (FAILED(result)) {
      throw std::runtime_error(
          HresultFailure("CreateCommandAllocator(DIRECT)", result));
    }
    ComPtr<ID3D12GraphicsCommandList4> command_list;
    result = impl_->d3d12_device->CreateCommandList(
        0U, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
        IID_PPV_ARGS(&command_list));
    if (FAILED(result)) {
      throw std::runtime_error(
          HresultFailure("CreateCommandList(DIRECT)", result));
    }

    const std::array<float, 9U> vertices = {
        -1.0F, -1.0F, 0.0F, 1.0F, -1.0F, 0.0F,
        0.0F,  1.0F,  0.0F};
    ComPtr<ID3D12Resource> vertex_buffer = CreateBuffer(
        impl_->d3d12_device.Get(), sizeof(vertices), D3D12_HEAP_TYPE_UPLOAD,
        D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
    UploadBytes(vertex_buffer.Get(), vertices.data(), sizeof(vertices));

    D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
    geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geometry.Triangles.VertexBuffer.StartAddress =
        vertex_buffer->GetGPUVirtualAddress();
    geometry.Triangles.VertexBuffer.StrideInBytes = 3U * sizeof(float);
    geometry.Triangles.VertexCount = 3U;
    geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blas_inputs{};
    blas_inputs.Type =
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    blas_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    blas_inputs.Flags =
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    blas_inputs.NumDescs = 1U;
    blas_inputs.pGeometryDescs = &geometry;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO blas_info{};
    impl_->d3d12_device->GetRaytracingAccelerationStructurePrebuildInfo(
        &blas_inputs, &blas_info);
    if (blas_info.ResultDataMaxSizeInBytes == 0U ||
        blas_info.ScratchDataSizeInBytes == 0U) {
      throw std::runtime_error("DXR returned empty BLAS sizing");
    }
    ComPtr<ID3D12Resource> blas_scratch = CreateBuffer(
        impl_->d3d12_device.Get(),
        AlignUp(blas_info.ScratchDataSizeInBytes,
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT),
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ComPtr<ID3D12Resource> blas = CreateBuffer(
        impl_->d3d12_device.Get(),
        AlignUp(blas_info.ResultDataMaxSizeInBytes,
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT),
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC blas_build{};
    blas_build.Inputs = blas_inputs;
    blas_build.ScratchAccelerationStructureData =
        blas_scratch->GetGPUVirtualAddress();
    blas_build.DestAccelerationStructureData = blas->GetGPUVirtualAddress();
    command_list->BuildRaytracingAccelerationStructure(&blas_build, 0U,
                                                        nullptr);
    D3D12_RESOURCE_BARRIER blas_barrier{};
    blas_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    blas_barrier.UAV.pResource = blas.Get();
    command_list->ResourceBarrier(1U, &blas_barrier);
    impl_->evidence.blas_built = true;

    D3D12_RAYTRACING_INSTANCE_DESC instance{};
    instance.Transform[0][0] = 1.0F;
    instance.Transform[1][1] = 1.0F;
    instance.Transform[2][2] = 1.0F;
    instance.InstanceMask = 0xffU;
    instance.AccelerationStructure = blas->GetGPUVirtualAddress();
    ComPtr<ID3D12Resource> instance_buffer = CreateBuffer(
        impl_->d3d12_device.Get(), sizeof(instance), D3D12_HEAP_TYPE_UPLOAD,
        D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
    UploadValue(instance_buffer.Get(), instance);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlas_inputs{};
    tlas_inputs.Type =
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    tlas_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    tlas_inputs.Flags =
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    tlas_inputs.NumDescs = 1U;
    tlas_inputs.InstanceDescs = instance_buffer->GetGPUVirtualAddress();
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO tlas_info{};
    impl_->d3d12_device->GetRaytracingAccelerationStructurePrebuildInfo(
        &tlas_inputs, &tlas_info);
    if (tlas_info.ResultDataMaxSizeInBytes == 0U ||
        tlas_info.ScratchDataSizeInBytes == 0U) {
      throw std::runtime_error("DXR returned empty TLAS sizing");
    }
    ComPtr<ID3D12Resource> tlas_scratch = CreateBuffer(
        impl_->d3d12_device.Get(),
        AlignUp(tlas_info.ScratchDataSizeInBytes,
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT),
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ComPtr<ID3D12Resource> tlas = CreateBuffer(
        impl_->d3d12_device.Get(),
        AlignUp(tlas_info.ResultDataMaxSizeInBytes,
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT),
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC tlas_build{};
    tlas_build.Inputs = tlas_inputs;
    tlas_build.ScratchAccelerationStructureData =
        tlas_scratch->GetGPUVirtualAddress();
    tlas_build.DestAccelerationStructureData = tlas->GetGPUVirtualAddress();
    command_list->BuildRaytracingAccelerationStructure(&tlas_build, 0U,
                                                        nullptr);
    D3D12_RESOURCE_BARRIER tlas_barrier{};
    tlas_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    tlas_barrier.UAV.pResource = tlas.Get();
    command_list->ResourceBarrier(1U, &tlas_barrier);
    impl_->evidence.tlas_built = true;

    std::array<D3D12_ROOT_PARAMETER, 2U> root_parameters{};
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    root_parameters[0].Descriptor.ShaderRegister = 0U;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_parameters[1].Descriptor.ShaderRegister = 0U;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC root_description{};
    root_description.NumParameters =
        static_cast<UINT>(root_parameters.size());
    root_description.pParameters = root_parameters.data();
    ComPtr<ID3DBlob> root_blob;
    ComPtr<ID3DBlob> root_error;
    result = D3D12SerializeRootSignature(
        &root_description, D3D_ROOT_SIGNATURE_VERSION_1,
        &root_blob, &root_error);
    if (FAILED(result)) {
      const char* detail = root_error
                               ? static_cast<const char*>(
                                     root_error->GetBufferPointer())
                               : "no compiler detail";
      throw std::runtime_error(std::string("root signature serialization: ") +
                               detail);
    }
    ComPtr<ID3D12RootSignature> root_signature;
    result = impl_->d3d12_device->CreateRootSignature(
        0U, root_blob->GetBufferPointer(), root_blob->GetBufferSize(),
        IID_PPV_ARGS(&root_signature));
    if (FAILED(result)) {
      throw std::runtime_error(
          HresultFailure("ID3D12Device::CreateRootSignature", result));
    }

    constexpr wchar_t kRayGen[] = L"RayGen";
    constexpr wchar_t kMiss[] = L"Miss";
    constexpr wchar_t kClosestHit[] = L"ClosestHit";
    constexpr wchar_t kHitGroup[] = L"HitGroup";
    std::array<D3D12_EXPORT_DESC, 3U> exports{};
    exports[0].Name = kRayGen;
    exports[1].Name = kMiss;
    exports[2].Name = kClosestHit;
    D3D12_DXIL_LIBRARY_DESC library{};
    library.DXILLibrary.pShaderBytecode = dxil.data();
    library.DXILLibrary.BytecodeLength = dxil.size();
    library.NumExports = static_cast<UINT>(exports.size());
    library.pExports = exports.data();
    D3D12_HIT_GROUP_DESC hit_group{};
    hit_group.HitGroupExport = kHitGroup;
    hit_group.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
    hit_group.ClosestHitShaderImport = kClosestHit;
    D3D12_RAYTRACING_SHADER_CONFIG shader_config{};
    shader_config.MaxPayloadSizeInBytes = sizeof(std::uint32_t);
    shader_config.MaxAttributeSizeInBytes = 2U * sizeof(float);
    D3D12_GLOBAL_ROOT_SIGNATURE global_root{};
    global_root.pGlobalRootSignature = root_signature.Get();
    D3D12_RAYTRACING_PIPELINE_CONFIG pipeline_config{};
    pipeline_config.MaxTraceRecursionDepth = 1U;
    std::array<D3D12_STATE_SUBOBJECT, 5U> subobjects{};
    subobjects[0] = {D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &library};
    subobjects[1] = {D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, &hit_group};
    subobjects[2] = {D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG,
                     &shader_config};
    subobjects[3] = {D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE,
                     &global_root};
    subobjects[4] = {D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG,
                     &pipeline_config};
    D3D12_STATE_OBJECT_DESC state_description{};
    state_description.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    state_description.NumSubobjects = static_cast<UINT>(subobjects.size());
    state_description.pSubobjects = subobjects.data();
    ComPtr<ID3D12StateObject> state_object;
    result = impl_->d3d12_device->CreateStateObject(
        &state_description, IID_PPV_ARGS(&state_object));
    if (FAILED(result)) {
      throw std::runtime_error(
          HresultFailure("ID3D12Device5::CreateStateObject", result));
    }
    impl_->evidence.state_object_created = true;
    ComPtr<ID3D12StateObjectProperties> state_properties;
    result = state_object.As(&state_properties);
    if (FAILED(result)) {
      throw std::runtime_error(HresultFailure(
          "QueryInterface(ID3D12StateObjectProperties)", result));
    }
    const void* raygen_id = state_properties->GetShaderIdentifier(kRayGen);
    const void* miss_id = state_properties->GetShaderIdentifier(kMiss);
    const void* hit_id = state_properties->GetShaderIdentifier(kHitGroup);
    if (raygen_id == nullptr || miss_id == nullptr || hit_id == nullptr) {
      throw std::runtime_error("DXR shader identifier resolution failed");
    }
    impl_->evidence.shader_identifiers_resolved = true;

    constexpr std::uint64_t kRecordStride =
        D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
    constexpr std::uint64_t kTableBytes = 3U * kRecordStride;
    ComPtr<ID3D12Resource> shader_table = CreateBuffer(
        impl_->d3d12_device.Get(), kTableBytes, D3D12_HEAP_TYPE_UPLOAD,
        D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
    void* table_mapping = nullptr;
    const D3D12_RANGE no_read{0U, 0U};
    result = shader_table->Map(0U, &no_read, &table_mapping);
    if (FAILED(result) || table_mapping == nullptr) {
      throw std::runtime_error(
          HresultFailure("shader table Map", result));
    }
    std::memset(table_mapping, 0, static_cast<std::size_t>(kTableBytes));
    auto* table_bytes = static_cast<std::uint8_t*>(table_mapping);
    std::memcpy(table_bytes, raygen_id,
                D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    std::memcpy(table_bytes + kRecordStride, miss_id,
                D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    std::memcpy(table_bytes + 2U * kRecordStride, hit_id,
                D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    shader_table->Unmap(0U, nullptr);

    ComPtr<ID3D12Resource> output = CreateBuffer(
        impl_->d3d12_device.Get(), sizeof(std::uint32_t),
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ComPtr<ID3D12Resource> readback = CreateBuffer(
        impl_->d3d12_device.Get(), sizeof(std::uint32_t),
        D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST);

    command_list->SetComputeRootSignature(root_signature.Get());
    command_list->SetPipelineState1(state_object.Get());
    command_list->SetComputeRootShaderResourceView(
        0U, tlas->GetGPUVirtualAddress());
    command_list->SetComputeRootUnorderedAccessView(
        1U, output->GetGPUVirtualAddress());
    const D3D12_GPU_VIRTUAL_ADDRESS table_address =
        shader_table->GetGPUVirtualAddress();
    D3D12_DISPATCH_RAYS_DESC dispatch{};
    dispatch.RayGenerationShaderRecord.StartAddress = table_address;
    dispatch.RayGenerationShaderRecord.SizeInBytes =
        D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    dispatch.MissShaderTable.StartAddress = table_address + kRecordStride;
    dispatch.MissShaderTable.SizeInBytes =
        D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    dispatch.MissShaderTable.StrideInBytes =
        D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    dispatch.HitGroupTable.StartAddress = table_address + 2U * kRecordStride;
    dispatch.HitGroupTable.SizeInBytes =
        D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    dispatch.HitGroupTable.StrideInBytes =
        D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    dispatch.Width = 1U;
    dispatch.Height = 1U;
    dispatch.Depth = 1U;
    command_list->DispatchRays(&dispatch);
    impl_->evidence.dispatch_rays_called = true;
    impl_->evidence.dispatch_width = dispatch.Width;
    impl_->evidence.dispatch_height = dispatch.Height;
    impl_->evidence.dispatch_depth = dispatch.Depth;

    D3D12_RESOURCE_BARRIER output_barrier{};
    output_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    output_barrier.Transition.pResource = output.Get();
    output_barrier.Transition.StateBefore =
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    output_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    output_barrier.Transition.Subresource =
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list->ResourceBarrier(1U, &output_barrier);
    command_list->CopyBufferRegion(readback.Get(), 0U, output.Get(), 0U,
                                   sizeof(std::uint32_t));
    result = command_list->Close();
    if (FAILED(result)) {
      throw std::runtime_error(
          HresultFailure("ID3D12GraphicsCommandList::Close", result));
    }
    ID3D12CommandList* command_lists[] = {command_list.Get()};
    impl_->direct_queue->ExecuteCommandLists(
        static_cast<UINT>(std::size(command_lists)), command_lists);
    Dxr7BootstrapResult wait = impl_->WaitForFence(2U);
    if (!wait.ready()) {
      return wait;
    }
    impl_->evidence.fence_after_dispatch = 2U;

    void* readback_mapping = nullptr;
    const D3D12_RANGE read_range{0U, sizeof(std::uint32_t)};
    result = readback->Map(0U, &read_range, &readback_mapping);
    if (FAILED(result) || readback_mapping == nullptr) {
      throw std::runtime_error(
          HresultFailure("readback Map", result));
    }
    std::memcpy(&impl_->evidence.readback_value, readback_mapping,
                sizeof(impl_->evidence.readback_value));
    const D3D12_RANGE no_write{0U, 0U};
    readback->Unmap(0U, &no_write);
    impl_->evidence.closest_hit_readback_exact =
        impl_->evidence.readback_value == kClosestHitValue;
    if (!impl_->evidence.closest_hit_readback_exact) {
      return Failure("DXR dispatch did not return the exact closest-hit value");
    }
    return Ready();
  } catch (const std::exception& error) {
    return Failure(error.what());
  }
}

std::uintptr_t
OgreNextD3D12DxrBootstrap::external_d3d11_device_address() const noexcept {
  return reinterpret_cast<std::uintptr_t>(impl_->d3d11_device.Get());
}

Dxr7BootstrapResult
OgreNextD3D12DxrBootstrap::MarkOgreAttached() noexcept {
  if (!impl_->d3d11_device || impl_->ogre_attached ||
      impl_->evidence.ogre_shutdown_before_d3d11_release) {
    return Failure("DXR7 Ogre attachment is out of order");
  }
  impl_->ogre_attached = true;
  impl_->evidence.ogre_external_device_option_used = true;
  return Ready();
}

Dxr7BootstrapResult OgreNextD3D12DxrBootstrap::VerifyOgreAdoption(
    Ogre::RenderSystem* render_system) noexcept {
  if (!impl_->ogre_attached || render_system == nullptr) {
    return Failure("DXR7 Ogre adoption verification is out of order");
  }
  ID3D11Device* ogre_device = nullptr;
  bool external_active = false;
  render_system->getCustomAttribute("D3DDEVICE", &ogre_device);
  render_system->getCustomAttribute("D3D11_EXTERNAL_DEVICE_ACTIVE",
                                    &external_active);
  impl_->evidence.ogre_d3d11_device_exact =
      SameComIdentity(ogre_device, impl_->d3d11_device.Get());
  impl_->evidence.ogre_external_device_active = external_active;
  if (!impl_->evidence.ogre_d3d11_device_exact || !external_active) {
    return Failure("Ogre did not retain the exact external D3D11On12 device");
  }
  return Ready();
}

Dxr7BootstrapResult OgreNextD3D12DxrBootstrap::RecordOgreFrameProof(
    std::uint32_t width, std::uint32_t height,
    std::uint32_t distinct_pixels,
    std::uint32_t non_background_pixels,
    std::uint64_t fnv1a64, bool ui_free,
    const Dxr7OgreTeardownContract& teardown) noexcept {
  if (!impl_->ogre_attached ||
      !impl_->evidence.ogre_d3d11_device_exact ||
      !impl_->evidence.ogre_external_device_active || width == 0U ||
      height == 0U || distinct_pixels < 8U ||
      non_background_pixels < 512U || fnv1a64 == 0U || !ui_free ||
      !teardown.workspace_removed ||
      !teardown.workspace_definition_removed ||
      !teardown.render_target_destroyed || !teardown.scene_destroyed ||
      !teardown.pbs_datablock_destroyed ||
      !teardown.pbs_hlms_unregistered ||
      !teardown.native_window_destroyed ||
      !teardown.root_shutdown_completed) {
    return Failure("DXR7 Ogre frame proof is incomplete or out of order");
  }
  impl_->evidence.ogre_native_window_created = true;
  impl_->evidence.ogre_pbs_material_created = true;
  impl_->evidence.ogre_compositor_workspace_created = true;
  impl_->evidence.ogre_frame_submitted = true;
  impl_->evidence.ogre_frame_readback_completed = true;
  impl_->evidence.ogre_frame_nonblank = true;
  impl_->evidence.ogre_frame_ui_free = ui_free;
  impl_->evidence.ogre_frame_resources_destroyed = true;
  impl_->evidence.ogre_teardown = teardown;
  impl_->evidence.ogre_frame_width = width;
  impl_->evidence.ogre_frame_height = height;
  impl_->evidence.ogre_frame_distinct_pixels = distinct_pixels;
  impl_->evidence.ogre_frame_non_background_pixels =
      non_background_pixels;
  impl_->evidence.ogre_frame_fnv1a64 = fnv1a64;
  return Ready();
}

Dxr7BootstrapResult
OgreNextD3D12DxrBootstrap::MarkOgreDetached() noexcept {
  if (!impl_->ogre_attached) {
    return Failure("DXR7 Ogre detachment is out of order");
  }
  impl_->ogre_attached = false;
  impl_->evidence.ogre_shutdown_before_d3d11_release = true;
  return Ready();
}

Dxr7BootstrapResult
OgreNextD3D12DxrBootstrap::ProveFenceAfterOgre() {
  if (impl_->ogre_attached ||
      !impl_->evidence.ogre_shutdown_before_d3d11_release ||
      impl_->evidence.fence_after_dispatch != 2U) {
    return Failure("DXR7 post-Ogre fence proof is out of order");
  }
  impl_->d3d11_context->Flush();
  Dxr7BootstrapResult result = impl_->WaitForFence(3U);
  if (result.ready()) {
    impl_->evidence.fence_after_ogre = 3U;
  }
  return result;
}

Dxr7BootstrapResult OgreNextD3D12DxrBootstrap::Shutdown() noexcept {
  if (impl_->ogre_attached || impl_->evidence.shutdown_completed) {
    return Failure("DXR7 owner shutdown is out of order");
  }
  if (impl_->d3d12_device &&
      (impl_->evidence.fence_before_dispatch != 1U ||
       impl_->evidence.fence_after_dispatch != 2U ||
       impl_->evidence.fence_after_ogre != 3U)) {
    return Failure("DXR7 owner shutdown requires the complete fence sequence");
  }
  return impl_->DestroyOwnedObjects();
}

Dxr7BootstrapResult
OgreNextD3D12DxrBootstrap::AbortAfterFailure() noexcept {
  if (impl_->ogre_attached) {
    impl_->ogre_attached = false;
  }
  return impl_->DestroyOwnedObjects();
}

const Dxr7BootstrapEvidence& OgreNextD3D12DxrBootstrap::evidence()
    const noexcept {
  return impl_->evidence;
}

Dxr7PassContract OgreNextD3D12DxrBootstrap::pass_contract() const noexcept {
  Dxr7PassContract contract;
  contract.candidate = impl_->evidence.candidate;
  contract.d3d11on12_device_created =
      impl_->evidence.d3d11on12_device_created;
  contract.d3d11on12_created_with_exact_direct_queue =
      impl_->evidence.d3d11on12_created_with_exact_direct_queue;
  contract.d3d11on12_underlying_d3d12_device_exact =
      impl_->evidence.d3d11on12_underlying_d3d12_device_exact;
  contract.d3d11on12_adapter_luid_exact =
      impl_->evidence.d3d11on12_adapter_luid_exact;
  contract.ogre_external_device_option_used =
      impl_->evidence.ogre_external_device_option_used;
  contract.ogre_d3d11_device_exact =
      impl_->evidence.ogre_d3d11_device_exact;
  contract.ogre_external_device_active =
      impl_->evidence.ogre_external_device_active;
  contract.ogre_native_window_created =
      impl_->evidence.ogre_native_window_created;
  contract.ogre_pbs_material_created =
      impl_->evidence.ogre_pbs_material_created;
  contract.ogre_compositor_workspace_created =
      impl_->evidence.ogre_compositor_workspace_created;
  contract.ogre_frame_submitted = impl_->evidence.ogre_frame_submitted;
  contract.ogre_frame_readback_completed =
      impl_->evidence.ogre_frame_readback_completed;
  contract.ogre_frame_nonblank = impl_->evidence.ogre_frame_nonblank;
  contract.ogre_frame_ui_free = impl_->evidence.ogre_frame_ui_free;
  contract.ogre_frame_resources_destroyed =
      impl_->evidence.ogre_frame_resources_destroyed;
  contract.ogre_teardown = impl_->evidence.ogre_teardown;
  contract.blas_built = impl_->evidence.blas_built;
  contract.tlas_built = impl_->evidence.tlas_built;
  contract.state_object_created = impl_->evidence.state_object_created;
  contract.shader_identifiers_resolved =
      impl_->evidence.shader_identifiers_resolved;
  contract.dispatch_rays_called = impl_->evidence.dispatch_rays_called;
  contract.closest_hit_readback_exact =
      impl_->evidence.closest_hit_readback_exact;
  contract.queue_fence_before_dispatch =
      impl_->evidence.fence_before_dispatch == 1U;
  contract.queue_fence_after_dispatch =
      impl_->evidence.fence_after_dispatch == 2U;
  contract.queue_fence_after_ogre = impl_->evidence.fence_after_ogre == 3U;
  contract.ogre_shutdown_before_d3d11_release =
      impl_->evidence.ogre_shutdown_before_d3d11_release;
  contract.d3d11_context_flushed_before_release =
      impl_->evidence.d3d11_context_flushed_before_release;
  contract.d3d11_released_before_d3d12_queue =
      impl_->evidence.d3d11_released_before_d3d12_queue;
  contract.d3d12_queue_released_before_device =
      impl_->evidence.d3d12_queue_released_before_device;
  contract.shutdown_completed = impl_->evidence.shutdown_completed;
  return contract;
}

}  // namespace RoR::Render
