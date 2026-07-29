/// \title CityWorld Next bridge transition runtime gate
/// \brief Drives a pinned DAF across three curves and the exit abutment.

const int APP_STATE_SIMULATION = 2;
const int SIM_STATE_PAUSED = 2;
const int64 ACTOR_ID = 2026072804;
const uint64 MAX_PHYSICS_STEPS = 40000;
const string VEHICLE = "b6b0UID-semi.truck";

const float SPAWN_X = 512.0f;
const float SPAWN_Z = 452.0f;
const float PATH_ENTRY_Z = 482.0f;
const float PATH_CENTRE_X = 588.394372684f;
const float PATH_CENTRE_Z = 482.0f;
const float PATH_RADIUS = 76.394372684f;
const float SEAM_0_Z = 501.772318589f;
const float SEAM_1_Z = 520.197186344f;
const float CURVE_EXIT_X = 534.375393687f;
const float CURVE_EXIT_Z = 536.018978981f;
const float EXIT_X = 542.860675053f;
const float EXIT_Z = 544.504260364f;
const float TRANSITION_DIR_X = 0.707106781f;
const float TRANSITION_DIR_Z = 0.707106781f;
const float ACTOR_REFERENCE_CROSS_TRACK = 1.75f;
const float PASS_PROGRESS = 10.5f;

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
bool gSeam2 = false;
bool gExited = false;
bool gHasLastPosition = false;
uint gFrames = 0;
float gMinY = 100000.0f;
float gMaxY = -100000.0f;
float gMaxPathError = 0.0f;
float gDistance = 0.0f;
vector3 gLastPosition;

float Clamp(float value, float minimum, float maximum)
{
    if (value < minimum)
        return minimum;
    if (value > maximum)
        return maximum;
    return value;
}

float Distance2D(float firstX, float firstZ, float secondX, float secondZ)
{
    const float dx = firstX - secondX;
    const float dz = firstZ - secondZ;
    return sqrt(dx * dx + dz * dz);
}

float TransitionProgress(const vector3 &in position)
{
    return
        (position.x - CURVE_EXIT_X) * TRANSITION_DIR_X +
        (position.z - CURVE_EXIT_Z) * TRANSITION_DIR_Z;
}

float TransitionCrossTrack(const vector3 &in position)
{
    return
        (position.x - CURVE_EXIT_X) * -TRANSITION_DIR_Z +
        (position.z - CURVE_EXIT_Z) * TRANSITION_DIR_X;
}

void ApplySteer(float steer)
{
    if (steer >= 0.0f)
    {
        gActor.setEventSimulatedValue(EV_TRUCK_STEER_LEFT, steer);
        gActor.setEventSimulatedValue(EV_TRUCK_STEER_RIGHT, 0.0f);
    }
    else
    {
        gActor.setEventSimulatedValue(EV_TRUCK_STEER_LEFT, 0.0f);
        gActor.setEventSimulatedValue(EV_TRUCK_STEER_RIGHT, -steer);
    }
}

void ClearControls()
{
    if (@gActor == null)
        return;
    gActor.clearEventSimulatedValues();
}

void Fail(const string &in reason)
{
    if (gState == FINISHED)
        return;

    ClearControls();
    console.cVarSet("sim_deterministic_fixed_steps_per_frame", "0");
    console.cVarSet("ui_hide_gui", "false");
    gState = FINISHED;
    game.log("[RoR|CW2|TransitionRuntime] FAIL reason=" + reason);
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
        "[RoR|CW2|TransitionRuntime] START modules=4 seams=3 "
        "turn_degrees=45 transition_m=12");
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
            {"rotation", quaternion(radian(1.57079633f), vector3(0.0f, 1.0f, 0.0f))}
        });
}

void HoldCamera()
{
    game.setCameraPosition(vector3(574.0f, 30.0f, 483.0f));
    game.cameraLookAt(vector3(527.0f, 0.2f, 520.0f));
}

void UpdateControls(const vector3 &in position)
{
    gActor.setEventSimulatedValue(EV_TRUCK_ACCELERATE, 0.52f);
    if (position.z < PATH_ENTRY_Z - 3.0f)
    {
        ApplySteer(0.0f);
        return;
    }
    if (position.z < CURVE_EXIT_Z - 4.0f)
    {
        const float radius = Distance2D(
            position.x,
            position.z,
            PATH_CENTRE_X,
            PATH_CENTRE_Z);
        const float radialError = radius - PATH_RADIUS;
        ApplySteer(Clamp(0.10f + radialError * 0.06f, -0.15f, 0.30f));
        return;
    }
    const float crossTrack = TransitionCrossTrack(position);
    const float crossError = crossTrack - ACTOR_REFERENCE_CROSS_TRACK;
    ApplySteer(Clamp(-0.08f - crossError * 0.12f, -0.30f, 0.18f));
}

void UpdateTraversal()
{
    const vector3 position = gActor.getPosition();
    UpdateControls(position);
    if (gHasLastPosition)
        gDistance += Distance2D(position.x, position.z, gLastPosition.x, gLastPosition.z);
    gLastPosition = position;
    gHasLastPosition = true;

    if (position.z >= PATH_ENTRY_Z - 1.0f)
    {
        if (position.y < gMinY)
            gMinY = position.y;
        if (position.y > gMaxY)
            gMaxY = position.y;
        float pathError = abs(
            TransitionCrossTrack(position) -
            ACTOR_REFERENCE_CROSS_TRACK);
        if (position.z < CURVE_EXIT_Z - 4.0f)
        {
            pathError = abs(
                Distance2D(
                    position.x,
                    position.z,
                    PATH_CENTRE_X,
                    PATH_CENTRE_Z)
                - PATH_RADIUS);
        }
        if (pathError > gMaxPathError)
            gMaxPathError = pathError;
    }

    if (!gEntered && position.z >= PATH_ENTRY_Z)
    {
        gEntered = true;
        game.log("[RoR|CW2|TransitionRuntime] ENTER x=" + position.x + " z=" + position.z);
    }
    if (!gSeam0 && position.z >= SEAM_0_Z)
    {
        gSeam0 = true;
        game.log("[RoR|CW2|TransitionRuntime] SEAM index=0 x=" + position.x + " z=" + position.z);
    }
    if (!gCaptured && position.z >= 522.0f)
    {
        gCaptured = true;
        game.log("[RoR|CW2|TransitionRuntime] CAPTURE x=" + position.x + " z=" + position.z);
        game.pushMessage(MSG_APP_SCREENSHOT_REQUESTED, {});
    }
    if (!gSeam1 && position.z >= SEAM_1_Z)
    {
        gSeam1 = true;
        game.log("[RoR|CW2|TransitionRuntime] SEAM index=1 x=" + position.x + " z=" + position.z);
    }
    const float progress = TransitionProgress(position);
    if (!gSeam2 && progress >= 0.0f)
    {
        gSeam2 = true;
        game.log("[RoR|CW2|TransitionRuntime] SEAM index=2 x=" + position.x + " z=" + position.z);
    }
    if (!gExited && progress >= 8.5f)
    {
        gExited = true;
        game.log("[RoR|CW2|TransitionRuntime] EXIT x=" + position.x + " z=" + position.z);
    }

    if (progress >= PASS_PROGRESS)
    {
        ClearControls();
        if (
            !gEntered || !gSeam0 || !gSeam1 || !gSeam2 ||
            !gExited || !gCaptured)
        {
            Fail("connector-marker-missing");
            return;
        }
        if (
            position.x < EXIT_X - 4.0f || position.x > EXIT_X + 4.0f ||
            position.z < EXIT_Z - 4.0f || position.z > EXIT_Z + 4.0f)
        {
            Fail("exit-position-" + position.x + "-" + position.z);
            return;
        }
        if (gMaxPathError > 2.5f)
        {
            Fail("path-error-" + gMaxPathError);
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
            "[RoR|CW2|TransitionRuntime] PASS modules=4 seams=3 turn_degrees=45 "
            "distance_m=" + gDistance +
            " min_y=" + gMinY +
            " max_y=" + gMaxY +
            " path_error=" + gMaxPathError +
            " exit_x=" + position.x +
            " exit_z=" + position.z +
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
        if (gActor.getParkingBrake())
            gActor.parkingbrakeToggle();
        game.pushMessage(MSG_SIM_UNPAUSE_REQUESTED, {});
        gState = DRIVING;
        game.log(
            "[RoR|CW2|TransitionRuntime] ARMED actor=2026072804 "
            "heading=" + gActor.getHeadingDirectionAngle());
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
                "[RoR|CW2|TransitionRuntime] SAMPLE frame=" + gFrames +
                " x=" + position.x +
                " y=" + position.y +
                " z=" + position.z +
                " speed=" + gActor.getSpeed());
        }
        UpdateTraversal();
    }
}
