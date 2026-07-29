/*
    This source file is part of Rigs of Rods
    Rigs of Rods is free software under the GNU General Public License v3.
*/

#include "RoRRuntimeCaptureProvider.h"

#include "Actor.h"
#include "ActorManager.h"
#include "ActorStateDigestAdapter.h"
#include "AppContext.h"
#include "Application.h"
#include "CacheSystem.h"
#include "EpisodeFormat.h"
#include "GameContext.h"
#include "GfxActor.h"
#include "GfxScene.h"
#include "InputEngine.h"
#include "RoRWorldModelRuntimeAdapter.h"
#include "ScriptEngine.h"
#include "Terrain.h"
#include "TerrainObjectManager.h"
#include "WorldModelCaptureEncoding.h"
#include "WorldModelLiveCaptureConfig.h"

#include <OgreCamera.h>
#include <OgreColourValue.h>
#include <OgreDataStream.h>
#include <OgreHardwarePixelBuffer.h>
#include <OgreMatrix4.h>
#include <OgrePixelFormat.h>
#include <OgreRenderTarget.h>
#include <OgreResourceGroupManager.h>
#include <OgreRoot.h>
#include <OgreSceneManager.h>
#include <OgreTextureManager.h>
#include <OgreViewport.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <set>
#include <sstream>
#include <utility>

namespace {

using RoR::WorldModel::CameraTelemetry;
using RoR::WorldModel::ContactSummary;
using RoR::WorldModel::ControlLineage;
using RoR::WorldModel::ControlSample;
using RoR::WorldModel::EngineTelemetry;
using RoR::WorldModel::EventRecord;
using RoR::WorldModel::ObservationId;
using RoR::WorldModel::ObservationRecord;
using RoR::WorldModel::ObservationSample;
using RoR::WorldModel::QuaternionWxyz;
using RoR::WorldModel::RoRRuntimeCaptureConfig;
using RoR::WorldModel::TransitionId;
using RoR::WorldModel::TransitionRecord;
using RoR::WorldModel::TransitionSample;
using RoR::WorldModel::Vector3;

const char CONTROL_BRAKE[] = "vehicle.brake";
const char CONTROL_CLUTCH[] = "vehicle.clutch";
const char CONTROL_PARKING_BRAKE[] = "vehicle.parking-brake";
const char CONTROL_STEERING[] = "vehicle.steering";
const char CONTROL_THROTTLE[] = "vehicle.throttle";
const char SCHEMA1_CAMERA_ID[] = "driver/main";
const char SCHEMA1_COORDINATE_FRAME[] = "ror.world.rh-y-up";
const char SCHEMA1_WEATHER_ID[] = "ror.weather/static";
const char SCHEMA1_CONTROLLER_PROFILE_ID[] =
    "ror.controller/physical-event-aggregate-v1";
const char RAW_PHYSICAL_SOURCE_ID[] = "input.physical-events";
const char ISSUED_CONTROL_SOURCE_ID[] = "control.actor-issued";
const char RESOLVED_CONTROL_SOURCE_ID[] = "vehicle.truck-resolver";
const char APPLIED_CONTROL_SOURCE_ID[] = "physics.actor-state";
const Ogre::Real SCHEMA1_CAMERA_FOV_DEGREES = 60.0f;
const Ogre::Real SCHEMA1_CAMERA_NEAR_CLIP_METERS = 0.1f;
const Ogre::Real SCHEMA1_CAMERA_FAR_CLIP_METERS = 2000.0f;

const double FIXED_STEP_SECONDS = 1.0 / 2000.0;
const double PI = 3.141592653589793238462643383279502884;

std::atomic<std::uint64_t> g_render_resource_sequence(1U);

double CanonicalDouble(double value)
{
    return value == 0.0 ? 0.0 : value;
}

double UnitValue(double value)
{
    // Preserve the live value. Typed record validation rejects values outside
    // [0,1]; silently clamping a corrupt controller or drivetrain state would
    // make the recorded action differ from the game state.
    return CanonicalDouble(value);
}

double SteeringValue(double value)
{
    if (!std::isfinite(value))
        return value;
    return CanonicalDouble(std::max(-1.0, std::min(1.0, value)));
}

bool FiniteVector(const Ogre::Vector3& value)
{
    return
        std::isfinite(static_cast<double>(value.x)) &&
        std::isfinite(static_cast<double>(value.y)) &&
        std::isfinite(static_cast<double>(value.z));
}

bool FiniteQuaternion(const Ogre::Quaternion& value)
{
    return
        std::isfinite(static_cast<double>(value.w)) &&
        std::isfinite(static_cast<double>(value.x)) &&
        std::isfinite(static_cast<double>(value.y)) &&
        std::isfinite(static_cast<double>(value.z));
}

bool ComputeResourceGroupSha256(
    const RoR::CacheEntryPtr& entry,
    std::string& sha256)
{
    if (entry == nullptr ||
        entry->fname.empty() ||
        entry->resource_group.empty() ||
        Ogre::ResourceGroupManager::getSingletonPtr() == nullptr)
    {
        return false;
    }

    try
    {
        Ogre::ResourceGroupManager& resources =
            Ogre::ResourceGroupManager::getSingleton();
        Ogre::StringVectorPtr names =
            resources.listResourceNames(
                entry->resource_group,
                false);
        if (!names || names->empty())
            return false;
        std::sort(names->begin(), names->end());
        if (std::adjacent_find(
                names->begin(),
                names->end()) != names->end() ||
            !std::binary_search(
                names->begin(),
                names->end(),
                entry->fname))
        {
            return false;
        }

        // Hash a canonical manifest of every loaded bundle member rather than
        // trusting cache names or a caller-provided digest. Per-file hashing
        // keeps peak memory bounded to the largest resource.
        std::ostringstream manifest;
        manifest
            << "ror.resource-group@1\n"
            << entry->fname.size() << ':' << entry->fname << '\n';
        for (const Ogre::String& name : *names)
        {
            Ogre::DataStreamPtr stream =
                resources.openResource(
                    name,
                    entry->resource_group,
                    nullptr,
                    false);
            if (!stream || !stream->isReadable())
                return false;
            const Ogre::String bytes = stream->getAsString();
            const std::string content_sha256 =
                RoR::WorldModel::ComputeSha256(
                    bytes.data(),
                    bytes.size()).ToHex();
            manifest
                << name.size() << ':' << name << ':'
                << bytes.size() << ':' << content_sha256 << '\n';
        }
        const std::string canonical_manifest = manifest.str();
        sha256 =
            RoR::WorldModel::ComputeSha256(
                canonical_manifest.data(),
                canonical_manifest.size()).ToHex();
        return RoR::WorldModel::IsCanonicalSha256(sha256);
    }
    catch (...)
    {
        return false;
    }
}

bool InspectRuntimeResourceIdentity(
    const RoR::ActorPtr& actor,
    RoR::Terrain* terrain,
    RoR::WorldModel::RoRRuntimeResourceIdentity& identity)
{
    if (actor == nullptr ||
        actor->ar_state == RoR::ActorState::DISPOSED ||
        terrain == nullptr)
    {
        return false;
    }
    RoR::CacheEntryPtr actor_entry = actor->getUsedActorEntry();
    RoR::CacheEntryPtr terrain_entry = terrain->getCacheEntry();
    if (actor_entry == nullptr ||
        terrain_entry == nullptr ||
        actor_entry->fname != actor->ar_filename ||
        terrain_entry->fname != terrain->getTerrainFileName())
    {
        return false;
    }

    RoR::WorldModel::RoRRuntimeResourceIdentity candidate;
    if (!ComputeResourceGroupSha256(
            actor_entry,
            candidate.vehicle_sha256) ||
        !ComputeResourceGroupSha256(
            terrain_entry,
            candidate.terrain_sha256))
    {
        return false;
    }
    candidate.target_id =
        "ror.vehicle/" + candidate.vehicle_sha256;
    candidate.terrain_id =
        "ror.terrain/" + candidate.terrain_sha256;
    identity = std::move(candidate);
    return true;
}

Vector3 ToVector3(const Ogre::Vector3& value)
{
    return {
        CanonicalDouble(static_cast<double>(value.x)),
        CanonicalDouble(static_cast<double>(value.y)),
        CanonicalDouble(static_cast<double>(value.z))};
}

Ogre::Quaternion CanonicalOgreQuaternion(Ogre::Quaternion value)
{
    value.normalise();
    // q and -q encode the same rotation. Pick one representation so camera
    // and vehicle telemetry do not flip signs between identical frames.
    const bool negate =
        value.w < 0.0f ||
        (value.w == 0.0f && value.x < 0.0f) ||
        (value.w == 0.0f && value.x == 0.0f && value.y < 0.0f) ||
        (value.w == 0.0f && value.x == 0.0f &&
            value.y == 0.0f && value.z < 0.0f);
    if (negate)
    {
        value.w = -value.w;
        value.x = -value.x;
        value.y = -value.y;
        value.z = -value.z;
    }
    return value;
}

QuaternionWxyz ToQuaternion(Ogre::Quaternion value)
{
    value = CanonicalOgreQuaternion(value);
    return {
        CanonicalDouble(static_cast<double>(value.w)),
        CanonicalDouble(static_cast<double>(value.x)),
        CanonicalDouble(static_cast<double>(value.y)),
        CanonicalDouble(static_cast<double>(value.z))};
}

template <std::size_t Size>
void CopyMatrix(
    const Ogre::Matrix4& source,
    std::array<double, Size>& destination)
{
    static_assert(Size == 16U, "only 4x4 matrices are supported");
    for (std::size_t row = 0U; row < 4U; ++row)
    {
        for (std::size_t column = 0U; column < 4U; ++column)
        {
            destination[row * 4U + column] =
                CanonicalDouble(
                    static_cast<double>(source[row][column]));
        }
    }
}

bool IsSupportedControl(const std::string& control)
{
    return
        control == CONTROL_BRAKE ||
        control == CONTROL_CLUTCH ||
        control == CONTROL_PARKING_BRAKE ||
        control == CONTROL_STEERING ||
        control == CONTROL_THROTTLE;
}

void AppendControlEvents(
    const std::string& control,
    std::vector<int>& events)
{
    if (control == CONTROL_STEERING)
    {
        events.push_back(RoR::EV_TRUCK_STEER_LEFT);
        events.push_back(RoR::EV_TRUCK_STEER_RIGHT);
    }
    else if (control == CONTROL_BRAKE)
    {
        events.push_back(RoR::EV_TRUCK_BRAKE);
        events.push_back(RoR::EV_TRUCK_BRAKE_MODIFIER_25);
        events.push_back(RoR::EV_TRUCK_BRAKE_MODIFIER_50);
    }
    else if (control == CONTROL_THROTTLE)
    {
        events.push_back(RoR::EV_TRUCK_ACCELERATE);
        events.push_back(RoR::EV_TRUCK_ACCELERATE_MODIFIER_25);
        events.push_back(RoR::EV_TRUCK_ACCELERATE_MODIFIER_50);
    }
    else if (control == CONTROL_CLUTCH)
    {
        events.push_back(RoR::EV_TRUCK_MANUAL_CLUTCH);
        events.push_back(RoR::EV_TRUCK_MANUAL_CLUTCH_MODIFIER_25);
        events.push_back(RoR::EV_TRUCK_MANUAL_CLUTCH_MODIFIER_50);
    }
    else if (control == CONTROL_PARKING_BRAKE)
    {
        events.push_back(RoR::EV_TRUCK_PARKING_BRAKE);
        events.push_back(
            RoR::EV_TRUCK_TRAILER_PARKING_BRAKE);
    }
}

const char* ControlMappingDescription(const std::string& control)
{
    if (control == CONTROL_STEERING)
    {
        return
            "max(steer-right.digital,steer-right.analog)-"
            "max(steer-left.digital,steer-left.analog)";
    }
    if (control == CONTROL_BRAKE)
    {
        return
            "brake*((modifier-25?0.25:0)+(modifier-50?0.50:0));"
            "unmodified-when-no-modifier";
    }
    if (control == CONTROL_THROTTLE)
    {
        return
            "accelerate*((modifier-25?0.25:0)+(modifier-50?0.50:0));"
            "unmodified-when-no-modifier";
    }
    if (control == CONTROL_CLUTCH)
    {
        return
            "manual-clutch*((modifier-25?0.25:0)+"
            "(modifier-50?0.50:0));unmodified-when-no-modifier";
    }
    if (control == CONTROL_PARKING_BRAKE)
    {
        return
            "raw=parking-brake>0.5;advance-input-bounce-once-by-"
            "exact-batch-duration;edge-window=0.2s;resolved=latch-"
            "toggle-if-edge-and-trailer-parking-brake-zero;"
            "trailer-parking-brake-nonzero=forbidden";
    }
    return "";
}

bool ValidateConfig(
    const RoRRuntimeCaptureConfig& config,
    std::string* error)
{
    const auto fail = [error](const std::string& message)
    {
        if (error != nullptr)
            *error = message;
        return false;
    };
    if (!RoR::WorldModel::IsCanonicalWorldModelIdentifier(config.target_id) ||
        !RoR::WorldModel::IsCanonicalSha256(config.vehicle_sha256) ||
        !RoR::WorldModel::IsCanonicalWorldModelIdentifier(config.world_id) ||
        !RoR::WorldModel::IsCanonicalWorldModelIdentifier(config.terrain_id) ||
        !RoR::WorldModel::IsCanonicalSha256(config.terrain_sha256) ||
        !RoR::WorldModel::IsCanonicalWorldModelIdentifier(config.weather_id) ||
        !RoR::WorldModel::IsCanonicalWorldModelIdentifier(config.camera_id) ||
        !RoR::WorldModel::IsCanonicalWorldModelIdentifier(
            config.coordinate_frame))
    {
        return fail("runtime capture metadata is not canonical");
    }
    if (config.camera_id != SCHEMA1_CAMERA_ID)
    {
        return fail(
            "schema 1 camera_id must be driver/main");
    }
    if (config.coordinate_frame != SCHEMA1_COORDINATE_FRAME)
    {
        return fail(
            "schema 1 coordinate_frame must be ror.world.rh-y-up");
    }
    if (config.weather_id != SCHEMA1_WEATHER_ID)
    {
        return fail(
            "schema 1 weather_id must be ror.weather/static");
    }
    if (config.state_digest_scenario_id == 0U)
        return fail("state_digest_scenario_id must be nonzero");
    if (!std::isfinite(config.analog_smoothing) ||
        !std::isfinite(config.analog_sensitivity))
    {
        return fail("runtime analog input parameters must be finite");
    }
    if (config.rgb_width == 0U || config.rgb_height == 0U ||
        config.rgb_width >
            std::numeric_limits<std::uint32_t>::max() / 3U)
    {
        return fail("RGB dimensions are invalid");
    }
    const std::size_t stride =
        static_cast<std::size_t>(config.rgb_width) * 3U;
    if (static_cast<std::size_t>(config.rgb_height) >
        std::numeric_limits<std::size_t>::max() / stride)
    {
        return fail("RGB byte count overflows this platform");
    }
    if (config.control_ids.empty())
        return fail("at least one control id is required");
    std::string previous;
    for (std::size_t index = 0U;
         index < config.control_ids.size();
         ++index)
    {
        const std::string& control = config.control_ids[index];
        if (!IsSupportedControl(control))
            return fail("runtime capture control id is unsupported: " + control);
        if (index != 0U && previous >= control)
            return fail("runtime capture control ids must be sorted and unique");
        previous = control;
    }
    return true;
}

bool BuildCaptureDescriptor(
    const RoRRuntimeCaptureConfig& config,
    RoR::WorldModel::RoRRuntimeCaptureDescriptor& descriptor,
    std::string* error)
{
    if (!ValidateConfig(config, error))
        return false;

    RoR::WorldModel::RoRRuntimeCaptureDescriptor candidate;
    candidate.control_ids = config.control_ids;
    candidate.controller_profile_id =
        SCHEMA1_CONTROLLER_PROFILE_ID;
    candidate.camera_profile_id = config.camera_id;

    std::ostringstream controller;
    controller
        << "ror.controller-profile@1\n"
        << "id=" << candidate.controller_profile_id << '\n'
        << "raw-source=" << RAW_PHYSICAL_SOURCE_ID << '\n'
        << "issued-source=" << ISSUED_CONTROL_SOURCE_ID << '\n'
        << "resolved-source=" << RESOLVED_CONTROL_SOURCE_ID << '\n'
        << "applied-source=" << APPLIED_CONTROL_SOURCE_ID << '\n'
        << "actor-simulated-events=forbidden\n"
        << "input-engine-simulated-events=forbidden\n"
        << "non-physical-policy-values=forbidden\n"
        << "bounce-timers=advance-once-before-resolver-by-exact-batch-duration\n"
        << "controls=" << config.control_ids.size() << '\n';
    for (const std::string& control : config.control_ids)
    {
        controller
            << control.size() << ':' << control << '='
            << ControlMappingDescription(control) << '\n';
    }
    const std::string controller_text = controller.str();
    candidate.controller_profile_sha256 =
        RoR::WorldModel::ComputeSha256(
            controller_text.data(),
            controller_text.size()).ToHex();

    std::ostringstream camera;
    camera
        << "ror.camera-profile@1\n"
        << "id=" << candidate.camera_profile_id << '\n'
        << "coordinate-frame=" << config.coordinate_frame << '\n'
        << "pose=cinecam-0-camera-frame-0-user-look-zero\n"
        << "projection=perspective\n"
        << "vertical-fov-degrees=60\n"
        << "near-clip-meters=1/10\n"
        << "far-clip-meters=2000\n"
        << "width=" << config.rgb_width << '\n'
        << "height=" << config.rgb_height << '\n'
        << "pixel-format=rgb8\n"
        << "color-space=srgb-hardware-gamma\n"
        << "row-origin=top-left\n"
        << "overlays=disabled\n"
        << "skies=disabled;sky-mode=sandstorm-static-required\n"
        << "shadows=disabled\n"
        << "water=forbidden\n"
        << "environment-map=forbidden\n"
        << "terrain-time-varying-visuals=forbidden\n"
        << "terrain-editor-mutation=forbidden\n"
        << "vegetation=forbidden\n"
        << "particles=forbidden\n"
        << "flares=forbidden\n"
        << "video-cameras=forbidden\n"
        << "skidmarks=forbidden\n"
        << "actor-stateful-prop-animation=forbidden\n"
        << "driver-character=joined-state-synchronized\n"
        << "display-camera-state=overridden-and-restored\n";
    const std::string camera_text = camera.str();
    candidate.camera_profile_sha256 =
        RoR::WorldModel::ComputeSha256(
            camera_text.data(),
            camera_text.size()).ToHex();

    if (!RoR::WorldModel::IsCanonicalWorldModelIdentifier(
            candidate.controller_profile_id) ||
        !RoR::WorldModel::IsCanonicalSha256(
            candidate.controller_profile_sha256) ||
        !RoR::WorldModel::IsCanonicalWorldModelIdentifier(
            candidate.camera_profile_id) ||
        !RoR::WorldModel::IsCanonicalSha256(
            candidate.camera_profile_sha256))
    {
        if (error != nullptr)
            *error = "runtime capture descriptor is not canonical";
        return false;
    }
    descriptor = std::move(candidate);
    return true;
}

std::string GearboxModeName(RoR::SimGearboxMode mode)
{
    switch (mode)
    {
    case RoR::SimGearboxMode::AUTO:
        return "automatic";
    case RoR::SimGearboxMode::SEMI_AUTO:
        return "semi-automatic";
    case RoR::SimGearboxMode::MANUAL:
        return "manual";
    case RoR::SimGearboxMode::MANUAL_STICK:
        return "manual-stick";
    case RoR::SimGearboxMode::MANUAL_RANGES:
        return "manual-ranges";
    }
    return "unknown";
}

std::string ControlSuffix(const std::string& control_id)
{
    const std::size_t separator = control_id.find('.');
    return separator == std::string::npos ?
        control_id :
        control_id.substr(separator + 1U);
}

std::string SampleId(
    const char* stage,
    std::uint64_t transition_index,
    const std::string& control_id,
    const std::uint64_t* tick = nullptr)
{
    std::ostringstream text;
    text << stage << ':' << transition_index << ':'
         << ControlSuffix(control_id);
    if (tick != nullptr)
        text << ':' << *tick;
    return text.str();
}

ControlSample MakeControlSample(
    const std::string& sample_id,
    const std::string& control_id,
    const std::string& source_id,
    std::uint64_t source_tick,
    std::uint64_t effective_tick,
    double value,
    const std::string& parent)
{
    ControlSample sample;
    sample.sample_id = sample_id;
    sample.control_id = control_id;
    sample.source_id = source_id;
    sample.source_tick = source_tick;
    sample.effective_tick = effective_tick;
    sample.value = CanonicalDouble(value);
    if (!parent.empty())
        sample.parent_sample_ids.push_back(parent);
    return sample;
}

bool ControlSamplesFinite(const std::vector<ControlSample>& samples)
{
    for (const ControlSample& sample : samples)
    {
        if (!std::isfinite(sample.value))
            return false;
    }
    return true;
}

} // namespace

namespace RoR {
namespace WorldModel {

class RoRRuntimeCaptureProvider::Impl
{
public:
    struct TerrainObjectBinding
    {
        TerrainEditorObjectPtr object;
        std::string name;
        std::string instance_name;
        std::string type;
        Ogre::Vector3 position = Ogre::Vector3::ZERO;
        Ogre::Vector3 rotation = Ogre::Vector3::ZERO;
        Ogre::Vector3 initial_position = Ogre::Vector3::ZERO;
        Ogre::Vector3 initial_rotation = Ogre::Vector3::ZERO;
        int tobj_cache_id = -1;
        bool enable_collisions = false;
        int script_handler = -1;
        TObjSpecialObject special_object_type =
            TObjSpecialObject::NONE;
        ActorInstanceID_t actor_instance_id =
            ACTORINSTANCEID_INVALID;
        Ogre::SceneNode* scene_node = nullptr;
        Ogre::Vector3 scene_position = Ogre::Vector3::ZERO;
        Ogre::Quaternion scene_orientation =
            Ogre::Quaternion::IDENTITY;
        Ogre::Vector3 scene_scale = Ogre::Vector3::UNIT_SCALE;
    };

    struct RenderResources
    {
        Ogre::SceneManager* scene_manager = nullptr;
        Ogre::TexturePtr texture;
        Ogre::RenderTarget* target = nullptr;
        Ogre::Camera* camera = nullptr;
        Ogre::Viewport* viewport = nullptr;
        std::string texture_name;
        std::string camera_name;

        ~RenderResources()
        {
            Reset();
        }

        void Reset() noexcept
        {
            try
            {
                if (Ogre::Root::getSingletonPtr() != nullptr)
                {
                    if (target != nullptr)
                        target->removeAllViewports();
                    if (camera != nullptr && scene_manager != nullptr)
                        scene_manager->destroyCamera(camera);
                    if (!texture.isNull() &&
                        Ogre::TextureManager::getSingletonPtr() != nullptr)
                    {
                        Ogre::TextureManager::getSingleton().remove(
                            texture->getHandle());
                    }
                }
            }
            catch (...)
            {
                // Renderer teardown may already be in progress. Destructors
                // cannot recover, but all local handles are still cleared.
            }
            viewport = nullptr;
            camera = nullptr;
            target = nullptr;
            scene_manager = nullptr;
            texture.reset();
        }
    };

    Impl(
        ActorManager& manager,
        ActorPtr actor,
        const RoRRuntimeCaptureConfig& capture_config):
        actor_manager(manager),
        player_actor(actor),
        config(capture_config)
    {
        config_valid = ValidateConfig(config, &config_error);
        if (!config_valid)
            return;
        if (!BuildCaptureDescriptor(
                config,
                capture_descriptor,
                &config_error))
        {
            config_valid = false;
            return;
        }
        if (!BoundActorReady())
        {
            config_valid = false;
            config_error =
                "player Actor is not the live player in the bound ActorManager";
        }
        else if (player_actor->ar_driveable != TRUCK)
        {
            config_valid = false;
            config_error =
                "schema 1 live controls currently require a truck Actor";
        }
        else if (!HasSchema1DriverCamera())
        {
            config_valid = false;
            config_error =
                "schema 1 requires cinecam 0 and camera reference frame 0";
        }
        else
        {
            GameContext* const context = App::GetGameContext();
            if (context == nullptr ||
                context->GetTerrain() == nullptr ||
                !BindRuntimeResources(
                    context->GetTerrain().GetRef()) ||
                config.target_id != resource_identity.target_id ||
                config.vehicle_sha256 !=
                    resource_identity.vehicle_sha256 ||
                config.terrain_id != resource_identity.terrain_id ||
                config.terrain_sha256 !=
                    resource_identity.terrain_sha256)
            {
                config_valid = false;
                config_error =
                    "capture identity does not match loaded vehicle/terrain bytes";
            }
            else if (!CaptureEnvironmentReady(&config_error))
            {
                config_valid = false;
            }
        }
    }

    ~Impl()
    {
        DestroyRenderResources();
    }

    void DestroyRenderResources()
    {
        render.reset();
    }

    bool BindRuntimeResources(Terrain* terrain)
    {
        if (terrain == nullptr ||
            player_actor == nullptr ||
            App::GetGfxScene() == nullptr ||
            App::GetGfxScene()->GetSceneManager() == nullptr)
        {
            return false;
        }

        CacheEntryPtr vehicle_entry =
            player_actor->getUsedActorEntry();
        CacheEntryPtr terrain_entry = terrain->getCacheEntry();
        TerrainObjectManager* const object_manager =
            terrain->getObjectManager();
        if (vehicle_entry == nullptr ||
            terrain_entry == nullptr ||
            object_manager == nullptr ||
            vehicle_entry->fname.empty() ||
            vehicle_entry->resource_group.empty() ||
            terrain_entry->fname.empty() ||
            terrain_entry->resource_group.empty())
        {
            return false;
        }

        RoRRuntimeResourceIdentity identity;
        if (!InspectRuntimeResourceIdentity(
                player_actor,
                terrain,
                identity))
        {
            return false;
        }

        std::vector<TerrainObjectBinding> objects;
        const TerrainEditorObjectPtrVec& live_objects =
            object_manager->GetEditorObjects();
        objects.reserve(live_objects.size());
        for (const TerrainEditorObjectPtr& object : live_objects)
        {
            if (object == nullptr ||
                !FiniteVector(object->position) ||
                !FiniteVector(object->rotation) ||
                !FiniteVector(object->initial_position) ||
                !FiniteVector(object->initial_rotation) ||
                object->position != object->initial_position ||
                object->rotation != object->initial_rotation)
            {
                return false;
            }

            TerrainObjectBinding binding;
            binding.object = object;
            binding.name = object->name;
            binding.instance_name = object->instance_name;
            binding.type = object->type;
            binding.position = object->position;
            binding.rotation = object->rotation;
            binding.initial_position = object->initial_position;
            binding.initial_rotation = object->initial_rotation;
            binding.tobj_cache_id = object->tobj_cache_id;
            binding.enable_collisions = object->enable_collisions;
            binding.script_handler = object->script_handler;
            binding.special_object_type =
                object->special_object_type;
            binding.actor_instance_id =
                object->actor_instance_id;
            binding.scene_node = object->static_object_node;
            if (binding.scene_node != nullptr)
            {
                binding.scene_position =
                    binding.scene_node->getPosition();
                binding.scene_orientation =
                    binding.scene_node->getOrientation();
                binding.scene_scale =
                    binding.scene_node->getScale();
                if (!FiniteVector(binding.scene_position) ||
                    !FiniteQuaternion(binding.scene_orientation) ||
                    !FiniteVector(binding.scene_scale))
                {
                    return false;
                }
            }
            objects.push_back(std::move(binding));
        }

        bound_terrain = terrain;
        bound_vehicle_entry = vehicle_entry;
        bound_terrain_entry = terrain_entry;
        bound_vehicle_filename = vehicle_entry->fname;
        bound_vehicle_resource_group =
            vehicle_entry->resource_group;
        bound_terrain_filename = terrain_entry->fname;
        bound_terrain_resource_group =
            terrain_entry->resource_group;
        bound_object_manager = object_manager;
        bound_scene_manager =
            App::GetGfxScene()->GetSceneManager();
        terrain_objects = std::move(objects);
        resource_identity = std::move(identity);
        return true;
    }

    bool TerrainObjectsLive() const
    {
        if (bound_terrain == nullptr ||
            bound_object_manager == nullptr ||
            bound_terrain->getObjectManager() !=
                bound_object_manager)
        {
            return false;
        }
        const TerrainEditorObjectPtrVec& live_objects =
            bound_object_manager->GetEditorObjects();
        if (live_objects.size() != terrain_objects.size())
            return false;

        for (std::size_t index = 0U;
             index < live_objects.size();
             ++index)
        {
            const TerrainObjectBinding& binding =
                terrain_objects[index];
            const TerrainEditorObjectPtr& object =
                live_objects[index];
            if (object == nullptr ||
                object != binding.object ||
                object->name != binding.name ||
                object->instance_name != binding.instance_name ||
                object->type != binding.type ||
                object->position != binding.position ||
                object->rotation != binding.rotation ||
                object->initial_position !=
                    binding.initial_position ||
                object->initial_rotation !=
                    binding.initial_rotation ||
                object->tobj_cache_id != binding.tobj_cache_id ||
                object->enable_collisions !=
                    binding.enable_collisions ||
                object->script_handler != binding.script_handler ||
                object->special_object_type !=
                    binding.special_object_type ||
                object->actor_instance_id !=
                    binding.actor_instance_id ||
                object->static_object_node != binding.scene_node)
            {
                return false;
            }
            if (binding.scene_node != nullptr &&
                (binding.scene_node->getPosition() !=
                        binding.scene_position ||
                 binding.scene_node->getOrientation() !=
                        binding.scene_orientation ||
                 binding.scene_node->getScale() !=
                        binding.scene_scale))
            {
                return false;
            }
        }
        return true;
    }

    bool BoundResourcesLive(
        bool rehash,
        std::string* error = nullptr) const
    {
        const auto fail = [error](const char* message)
        {
            if (error != nullptr)
                *error = message;
            return false;
        };
        GameContext* const context = App::GetGameContext();
        if (!BoundActorReady() ||
            context == nullptr ||
            context->GetTerrain() == nullptr ||
            context->GetTerrain().GetRef() != bound_terrain)
        {
            return fail(
                "bound ActorManager/player/Terrain identity changed");
        }
        const CacheEntryPtr vehicle_entry =
            player_actor->getUsedActorEntry();
        const CacheEntryPtr terrain_entry =
            bound_terrain->getCacheEntry();
        if (vehicle_entry == nullptr ||
            terrain_entry == nullptr ||
            vehicle_entry != bound_vehicle_entry ||
            terrain_entry != bound_terrain_entry ||
            vehicle_entry->fname != bound_vehicle_filename ||
            vehicle_entry->resource_group !=
                bound_vehicle_resource_group ||
            terrain_entry->fname != bound_terrain_filename ||
            terrain_entry->resource_group !=
                bound_terrain_resource_group ||
            player_actor->ar_filename !=
                bound_vehicle_filename ||
            bound_terrain->getTerrainFileName() !=
                bound_terrain_filename)
        {
            return fail(
                "bound vehicle/Terrain cache entry or resource group changed");
        }
        if (!TerrainObjectsLive())
        {
            return fail(
                "bound terrain object identity or transform changed");
        }
        if (!rehash)
            return true;

        RoRRuntimeResourceIdentity identity;
        if (!InspectRuntimeResourceIdentity(
                player_actor,
                bound_terrain,
                identity) ||
            identity.target_id != resource_identity.target_id ||
            identity.vehicle_sha256 !=
                resource_identity.vehicle_sha256 ||
            identity.terrain_id != resource_identity.terrain_id ||
            identity.terrain_sha256 !=
                resource_identity.terrain_sha256 ||
            identity.target_id != config.target_id ||
            identity.vehicle_sha256 != config.vehicle_sha256 ||
            identity.terrain_id != config.terrain_id ||
            identity.terrain_sha256 != config.terrain_sha256)
        {
            return fail(
                "bound vehicle/Terrain resource bytes changed");
        }
        return true;
    }

    bool BoundActorReady() const
    {
        if (player_actor == nullptr)
            return false;
        GameContext* const context = App::GetGameContext();
        if (context == nullptr ||
            context->GetActorManager() != &actor_manager ||
            context->GetPlayerActor() != player_actor ||
            player_actor->ar_state != ActorState::LOCAL_SIMULATED)
        {
            return false;
        }

        std::size_t matches = 0U;
        const ActorPtrVec& actors = actor_manager.GetActors();
        for (const ActorPtr& actor : actors)
        {
            if (actor == player_actor)
                ++matches;
        }
        // Schema 1 seals a single-actor render profile. Additional actors
        // would require their complete post-step graphics state and resource
        // identity to be represented in provenance.
        return
            matches == 1U &&
            actors.size() == 1U &&
            !player_actor->ar_arcade_controls;
    }

    bool PhysicalControlInputsReady(
        std::string* error = nullptr) const
    {
        const auto fail = [error](const char* message)
        {
            if (error != nullptr)
                *error = message;
            return false;
        };
        InputEngine* const input = App::GetInputEngine();
        if (player_actor == nullptr ||
            input == nullptr ||
            !player_actor->ShouldAllowNonSimulatedInputs())
        {
            return fail(
                "physical controller input is not available for the player");
        }
        std::vector<int> events;
        for (const std::string& control : config.control_ids)
            AppendControlEvents(control, events);
        std::sort(events.begin(), events.end());
        events.erase(
            std::unique(events.begin(), events.end()),
            events.end());
        for (int event : events)
        {
            if (player_actor->hasEventSimulatedValue(event))
            {
                return fail(
                    "Actor simulated/policy input is forbidden by the physical controller profile");
            }
            if (input->hasEventSimulatedValue(event))
            {
                return fail(
                    "InputEngine simulated/policy input is forbidden by the physical controller profile");
            }
        }
        if (std::binary_search(
                config.control_ids.begin(),
                config.control_ids.end(),
                std::string(CONTROL_PARKING_BRAKE)))
        {
            const float trailer_raw =
                player_actor->getEventValue(
                    EV_TRUCK_TRAILER_PARKING_BRAKE,
                    true);
            const float trailer_issued =
                player_actor->getEventValue(
                    EV_TRUCK_TRAILER_PARKING_BRAKE,
                    false);
            if (!std::isfinite(trailer_raw) ||
                !std::isfinite(trailer_issued) ||
                trailer_raw != 0.0f ||
                trailer_issued != 0.0f)
            {
                return fail(
                    "trailer parking-brake input is an unrecorded resolver gate");
            }
        }
        return true;
    }

    bool HasCanonicalActorVisuals() const
    {
        GfxActor* const gfx_actor =
            player_actor != nullptr ?
                player_actor->GetGfxActor() :
                nullptr;
        if (gfx_actor == nullptr ||
            gfx_actor->GetDebugView() !=
                DebugViewType::DEBUGVIEW_NONE)
        {
            return false;
        }
        for (const Prop& prop : gfx_actor->getProps())
        {
            for (const PropAnim& animation : prop.pp_animations)
            {
                if ((animation.animFlags &
                        PROP_ANIM_FLAG_TORQUE) != 0U ||
                    (animation.animFlags &
                        PROP_ANIM_FLAG_SHIFTER) != 0U ||
                    (animation.animMode &
                        PROP_ANIM_MODE_AUTOANIMATE) != 0U)
                {
                    return false;
                }
            }
        }
        return true;
    }

    bool CaptureEnvironmentReady(
        std::string* error = nullptr) const
    {
        const auto fail = [error](const char* message)
        {
            if (error != nullptr)
                *error = message;
            return false;
        };
        if (!BoundResourcesLive(false, error))
            return false;
        if (!ValidateCurrentRoRLiveCaptureRuntimeState(
                player_actor,
                bound_terrain,
                error))
        {
            return false;
        }
        if (App::io_analog_smoothing == nullptr ||
            App::io_analog_sensitivity == nullptr)
        {
            return fail("schema-1 analog input CVars are unavailable");
        }
        if (!ValidateLiveCaptureAnalogInputState(
                config.analog_smoothing,
                config.analog_sensitivity,
                App::io_analog_smoothing->getFloat(),
                App::io_analog_sensitivity->getFloat(),
                error))
        {
            return false;
        }
        if (config.weather_id != SCHEMA1_WEATHER_ID)
            return fail("capture weather profile changed");
        if (bound_scene_manager == nullptr ||
            App::GetGfxScene() == nullptr ||
            App::GetGfxScene()->GetSceneManager() !=
                bound_scene_manager)
        {
            return fail("bound OGRE scene manager changed");
        }
        GfxActor* const gfx_actor =
            player_actor->GetGfxActor();
        const std::vector<GfxActor*>& gfx_actors =
            App::GetGfxScene()->GetGfxActors();
        const std::vector<GfxCharacter*>& gfx_characters =
            App::GetGfxScene()->GetGfxCharacters();
        if (gfx_actor == nullptr ||
            gfx_actors.size() != 1U ||
            gfx_actors.front() != gfx_actor ||
            gfx_characters.size() > 1U ||
            (gfx_characters.size() == 1U &&
             gfx_characters.front() == nullptr))
        {
            return fail(
                "capture requires one graphics Actor and at most one joined driver character");
        }
        if (!HasCanonicalActorVisuals())
        {
            return fail(
                "debug visuals or stateful display-frame prop animations are unsupported");
        }
        if (bound_object_manager == nullptr ||
            bound_object_manager->HasTimeVaryingVisuals() ||
            bound_terrain->GetTerrainEditor() == nullptr ||
            bound_terrain->GetTerrainEditor()->
                GetSelectedObjectID() !=
                    TERRAINEDITOROBJECTID_INVALID)
        {
            return fail(
                "time-varying or edited terrain visuals are unsupported");
        }
        if (!player_actor->ar_linked_actors.empty() ||
            !actor_manager.inter_actor_links.empty() ||
            !actor_manager.GetFreeForces().empty())
        {
            return fail(
                "linked actors and dynamic free forces are unsupported");
        }

        if (App::gfx_water_mode == nullptr ||
            App::gfx_water_waves == nullptr ||
            App::gfx_sky_mode == nullptr ||
            App::gfx_sky_time_cycle == nullptr ||
            App::gfx_envmap_enabled == nullptr ||
            App::gfx_shadow_type == nullptr ||
            App::gfx_vegetation_mode == nullptr ||
            App::gfx_particles_mode == nullptr ||
            App::gfx_flares_mode == nullptr ||
            App::gfx_enable_videocams == nullptr ||
            App::gfx_skidmarks_mode == nullptr ||
            App::io_arcade_controls == nullptr ||
            App::mp_state == nullptr)
        {
            return fail(
                "capture environment CVars are unavailable");
        }
        if (bound_terrain->getWater() != nullptr ||
            bound_terrain->getGfxWater() != nullptr ||
            App::gfx_water_mode->getEnum<GfxWaterMode>() !=
                GfxWaterMode::NONE ||
            App::gfx_water_waves->getBool())
        {
            return fail(
                "water, reflection and wavefield rendering are unsupported");
        }
        if (App::gfx_sky_time_cycle->getBool())
            return fail("dynamic sky time is unsupported");
        if (App::gfx_sky_mode->getEnum<GfxSkyMode>() !=
                GfxSkyMode::SANDSTORM)
        {
            return fail(
                "camera-dependent Caelum/SkyX lighting is unsupported");
        }
        if (App::gfx_envmap_enabled->getBool())
            return fail("dynamic environment maps are unsupported");
        if (App::gfx_shadow_type->getEnum<GfxShadowType>() !=
                GfxShadowType::NONE)
        {
            return fail("display-frame shadows are unsupported");
        }
        if (App::gfx_vegetation_mode->getEnum<GfxVegetation>() !=
                GfxVegetation::NONE)
        {
            return fail("paged vegetation is unsupported");
        }
        if (App::gfx_particles_mode->getInt() != 0)
            return fail("display-frame particles are unsupported");
        if (App::gfx_flares_mode->getEnum<GfxFlaresMode>() !=
                GfxFlaresMode::NONE)
        {
            return fail("display-frame flares are unsupported");
        }
        if (App::gfx_enable_videocams->getBool())
            return fail("render-to-texture video cameras are unsupported");
        if (App::gfx_skidmarks_mode->getInt() != 0)
            return fail("display-frame skidmarks are unsupported");
        if (App::io_arcade_controls->getBool())
            return fail("arcade control remapping is unsupported");
        if (App::mp_state->getEnum<MpState>() != MpState::DISABLED)
            return fail("multiplayer capture is unsupported");
#ifdef USE_ANGELSCRIPT
        if (App::GetScriptEngine() != nullptr &&
            !App::GetScriptEngine()->getScriptUnits().empty())
        {
            return fail("loaded scripts are unsupported");
        }
#endif
        return true;
    }

    bool ValidActorNode(NodeNum_t node) const
    {
        return
            player_actor != nullptr &&
            player_actor->ar_nodes != nullptr &&
            node != NODENUM_INVALID &&
            node >= 0 &&
            node < player_actor->ar_num_nodes;
    }

    bool HasSchema1DriverCamera() const
    {
        return
            BoundActorReady() &&
            player_actor->ar_num_cinecams > 0 &&
            player_actor->ar_num_cameras > 0 &&
            ValidActorNode(player_actor->ar_cinecam_node[0]) &&
            ValidActorNode(player_actor->ar_camera_node_pos[0]) &&
            ValidActorNode(player_actor->ar_camera_node_dir[0]) &&
            ValidActorNode(player_actor->ar_camera_node_roll[0]);
    }

    bool BuildSchema1DriverCameraPose(
        Ogre::Vector3& position,
        Ogre::Quaternion& orientation) const
    {
        if (!HasSchema1DriverCamera())
            return false;

        const NodeNum_t position_node =
            player_actor->ar_camera_node_pos[0];
        const NodeNum_t direction_node =
            player_actor->ar_camera_node_dir[0];
        const NodeNum_t roll_node =
            player_actor->ar_camera_node_roll[0];
        const NodeNum_t cinecam_node =
            player_actor->ar_cinecam_node[0];

        Ogre::Vector3 forward =
            player_actor->ar_nodes[position_node].AbsPosition -
            player_actor->ar_nodes[direction_node].AbsPosition;
        Ogre::Vector3 right =
            player_actor->ar_nodes[position_node].AbsPosition -
            player_actor->ar_nodes[roll_node].AbsPosition;
        if (player_actor->ar_camera_node_roll_inv[0])
            right = -right;
        if (!FiniteVector(forward) ||
            !FiniteVector(right) ||
            forward.normalise() <=
                std::numeric_limits<Ogre::Real>::epsilon() ||
            right.normalise() <=
                std::numeric_limits<Ogre::Real>::epsilon())
        {
            return false;
        }

        // This is CameraManager's cinecam basis with both mutable user-look
        // rotations fixed to zero. OGRE cameras look down local -Z.
        Ogre::Vector3 down = forward.crossProduct(right);
        if (!FiniteVector(down) ||
            down.normalise() <=
                std::numeric_limits<Ogre::Real>::epsilon())
        {
            return false;
        }
        right = down.crossProduct(forward);
        if (!FiniteVector(right) ||
            right.normalise() <=
                std::numeric_limits<Ogre::Real>::epsilon())
        {
            return false;
        }

        position =
            player_actor->ar_nodes[cinecam_node].AbsPosition;
        orientation =
            Ogre::Quaternion(
                Ogre::Degree(180.0f),
                right) *
            Ogre::Quaternion(
                right,
                down,
                forward);
        if (!FiniteVector(position) ||
            !FiniteQuaternion(orientation) ||
            orientation.normalise() <=
                std::numeric_limits<Ogre::Real>::epsilon())
        {
            return false;
        }
        orientation = CanonicalOgreQuaternion(orientation);
        return FiniteQuaternion(orientation);
    }

    bool EnsureRenderResources()
    {
        if (render)
            return true;
        if (App::GetGfxScene() == nullptr ||
            Ogre::TextureManager::getSingletonPtr() == nullptr)
        {
            return false;
        }
        Ogre::SceneManager* const scene_manager =
            App::GetGfxScene()->GetSceneManager();
        if (scene_manager == nullptr)
            return false;

        const std::uint64_t sequence =
            g_render_resource_sequence.fetch_add(1U);
        std::unique_ptr<RenderResources> candidate(
            new RenderResources());
        candidate->scene_manager = scene_manager;
        candidate->texture_name =
            "RoRWorldModelRgb-" + std::to_string(sequence);
        candidate->camera_name =
            "RoRWorldModelCamera-" + std::to_string(sequence);

        try
        {
            candidate->texture =
                Ogre::TextureManager::getSingleton().createManual(
                    candidate->texture_name,
                    Ogre::ResourceGroupManager::
                        DEFAULT_RESOURCE_GROUP_NAME,
                    Ogre::TEX_TYPE_2D,
                    config.rgb_width,
                    config.rgb_height,
                    0U,
                    Ogre::PF_BYTE_RGB,
                    Ogre::TU_RENDERTARGET,
                    nullptr,
                    true);
            if (candidate->texture.isNull() ||
                !candidate->texture->isHardwareGammaEnabled())
                return false;
            candidate->target =
                candidate->texture->getBuffer()->getRenderTarget();
            if (candidate->target == nullptr ||
                !candidate->target->isHardwareGammaEnabled())
                return false;
            candidate->target->setAutoUpdated(false);
            candidate->camera =
                scene_manager->createCamera(candidate->camera_name);
            if (candidate->camera == nullptr)
                return false;
            candidate->camera->setFixedYawAxis(false);
            candidate->viewport =
                candidate->target->addViewport(candidate->camera);
            if (candidate->viewport == nullptr)
                return false;
            candidate->viewport->setClearEveryFrame(true);
            candidate->viewport->setOverlaysEnabled(false);
            candidate->viewport->setSkiesEnabled(false);
            candidate->viewport->setShadowsEnabled(false);
            candidate->viewport->setBackgroundColour(
                Ogre::ColourValue::Black);
        }
        catch (...)
        {
            return false;
        }
        render = std::move(candidate);
        return true;
    }

    bool ReadRawOrIssuedControl(
        const std::string& control_id,
        bool pure,
        double& value) const
    {
        if (player_actor == nullptr)
            return false;
        if (control_id == CONTROL_STEERING)
        {
            const double left_digital =
                player_actor->getEventValue(
                    EV_TRUCK_STEER_LEFT,
                    pure,
                    InputSourceType::IST_DIGITAL);
            const double right_digital =
                player_actor->getEventValue(
                    EV_TRUCK_STEER_RIGHT,
                    pure,
                    InputSourceType::IST_DIGITAL);
            const double left_analog =
                player_actor->getEventValue(
                    EV_TRUCK_STEER_LEFT,
                    pure,
                    InputSourceType::IST_ANALOG);
            const double right_analog =
                player_actor->getEventValue(
                    EV_TRUCK_STEER_RIGHT,
                    pure,
                    InputSourceType::IST_ANALOG);
            value = SteeringValue(
                -std::max(left_digital, left_analog) +
                std::max(right_digital, right_analog));
        }
        else if (control_id == CONTROL_BRAKE)
        {
            value =
                player_actor->getEventValue(
                    EV_TRUCK_BRAKE,
                    pure);
            const double modifier_25 =
                player_actor->getEventValue(
                    EV_TRUCK_BRAKE_MODIFIER_25,
                    pure);
            const double modifier_50 =
                player_actor->getEventValue(
                    EV_TRUCK_BRAKE_MODIFIER_50,
                    pure);
            if (modifier_25 != 0.0 || modifier_50 != 0.0)
            {
                value *=
                    (modifier_25 != 0.0 ? 0.25 : 0.0) +
                    (modifier_50 != 0.0 ? 0.50 : 0.0);
            }
            value = UnitValue(value);
        }
        else if (control_id == CONTROL_THROTTLE)
        {
            value =
                player_actor->getEventValue(
                    EV_TRUCK_ACCELERATE,
                    pure);
            const double modifier_25 =
                player_actor->getEventValue(
                    EV_TRUCK_ACCELERATE_MODIFIER_25,
                    pure);
            const double modifier_50 =
                player_actor->getEventValue(
                    EV_TRUCK_ACCELERATE_MODIFIER_50,
                    pure);
            if (modifier_25 != 0.0 || modifier_50 != 0.0)
            {
                value *=
                    (modifier_25 != 0.0 ? 0.25 : 0.0) +
                    (modifier_50 != 0.0 ? 0.50 : 0.0);
            }
            value = UnitValue(value);
        }
        else if (control_id == CONTROL_CLUTCH)
        {
            value =
                player_actor->getEventValue(
                    EV_TRUCK_MANUAL_CLUTCH,
                    pure);
            const double modifier_25 =
                player_actor->getEventValue(
                    EV_TRUCK_MANUAL_CLUTCH_MODIFIER_25,
                    pure);
            const double modifier_50 =
                player_actor->getEventValue(
                    EV_TRUCK_MANUAL_CLUTCH_MODIFIER_50,
                    pure);
            if (modifier_25 != 0.0 || modifier_50 != 0.0)
            {
                value *=
                    (modifier_25 != 0.0 ? 0.25 : 0.0) +
                    (modifier_50 != 0.0 ? 0.50 : 0.0);
            }
            value = UnitValue(value);
        }
        else if (control_id == CONTROL_PARKING_BRAKE)
        {
            value =
                player_actor->getEventValue(
                    EV_TRUCK_PARKING_BRAKE,
                    pure) > 0.5f ?
                    1.0 :
                    0.0;
        }
        else
        {
            return false;
        }
        return std::isfinite(value);
    }

    bool ReadResolvedControl(
        const std::string& control_id,
        double& value) const
    {
        if (player_actor == nullptr)
            return false;
        EnginePtr engine = player_actor->getEngine();
        if (control_id == CONTROL_STEERING)
            value = SteeringValue(player_actor->ar_hydro_dir_command);
        else if (control_id == CONTROL_BRAKE)
            value = UnitValue(player_actor->ar_brake);
        else if (control_id == CONTROL_PARKING_BRAKE)
            value = player_actor->ar_parking_brake ? 1.0 : 0.0;
        else if (control_id == CONTROL_THROTTLE)
            value = engine != nullptr ? UnitValue(engine->getAcc()) : 0.0;
        else if (control_id == CONTROL_CLUTCH)
            value = engine != nullptr ? UnitValue(engine->getClutch()) : 0.0;
        else
            return false;
        return std::isfinite(value);
    }

    bool MakeInitialControlStages(const TransitionId& transition)
    {
        if (!PhysicalControlInputsReady())
            return false;
        controls = ControlLineage();
        const std::uint64_t transition_index =
            transition.source.observation_index;
        const std::uint64_t first_tick =
            transition.source.completed_physics_steps;
        controls.raw.reserve(config.control_ids.size());
        controls.issued.reserve(config.control_ids.size());

        for (const std::string& control_id : config.control_ids)
        {
            double raw_value = 0.0;
            double issued_value = 0.0;
            if (!ReadRawOrIssuedControl(control_id, true, raw_value) ||
                !ReadRawOrIssuedControl(control_id, false, issued_value))
            {
                return false;
            }
            const std::string raw_id =
                SampleId("raw", transition_index, control_id);
            const std::string issued_id =
                SampleId("issued", transition_index, control_id);
            controls.raw.push_back(
                MakeControlSample(
                    raw_id,
                    control_id,
                    RAW_PHYSICAL_SOURCE_ID,
                    first_tick,
                    first_tick,
                    raw_value,
                    std::string()));
            controls.issued.push_back(
                MakeControlSample(
                    issued_id,
                    control_id,
                    ISSUED_CONTROL_SOURCE_ID,
                    first_tick,
                    first_tick,
                    issued_value,
                    raw_id));
        }
        return
            ControlSamplesFinite(controls.raw) &&
            ControlSamplesFinite(controls.issued);
    }

    bool MakeResolvedStage()
    {
        if (resolved_captured)
            return true;
        if (!PhysicalControlInputsReady() ||
            controls.issued.size() != config.control_ids.size())
            return false;
        for (std::size_t index = 0U;
             index < config.control_ids.size();
             ++index)
        {
            double current_issued = 0.0;
            if (!ReadRawOrIssuedControl(
                    config.control_ids[index],
                    false,
                    current_issued) ||
                current_issued != controls.issued[index].value)
            {
                // The shared resolver must consume the same issued snapshot
                // captured at BeginTransition. A device update or alternate
                // input path between those boundaries invalidates lineage.
                return false;
            }
        }
        const std::uint64_t transition_index =
            active_transition.source.observation_index;
        const std::uint64_t first_tick =
            active_transition.source.completed_physics_steps;
        controls.resolved.reserve(config.control_ids.size());
        for (const std::string& control_id : config.control_ids)
        {
            double value = 0.0;
            if (!ReadResolvedControl(control_id, value))
                return false;
            controls.resolved.push_back(
                MakeControlSample(
                    SampleId(
                        "resolved",
                        transition_index,
                        control_id),
                    control_id,
                    RESOLVED_CONTROL_SOURCE_ID,
                    first_tick,
                    first_tick,
                    value,
                    SampleId(
                        "issued",
                        transition_index,
                        control_id)));
        }
        resolved_captured = ControlSamplesFinite(controls.resolved);
        return resolved_captured;
    }

    ContactSummary ReadContacts(
        std::set<std::uint32_t>* active_nodes = nullptr) const
    {
        ContactSummary summary;
        if (active_nodes != nullptr)
            active_nodes->clear();
        if (player_actor == nullptr ||
            player_actor->ar_num_nodes < 0 ||
            player_actor->ar_nodes == nullptr)
        {
            return summary;
        }
        for (int index = 0;
             index < player_actor->ar_num_nodes;
             ++index)
        {
            const node_t& node = player_actor->ar_nodes[index];
            if (!node.nd_has_ground_contact &&
                !node.nd_has_mesh_contact)
            {
                continue;
            }
            ++summary.contact_count;
            if (node.nd_tyre_node || node.nd_rim_node)
                ++summary.wheel_contact_count;
            if (active_nodes != nullptr)
            {
                active_nodes->insert(
                    static_cast<std::uint32_t>(index));
            }
            const double impulse =
                static_cast<double>(
                    node.nd_last_collision_force.length()) *
                FIXED_STEP_SECONDS;
            if (std::isfinite(impulse))
            {
                summary.maximum_normal_impulse_ns =
                    std::max(
                        summary.maximum_normal_impulse_ns,
                        CanonicalDouble(std::abs(impulse)));
            }
        }
        // RoR's node collision state does not retain penetration depth after
        // resolution. Schema 1 reports the truthful lower bound instead of
        // inventing geometry from the last force vector.
        summary.maximum_penetration_m = 0.0;
        return summary;
    }

    void AccumulateContacts(const ContactSummary& current)
    {
        transition_contacts.contact_count =
            std::max(
                transition_contacts.contact_count,
                current.contact_count);
        transition_contacts.wheel_contact_count =
            std::max(
                transition_contacts.wheel_contact_count,
                current.wheel_contact_count);
        transition_contacts.maximum_normal_impulse_ns =
            std::max(
                transition_contacts.maximum_normal_impulse_ns,
                current.maximum_normal_impulse_ns);
        transition_contacts.maximum_penetration_m =
            std::max(
                transition_contacts.maximum_penetration_m,
                current.maximum_penetration_m);
    }

    bool BuildStateSha256(
        std::uint64_t completed_physics_steps,
        std::string& state_sha256) const
    {
        if (!BoundActorReady())
            return false;
        std::vector<const Actor*> actors;
        try
        {
            const ActorPtrVec& live_actors = actor_manager.GetActors();
            actors.reserve(live_actors.size());
            for (const ActorPtr& actor : live_actors)
                actors.push_back(actor.GetRef());
        }
        catch (...)
        {
            return false;
        }

        // Surface-contact counts and all Actor/node/beam state are included.
        // ActorManager's full inter-actor contact key buffer is transient and
        // private to its optional trace writer, so this recorder does not
        // claim those keys in digest schema 1.
        const std::vector<
            DeterministicContactOrder::InterActorKey> inter_actor_contacts;
        DeterministicStateDigest::Digest digest;
        DeterministicStateDigest::SnapshotStatus status;
        if (!DeterministicStateDigest::BuildActorSnapshotDigest(
                completed_physics_steps,
                config.state_digest_scenario_id,
                actors,
                inter_actor_contacts,
                digest,
                &status))
        {
            return false;
        }
        state_sha256 = digest.ToHex();
        return true;
    }

    bool UpdateRenderState(
        double elapsed_seconds,
        const Ogre::Vector3& camera_position,
        const Ogre::Quaternion& camera_orientation)
    {
        if (!BoundActorReady() ||
            !std::isfinite(elapsed_seconds) ||
            elapsed_seconds < 0.0 ||
            elapsed_seconds >
                static_cast<double>(
                    std::numeric_limits<float>::max()) ||
            player_actor->GetGfxActor() == nullptr ||
            App::GetGfxScene() == nullptr)
        {
            return false;
        }
        try
        {
            player_actor->GetGfxActor()->UpdateSimDataBuffer();
            return
                App::GetGfxScene()->
                    ForceUpdateSingleGfxActorForCapture(
                        player_actor->GetGfxActor(),
                        static_cast<float>(elapsed_seconds),
                        camera_position,
                        camera_orientation);
        }
        catch (...)
        {
            return false;
        }
    }

    bool CaptureCameraAndRgb(
        double elapsed_seconds,
        CameraTelemetry& camera_telemetry,
        std::vector<std::uint8_t>& rgb8)
    {
        Ogre::Vector3 position;
        Ogre::Quaternion orientation;
        if (!EnsureRenderResources() ||
            !BuildSchema1DriverCameraPose(
                position,
                orientation) ||
            !UpdateRenderState(
                elapsed_seconds,
                position,
                orientation) ||
            render == nullptr ||
            render->camera == nullptr ||
            render->target == nullptr)
        {
            return false;
        }

        try
        {
            render->camera->setProjectionType(
                Ogre::PT_PERSPECTIVE);
            render->camera->setPosition(position);
            render->camera->setOrientation(orientation);
            render->camera->setNearClipDistance(
                SCHEMA1_CAMERA_NEAR_CLIP_METERS);
            render->camera->setFarClipDistance(
                SCHEMA1_CAMERA_FAR_CLIP_METERS);
            render->camera->setAspectRatio(
                static_cast<Ogre::Real>(config.rgb_width) /
                static_cast<Ogre::Real>(config.rgb_height));
            render->camera->setFOVy(
                Ogre::Degree(
                    SCHEMA1_CAMERA_FOV_DEGREES));
            render->target->update();

            const std::size_t byte_count =
                static_cast<std::size_t>(config.rgb_width) *
                static_cast<std::size_t>(config.rgb_height) * 3U;
            rgb8.assign(byte_count, 0U);
            Ogre::PixelBox pixels(
                config.rgb_width,
                config.rgb_height,
                1U,
                Ogre::PF_BYTE_RGB,
                rgb8.data());
            render->texture->getBuffer()->blitToMemory(pixels);
            if (render->target->requiresTextureFlipping())
            {
                const std::size_t row_bytes =
                    static_cast<std::size_t>(config.rgb_width) * 3U;
                for (std::size_t top = 0U,
                         bottom =
                             static_cast<std::size_t>(
                                 config.rgb_height) - 1U;
                     top < bottom;
                     ++top, --bottom)
                {
                    std::swap_ranges(
                        rgb8.begin() +
                            static_cast<std::ptrdiff_t>(
                                top * row_bytes),
                        rgb8.begin() +
                            static_cast<std::ptrdiff_t>(
                                (top + 1U) * row_bytes),
                        rgb8.begin() +
                            static_cast<std::ptrdiff_t>(
                                bottom * row_bytes));
                }
            }

            camera_telemetry.camera_id =
                SCHEMA1_CAMERA_ID;
            camera_telemetry.coordinate_frame =
                config.coordinate_frame;
            camera_telemetry.position_m = ToVector3(position);
            camera_telemetry.orientation_world_from_camera =
                ToQuaternion(orientation);
            CopyMatrix(
                render->camera->getViewMatrix(true),
                camera_telemetry.view_matrix);
            CopyMatrix(
                render->camera->getProjectionMatrix(),
                camera_telemetry.projection_matrix);
            const double vertical_fov =
                CanonicalDouble(
                    static_cast<double>(
                        render->camera->getFOVy().valueRadians()));
            const double fy =
                0.5 * static_cast<double>(config.rgb_height) /
                std::tan(vertical_fov * 0.5);
            const double fx = fy;
            camera_telemetry.intrinsics = {{
                CanonicalDouble(fx), 0.0,
                CanonicalDouble(
                    0.5 * static_cast<double>(config.rgb_width)),
                0.0, CanonicalDouble(fy),
                CanonicalDouble(
                    0.5 * static_cast<double>(config.rgb_height)),
                0.0, 0.0, 1.0}};
            camera_telemetry.vertical_fov_radians = vertical_fov;
            camera_telemetry.near_clip_m =
                CanonicalDouble(
                    static_cast<double>(
                        SCHEMA1_CAMERA_NEAR_CLIP_METERS));
            camera_telemetry.far_clip_m =
                CanonicalDouble(
                    static_cast<double>(
                        SCHEMA1_CAMERA_FAR_CLIP_METERS));
        }
        catch (...)
        {
            return false;
        }
        return true;
    }

    bool BuildObservation(
        const ObservationId& expected_id,
        const PhysicsStepRange& steps,
        ObservationSample& observation,
        bool rehash_resources)
    {
        if (!config_valid ||
            !BoundResourcesLive(rehash_resources) ||
            !CaptureEnvironmentReady() ||
            App::GetGameContext() == nullptr ||
            App::GetGameContext()->GetTerrain() == nullptr)
        {
            return false;
        }

        ObservationRecord record;
        record.episode_id = expected_id.episode;
        record.observation_id = expected_id;
        record.frame_id = expected_id.observation_index;
        record.target_id = config.target_id;
        record.nominal_time = {
            expected_id.observation_index,
            WORLD_MODEL_OBSERVATION_RATE_HZ};
        record.physics_steps = steps;

        const Ogre::Vector3 position = player_actor->getPosition();
        const Ogre::Quaternion orientation =
            CanonicalOgreQuaternion(player_actor->getOrientation());
        record.vehicle.position_m = ToVector3(position);
        record.vehicle.orientation_world_from_vehicle =
            ToQuaternion(orientation);
        record.vehicle.linear_velocity_mps =
            ToVector3(player_actor->getVelocity());
        record.vehicle.speed_mps =
            CanonicalDouble(
                static_cast<double>(player_actor->getSpeed()));
        record.vehicle.mass_kg =
            CanonicalDouble(
                static_cast<double>(
                    player_actor->getTotalMass(true)));

        if (have_previous_vehicle_orientation &&
            steps.substep_count != 0U)
        {
            Ogre::Quaternion delta =
                orientation *
                previous_vehicle_orientation.Inverse();
            delta = CanonicalOgreQuaternion(delta);
            Ogre::Radian angle;
            Ogre::Vector3 axis;
            delta.ToAngleAxis(angle, axis);
            double radians =
                static_cast<double>(angle.valueRadians());
            if (radians > PI)
                radians -= 2.0 * PI;
            const double seconds =
                static_cast<double>(steps.substep_count) *
                FIXED_STEP_SECONDS;
            if (axis.isZeroLength() || seconds <= 0.0)
            {
                record.vehicle.angular_velocity_radps =
                    Vector3();
            }
            else
            {
                axis.normalise();
                record.vehicle.angular_velocity_radps =
                    ToVector3(
                        axis * static_cast<Ogre::Real>(
                            radians / seconds));
            }
        }
        previous_vehicle_orientation = orientation;
        have_previous_vehicle_orientation = true;

        EnginePtr engine = player_actor->getEngine();
        if (engine != nullptr)
        {
            record.engine.running = engine->isRunning();
            record.engine.contact = engine->hasContact();
            record.engine.starter = engine->isStarterActive();
            record.engine.rpm =
                CanonicalDouble(
                    static_cast<double>(engine->getRPM()));
            record.engine.torque_nm =
                CanonicalDouble(
                    static_cast<double>(engine->getTorque()));
            record.engine.throttle = UnitValue(engine->getAcc());
            record.engine.clutch = UnitValue(engine->getClutch());
            record.engine.gear = engine->getGear();
            record.engine.gear_range = engine->getGearRange();
            record.engine.mode =
                GearboxModeName(engine->getAutoMode());
            record.engine.timers_seconds = {
                {"post-shift",
                    CanonicalDouble(
                        static_cast<double>(
                            engine->getPostShiftClock()))},
                {"shift",
                    CanonicalDouble(
                        static_cast<double>(
                            engine->getShiftClock()))}};
        }
        else
        {
            record.engine.mode = "none";
        }

        const TerrainPtr& terrain =
            App::GetGameContext()->GetTerrain();
        record.world.world_id = config.world_id;
        record.world.terrain_id = config.terrain_id;
        record.world.terrain_sha256 = config.terrain_sha256;
        record.world.gravity_mps2 = {
            0.0,
            CanonicalDouble(
                static_cast<double>(terrain->getGravity())),
            0.0};
        record.world.water_enabled =
            terrain->getWater() != nullptr ||
            terrain->getGfxWater() != nullptr;
        record.world.water_level_m =
            record.world.water_enabled ?
                CanonicalDouble(
                    static_cast<double>(terrain->getWaterHeight())) :
                0.0;
        record.world.weather_id = config.weather_id;

        std::vector<std::uint8_t> rgb8;
        if (!CaptureCameraAndRgb(
                static_cast<double>(steps.substep_count) *
                    FIXED_STEP_SECONDS,
                record.camera,
                rgb8) ||
            !CanonicalRgbRecordId(
                expected_id.observation_index,
                record.rgb.record_id))
        {
            return false;
        }
        record.rgb.width = config.rgb_width;
        record.rgb.height = config.rgb_height;
        record.rgb.pixel_format = "rgb8";
        record.rgb.color_space = "srgb";
        record.rgb.row_origin = "top-left";
        record.rgb.row_stride_bytes = config.rgb_width * 3U;
        record.rgb.raw_sha256 =
            ComputeSha256(rgb8.data(), rgb8.size()).ToHex();
        record.contacts = ReadContacts();
        if (!BuildStateSha256(
                expected_id.completed_physics_steps,
                record.state_sha256))
        {
            return false;
        }
        return EncodeObservationSample(
            record,
            rgb8,
            observation);
    }

    void AppendContactEvents(
        std::uint64_t transition_index,
        std::uint64_t event_tick,
        const std::set<std::uint32_t>& previous,
        const std::set<std::uint32_t>& current,
        std::vector<EventRecord>& events)
    {
        std::vector<EventRecord> changes;
        for (std::uint32_t node : current)
        {
            if (previous.count(node) != 0U)
                continue;
            EventRecord event;
            event.event_id =
                "contact.begin:" +
                std::to_string(transition_index) + ":" +
                std::to_string(event_tick) + ":" +
                std::to_string(node);
            event.event_type = "contact.begin";
            event.physics_tick = event_tick;
            event.detail = "node=" + std::to_string(node);
            changes.push_back(event);
        }
        for (std::uint32_t node : previous)
        {
            if (current.count(node) != 0U)
                continue;
            EventRecord event;
            event.event_id =
                "contact.end:" +
                std::to_string(transition_index) + ":" +
                std::to_string(event_tick) + ":" +
                std::to_string(node);
            event.event_type = "contact.end";
            event.physics_tick = event_tick;
            event.detail = "node=" + std::to_string(node);
            changes.push_back(event);
        }
        std::sort(
            changes.begin(),
            changes.end(),
            [](const EventRecord& first, const EventRecord& second)
            {
                return first.event_id < second.event_id;
            });
        events.insert(
            events.end(),
            changes.begin(),
            changes.end());
    }

    ActorManager& actor_manager;
    ActorPtr player_actor;
    RoRRuntimeCaptureConfig config;
    RoRRuntimeCaptureDescriptor capture_descriptor;
    RoRRuntimeResourceIdentity resource_identity;
    Terrain* bound_terrain = nullptr;
    CacheEntryPtr bound_vehicle_entry;
    CacheEntryPtr bound_terrain_entry;
    std::string bound_vehicle_filename;
    std::string bound_vehicle_resource_group;
    std::string bound_terrain_filename;
    std::string bound_terrain_resource_group;
    TerrainObjectManager* bound_object_manager = nullptr;
    Ogre::SceneManager* bound_scene_manager = nullptr;
    std::vector<TerrainObjectBinding> terrain_objects;
    bool config_valid = false;
    std::string config_error;
    std::unique_ptr<RenderResources> render;

    TransitionId active_transition;
    ControlLineage controls;
    ContactSummary transition_contacts;
    std::set<std::uint32_t> previous_contact_nodes;
    std::set<std::uint32_t> transition_contact_nodes;
    std::vector<EventRecord> transition_events;
    bool transition_active = false;
    bool resolved_captured = false;

    Ogre::Quaternion previous_vehicle_orientation =
        Ogre::Quaternion::IDENTITY;
    bool have_previous_vehicle_orientation = false;
};

RoRRuntimeCaptureProvider::RoRRuntimeCaptureProvider(
    ActorManager& actor_manager,
    ActorPtr player_actor,
    const RoRRuntimeCaptureConfig& config):
    m_impl(
        new Impl(
            actor_manager,
            player_actor,
            config))
{
}

RoRRuntimeCaptureProvider::~RoRRuntimeCaptureProvider() = default;

bool RoRRuntimeCaptureProvider::IsReady(std::string* error) const
{
    if (m_impl == nullptr)
    {
        if (error != nullptr)
            *error = "runtime capture provider is unavailable";
        return false;
    }
    if (!m_impl->config_valid)
    {
        if (error != nullptr)
            *error = m_impl->config_error;
        return false;
    }
    if (!m_impl->HasSchema1DriverCamera())
    {
        if (error != nullptr)
        {
            *error =
                "runtime capture player/manager/camera binding is no longer live";
        }
        return false;
    }
    return
        m_impl->BoundResourcesLive(false, error) &&
        m_impl->CaptureEnvironmentReady(error) &&
        m_impl->PhysicalControlInputsReady(error);
}

bool RoRRuntimeCaptureProvider::InspectCaptureDescriptor(
    RoRRuntimeCaptureDescriptor& descriptor,
    std::string* error) const
{
    if (!IsReady(error) || m_impl == nullptr)
        return false;
    descriptor = m_impl->capture_descriptor;
    return true;
}

bool RoRRuntimeCaptureProvider::CaptureCurrentStateSha256(
    std::uint64_t completed_physics_steps,
    std::string& state_sha256) const
{
    if (m_impl == nullptr ||
        !m_impl->config_valid ||
        !m_impl->BoundResourcesLive(true) ||
        !m_impl->CaptureEnvironmentReady())
        return false;
    std::string candidate;
    try
    {
        if (!m_impl->BuildStateSha256(
                completed_physics_steps,
                candidate))
        {
            return false;
        }
    }
    catch (...)
    {
        return false;
    }
    state_sha256 = std::move(candidate);
    return true;
}

bool RoRRuntimeCaptureProvider::CaptureResetBaseline(
    const ObservationId& expected_id,
    ObservationSample& observation)
{
    if (m_impl == nullptr ||
        !m_impl->config_valid ||
        !m_impl->BoundResourcesLive(false) ||
        !m_impl->CaptureEnvironmentReady() ||
        m_impl->transition_active)
    {
        return false;
    }
    try
    {
        PhysicsStepRange steps;
        steps.first_completed_step =
            expected_id.completed_physics_steps;
        steps.last_completed_step =
            expected_id.completed_physics_steps;
        steps.substep_count = 0U;
        ObservationSample candidate;
        if (!m_impl->BuildObservation(
                expected_id,
                steps,
                candidate,
                true))
        {
            return false;
        }
        m_impl->ReadContacts(&m_impl->previous_contact_nodes);
        observation = std::move(candidate);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool RoRRuntimeCaptureProvider::BeginTransition(
    const TransitionId& transition)
{
    if (m_impl == nullptr ||
        !m_impl->config_valid ||
        !m_impl->BoundResourcesLive(false) ||
        !m_impl->CaptureEnvironmentReady() ||
        m_impl->transition_active ||
        !IsValidTransitionId(transition))
    {
        return false;
    }
    try
    {
        if (!m_impl->MakeInitialControlStages(transition))
            return false;
        m_impl->active_transition = transition;
        m_impl->transition_contacts = ContactSummary();
        m_impl->transition_contact_nodes =
            m_impl->previous_contact_nodes;
        m_impl->transition_events.clear();
        m_impl->resolved_captured = false;
        m_impl->transition_active = true;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool RoRRuntimeCaptureProvider::ObserveAppliedInputAtFixedStepStart(
    const FixedStepCaptureBridge::StepStartIdentity& identity)
{
    if (m_impl == nullptr ||
        !m_impl->BoundActorReady() ||
        !m_impl->transition_active ||
        identity.completed_physics_steps !=
            m_impl->active_transition.source.completed_physics_steps +
                identity.batch_step_index ||
        identity.batch_step_count !=
            m_impl->active_transition.target.completed_physics_steps -
                m_impl->active_transition.source.completed_physics_steps)
    {
        return false;
    }
    try
    {
        if (!m_impl->MakeResolvedStage())
            return false;
        const std::uint64_t transition_index =
            m_impl->active_transition.source.observation_index;
        for (const std::string& control_id :
             m_impl->config.control_ids)
        {
            double value = 0.0;
            if (!m_impl->ReadResolvedControl(control_id, value))
                return false;
            const std::uint64_t tick =
                identity.effective_input_tick;
            m_impl->controls.applied.push_back(
                MakeControlSample(
                    SampleId(
                        "applied",
                        transition_index,
                        control_id,
                        &tick),
                    control_id,
                    APPLIED_CONTROL_SOURCE_ID,
                    m_impl->active_transition.source.
                        completed_physics_steps,
                    tick,
                    value,
                    SampleId(
                        "resolved",
                        transition_index,
                        control_id)));
        }
        std::set<std::uint32_t> current_contact_nodes;
        m_impl->AccumulateContacts(
            m_impl->ReadContacts(&current_contact_nodes));
        if (identity.batch_step_index != 0U)
        {
            m_impl->AppendContactEvents(
                transition_index,
                identity.effective_input_tick - 1U,
                m_impl->transition_contact_nodes,
                current_contact_nodes,
                m_impl->transition_events);
        }
        m_impl->transition_contact_nodes =
            std::move(current_contact_nodes);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool RoRRuntimeCaptureProvider::CaptureCompletedTransition(
    const TransitionId& expected_transition,
    TransitionSample& transition,
    ObservationSample& observation)
{
    if (m_impl == nullptr ||
        !m_impl->BoundActorReady() ||
        !m_impl->CaptureEnvironmentReady() ||
        !m_impl->transition_active ||
        expected_transition != m_impl->active_transition)
    {
        return false;
    }
    try
    {
        const std::uint64_t substeps =
            expected_transition.target.completed_physics_steps -
            expected_transition.source.completed_physics_steps;
        if (!m_impl->resolved_captured ||
            substeps > std::numeric_limits<std::uint32_t>::max())
        {
            return false;
        }
        const std::size_t substep_count =
            static_cast<std::size_t>(substeps);
        const std::size_t control_count =
            m_impl->config.control_ids.size();
        if (control_count != 0U &&
            substep_count >
                std::numeric_limits<std::size_t>::max() / control_count)
        {
            return false;
        }
        const std::size_t expected_applied_count =
            substep_count * control_count;
        if (m_impl->controls.applied.size() != expected_applied_count)
        {
            return false;
        }

        std::set<std::uint32_t> current_contact_nodes;
        const ContactSummary final_contacts =
            m_impl->ReadContacts(&current_contact_nodes);
        m_impl->AccumulateContacts(final_contacts);

        TransitionRecord transition_record;
        transition_record.episode_id =
            expected_transition.source.episode;
        transition_record.transition_index =
            expected_transition.source.observation_index;
        transition_record.transition_id = expected_transition;
        transition_record.target_id = m_impl->config.target_id;
        transition_record.source_time = {
            expected_transition.source.observation_index,
            WORLD_MODEL_OBSERVATION_RATE_HZ};
        transition_record.target_time = {
            expected_transition.target.observation_index,
            WORLD_MODEL_OBSERVATION_RATE_HZ};
        transition_record.effective_steps = {
            expected_transition.source.completed_physics_steps,
            expected_transition.target.completed_physics_steps,
            static_cast<std::uint32_t>(substeps)};
        transition_record.controls = m_impl->controls;
        transition_record.contacts = m_impl->transition_contacts;
        m_impl->AppendContactEvents(
            transition_record.transition_index,
            expected_transition.target.completed_physics_steps - 1U,
            m_impl->transition_contact_nodes,
            current_contact_nodes,
            m_impl->transition_events);
        transition_record.events = m_impl->transition_events;
        transition_record.outcome.status = "running";
        transition_record.outcome.terminal = false;
        transition_record.outcome.reset = false;
        transition_record.outcome.success = false;
        transition_record.outcome.reward = 0.0;
        transition_record.outcome.detail.clear();

        TransitionSample transition_candidate;
        if (!EncodeTransitionSample(
                transition_record,
                transition_candidate))
        {
            return false;
        }

        PhysicsStepRange observation_steps = {
            expected_transition.source.completed_physics_steps,
            expected_transition.target.completed_physics_steps,
            static_cast<std::uint32_t>(substeps)};
        ObservationSample observation_candidate;
        if (!m_impl->BuildObservation(
                expected_transition.target,
                observation_steps,
                observation_candidate,
                false))
        {
            return false;
        }

        transition = std::move(transition_candidate);
        observation = std::move(observation_candidate);
        m_impl->previous_contact_nodes =
            std::move(current_contact_nodes);
        m_impl->transition_active = false;
        m_impl->resolved_captured = false;
        m_impl->controls = ControlLineage();
        m_impl->transition_events.clear();
        m_impl->transition_contact_nodes.clear();
        return true;
    }
    catch (...)
    {
        return false;
    }
}

class RoRWorldModelRuntime::Impl
{
public:
    Impl(
        ActorManager& actor_manager,
        ActorPtr player_actor,
        const RoRRuntimeCaptureConfig& config):
        runtime(actor_manager, player_actor),
        provider(actor_manager, player_actor, config),
        backend(runtime, provider)
    {
    }

    ActorManagerFixedStepRuntime runtime;
    RoRRuntimeCaptureProvider provider;
    RuntimeCaptureBackend backend;
};

bool ValidateCurrentRoRLiveCaptureRuntimeState(
    const ActorPtr& player_actor,
    Terrain* terrain,
    std::string* error)
{
    try
    {
        if (player_actor == nullptr || terrain == nullptr ||
            terrain->getObjectManager() == nullptr)
        {
            if (error != nullptr)
            {
                *error =
                    "schema-1 runtime player Actor/Terrain is unavailable";
            }
            return false;
        }
        if (App::sim_spawn_running == nullptr ||
            App::sim_replay_enabled == nullptr ||
            App::sim_realistic_commands == nullptr ||
            App::sim_races_enabled == nullptr ||
            App::sim_no_collisions == nullptr ||
            App::sim_no_self_collisions == nullptr ||
            App::sim_deterministic_sleeping_engine == nullptr ||
            App::sim_deterministic_fixed_steps_per_frame == nullptr)
        {
            if (error != nullptr)
                *error = "schema-1 simulation CVars are unavailable";
            return false;
        }

        LiveCaptureRuntimeState state;
        state.has_section_config =
            !player_actor->getSectionConfig().empty();
        state.has_working_tuneup =
            player_actor->getWorkingTuneupDef() != nullptr;
        state.has_skin =
            player_actor->getUsedSkinEntry() != nullptr;
        state.addonpart_count =
            static_cast<std::uint64_t>(
                player_actor->getUsedAddonpartEntries().size());
        state.assetpack_count =
            static_cast<std::uint64_t>(
                player_actor->getUsedAssetpackEntries().size());
        state.has_inter_point_collision_detector =
            player_actor->hasInterPointCollisionDetector();
        state.has_intra_point_collision_detector =
            player_actor->hasIntraPointCollisionDetector();
        state.has_replay_handler =
            player_actor->getReplay() != nullptr;
        state.terrain_collision_profile_canonical =
            terrain->getObjectManager()->
                HasCanonicalWorldModelCollisionProfile();
        state.sim_spawn_running =
            App::sim_spawn_running->getBool();
        state.sim_replay_enabled =
            App::sim_replay_enabled->getBool();
        state.sim_realistic_commands =
            App::sim_realistic_commands->getBool();
        state.sim_races_enabled =
            App::sim_races_enabled->getBool();
        state.sim_no_collisions =
            App::sim_no_collisions->getBool();
        state.sim_no_self_collisions =
            App::sim_no_self_collisions->getBool();
        state.sim_deterministic_sleeping_engine =
            App::sim_deterministic_sleeping_engine->getBool();
        state.sim_deterministic_fixed_steps_per_frame =
            App::sim_deterministic_fixed_steps_per_frame->getInt();
        return ValidateLiveCaptureRuntimeState(state, error);
    }
    catch (...)
    {
        if (error != nullptr)
            *error = "schema-1 runtime-state inspection failed";
        return false;
    }
}

bool InspectCurrentRoRRuntimeResourceIdentity(
    RoRRuntimeResourceIdentity& identity,
    std::string* error)
{
    try
    {
        GameContext* const context = App::GetGameContext();
        if (context == nullptr ||
            context->GetActorManager() == nullptr ||
            context->GetPlayerActor() == nullptr ||
            context->GetTerrain() == nullptr)
        {
            if (error != nullptr)
            {
                *error =
                    "no loaded GameContext/ActorManager/player/Terrain";
            }
            return false;
        }
        context->GetActorManager()->SyncWithSimThread();
        if (!ValidateCurrentRoRLiveCaptureRuntimeState(
                context->GetPlayerActor(),
                context->GetTerrain().GetRef(),
                error))
        {
            return false;
        }
        RoRRuntimeResourceIdentity candidate;
        if (!InspectRuntimeResourceIdentity(
                context->GetPlayerActor(),
                context->GetTerrain().GetRef(),
                candidate))
        {
            if (error != nullptr)
            {
                *error =
                    "failed to hash loaded vehicle/terrain resource groups";
            }
            return false;
        }
        identity = std::move(candidate);
        return true;
    }
    catch (...)
    {
        if (error != nullptr)
            *error = "loaded resource identity inspection failed";
        return false;
    }
}

RoRWorldModelRuntime::RoRWorldModelRuntime(
    ActorManager& actor_manager,
    ActorPtr player_actor,
    const RoRRuntimeCaptureConfig& config):
    m_impl(
        new Impl(
            actor_manager,
            player_actor,
            config))
{
}

RoRWorldModelRuntime::~RoRWorldModelRuntime() = default;

CaptureBackend& RoRWorldModelRuntime::GetBackend()
{
    return m_impl->backend;
}

RoRRuntimeCaptureProvider& RoRWorldModelRuntime::GetProvider()
{
    return m_impl->provider;
}

std::unique_ptr<RoRWorldModelRuntime>
CreateCurrentRoRWorldModelRuntime(
    const RoRRuntimeCaptureConfig& config,
    std::string* error)
{
    try
    {
        GameContext* const context = App::GetGameContext();
        if (context == nullptr ||
            context->GetActorManager() == nullptr ||
            context->GetPlayerActor() == nullptr)
        {
            if (error != nullptr)
            {
                *error =
                    "no loaded GameContext/ActorManager/player Actor";
            }
            return std::unique_ptr<RoRWorldModelRuntime>();
        }
        std::unique_ptr<RoRWorldModelRuntime> runtime(
            new RoRWorldModelRuntime(
                *context->GetActorManager(),
                context->GetPlayerActor(),
                config));
        if (!runtime->GetProvider().IsReady(error))
            return std::unique_ptr<RoRWorldModelRuntime>();
        return runtime;
    }
    catch (...)
    {
        if (error != nullptr)
            *error = "live RoR world-model runtime construction failed";
        return std::unique_ptr<RoRWorldModelRuntime>();
    }
}

} // namespace WorldModel
} // namespace RoR
