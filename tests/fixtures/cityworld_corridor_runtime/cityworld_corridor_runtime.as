/// \title CityWorld Next v4 seamless corridor acceptance
/// \brief Traverses the Penguinville-to-NeoQueretaro wheel path both ways.

const int APP_STATE_SIMULATION = 2;
const int SIM_STATE_PAUSED = 2;
const int64 ACTOR_ID = 2026072901;
const int64 REVERSE_ACTOR_ID = 2026072902;
const uint64 MAX_PHYSICS_STEPS = 480000;
const string VEHICLE = "b6b0UID-semi.truck";

const float ROUTE_LENGTH_M = 1038.350024882f;
const float SOURCE_SEAM_STATION = 0.0f;
const float MIDPOINT_STATION = 519.175012441f;
const float DESTINATION_SEAM_STATION = 1038.350024882f;
const float FORWARD_TURN_STATION = 1048.350024882f;
const float REVERSE_PASS_STATION = -18.0f;
const float LOOKAHEAD_M = 24.0f;
const float MAX_PATH_ERROR_M = 4.0f;
const float MAX_VERTICAL_ERROR_M = 3.5f;
const uint CAPTURE_REQUEST_FRAME = 4;
const uint CAPTURE_HOLD_FRAMES = 16;

// Samples 1..55 are the exact v4 overlay-report waypoints. Sample 0
// continues through the authenticated Penguinville road and Blender crown
// transition. Sample 56 continues into the NeoQueretaro carriageway.
array<vector3> gPath = {
    vector3(502.000000000f, 0.198014000f, 370.000002000f),
    vector3(522.000000000f, 0.100001000f, 370.023095000f),
    vector3(541.818216728f, 0.140000999f, 372.408387995f),
    vector3(561.010385479f, 0.180000996f, 377.991149001f),
    vector3(579.576732534f, 0.523750990f, 385.413575122f),
    vector3(597.630369032f, 1.430000983f, 394.014238532f),
    vector3(615.272776705f, 2.711250974f, 403.432013458f),
    vector3(632.582269209f, 4.180000963f, 413.449573050f),
    vector3(649.618294000f, 5.648750950f, 423.925977783f),
    vector3(666.426511639f, 6.930000936f, 434.764403705f),
    vector3(683.042647391f, 7.836250920f, 445.895242455f),
    vector3(699.495204646f, 8.180000903f, 457.266586818f),
    vector3(715.807341131f, 8.180000884f, 468.838535544f),
    vector3(731.998197343f, 8.180000864f, 480.579623485f),
    vector3(748.083838621f, 8.180000843f, 492.464481167f),
    vector3(764.077951849f, 8.180000821f, 504.472257049f),
    vector3(779.992346761f, 8.180000798f, 516.585504928f),
    vector3(795.837348987f, 8.180000774f, 528.789396854f),
    vector3(811.622092717f, 8.180000749f, 541.071138685f),
    vector3(827.354754729f, 8.180000723f, 553.419535767f),
    vector3(843.042732833f, 8.180000696f, 565.824654760f),
    vector3(858.692801449f, 8.180000669f, 578.277568578f),
    vector3(874.311230240f, 8.180000642f, 590.770146317f),
    vector3(889.903884344f, 8.180000613f, 603.294883224f),
    vector3(905.476312490f, 8.180000585f, 615.844761529f),
    vector3(921.033821272f, 8.180000556f, 628.413130033f),
    vector3(936.581545082f, 8.180000528f, 640.993601759f),
    vector3(952.124505982f, 8.180000499f, 653.579958399f),
    vector3(967.667671474f, 8.180000470f, 666.166062397f),
    vector3(983.216011367f, 8.180000441f, 678.745772718f),
    vector3(998.774554677f, 8.180000413f, 691.312860458f),
    vector3(1014.348447779f, 8.180000384f, 703.860920659f),
    vector3(1029.943014484f, 8.180000356f, 716.383275909f),
    vector3(1045.563827003f, 8.180000329f, 728.872873318f),
    vector3(1061.216780309f, 8.180000302f, 741.322162053f),
    vector3(1076.908180844f, 8.180000275f, 753.722951752f),
    vector3(1092.644847262f, 8.180000249f, 766.066239025f),
    vector3(1108.434241988f, 8.180000224f, 778.342002195f),
    vector3(1124.284609174f, 8.180000200f, 790.538925249f),
    vector3(1140.205171496f, 8.180000177f, 802.644064221f),
    vector3(1156.206365650f, 8.180000155f, 814.642400226f),
    vector3(1172.300141029f, 8.180000134f, 826.516240206f),
    vector3(1188.500357307f, 8.180000114f, 838.244405439f),
    vector3(1203.471592354f, 8.180000097f, 848.854897182f),
    vector3(1204.823305418f, 8.177465375f, 849.801092254f),
    vector3(1221.288417399f, 7.780213616f, 861.154241099f),
    vector3(1237.919236474f, 6.835930375f, 872.263117686f),
    vector3(1254.744784024f, 5.532115653f, 883.074609419f),
    vector3(1271.801482849f, 4.056269449f, 893.517289214f),
    vector3(1289.135967998f, 2.595891763f, 903.491473439f),
    vector3(1306.809035179f, 1.338482596f, 912.851442192f),
    vector3(1324.900811124f, 0.471541947f, 921.371280270f),
    vector3(1341.956411521f, 0.180000004f, 928.130334999f),
    vector3(1343.514628754f, 0.179602871f, 928.672888965f),
    vector3(1362.762213194f, 0.135061305f, 934.057451713f),
    vector3(1380.966797000f, 0.100000000f, 936.098389000f),
    vector3(1400.966797000f, 0.100000000f, 936.098389000f)
};

array<float> gStation = {
    -20.000000000f,
    0.000000000f,
    20.000000000f,
    40.000000000f,
    60.000000000f,
    80.000000000f,
    100.000000000f,
    120.000000000f,
    140.000000000f,
    160.000000000f,
    180.000000000f,
    200.000000000f,
    220.000000000f,
    240.000000000f,
    260.000000000f,
    280.000000000f,
    300.000000000f,
    320.000000000f,
    340.000000000f,
    360.000000000f,
    380.000000000f,
    400.000000000f,
    420.000000000f,
    440.000000000f,
    460.000000000f,
    480.000000000f,
    500.000000000f,
    520.000000000f,
    540.000000000f,
    560.000000000f,
    580.000000000f,
    600.000000000f,
    620.000000000f,
    640.000000000f,
    660.000000000f,
    680.000000000f,
    700.000000000f,
    720.000000000f,
    740.000000000f,
    760.000000000f,
    780.000000000f,
    800.000000000f,
    820.000000000f,
    838.350024882f,
    840.000000000f,
    860.000000000f,
    880.000000000f,
    900.000000000f,
    920.000000000f,
    940.000000000f,
    960.000000000f,
    980.000000000f,
    998.350024882f,
    1000.000000000f,
    1020.000000000f,
    1038.350024882f,
    1058.350024882f
};

enum RuntimeState
{
    WAITING_FOR_TERRAIN = 0,
    WAITING_FOR_PAUSE,
    WAITING_FOR_ACTOR,
    DRIVING_FORWARD,
    BRAKING_FOR_REVERSE,
    WAITING_FOR_REVERSE_ACTOR,
    DRIVING_REVERSE,
    FINISHED
}

RuntimeState gState = WAITING_FOR_TERRAIN;
BeamClass@ gActor;
EngineClass@ gEngine;
CVarClass@ gAppState;
CVarClass@ gSimState;
bool gActorSpawned = false;
int64 gExpectedActorId = ACTOR_ID;
bool gForwardSourceSeam = false;
bool gForwardMidpoint = false;
bool gForwardDestinationSeam = false;
bool gReverseDestinationSeam = false;
bool gReverseMidpoint = false;
bool gReverseSourceSeam = false;
bool gSourceCapturesComplete = false;
bool gDestinationCapturesComplete = false;
bool gHasLastPosition = false;
uint gDrivingFrames = 0;
int gCaptureMode = -1;
uint gCaptureFrame = 0;
float gDistance = 0.0f;
float gForwardDistance = 0.0f;
float gClosestStation = -20.0f;
float gClosestCrossTrack = 0.0f;
float gClosestSurfaceY = 0.198014f;
float gReferenceCrossTrack = 0.0f;
float gReferenceHeight = 0.0f;
float gMaximumPathError = 0.0f;
float gMaximumVerticalError = 0.0f;
float gMaximumRegression = 0.0f;
float gPreviousProgress = -20.0f;
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
    if (steer < 0.0f)
    {
        gActor.setEventSimulatedValue(EV_TRUCK_STEER_LEFT, -steer);
        gActor.setEventSimulatedValue(EV_TRUCK_STEER_RIGHT, 0.0f);
    }
    else
    {
        gActor.setEventSimulatedValue(EV_TRUCK_STEER_LEFT, 0.0f);
        gActor.setEventSimulatedValue(EV_TRUCK_STEER_RIGHT, steer);
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
    game.log("[RoR|CW2|CorridorRuntime] FAIL reason=" + reason);
    game.quitGame();
}

void main()
{
    if (gPath.length() != gStation.length() || gPath.length() != 57)
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
        "[RoR|CW2|CorridorRuntime] START route_m=1038.350024882 "
        "waypoints=55 vehicle=b6b0UID-semi.truck batch=20 "
        "traversal=bidirectional screenshots=4");
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
    if (arg1 != gExpectedActorId)
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
            {"position", vector3(502.0f, 2.1f, 370.000002f)},
            // The DAF's physical forward axis is -X. A 180 degree Y rotation
            // aligns it with the eastbound source-road tangent.
            {"rotation", quaternion(
                radian(3.141592654f),
                vector3(0.0f, 1.0f, 0.0f))}
        });
}

bool SpawnReverseActor()
{
    return game.pushMessage(
        MSG_SIM_SPAWN_ACTOR_REQUESTED,
        {
            {"filename", VEHICLE},
            {"instance_id", REVERSE_ACTOR_ID},
            {"free_position", true},
            {"enter", false},
            {"position", vector3(
                1400.966797f,
                2.1f,
                936.098389f)},
            // With no Y rotation the DAF's -X physical forward axis points
            // west, along the NeoQueretaro-to-Penguinville traversal.
            {"rotation", quaternion(
                radian(0.0f),
                vector3(0.0f, 1.0f, 0.0f))}
        });
}

void HoldTraversalCamera(
    const vector3 &in position,
    float direction)
{
    const vector3 target = SamplePath(
        gClosestStation + direction * 18.0f);
    game.setCameraPosition(position + vector3(-28.0f, 16.0f, -25.0f));
    game.cameraLookAt(target + vector3(0.0f, 1.5f, 0.0f));
}

string CaptureId(int mode)
{
    if (mode == 0)
        return "penguinville_low_forward_approach";
    if (mode == 1)
        return "penguinville_oblique_forward_approach";
    if (mode == 2)
        return "neoq_low_forward_approach";
    return "neoq_oblique_forward_approach";
}

void HoldCaptureCamera(int mode)
{
    if (mode == 0)
    {
        const vector3 approach = SamplePath(-14.0f);
        game.setCameraPosition(
            approach + vector3(0.0f, 2.25f, -3.2f));
        game.cameraLookAt(
            vector3(522.0f, 1.35f, 370.023095f));
        return;
    }
    if (mode == 1)
    {
        game.setCameraPosition(
            vector3(500.0f, 12.5f, 349.0f));
        game.cameraLookAt(
            vector3(516.0f, 0.9f, 370.023095f));
        return;
    }
    if (mode == 2)
    {
        const vector3 approach = SamplePath(
            DESTINATION_SEAM_STATION - 14.0f);
        const vector3 tangent = PathTangent(
            DESTINATION_SEAM_STATION - 14.0f);
        game.setCameraPosition(
            approach +
            vector3(-tangent.z * 3.2f, 2.25f, tangent.x * 3.2f));
        game.cameraLookAt(
            vector3(1380.966797f, 1.35f, 936.098389f));
        return;
    }
    game.setCameraPosition(
        vector3(1358.0f, 13.5f, 914.0f));
    game.cameraLookAt(
        vector3(1380.966797f, 0.9f, 936.098389f));
}

void BeginCapture(int mode)
{
    gCaptureMode = mode;
    gCaptureFrame = 0;
}

bool UpdateCapture()
{
    if (gCaptureMode < 0)
        return false;
    ClearControls();
    gActor.setEventSimulatedValue(EV_TRUCK_BRAKE, 1.0f);
    HoldCaptureCamera(gCaptureMode);
    gCaptureFrame++;
    if (gCaptureFrame == CAPTURE_REQUEST_FRAME)
    {
        const string captureId = CaptureId(gCaptureMode);
        game.log(
            "[RoR|CW2|CorridorRuntime] CAPTURE id=" + captureId +
            " rgb=true ui=false station=" + gClosestStation);
        game.pushMessage(MSG_APP_SCREENSHOT_REQUESTED, {});
    }
    if (gCaptureFrame < CAPTURE_HOLD_FRAMES)
        return true;

    if (gCaptureMode == 0)
    {
        BeginCapture(1);
    }
    else if (gCaptureMode == 1)
    {
        gCaptureMode = -1;
        gSourceCapturesComplete = true;
    }
    else if (gCaptureMode == 2)
    {
        BeginCapture(3);
    }
    else
    {
        gCaptureMode = -1;
        gDestinationCapturesComplete = true;
    }
    return true;
}

void UpdateControls(
    const vector3 &in position,
    float direction)
{
    const float targetStation =
        gClosestStation + direction * LOOKAHEAD_M;
    vector3 target = SamplePath(targetStation);
    const vector3 tangent = PathTangent(targetStation);
    target.x += -tangent.z * gReferenceCrossTrack;
    target.z += tangent.x * gReferenceCrossTrack;
    float desiredHeading = 0.0f;
    desiredHeading = atan2(
        target.x - position.x,
        -(target.z - position.z));
    const float headingError = WrapAngle(
        desiredHeading - gActor.getHeadingDirectionAngle());
    float steer = Clamp(headingError * 0.90f, -0.32f, 0.32f);
    ApplySteer(steer);

    const float speed = abs(gActor.getSpeed());
    float throttle = 0.18f;
    if (speed < 4.0f)
        throttle = 0.72f;
    else if (speed < 9.0f)
        throttle = 0.50f;
    else if (speed < 13.0f)
        throttle = 0.30f;
    gActor.setEventSimulatedValue(EV_TRUCK_ACCELERATE, throttle);
    gActor.setEventSimulatedValue(
        EV_TRUCK_BRAKE,
        speed > 15.0f ? 0.24f : 0.0f);
}

void RecordMotion(
    const vector3 &in position,
    float direction)
{
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
    const float regression = direction > 0.0f
        ? gPreviousProgress - gClosestStation
        : gClosestStation - gPreviousProgress;
    if (regression > gMaximumRegression)
        gMaximumRegression = regression;
    if (
        (direction > 0.0f && gClosestStation > gPreviousProgress) ||
        (direction < 0.0f && gClosestStation < gPreviousProgress)
    )
    {
        gPreviousProgress = gClosestStation;
    }
}

bool CheckPhysicalBounds()
{
    if (gDrivingFrames > 300 && gMaximumPathError > MAX_PATH_ERROR_M)
    {
        Fail(
            "maximum-path-error-" + gMaximumPathError +
            "-station-" + gClosestStation +
            "-cross-" + gClosestCrossTrack +
            "-reference-" + gReferenceCrossTrack);
        return false;
    }
    if (gDrivingFrames > 300 &&
        gMaximumVerticalError > MAX_VERTICAL_ERROR_M)
    {
        Fail(
            "maximum-vertical-error-" + gMaximumVerticalError +
            "-station-" + gClosestStation +
            "-surface-y-" + gClosestSurfaceY);
        return false;
    }
    return true;
}

void UpdateForward()
{
    const vector3 position = gActor.getPosition();
    FindClosestPath(position);
    RecordMotion(position, 1.0f);
    if (UpdateCapture())
        return;
    if (!gSourceCapturesComplete && gClosestStation >= -16.0f)
    {
        BeginCapture(0);
        return;
    }
    if (
        gSourceCapturesComplete &&
        !gDestinationCapturesComplete &&
        gClosestStation >= DESTINATION_SEAM_STATION - 16.0f
    )
    {
        BeginCapture(2);
        return;
    }

    HoldTraversalCamera(position, 1.0f);
    UpdateControls(position, 1.0f);
    if (!gForwardSourceSeam &&
        gClosestStation >= SOURCE_SEAM_STATION)
    {
        gForwardSourceSeam = true;
        game.log(
            "[RoR|CW2|CorridorRuntime] FORWARD_SOURCE_SEAM "
            "direction=penguinville_to_neoq target_station=0 "
            "world=522,0.100001,370.023095 station=" +
            gClosestStation);
    }
    if (!gForwardMidpoint && gClosestStation >= MIDPOINT_STATION)
    {
        gForwardMidpoint = true;
        game.log(
            "[RoR|CW2|CorridorRuntime] FORWARD_MIDPOINT "
            "direction=penguinville_to_neoq station=" +
            gClosestStation);
    }
    if (!gForwardDestinationSeam &&
        gClosestStation >= DESTINATION_SEAM_STATION)
    {
        gForwardDestinationSeam = true;
        game.log(
            "[RoR|CW2|CorridorRuntime] FORWARD_DESTINATION_SEAM "
            "direction=penguinville_to_neoq "
            "target_station=1038.350024882 "
            "world=1380.966797,0.1,936.098389 station=" +
            gClosestStation);
    }
    if (!CheckPhysicalBounds())
        return;
    if (gClosestStation >= FORWARD_TURN_STATION)
    {
        if (
            !gForwardSourceSeam ||
            !gForwardMidpoint ||
            !gForwardDestinationSeam ||
            !gSourceCapturesComplete ||
            !gDestinationCapturesComplete
        )
        {
            Fail("forward-marker-missing");
            return;
        }
        ClearControls();
        gActor.setEventSimulatedValue(EV_TRUCK_BRAKE, 1.0f);
        gState = BRAKING_FOR_REVERSE;
    }
}

void UpdateBrakeForReverse()
{
    const vector3 position = gActor.getPosition();
    FindClosestPath(position);
    if (gHasLastPosition)
        gDistance += Distance2D(
            position.x,
            position.z,
            gLastPosition.x,
            gLastPosition.z);
    gLastPosition = position;
    ClearControls();
    gActor.setEventSimulatedValue(EV_TRUCK_BRAKE, 1.0f);
    HoldTraversalCamera(position, -1.0f);
    if (abs(gActor.getSpeed()) > 0.35f)
        return;
    gForwardDistance = gDistance;
    game.pushMessage(
        MSG_SIM_DELETE_ACTOR_REQUESTED,
        {{"instance_id", ACTOR_ID}});
    @gActor = null;
    @gEngine = null;
    gActorSpawned = false;
    gExpectedActorId = REVERSE_ACTOR_ID;
    if (!SpawnReverseActor())
    {
        Fail("reverse-spawn-request-rejected");
        return;
    }
    gState = WAITING_FOR_REVERSE_ACTOR;
}

void ArmReverseActor()
{
    @gActor = game.getTruckByNum(REVERSE_ACTOR_ID);
    if (@gActor == null)
    {
        Fail("reverse-actor-not-addressable");
        return;
    }
    @gEngine = gActor.getEngine();
    if (@gEngine == null)
    {
        Fail("reverse-engine-missing");
        return;
    }
    gEngine.setAutoMode(SimGearboxMode::AUTO);
    gEngine.startEngine();
    if (gActor.getParkingBrake())
        gActor.parkingbrakeToggle();
    const vector3 position = gActor.getPosition();
    FindClosestPath(position);
    if (
        gClosestStation < DESTINATION_SEAM_STATION + 8.0f ||
        gClosestStation > DESTINATION_SEAM_STATION + 20.0f
    )
    {
        Fail("reverse-spawn-not-inside-neoq-road-" + gClosestStation);
        return;
    }
    gReferenceCrossTrack = gClosestCrossTrack;
    gReferenceHeight = position.y - gClosestSurfaceY;
    gPreviousProgress = gClosestStation;
    gLastPosition = position;
    gHasLastPosition = true;
    gState = DRIVING_REVERSE;
    game.log(
        "[RoR|CW2|CorridorRuntime] REVERSE_ARMED actor=2026072902 "
        "direction=neoq_to_penguinville heading=" +
        gActor.getHeadingDirectionAngle() +
        " station=" + gClosestStation +
        " cross_track=" + gReferenceCrossTrack +
        " height=" + gReferenceHeight +
        " forward_distance_m=" + gForwardDistance);
}

void UpdateReverse()
{
    const vector3 position = gActor.getPosition();
    FindClosestPath(position);
    RecordMotion(position, -1.0f);
    HoldTraversalCamera(position, -1.0f);
    UpdateControls(position, -1.0f);
    if (!gReverseDestinationSeam &&
        gClosestStation <= DESTINATION_SEAM_STATION)
    {
        gReverseDestinationSeam = true;
        game.log(
            "[RoR|CW2|CorridorRuntime] REVERSE_DESTINATION_SEAM "
            "direction=neoq_to_penguinville "
            "target_station=1038.350024882 "
            "world=1380.966797,0.1,936.098389 station=" +
            gClosestStation);
    }
    if (!gReverseMidpoint && gClosestStation <= MIDPOINT_STATION)
    {
        gReverseMidpoint = true;
        game.log(
            "[RoR|CW2|CorridorRuntime] REVERSE_MIDPOINT "
            "direction=neoq_to_penguinville station=" +
            gClosestStation);
    }
    if (!gReverseSourceSeam &&
        gClosestStation <= SOURCE_SEAM_STATION)
    {
        gReverseSourceSeam = true;
        game.log(
            "[RoR|CW2|CorridorRuntime] REVERSE_SOURCE_SEAM "
            "direction=neoq_to_penguinville target_station=0 "
            "world=522,0.100001,370.023095 station=" +
            gClosestStation);
    }
    if (!CheckPhysicalBounds())
        return;
    if (gClosestStation <= REVERSE_PASS_STATION)
    {
        ClearControls();
        if (
            !gReverseDestinationSeam ||
            !gReverseMidpoint ||
            !gReverseSourceSeam
        )
        {
            Fail("reverse-marker-missing");
            return;
        }
        const float reverseDistance = gDistance - gForwardDistance;
        if (
            gForwardDistance < 1020.0f ||
            gForwardDistance > 1120.0f ||
            reverseDistance < 1020.0f ||
            reverseDistance > 1120.0f
        )
        {
            Fail("directional-distance-" +
                gForwardDistance + "-" + reverseDistance);
            return;
        }
        if (gDistance < 2080.0f || gDistance > 2220.0f)
        {
            Fail("distance-" + gDistance);
            return;
        }
        if (gMaximumRegression > 8.0f)
        {
            Fail("route-regression-" + gMaximumRegression);
            return;
        }
        console.cVarSet("sim_deterministic_fixed_steps_per_frame", "0");
        console.cVarSet("ui_hide_gui", "false");
        gState = FINISHED;
        game.log(
            "[RoR|CW2|CorridorRuntime] PASS seams=4 screenshots=4 "
            "traversals=2 route_m=1038.350024882 "
            "distance_m=" + gDistance +
            " forward_distance_m=" + gForwardDistance +
            " reverse_distance_m=" + reverseDistance +
            " path_error_m=" + gMaximumPathError +
            " vertical_error_m=" + gMaximumVerticalError +
            " regression_m=" + gMaximumRegression +
            " speed_mps=" + abs(gActor.getSpeed()) +
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
        @gEngine = gActor.getEngine();
        if (@gEngine == null)
        {
            Fail("engine-missing");
            return;
        }
        gEngine.setAutoMode(SimGearboxMode::AUTO);
        gEngine.startEngine();
        if (gActor.getParkingBrake())
            gActor.parkingbrakeToggle();
        const vector3 position = gActor.getPosition();
        FindClosestPath(position);
        if (gClosestStation < -20.0f || gClosestStation > -12.0f)
        {
            Fail("spawn-not-inside-penguinville-road-" + gClosestStation);
            return;
        }
        gReferenceCrossTrack = gClosestCrossTrack;
        gReferenceHeight = position.y - gClosestSurfaceY;
        gPreviousProgress = gClosestStation;
        gLastPosition = position;
        gHasLastPosition = true;
        game.pushMessage(MSG_SIM_UNPAUSE_REQUESTED, {});
        gState = DRIVING_FORWARD;
        game.log(
            "[RoR|CW2|CorridorRuntime] ARMED actor=2026072901 "
            "direction=penguinville_to_neoq "
            "heading=" + gActor.getHeadingDirectionAngle() +
            " station=" + gClosestStation +
            " cross_track=" + gReferenceCrossTrack +
            " height=" + gReferenceHeight);
        return;
    }

    gDrivingFrames++;
    if (game.getCompletedPhysicsSteps() > MAX_PHYSICS_STEPS)
    {
        Fail("physics-step-timeout");
        return;
    }
    if (gDrivingFrames % 100 == 0)
    {
        const vector3 position = gActor.getPosition();
        game.log(
            "[RoR|CW2|CorridorRuntime] SAMPLE frame=" + gDrivingFrames +
            " state=" + gState +
            " station=" + gClosestStation +
            " x=" + position.x +
            " y=" + position.y +
            " z=" + position.z +
            " speed=" + gActor.getSpeed());
    }
    if (gState == DRIVING_FORWARD)
    {
        UpdateForward();
    }
    else if (gState == BRAKING_FOR_REVERSE)
    {
        UpdateBrakeForReverse();
    }
    else if (gState == WAITING_FOR_REVERSE_ACTOR)
    {
        if (gActorSpawned)
            ArmReverseActor();
    }
    else if (gState == DRIVING_REVERSE)
    {
        UpdateReverse();
    }
}
