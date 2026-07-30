/// \title CityWorld regional-infill fixed-camera visual acceptance
/// \brief Captures all eight project-authored infill districts without UI.

const int APP_STATE_SIMULATION = 2;
const uint CAPTURE_COUNT = 8;
const uint CAPTURE_HOLD_FRAMES = 40;
const uint PASS_FRAME = 345;

CVarClass@ gAppState;
bool gFinished = false;
uint gReadyFrames = 0;
uint gCaptures = 0;

string CaptureId(uint captureIndex)
{
    if (captureIndex == 0)
        return "west_farm_belt";
    if (captureIndex == 1)
        return "sunset_courts";
    if (captureIndex == 2)
        return "west_highway_service";
    if (captureIndex == 3)
        return "coyote_arch";
    if (captureIndex == 4)
        return "arroyo_vista";
    if (captureIndex == 5)
        return "intercity_service";
    if (captureIndex == 6)
        return "intercity_farm";
    return "sagebrush_arroyo";
}

void HoldCamera(uint captureIndex)
{
    if (captureIndex == 0)
    {
        game.setCameraPosition(vector3(860.0f, 80.0f, 560.0f));
        game.cameraLookAt(vector3(860.0f, 0.1f, 250.0f));
    }
    else if (captureIndex == 1)
    {
        game.setCameraPosition(vector3(1055.0f, 95.0f, 1780.0f));
        game.cameraLookAt(vector3(1055.0f, 0.1f, 1360.0f));
    }
    else if (captureIndex == 2)
    {
        game.setCameraPosition(vector3(970.0f, 32.0f, 1520.0f));
        game.cameraLookAt(vector3(805.0f, 2.0f, 1395.0f));
    }
    else if (captureIndex == 3)
    {
        game.setCameraPosition(vector3(3925.0f, 90.0f, 3010.0f));
        game.cameraLookAt(vector3(3925.0f, 12.0f, 2575.0f));
    }
    else if (captureIndex == 4)
    {
        game.setCameraPosition(vector3(4240.0f, 115.0f, 3960.0f));
        game.cameraLookAt(vector3(4240.0f, 0.1f, 3550.0f));
    }
    else if (captureIndex == 5)
    {
        game.setCameraPosition(vector3(3910.0f, 28.0f, 3710.0f));
        game.cameraLookAt(vector3(3820.0f, 2.0f, 3635.0f));
    }
    else if (captureIndex == 6)
    {
        game.setCameraPosition(vector3(4070.0f, 85.0f, 4630.0f));
        game.cameraLookAt(vector3(4070.0f, 0.1f, 4325.0f));
    }
    else
    {
        game.setCameraPosition(vector3(1255.0f, 75.0f, 750.0f));
        game.cameraLookAt(vector3(1255.0f, 0.1f, 450.0f));
    }
}

void Finish()
{
    console.cVarSet("sim_deterministic_fixed_steps_per_frame", "0");
    console.cVarSet("ui_hide_gui", "false");
    gFinished = true;
}

void Fail(const string &in reason)
{
    if (gFinished)
        return;
    Finish();
    game.log("[RoR|CW2|InfillRuntime] FAIL reason=" + reason);
    game.quitGame();
}

void main()
{
    @gAppState = console.cVarFind("app_state");
    if (@gAppState == null)
    {
        Fail("app-state-cvar-missing");
        return;
    }
    console.cVarSet("sim_deterministic_fixed_steps_per_frame", "4");
    console.cVarSet("ui_hide_gui", "true");
    game.log(
        "[RoR|CW2|InfillRuntime] START cameras=8 hold_frames=40 "
        "batch=4 placements=46 routes=7 stations=2 station_lights=12");
}

void frameStep(float dt)
{
    if (gFinished)
        return;
    if (gAppState.getInt() != APP_STATE_SIMULATION)
        return;

    gReadyFrames++;
    const uint cameraIndex =
        gCaptures < CAPTURE_COUNT ? gCaptures : CAPTURE_COUNT - 1;
    HoldCamera(cameraIndex);

    if (
        gCaptures < CAPTURE_COUNT &&
        gReadyFrames == (gCaptures + 1) * CAPTURE_HOLD_FRAMES)
    {
        const string captureId = CaptureId(gCaptures);
        game.log(
            "[RoR|CW2|InfillRuntime] CAPTURE index=" + gCaptures +
            " id=" + captureId +
            " hold_frames=40");
        game.pushMessage(MSG_APP_SCREENSHOT_REQUESTED, {});
        gCaptures++;
    }
    if (gReadyFrames < PASS_FRAME)
        return;
    if (gCaptures != CAPTURE_COUNT)
    {
        Fail("capture-count-" + gCaptures);
        return;
    }

    const uint64 steps = game.getCompletedPhysicsSteps();
    Finish();
    game.log(
        "[RoR|CW2|InfillRuntime] PASS cameras=8 hold_frames=40 frames=" +
        gReadyFrames + " physics_steps=" + steps +
        " placements=46 routes=7 stations=2 station_lights=12");
    game.quitGame();
}
