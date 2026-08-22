/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Fail-closed policy for the first isolated Ogre-Next frontend.

#pragma once

#include "../RendererFrontend.h"
#include "OgreNextHdrSceneTopology.h"
#include "OgreNextPssmShadowPolicy.h"
#include "RasterFeatureTier.h"

#include <map>
#include <memory>
#include <vector>

namespace RoR::Render {

// Before a render device exists, N1 advertises only a conservative extent.
// The concrete frontend replaces this with the device-reported 2D limit after
// initialization and validates the requested offscreen extent against it.
constexpr std::uint32_t kOgreNextN1ConservativeMaximumTextureDimension = 2048U;
constexpr std::size_t kOgreNextN1MaximumDirectionalLights = 0U;
constexpr std::size_t kOgreNextRt4MaximumDirectionalLights = 1U;
/// Stage 2: RT4/V1 admits point/spot lights through Forward+ clustered
/// shading. The visible bound governs lights with positive intensity - the
/// producer's LOCAL_LIGHT_ACTIVE_BUDGET_MAXIMUM equals it so a raised
/// budget can never out-run presenter admission. The record bound only
/// stops runaway native Light allocation: over-budget records legitimately
/// keep publishing at zero intensity because a destroyed portable light
/// identity may never return (live-verified at 260 records / 256 visible).
constexpr std::size_t kOgreNextRt4MaximumLocalLights = 256U;
constexpr std::size_t kOgreNextRt4MaximumLocalLightRecords = 1024U;
/// Ogre's reference HDR scene scales physical illuminance by 2^-10 to keep
/// direct-sun values inside RGBA16_FLOAT headroom. RT4/V1 adopts that exact,
/// renderer-independent mapping for its one admitted directional light.
constexpr float kOgreNextRt4LuxToNativePowerScale = 1.0F / 1024.0F;
/// An 8-bit UNORM channel spans [-1, 1] in steps of 2/255 after canonical
/// normal decoding. A nearest-quantized authored B channel may therefore
/// differ from Ogre's reconstructed positive Z by at most half a step.
constexpr double kOgreNextRt4NormalDecodedQuantizationTolerance = 1.0 / 255.0;

/// Native analytic-sky geometry uses two disconnected 16-ring hemispheres so
/// the horizon-to-ground discontinuity is exact, plus a spherical 32-segment
/// sun cap. The background is one internal camera-centred object, not a
/// portable scene instance or identity.
constexpr std::uint32_t kOgreNextAnalyticSkyHemisphereRings = 16U;
constexpr std::uint32_t kOgreNextAnalyticSkyLongitudeSegments = 64U;
constexpr std::uint32_t kOgreNextAnalyticSkySunSegments = 32U;
/// A descriptor with cloud_coverage > 0 densifies only the upper hemisphere
/// so the per-vertex cloud field resolves individual shapes; the lower
/// hemisphere keeps the legacy density because ground radiance is constant.
/// The vertex layout is unchanged - clouds are baked into vertex radiance.
constexpr std::uint32_t kOgreNextAnalyticSkyCloudRings = 48U;
constexpr std::uint32_t kOgreNextAnalyticSkyCloudSegments = 128U;

struct OgreNextAnalyticSkyNativeVertex final {
  Float3 position;
  Float4 radiance{0.0F, 0.0F, 0.0F, 1.0F};
};

struct OgreNextAnalyticSkyNativeMesh final {
  std::vector<OgreNextAnalyticSkyNativeVertex> background_vertices;
  std::vector<std::uint32_t> background_indices;
  std::vector<OgreNextAnalyticSkyNativeVertex> sun_vertices;
  std::vector<std::uint32_t> sun_indices;
};

/// Builds the complete camera-local native geometry for one enabled analytic
/// sky. Radiance is pre-multiplied by environment_intensity exactly once.
/// Candidate vectors publish together; failure leaves `mesh` untouched.
[[nodiscard]] ValidationResult BuildOgreNextAnalyticSkyNativeMesh(
    const SceneEnvironmentDescriptor &environment,
    const LightDescriptor &sun, float radius,
    OgreNextAnalyticSkyNativeMesh &mesh);

/// SH-9 sky irradiance in the exact HlmsPbs AmbientSh shader polynomial basis
/// {1, y, z, x, y*x, y*z, 3z^2-1, z*x, x^2-y^2}. The shader carries no
/// spherical-harmonic constants at all - `irradianceSH(n)` is a raw dot
/// product against these nine coefficients - so every real-SH normalization
/// and Lambertian cosine-lobe factor (A0=pi, A1=2pi/3, A2=pi/4) is pre-folded
/// here. Coefficients feed Ogre::SceneManager::setSphericalHarmonics
/// unchanged. mean_irradiance is the band-0 sphere mean E; up_irradiance is
/// E(+Y); calibration_gain is the derived seat factor described at the
/// builder. All values are calibrated, finite binary32 on success.
struct OgreNextAnalyticSkyAmbientSh final {
  Float3 coefficients[9U]{};
  Float3 mean_irradiance{};
  Float3 up_irradiance{};
  float calibration_gain = 0.0F;
};

/// Integrates the transported analytic-sky descriptor into SH-9 irradiance
/// for HlmsPbs AmbientSh, excluding the sun disc (it is already the one
/// calibrated directional light; folding it again would double-count direct
/// sun and invite band-2 ringing). The dome radiance model matches
/// BuildOgreNextAnalyticSkyNativeMesh exactly: the upper hemisphere lerps
/// horizon->zenith linearly in elevation sine, the lower hemisphere is
/// constant ground radiance, clouds pull the upper hemisphere toward
/// cloud_radiance, and everything scales by environment_intensity once.
/// Azimuthal symmetry about +Y makes the integral zonal and closed-form.
///
/// The absolute level is seated, not assumed: AmbientFixed contributes
/// `ambient * albedo / pi` per pixel (HlmsPbs kD folds 1/pi into diffuse)
/// while AmbientSh contributes `E(n) * albedo`, and the raw descriptor
/// irradiance is roughly an order of magnitude above the calibrated
/// AmbientFixed scalar - the exact wash-out that retired the hemisphere
/// split. The gain therefore equates the Rec.709 luminance of the SH sphere
/// mean (band 0) with the AmbientFixed level derived from the same snapshot,
/// so band 1/2 redistribute light between sky-facing and ground-facing
/// surfaces around an unchanged average. Returns false (output untouched)
/// whenever the sky is disabled or any derived value is non-finite or
/// degenerate; the caller degrades to the AmbientFixed scalar for that
/// present.
[[nodiscard]] bool BuildOgreNextAnalyticSkyAmbientShCoefficients(
    const SceneEnvironmentDescriptor &environment,
    OgreNextAnalyticSkyAmbientSh &sh) noexcept;

/// Bounds after the portable descriptor has been reduced with overflow-safe
/// float arithmetic into Ogre's center/half-size representation.
struct OgreNextN1NativeMeshBounds final {
  Float3 center;
  Float3 half_size;
  float radius = 0.0F;
};

/// Exact texture-coordinate profile lowered into the frontend-owned PBS
/// shader piece. Pinned Ogre exposes three generic material float4 values but
/// no per-base-slot texture-transform API, so RT4/V1 admits one shared affine
/// across every bound PBS texture in a material. This preserves the authored
/// A0 road/wet/lane mappings without silently approximating independent slot
/// transforms or rotation.
struct OgreNextN1PbsUv0AffineTransform final {
  Float2 scale{1.0F, 1.0F};
  Float2 offset;
  std::uint32_t portable_texture_binding_count = 0U;
  std::uint32_t native_texture_slot_count = 0U;
  /// Detail slots are counted apart from the shared affine because each one
  /// keeps its own UV scale in the native datablock instead of joining
  /// userValue[0]. The frontend still has to account for every bound slot.
  std::uint32_t native_detail_texture_slot_count = 0U;
  bool transformed = false;
};

/// Builds the exact shared UV0 scale/offset profile for one RT4/V1 PBS
/// material. The candidate is published only on success. UV1, non-positive or
/// non-finite scale, non-finite offset, rotation, noncanonical absent state,
/// and differing transforms among bound slots all fail closed.
[[nodiscard]] ValidationResult BuildOgreNextN1PbsUv0AffineTransform(
    const MaterialDescriptor &material,
    OgreNextN1PbsUv0AffineTransform &transform,
    std::size_t material_index = ValidationResult::kNoElement);

/// Returns false when finite portable bounds would manufacture a non-finite
/// Ogre Aabb or bounding-sphere value during native float arithmetic.
[[nodiscard]] bool TryBuildOgreNextN1NativeMeshBounds(
    const Bounds3 &portable,
    OgreNextN1NativeMeshBounds &native) noexcept;

/// Returns false when Ogre's float TRS/Aabb evaluation could overflow while
/// composing an otherwise valid local bound with an otherwise valid TRS.
[[nodiscard]] bool CanRepresentOgreNextN1WorldBounds(
    const Bounds3 &local_bounds,
    const Matrix4x4 &render_from_object) noexcept;

/// Lifetime identity state for N1's synchronous one-frame adapter. It retains
/// weak ownership identities only while their caller-owned snapshots remain
/// alive. N1 additionally requires contiguous frame IDs beginning at one, so
/// completion is represented by one high-water mark instead of per-frame data.
class OgreNextN1SubmissionState final {
public:
  [[nodiscard]] RenderOperationResult
  Validate(const RenderFrameRequest &request) const;
  /// Performs the only potentially allocating identity insertion before a
  /// backend frame transaction reaches its no-fail publication point.
  [[nodiscard]] RenderOperationResult
  PrepareCommit(const RenderFrameRequest &request);
  [[nodiscard]] bool
  CanCommitPrepared(const RenderFrameRequest &request) const noexcept;
  /// Publishes a request previously accepted by PrepareCommit. This operation
  /// is allocation-free so it can follow native reflection finalization.
  void CommitPrepared(const RenderFrameRequest &request) noexcept;
  void AbortPrepared() noexcept;
  void Commit(const RenderFrameRequest &request);
  [[nodiscard]] bool IsFrameComplete(std::uint64_t frame_id) const noexcept;
  [[nodiscard]] std::size_t TrackedSnapshotIdentityCount() const noexcept;
  void Reset() noexcept;

private:
  std::map<std::uint64_t, std::weak_ptr<const SceneSnapshot>> snapshots_;
  std::shared_ptr<const SceneSnapshot> pending_snapshot_;
  std::uint64_t pending_frame_id_ = 0U;
  std::uint64_t pending_snapshot_id_ = 0U;
  bool pending_inserted_snapshot_ = false;
  std::uint64_t last_frame_id_ = 0U;
  std::uint64_t last_snapshot_id_ = 0U;
};

/// Converts the renderer-boundary right-handed [0, 1] depth projection into
/// Ogre's canonical right-handed [-1, 1] clip convention. The native render
/// system then performs its one normal API-specific projection conversion.
[[nodiscard]] bool TryConvertPortableProjectionToOgreClip(
    const Matrix4x4 &portable, Matrix4x4 &converted) noexcept;

/// The N1 adapter deliberately reports only what its shipping code path has
/// proved: one colour view, immutable v2 base geometry, synchronous full
/// deformable-mesh replacement for the current frame, and CPU readback. A
/// concrete native API is not exported by this milestone.
[[nodiscard]] FrontendCapabilityReport
BuildOgreNextN1CapabilityReport(RasterGraphicsApi raster_api,
                                const char *frontend_version);

[[nodiscard]] ValidationResult ValidateOgreNextN1Initialization(
    const FrontendInitializationRequest &request,
    const FrontendCapabilityReport &capabilities,
    bool native_presentation_enabled = false);
[[nodiscard]] ValidationResult
ValidateOgreNextN1AssetCatalog(const RenderAssetRegistry &registry,
                               bool allow_dynamic_meshes = false,
                               OgreNextRasterFeatureTier raster_feature_tier =
                                   OgreNextRasterFeatureTier::STATIC_PBR_N1);
/// Validates every sampler reachable from an admitted RT4/V1 material against
/// the exact active-device anisotropy limit. In particular, the authored
/// SPECULAR slot participates; no backend clamping is permitted.
[[nodiscard]] ValidationResult ValidateOgreNextN1SamplerDeviceLimits(
    const RenderAssetRegistry &registry, float maximum_anisotropy,
    OgreNextRasterFeatureTier raster_feature_tier =
        OgreNextRasterFeatureTier::STATIC_PBR_N1);
[[nodiscard]] ValidationResult ValidateOgreNextN1Scene(
    const SceneSnapshot &snapshot, const RenderAssetRegistry &registry,
    bool allow_dynamic_meshes = false,
    OgreNextRasterFeatureTier raster_feature_tier =
        OgreNextRasterFeatureTier::STATIC_PBR_N1,
    OgreNextDirectionalShadowMode shadow_mode =
        OgreNextDirectionalShadowMode::DISABLED,
    bool hdr_compositor_enabled = false,
    bool native_directional_shadow_enabled = false,
    OgreNextHdrSceneTopology hdr_scene_topology =
        OgreNextHdrSceneTopology::DIRECTIONAL_SPLIT_V2,
    bool native_sun_visibility_v2_enabled = false);
[[nodiscard]] ValidationResult ValidateOgreNextN1Frame(
    const RenderFrameRequest &request,
    const FrontendCapabilityReport &capabilities,
    const RenderAssetRegistry &registry,
    OgreNextRasterFeatureTier raster_feature_tier =
        OgreNextRasterFeatureTier::STATIC_PBR_N1,
    OgreNextDirectionalShadowMode shadow_mode =
        OgreNextDirectionalShadowMode::DISABLED,
    bool hdr_compositor_enabled = false,
    bool native_directional_shadow_enabled = false,
    bool native_presentation_enabled = false,
    bool native_sun_visibility_v2_enabled = false,
    OgreNextHdrSceneTopology hdr_scene_topology =
        OgreNextHdrSceneTopology::DIRECTIONAL_SPLIT_V2);

[[nodiscard]] RenderOperationResult
OgreNextN1OperationFromValidation(const ValidationResult &validation);

} // namespace RoR::Render
