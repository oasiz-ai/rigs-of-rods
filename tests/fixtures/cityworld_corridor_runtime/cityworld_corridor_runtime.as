/// \title CityWorld Next v3 curb-clearing route diagnostic
/// \brief Drives the pinned DAF from Penguinville across the v3 apron.

const int APP_STATE_SIMULATION = 2;
const int SIM_STATE_PAUSED = 2;
const int64 ACTOR_ID = 2026072901;
const uint64 MAX_PHYSICS_STEPS = 240000;
const string VEHICLE = "b6b0UID-semi.truck";

const float SOURCE_SEAM_STATION = 14.8491f;
const float MIDPOINT_STATION = 554.8491f;
const float DESTINATION_SEAM_STATION = 1075.447727259f;
const float PASS_STATION = 1087.447727259f;
const float LOOKAHEAD_M = 24.0f;
const float MAX_PATH_ERROR_M = 4.0f;
const float MAX_VERTICAL_ERROR_M = 3.5f;

// The 10 m source and 20 m destination extensions continue the endpoint
// tangents. Samples 1..59 are the exact v3 overlay-report waypoints. The
// source seam is the authenticated curb edge crossed by the raised apron.
array<vector3> gPath = {
    vector3(470.000000000f, 0.198000000f, 370.000000000f),
    vector3(480.000000000f, 0.198000000f, 370.000000000f),
    vector3(490.000000000f, 0.310000000f, 370.000000000f),
    vector3(494.849100000f, 0.310000000f, 370.000000000f),
    vector3(514.671323004f, 0.245000000f, 372.361372062f),
    vector3(533.892142129f, 0.180000000f, 377.847501062f),
    vector3(552.520727095f, 0.523750000f, 385.113320905f),
    vector3(570.667427185f, 1.430000000f, 393.516299503f),
    vector3(588.428720116f, 2.711250000f, 402.708106694f),
    vector3(605.878348816f, 4.180000000f, 412.479641991f),
    vector3(623.072000163f, 5.648750000f, 422.695368650f),
    vector3(640.052331493f, 6.930000000f, 433.262151032f),
    vector3(656.852669706f, 7.836250000f, 444.112997412f),
    vector3(673.499568614f, 8.180000000f, 455.197913737f),
    vector3(690.014582240f, 8.180000000f, 466.478443153f),
    vector3(706.415518767f, 8.180000000f, 477.924247170f),
    vector3(722.717311433f, 8.180000000f, 489.510853796f),
    vector3(738.932669539f, 8.180000000f, 501.218143544f),
    vector3(755.072550694f, 8.180000000f, 513.029285580f),
    vector3(771.146521856f, 8.180000000f, 524.929978319f),
    vector3(787.163040381f, 8.180000000f, 536.907895183f),
    vector3(803.129657216f, 8.180000000f, 548.952257178f),
    vector3(819.053197016f, 8.180000000f, 561.053519853f),
    vector3(834.939888619f, 8.180000000f, 573.203118513f),
    vector3(850.795484076f, 8.180000000f, 585.393274932f),
    vector3(866.625346641f, 8.180000000f, 597.616832426f),
    vector3(882.434530866f, 8.180000000f, 609.867123585f),
    vector3(898.227852355f, 8.180000000f, 622.137858857f),
    vector3(914.009948857f, 8.180000000f, 634.423029626f),
    vector3(929.785332566f, 8.180000000f, 646.716819559f),
    vector3(945.558442812f, 8.180000000f, 659.013526386f),
    vector3(961.333694246f, 8.180000000f, 671.307486021f),
    vector3(977.115524686f, 8.180000000f, 683.592998364f),
    vector3(992.908444194f, 8.180000000f, 695.864252223f),
    vector3(1008.717084510f, 8.180000000f, 708.115244804f),
    vector3(1024.546255691f, 8.180000000f, 720.339696802f),
    vector3(1040.401004336f, 8.180000000f, 732.530953774f),
    vector3(1056.286685247f, 8.180000000f, 744.681876926f),
    vector3(1072.209032431f, 8.180000000f, 756.784704894f),
    vector3(1088.174264134f, 8.180000000f, 768.830903155f),
    vector3(1104.189184444f, 8.180000000f, 780.810959218f),
    vector3(1120.261318963f, 8.180000000f, 792.714134282f),
    vector3(1136.399093525f, 8.180000000f, 804.528152490f),
    vector3(1152.612041342f, 8.180000000f, 816.238780941f),
    vector3(1168.911072506f, 8.180000000f, 827.829273480f),
    vector3(1185.308835860f, 8.180000000f, 839.279621400f),
    vector3(1201.820194127f, 8.180000000f, 850.565505299f),
    vector3(1202.316328386f, 8.180000000f, 850.900475263f),
    vector3(1218.462850731f, 7.855639651f, 861.656785419f),
    vector3(1235.258241604f, 6.963503967f, 872.515296772f),
    vector3(1252.232740322f, 5.690756152f, 883.091451551f),
    vector3(1269.419440720f, 4.224896206f, 893.318876740f),
    vector3(1286.860675144f, 2.753424131f, 903.105407888f),
    vector3(1304.611674852f, 1.463839924f, 912.317125813f),
    vector3(1322.745558636f, 0.543643587f, 920.747832026f),
    vector3(1341.358108917f, 0.180335119f, 928.054935292f),
    vector3(1341.923754871f, 0.180000000f, 928.250887938f),
    vector3(1360.559864366f, 0.141795345f, 933.608679345f),
    vector3(1380.368180125f, 0.100053217f, 936.095766387f),
    vector3(1380.966797000f, 0.100000000f, 936.098389000f),
    vector3(1400.966797000f, 0.100000000f, 936.098389000f)
};

array<float> gStation = {
    -10.000000000f,
    0.000000000f,
    10.000000000f,
    14.849100000f,
    34.849100000f,
    54.849100000f,
    74.849100000f,
    94.849100000f,
    114.849100000f,
    134.849100000f,
    154.849100000f,
    174.849100000f,
    194.849100000f,
    214.849100000f,
    234.849100000f,
    254.849100000f,
    274.849100000f,
    294.849100000f,
    314.849100000f,
    334.849100000f,
    354.849100000f,
    374.849100000f,
    394.849100000f,
    414.849100000f,
    434.849100000f,
    454.849100000f,
    474.849100000f,
    494.849100000f,
    514.849100000f,
    534.849100000f,
    554.849100000f,
    574.849100000f,
    594.849100000f,
    614.849100000f,
    634.849100000f,
    654.849100000f,
    674.849100000f,
    694.849100000f,
    714.849100000f,
    734.849100000f,
    754.849100000f,
    774.849100000f,
    794.849100000f,
    814.849100000f,
    834.849100000f,
    854.849100000f,
    874.849100000f,
    875.447727259f,
    894.849100000f,
    914.849100000f,
    934.849100000f,
    954.849100000f,
    974.849100000f,
    994.849100000f,
    1014.849100000f,
    1034.849100000f,
    1035.447727259f,
    1054.849100000f,
    1074.849100000f,
    1075.447727259f,
    1095.447727259f
};

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
bool gSourceSeam = false;
bool gMidpoint = false;
bool gCaptured = false;
bool gDestinationSeam = false;
bool gHasLastPosition = false;
uint gDrivingFrames = 0;
float gDistance = 0.0f;
float gClosestStation = -10.0f;
float gClosestCrossTrack = 0.0f;
float gClosestSurfaceY = 0.198f;
float gReferenceCrossTrack = 0.0f;
float gReferenceHeight = 0.0f;
float gMaximumPathError = 0.0f;
float gMaximumVerticalError = 0.0f;
float gMaximumRegression = 0.0f;
float gPreviousProgress = -10.0f;
vector3 gLastPosition;

float Clamp(float value, float minimum, float maximum)
{
    if (value < minimum)
        return minimum;
    if (value > maximum)
        return maximum;
    return value;
}

float Distance2D(
    float firstX,
    float firstZ,
    float secondX,
    float secondZ)
{
    const float dx = firstX - secondX;
    const float dz = firstZ - secondZ;
    return sqrt(dx * dx + dz * dz);
}

float WrapAngle(float value)
{
    while (value > 3.141592654f)
        value -= 6.283185307f;
    while (value < -3.141592654f)
        value += 6.283185307f;
    return value;
}

void FindClosestPath(const vector3 &in position)
{
    float bestSquared = 1000000000.0f;
    for (uint index = 0; index + 1 < gPath.length(); index++)
    {
        const float dx = gPath[index + 1].x - gPath[index].x;
        const float dz = gPath[index + 1].z - gPath[index].z;
        const float lengthSquared = dx * dx + dz * dz;
        if (lengthSquared <= 0.000001f)
            continue;
        const float fraction = Clamp(
            (
                (position.x - gPath[index].x) * dx +
                (position.z - gPath[index].z) * dz
            ) / lengthSquared,
            0.0f,
            1.0f);
        const float projectedX = gPath[index].x + fraction * dx;
        const float projectedZ = gPath[index].z + fraction * dz;
        const float offsetX = position.x - projectedX;
        const float offsetZ = position.z - projectedZ;
        const float squared = offsetX * offsetX + offsetZ * offsetZ;
        if (squared >= bestSquared)
            continue;
        bestSquared = squared;
        const float length = sqrt(lengthSquared);
        gClosestStation =
            gStation[index] +
            fraction * (gStation[index + 1] - gStation[index]);
        gClosestCrossTrack = (dx * offsetZ - dz * offsetX) / length;
        gClosestSurfaceY =
            gPath[index].y +
            fraction * (gPath[index + 1].y - gPath[index].y);
    }
}

vector3 SamplePath(float station)
{
    if (station <= gStation[0])
        return gPath[0];
    const uint last = gStation.length() - 1;
    if (station >= gStation[last])
        return gPath[last];
    for (uint index = 0; index < last; index++)
    {
        if (station > gStation[index + 1])
            continue;
        const float fraction =
            (station - gStation[index]) /
            (gStation[index + 1] - gStation[index]);
        return gPath[index] + (gPath[index + 1] - gPath[index]) * fraction;
    }
    return gPath[last];
}

vector3 PathTangent(float station)
{
    const vector3 before = SamplePath(station - 1.0f);
    const vector3 after = SamplePath(station + 1.0f);
    vector3 tangent(after.x - before.x, 0.0f, after.z - before.z);
    const float length = sqrt(tangent.x * tangent.x + tangent.z * tangent.z);
    if (length > 0.000001f)
    {
        tangent.x /= length;
        tangent.z /= length;
    }
    return tangent;
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
    if (@gActor != null)
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
    game.log("[RoR|CW2|CorridorRuntime] FAIL reason=" + reason);
    game.quitGame();
}

void main()
{
    if (gPath.length() != gStation.length() || gPath.length() != 61)
    {
        Fail("path-contract-invalid");
        return;
    }
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
        "[RoR|CW2|CorridorRuntime] START route_m=1075.447727259 "
        "waypoints=59 vehicle=b6b0UID-semi.truck batch=20");
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
            {"position", vector3(470.0f, 2.1f, 370.0f)},
            // The DAF's physical forward axis is -X. A 180 degree Y rotation
            // aligns it with the source road and the first corridor tangent.
            {"rotation", quaternion(
                radian(3.141592654f),
                vector3(0.0f, 1.0f, 0.0f))}
        });
}

void HoldCamera(const vector3 &in position)
{
    const vector3 target = SamplePath(gClosestStation + 18.0f);
    game.setCameraPosition(position + vector3(-28.0f, 16.0f, -25.0f));
    game.cameraLookAt(target + vector3(0.0f, 1.5f, 0.0f));
}

void UpdateControls(const vector3 &in position)
{
    const float targetStation = gClosestStation + LOOKAHEAD_M;
    vector3 target = SamplePath(targetStation);
    const vector3 tangent = PathTangent(targetStation);
    target.x += -tangent.z * gReferenceCrossTrack;
    target.z += tangent.x * gReferenceCrossTrack;
    const float desiredHeading = atan2(
        target.x - position.x,
        -(target.z - position.z));
    const float headingError = WrapAngle(
        gActor.getHeadingDirectionAngle() - desiredHeading);
    ApplySteer(Clamp(headingError * 0.90f, -0.32f, 0.32f));

    const float speed = gActor.getSpeed();
    float throttle = 0.18f;
    if (speed < 4.0f)
        throttle = 0.72f;
    else if (speed < 10.0f)
        throttle = 0.54f;
    else if (speed < 15.0f)
        throttle = 0.34f;
    gActor.setEventSimulatedValue(EV_TRUCK_ACCELERATE, throttle);
    gActor.setEventSimulatedValue(
        EV_TRUCK_BRAKE,
        speed > 18.0f ? 0.22f : 0.0f);
}

void UpdateTraversal()
{
    const vector3 position = gActor.getPosition();
    FindClosestPath(position);
    HoldCamera(position);
    UpdateControls(position);

    if (gHasLastPosition)
        gDistance += Distance2D(
            position.x,
            position.z,
            gLastPosition.x,
            gLastPosition.z);
    gLastPosition = position;
    gHasLastPosition = true;

    const float pathError = abs(
        gClosestCrossTrack - gReferenceCrossTrack);
    const float verticalError = abs(
        (position.y - gClosestSurfaceY) - gReferenceHeight);
    if (pathError > gMaximumPathError)
        gMaximumPathError = pathError;
    if (verticalError > gMaximumVerticalError)
        gMaximumVerticalError = verticalError;
    const float regression = gPreviousProgress - gClosestStation;
    if (regression > gMaximumRegression)
        gMaximumRegression = regression;
    if (gClosestStation > gPreviousProgress)
        gPreviousProgress = gClosestStation;

    if (!gSourceSeam && gClosestStation >= SOURCE_SEAM_STATION)
    {
        gSourceSeam = true;
        game.log(
            "[RoR|CW2|CorridorRuntime] SOURCE_SEAM station=" +
            gClosestStation + " x=" + position.x + " z=" + position.z);
    }
    if (!gMidpoint && gClosestStation >= MIDPOINT_STATION)
    {
        gMidpoint = true;
        game.log(
            "[RoR|CW2|CorridorRuntime] MIDPOINT station=" +
            gClosestStation + " x=" + position.x + " z=" + position.z);
    }
    if (!gCaptured && gClosestStation >= MIDPOINT_STATION + 12.0f)
    {
        gCaptured = true;
        game.log(
            "[RoR|CW2|CorridorRuntime] CAPTURE station=" +
            gClosestStation);
        game.pushMessage(MSG_APP_SCREENSHOT_REQUESTED, {});
    }
    if (!gDestinationSeam &&
        gClosestStation >= DESTINATION_SEAM_STATION)
    {
        gDestinationSeam = true;
        game.log(
            "[RoR|CW2|CorridorRuntime] DESTINATION_SEAM station=" +
            gClosestStation + " x=" + position.x + " z=" + position.z);
    }

    if (gDrivingFrames > 300 && pathError > MAX_PATH_ERROR_M)
    {
        Fail("path-error-" + pathError);
        return;
    }
    if (gDrivingFrames > 300 && verticalError > MAX_VERTICAL_ERROR_M)
    {
        Fail("vertical-error-" + verticalError);
        return;
    }
    if (gClosestStation >= PASS_STATION)
    {
        ClearControls();
        if (
            !gSourceSeam || !gMidpoint || !gCaptured ||
            !gDestinationSeam)
        {
            Fail("route-marker-missing");
            return;
        }
        if (gMaximumPathError > MAX_PATH_ERROR_M)
        {
            Fail("maximum-path-error-" + gMaximumPathError);
            return;
        }
        if (gMaximumVerticalError > MAX_VERTICAL_ERROR_M)
        {
            Fail("maximum-vertical-error-" + gMaximumVerticalError);
            return;
        }
        if (gMaximumRegression > 8.0f)
        {
            Fail("route-regression-" + gMaximumRegression);
            return;
        }
        if (gDistance < 1070.0f || gDistance > 1180.0f)
        {
            Fail("distance-" + gDistance);
            return;
        }
        console.cVarSet("sim_deterministic_fixed_steps_per_frame", "0");
        console.cVarSet("ui_hide_gui", "false");
        gState = FINISHED;
        game.log(
            "[RoR|CW2|CorridorRuntime] PASS seams=2 route_m=1075.447727259 "
            "distance_m=" + gDistance +
            " path_error_m=" + gMaximumPathError +
            " vertical_error_m=" + gMaximumVerticalError +
            " regression_m=" + gMaximumRegression +
            " speed_mps=" + gActor.getSpeed() +
            " physics_steps=" + game.getCompletedPhysicsSteps());
        game.quitGame();
    }
}

void frameStep(float dt)
{
    if (gState == FINISHED)
        return;

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
        const vector3 position = gActor.getPosition();
        FindClosestPath(position);
        if (gClosestStation < -10.0f || gClosestStation > -3.0f)
        {
            Fail("spawn-not-inside-penguinville-road-" + gClosestStation);
            return;
        }
        gReferenceCrossTrack = gClosestCrossTrack;
        gReferenceHeight = position.y - gClosestSurfaceY;
        gPreviousProgress = gClosestStation;
        game.pushMessage(MSG_SIM_UNPAUSE_REQUESTED, {});
        gState = DRIVING;
        game.log(
            "[RoR|CW2|CorridorRuntime] ARMED actor=2026072901 "
            "heading=" + gActor.getHeadingDirectionAngle() +
            " station=" + gClosestStation +
            " cross_track=" + gReferenceCrossTrack +
            " height=" + gReferenceHeight);
        return;
    }
    if (gState == DRIVING)
    {
        gDrivingFrames++;
        if (game.getCompletedPhysicsSteps() > MAX_PHYSICS_STEPS)
        {
            Fail("physics-step-timeout");
            return;
        }
        if (gDrivingFrames % 250 == 0)
        {
            const vector3 position = gActor.getPosition();
            game.log(
                "[RoR|CW2|CorridorRuntime] SAMPLE frame=" + gDrivingFrames +
                " station=" + gClosestStation +
                " x=" + position.x +
                " y=" + position.y +
                " z=" + position.z +
                " speed=" + gActor.getSpeed());
        }
        UpdateTraversal();
    }
}
