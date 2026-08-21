/// \title CityWorld Next compiled bridge runtime gate
/// \brief Drives a pinned DAF across three compiled bridge spans and captures RGB.

const int APP_STATE_SIMULATION = 2;
const int SIM_STATE_PAUSED = 2;
const int64 ACTOR_ID = 2026072802;
const uint64 MAX_PHYSICS_STEPS = 30000;
// Hosted llvmpipe renders this scene at roughly one frame per second.  The
// bridge contract is over fixed physics steps and exact crossing markers, not
// over render-frame cadence, so batch enough fixed steps per presented frame
// to finish inside the cross-platform runtime budget without weakening the
// integration, collision, screenshot, or route assertions below.
const int FIXED_STEPS_PER_FRAME = 80;
const string VEHICLE = "b6b0UID-semi.truck";

const float LANE_X = 510.25f;
const float SPAWN_Z = 458.0f;
const float BRIDGE_ENTRY_Z = 482.0f;
const float SEAM_0_Z = 502.0f;
const float SEAM_1_Z = 522.0f;
const float BRIDGE_EXIT_Z = 542.0f;
const float PASS_Z = 548.0f;

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
bool gCaptured = false;
bool gEntered = false;
bool gSeam0 = false;
bool gSeam1 = false;
bool gExited = false;
uint gFrames = 0;
float gMinY = 100000.0f;
float gMaxY = -100000.0f;
float gMaxLateralError = 0.0f;
float gTraversalStartX = 0.0f;

void Fail(const string &in reason)
{
    if (gState == FINISHED)
        return;

    if (@gActor != null)
        gActor.clearEventSimulatedValues();
    console.cVarSet("sim_deterministic_fixed_steps_per_frame", "0");
    console.cVarSet("ui_hide_gui", "false");
    gState = FINISHED;
    game.log("[RoR|CW2|BridgeRuntime] FAIL reason=" + reason);
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

    console.cVarSet(
        "sim_deterministic_fixed_steps_per_frame",
        "" + FIXED_STEPS_PER_FRAME);
    console.cVarSet("sim_deterministic_sleeping_engine", "true");
    console.cVarSet("sim_no_collisions", "false");
    console.cVarSet("sim_no_self_collisions", "false");
    console.cVarSet("ui_hide_gui", "true");
    game.registerForEvent(SE_GENERIC_NEW_TRUCK);
    game.log(
        "[RoR|CW2|BridgeRuntime] START spans=3 length_m=60 "
        "lane_x=510.25 surface_y=0.08");
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
            {"position", vector3(LANE_X, 2.0f, SPAWN_Z)},
            // The pinned DAF rig's authoring-forward axis is -X. A +90 degree
            // Y rotation aligns its physical forward direction with bridge +Z.
            {"rotation", quaternion(radian(1.57079633f), vector3(0.0f, 1.0f, 0.0f))}
        });
}

void HoldCamera()
{
    game.setCameraPosition(vector3(544.0f, 18.0f, 468.0f));
    game.cameraLookAt(vector3(512.0f, 0.5f, 512.0f));
}

void UpdateTraversal()
{
    const vector3 position = gActor.getPosition();
    if (position.y < gMinY)
        gMinY = position.y;
    if (position.y > gMaxY)
        gMaxY = position.y;
    // Track drift from the spawned rig's physical center rather than the
    // request position; rig node origins are not required to be centered.
    const float lateralError = abs(position.x - gTraversalStartX);
    if (lateralError > gMaxLateralError)
        gMaxLateralError = lateralError;

    if (!gEntered && position.z >= BRIDGE_ENTRY_Z)
    {
        gEntered = true;
        game.log("[RoR|CW2|BridgeRuntime] ENTER z=" + position.z);
    }
    if (!gSeam0 && position.z >= SEAM_0_Z)
    {
        gSeam0 = true;
        game.log("[RoR|CW2|BridgeRuntime] SEAM index=0 z=" + position.z);
    }
    if (!gSeam1 && position.z >= SEAM_1_Z)
    {
        gSeam1 = true;
        game.log("[RoR|CW2|BridgeRuntime] SEAM index=1 z=" + position.z);
    }
    if (!gExited && position.z >= BRIDGE_EXIT_Z)
    {
        gExited = true;
        game.log("[RoR|CW2|BridgeRuntime] EXIT z=" + position.z);
    }

    if (!gCaptured && position.z >= 506.0f)
    {
        gCaptured = true;
        game.log("[RoR|CW2|BridgeRuntime] CAPTURE z=" + position.z);
        game.pushMessage(MSG_APP_SCREENSHOT_REQUESTED, {});
    }

    if (position.z >= PASS_Z)
    {
        gActor.clearEventSimulatedValues();
        if (!gEntered || !gSeam0 || !gSeam1 || !gExited)
        {
            Fail("connector-marker-missing");
            return;
        }
        if (!gCaptured)
        {
            Fail("capture-missing");
            return;
        }
        if (gMaxLateralError > 2.0f)
        {
            Fail("lateral-error-" + gMaxLateralError);
            return;
        }
        if (gMaxY - gMinY > 2.5f || gMinY < -1.0f || gMaxY > 5.0f)
        {
            Fail("vertical-envelope-" + gMinY + "-" + gMaxY);
            return;
        }

        console.cVarSet("sim_deterministic_fixed_steps_per_frame", "0");
        console.cVarSet("ui_hide_gui", "false");
        gState = FINISHED;
        game.log(
            "[RoR|CW2|BridgeRuntime] PASS spans=3 seams=2 "
            "distance_m=" + (position.z - SPAWN_Z) +
            " min_y=" + gMinY +
            " max_y=" + gMaxY +
            " lateral_error=" + gMaxLateralError +
            " speed=" + gActor.getSpeed() +
            " physics_steps=" + game.getCompletedPhysicsSteps());
        game.quitGame();
    }
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
        gTraversalStartX = gActor.getPosition().x;
        if (gActor.getParkingBrake())
            gActor.parkingbrakeToggle();
        gActor.setEventSimulatedValue(EV_TRUCK_ACCELERATE, 0.72f);
        game.pushMessage(MSG_SIM_UNPAUSE_REQUESTED, {});
        gState = DRIVING;
        game.log(
            "[RoR|CW2|BridgeRuntime] ARMED actor=2026072802 "
            "heading=" + gActor.getHeadingDirectionAngle() +
            " start_x=" + gTraversalStartX);
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
                "[RoR|CW2|BridgeRuntime] SAMPLE frame=" + gFrames +
                " x=" + position.x +
                " y=" + position.y +
                " z=" + position.z +
                " speed=" + gActor.getSpeed());
        }
        UpdateTraversal();
    }
}
