/// \title CityWorld Next curved bridge runtime gate
/// \brief Drives a pinned DAF through three connector-solved 15-degree spans.

const int APP_STATE_SIMULATION = 2;
const int SIM_STATE_PAUSED = 2;
const int64 ACTOR_ID = 2026072803;
const uint64 MAX_PHYSICS_STEPS = 36000;
const string VEHICLE = "b6b0UID-semi.truck";

const float SPAWN_X = 512.0f;
const float SPAWN_Z = 452.0f;
const float PATH_ENTRY_Z = 482.0f;
const float PATH_CENTRE_X = 588.394372684f;
const float PATH_CENTRE_Z = 482.0f;
const float PATH_RADIUS = 76.394372684f;
const float SEAM_0_Z = 501.772318589f;
const float SEAM_1_Z = 520.197186344f;
const float EXIT_Z = 536.018978981f;
const float PASS_Z = 535.0f;

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
    game.log("[RoR|CW2|CurveRuntime] FAIL reason=" + reason);
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
        "[RoR|CW2|CurveRuntime] START modules=3 seams=2 "
        "turn_degrees=45 radius_m=76.394372684");
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
    game.setCameraPosition(vector3(561.0f, 25.0f, 471.0f));
    game.cameraLookAt(vector3(519.0f, 0.2f, 512.0f));
}

void UpdateControls(const vector3 &in position)
{
    gActor.setEventSimulatedValue(EV_TRUCK_ACCELERATE, 0.52f);
    if (position.z < PATH_ENTRY_Z - 3.0f)
    {
        gActor.setEventSimulatedValue(EV_TRUCK_STEER_LEFT, 0.0f);
        gActor.setEventSimulatedValue(EV_TRUCK_STEER_RIGHT, 0.0f);
        return;
    }

    const float radius = Distance2D(
        position.x,
        position.z,
        PATH_CENTRE_X,
        PATH_CENTRE_Z);
    const float radialError = radius - PATH_RADIUS;
    const float steer = Clamp(0.10f + radialError * 0.06f, -0.15f, 0.30f);
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
        const float pathError = abs(
            Distance2D(
                position.x,
                position.z,
                PATH_CENTRE_X,
                PATH_CENTRE_Z)
            - PATH_RADIUS);
        if (pathError > gMaxPathError)
            gMaxPathError = pathError;
    }

    if (!gEntered && position.z >= PATH_ENTRY_Z)
    {
        gEntered = true;
        game.log("[RoR|CW2|CurveRuntime] ENTER x=" + position.x + " z=" + position.z);
    }
    if (!gSeam0 && position.z >= SEAM_0_Z)
    {
        gSeam0 = true;
        game.log("[RoR|CW2|CurveRuntime] SEAM index=0 x=" + position.x + " z=" + position.z);
    }
    if (!gCaptured && position.z >= 510.0f)
    {
        gCaptured = true;
        game.log("[RoR|CW2|CurveRuntime] CAPTURE x=" + position.x + " z=" + position.z);
        game.pushMessage(MSG_APP_SCREENSHOT_REQUESTED, {});
    }
    if (!gSeam1 && position.z >= SEAM_1_Z)
    {
        gSeam1 = true;
        game.log("[RoR|CW2|CurveRuntime] SEAM index=1 x=" + position.x + " z=" + position.z);
    }
    if (!gExited && position.z >= EXIT_Z - 1.5f)
    {
        gExited = true;
        game.log("[RoR|CW2|CurveRuntime] EXIT x=" + position.x + " z=" + position.z);
    }

    if (position.z >= PASS_Z)
    {
        ClearControls();
        if (!gEntered || !gSeam0 || !gSeam1 || !gExited || !gCaptured)
        {
            Fail("connector-marker-missing");
            return;
        }
        if (position.x < 530.0f || position.x > 538.0f)
        {
            Fail("exit-x-" + position.x);
            return;
        }
        if (gMaxPathError > 2.25f)
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
            "[RoR|CW2|CurveRuntime] PASS modules=3 seams=2 turn_degrees=45 "
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
            "[RoR|CW2|CurveRuntime] ARMED actor=2026072803 "
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
                "[RoR|CW2|CurveRuntime] SAMPLE frame=" + gFrames +
                " x=" + position.x +
                " y=" + position.y +
                " z=" + position.z +
                " speed=" + gActor.getSpeed());
        }
        UpdateTraversal();
    }
}
