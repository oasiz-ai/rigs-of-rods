/*
    This source file is part of Rigs of Rods
    Rigs of Rods is free software under the GNU General Public License v3.
*/

/// @file
/// @brief Live RoR/OGRE telemetry, control-lineage and UI-free RGB provider.

#pragma once

#include "ForwardDeclarations.h"
#include "WorldModelRuntimeBackend.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace RoR {
namespace WorldModel {

/// Immutable episode metadata that cannot be reconstructed safely from display
/// names or mutable runtime CVars. IDs use the world-model identifier grammar;
/// content hashes are supplied by the episode provenance builder.
struct RoRRuntimeCaptureConfig
{
    std::string target_id;
    std::string vehicle_sha256;
    std::string world_id;
    std::string terrain_id;
    std::string terrain_sha256;
    std::string weather_id;
    std::string camera_id;
    std::string coordinate_frame;
    std::vector<std::string> control_ids;
    std::uint64_t state_digest_scenario_id = 0U;
    std::uint32_t rgb_width = 1920U;
    std::uint32_t rgb_height = 1080U;
};

/// Content-addressed identity recomputed from the resources that are actually
/// loaded by the current Actor and Terrain. Schema 1 IDs are not aliases:
/// `target_id` is `ror.vehicle/<vehicle_sha256>` and `terrain_id` is
/// `ror.terrain/<terrain_sha256>`.
struct RoRRuntimeResourceIdentity
{
    std::string target_id;
    std::string vehicle_sha256;
    std::string terrain_id;
    std::string terrain_sha256;
};

/// Provider-derived profiles which bind episode provenance to the exact
/// capture implementation. Callers must compare `control_ids` with their
/// requested policy and copy these hashes into provenance; hard-coded aliases
/// are not an acceptable substitute.
struct RoRRuntimeCaptureDescriptor
{
    std::vector<std::string> control_ids;
    std::string controller_profile_id;
    std::string controller_profile_sha256;
    std::string camera_profile_id;
    std::string camera_profile_sha256;
};

/// Recomputes current loaded-resource identity from OGRE resource bytes. This
/// is the source for live provenance; callers must not invent these fields.
bool InspectCurrentRoRRuntimeResourceIdentity(
    RoRRuntimeResourceIdentity& identity,
    std::string* error = nullptr);

/// Production provider for a single, stable player Actor.
///
/// Physics must be joined before baseline/completed capture. The provider is
/// intentionally observational: it reads input/Actor/Engine state, creates a
/// dedicated overlay-free OGRE render target, and never applies controls.
class RoRRuntimeCaptureProvider final : public RuntimeCaptureProvider
{
public:
    RoRRuntimeCaptureProvider(
        ActorManager& actor_manager,
        ActorPtr player_actor,
        const RoRRuntimeCaptureConfig& config);
    ~RoRRuntimeCaptureProvider() override;

    RoRRuntimeCaptureProvider(
        const RoRRuntimeCaptureProvider&) = delete;
    RoRRuntimeCaptureProvider& operator=(
        const RoRRuntimeCaptureProvider&) = delete;

    bool IsReady(std::string* error = nullptr) const;

    /// Returns canonical SHA-256 profiles derived from the actual ordered
    /// controls, raw-source semantics, fixed camera constants, and fail-closed
    /// render-environment policy implemented by this provider.
    bool InspectCaptureDescriptor(
        RoRRuntimeCaptureDescriptor& descriptor,
        std::string* error = nullptr) const;

    /// Lets the provenance builder seal the exact joined reset state before it
    /// constructs CaptureConfig. This uses the same digest path as observations.
    bool CaptureCurrentStateSha256(
        std::uint64_t completed_physics_steps,
        std::string& state_sha256) const;

    bool CaptureResetBaseline(
        const ObservationId& expected_id,
        ObservationSample& observation) override;
    bool BeginTransition(
        const TransitionId& transition) override;
    bool ObserveAppliedInputAtFixedStepStart(
        const FixedStepCaptureBridge::StepStartIdentity& identity) override;
    bool CaptureCompletedTransition(
        const TransitionId& expected_transition,
        TransitionSample& transition,
        ObservationSample& observation) override;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

/// Owns the complete live runtime boundary for one player/episode. Capture
/// policy, provenance, reset, and artifact paths remain explicit caller-owned
/// decisions rather than hidden global state.
class RoRWorldModelRuntime
{
public:
    RoRWorldModelRuntime(
        ActorManager& actor_manager,
        ActorPtr player_actor,
        const RoRRuntimeCaptureConfig& config);
    ~RoRWorldModelRuntime();

    RoRWorldModelRuntime(const RoRWorldModelRuntime&) = delete;
    RoRWorldModelRuntime& operator=(const RoRWorldModelRuntime&) = delete;

    CaptureBackend& GetBackend();
    RoRRuntimeCaptureProvider& GetProvider();

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

/// Minimal game wiring for the currently loaded simulation. Returns null until
/// GameContext, ActorManager and a player Actor all exist.
std::unique_ptr<RoRWorldModelRuntime>
CreateCurrentRoRWorldModelRuntime(
    const RoRRuntimeCaptureConfig& config,
    std::string* error = nullptr);

} // namespace WorldModel
} // namespace RoR
