/// \title CityWorld Next LED streetlight native runtime gate
/// \brief Renders the compiled fixture and drives a pinned DAF into its proxy.

const int APP_STATE_SIMULATION = 2;
const int SIM_STATE_PAUSED = 2;
const int64 ACTOR_ID = 2026072901;
const uint64 MAX_PHYSICS_STEPS = 24000;
const uint64 POST_CONTACT_STEPS = 1600;
const string VEHICLE = "b6b0UID-semi.truck";

// The DAF's authored node origin is left of its physical centerline.
const float SPAWN_X = 512.10f;
const float SPAWN_Z = 466.0f;
const float POLE_X = 512.0f;
const float POLE_Z = 500.0f;
const float POLE_RADIUS = 0.34f;
const float POLE_MIN_Y = 0.08f;
const float POLE_MAX_Y = 7.28f;

enum RuntimeState
{
    WAITING_FOR_TERRAIN = 0,
    WAITING_FOR_PAUSE,
    WAITING_FOR_ACTOR,
    DRIVING,
    FINISHED
}

RuntimeState gState = WAITING_FOR_TERRAIN;
BeamClass@ gActor;
CVarClass@ gAppState;
CVarClass@ gSimState;
bool gActorSpawned = false;
bool gContact = false;
bool gCaptured = false;
uint gFrames = 0;
uint64 gContactStep = 0;
float gApproachSpeed = 0.0f;
float gContactClearance = 100000.0f;
float gContactActorZ = 0.0f;
float gMinClearance = 100000.0f;
float gMaxPostContactZ = -100000.0f;

void ClearControls()
{
    if (@gActor != null)
        gActor.clearEventSimulatedValues();
}

void FinishRuntime()
{
    console.cVarSet("sim_deterministic_fixed_steps_per_frame", "0");
    console.cVarSet("ui_hide_gui", "false");
    gState = FINISHED;
}

void Fail(const string &in reason)
{
    if (gState == FINISHED)
        return;
    ClearControls();
    FinishRuntime();
    game.log("[RoR|CW1|StreetlightRuntime] FAIL reason=" + reason);
    game.quitGame();
}

void main()
{
    @gAppState = console.cVarFind("app_state");
    @gSimState = console.cVarFind("sim_state");
    if (@gAppState == null || @gSimState == null)
    {
        Fail("required-cvar-missing");
        return;
    }

    console.cVarSet("sim_deterministic_fixed_steps_per_frame", "20");
    console.cVarSet("sim_deterministic_sleeping_engine", "true");
    console.cVarSet("sim_no_collisions", "false");
    console.cVarSet("sim_no_self_collisions", "false");
    console.cVarSet("ui_hide_gui", "true");
    game.registerForEvent(SE_GENERIC_NEW_TRUCK);
    game.log(
        "[RoR|CW1|StreetlightRuntime] START fixtures=1 "
        "collision_triangles=44 pole_radius=0.34");
}

void eventCallbackEx(
    scriptEvents event,
    int arg1,
    int arg2,
    int arg3,
    int arg4,
    string arg5,
    string arg6,
    string arg7,
    string arg8)
{
    if (event != SE_GENERIC_NEW_TRUCK)
        return;
    if (arg1 != ACTOR_ID)
    {
        Fail("unexpected-actor-id-" + arg1);
        return;
    }
    gActorSpawned = true;
}

bool SpawnActor()
{
    return game.pushMessage(
        MSG_SIM_SPAWN_ACTOR_REQUESTED,
        {
            {"filename", VEHICLE},
            {"instance_id", ACTOR_ID},
            {"free_position", true},
            {"enter", false},
            {"position", vector3(SPAWN_X, 2.0f, SPAWN_Z)},
            {"rotation", quaternion(
                radian(1.57079633f),
                vector3(0.0f, 1.0f, 0.0f))}
        });
}

void HoldCamera()
{
    game.setCameraPosition(vector3(526.0f, 10.0f, 480.0f));
    game.cameraLookAt(vector3(512.0f, 3.8f, 500.0f));
}

float NodeClearance()
{
    float minimum = 100000.0f;
    const int count = gActor.getNodeCount();
    for (int index = 0; index < count; index++)
    {
        const vector3 node = gActor.getNodePosition(index);
        if (node.y < POLE_MIN_Y || node.y > POLE_MAX_Y)
            continue;
        const float dx = node.x - POLE_X;
        const float dz = node.z - POLE_Z;
        const float clearance = sqrt(dx * dx + dz * dz) - POLE_RADIUS;
        if (clearance < minimum)
            minimum = clearance;
    }
    return minimum;
}

void Pass()
{
    const uint64 steps = game.getCompletedPhysicsSteps();
    const float postContactSpeed = gActor.getSpeed();
    const float contactTravel = gMaxPostContactZ - gContactActorZ;
    ClearControls();

    if (!gCaptured)
    {
        Fail("capture-missing");
        return;
    }
    if (gApproachSpeed < 2.0f || gApproachSpeed > 20.0f)
    {
        Fail("approach-speed-" + gApproachSpeed);
        return;
    }
    if (gMinClearance < -0.15f || gMinClearance > 0.25f)
    {
        Fail("clearance-" + gMinClearance);
        return;
    }
    if (contactTravel < 0.0f || contactTravel > 3.5f)
    {
        Fail("contact-travel-" + contactTravel);
        return;
    }
    if (postContactSpeed > gApproachSpeed * 0.75f)
    {
        Fail("insufficient-speed-loss-" + postContactSpeed);
        return;
    }

    FinishRuntime();
    game.log(
        "[RoR|CW1|StreetlightRuntime] PASS fixtures=1 "
        "collision_triangles=44 approach_speed=" + gApproachSpeed +
        " post_contact_speed=" + postContactSpeed +
        " min_clearance=" + gMinClearance +
        " contact_travel=" + contactTravel +
        " max_z=" + gMaxPostContactZ +
        " physics_steps=" + steps);
    game.quitGame();
}

void UpdateCollision()
{
    const uint64 steps = game.getCompletedPhysicsSteps();
    const vector3 position = gActor.getPosition();
    const float speed = gActor.getSpeed();
    const float clearance = NodeClearance();

    if (clearance < gMinClearance)
        gMinClearance = clearance;
    // Freeze the approach maximum in the clear corridor, before any impact
    // impulse can pollute the actor's aggregate speed.
    if (
        !gContact &&
        position.z <= 493.0f &&
        clearance > 3.0f &&
        speed >= 0.0f &&
        speed <= 20.0f &&
        speed > gApproachSpeed)
        gApproachSpeed = speed;

    if (!gContact && gApproachSpeed >= 2.0f && clearance <= 0.25f)
    {
        gContact = true;
        gContactStep = steps;
        gContactClearance = clearance;
        gContactActorZ = position.z;
        gMaxPostContactZ = position.z;
        game.log(
            "[RoR|CW1|StreetlightRuntime] CONTACT step=" + gContactStep +
            " clearance=" + gContactClearance +
            " approach_speed=" + gApproachSpeed +
            " actor_z=" + gContactActorZ);
        game.log("[RoR|CW1|StreetlightRuntime] CAPTURE");
        game.pushMessage(MSG_APP_SCREENSHOT_REQUESTED, {});
        gCaptured = true;
    }

    if (!gContact)
    {
        if (position.z > POLE_Z + 4.0f)
            Fail("collision-proxy-not-contacted");
        return;
    }

    if (position.z > gMaxPostContactZ)
        gMaxPostContactZ = position.z;
    if (gMaxPostContactZ - gContactActorZ > 3.5f)
    {
        Fail("collision-proxy-penetrated");
        return;
    }
    if (steps >= gContactStep + POST_CONTACT_STEPS)
        Pass();
}

void frameStep(float dt)
{
    if (gState == FINISHED)
        return;

    gFrames++;
    HoldCamera();
    if (gState == WAITING_FOR_TERRAIN)
    {
        if (gAppState.getInt() == APP_STATE_SIMULATION)
        {
            console.cVarSet("sim_state", "" + SIM_STATE_PAUSED);
            gState = WAITING_FOR_PAUSE;
        }
        return;
    }
    if (gState == WAITING_FOR_PAUSE)
    {
        if (gSimState.getInt() != SIM_STATE_PAUSED)
            return;
        if (!SpawnActor())
        {
            Fail("spawn-request-rejected");
            return;
        }
        gState = WAITING_FOR_ACTOR;
        return;
    }
    if (gState == WAITING_FOR_ACTOR)
    {
        if (!gActorSpawned)
            return;
        @gActor = game.getTruckByNum(ACTOR_ID);
        if (@gActor == null)
        {
            Fail("actor-not-addressable");
            return;
        }
        EngineClass@ engine = gActor.getEngine();
        if (@engine == null)
        {
            Fail("engine-missing");
            return;
        }
        engine.setAutoMode(SimGearboxMode::AUTO);
        engine.startEngine();
        if (gActor.getParkingBrake())
            gActor.parkingbrakeToggle();
        gActor.setEventSimulatedValue(EV_TRUCK_ACCELERATE, 0.50f);
        game.pushMessage(MSG_SIM_UNPAUSE_REQUESTED, {});
        gState = DRIVING;
        game.log(
            "[RoR|CW1|StreetlightRuntime] ARMED actor=2026072901 nodes=" +
            gActor.getNodeCount() +
            " heading=" + gActor.getHeadingDirectionAngle());
        return;
    }
    if (gState == DRIVING)
    {
        if (game.getCompletedPhysicsSteps() > MAX_PHYSICS_STEPS)
        {
            Fail("physics-step-timeout");
            return;
        }
        if (gFrames % 100 == 0)
        {
            const vector3 position = gActor.getPosition();
            game.log(
                "[RoR|CW1|StreetlightRuntime] SAMPLE frame=" + gFrames +
                " x=" + position.x +
                " y=" + position.y +
                " z=" + position.z +
                " speed=" + gActor.getSpeed() +
                " clearance=" + NodeClearance());
        }
        UpdateCollision();
    }
}
