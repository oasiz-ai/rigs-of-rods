/// \title CityWorld NeoQ2.0 fixed-camera visual diagnostic
/// \brief Captures authenticated NeoQ2.0 geometry from four stable views.

const int APP_STATE_SIMULATION = 2;
const uint FIRST_CAPTURE_FRAME = 30;
const uint CAPTURE_INTERVAL = 30;
const uint CAPTURE_COUNT = 4;
const uint PASS_FRAME = 135;

CVarClass@ gAppState;
bool gFinished = false;
uint gReadyFrames = 0;
uint gCaptures = 0;

void HoldCamera(uint captureIndex)
{
    if (captureIndex == 0)
    {
        game.setCameraPosition(vector3(6550.0f, 100.0f, 3950.0f));
        game.cameraLookAt(vector3(7000.0f, 0.0f, 4300.0f));
    }
    else if (captureIndex == 1)
    {
        game.setCameraPosition(vector3(6600.0f, 130.0f, 5650.0f));
        game.cameraLookAt(vector3(7000.0f, 0.0f, 6000.0f));
    }
    else if (captureIndex == 2)
    {
        game.setCameraPosition(vector3(6300.0f, 8.0f, 5200.0f));
        game.cameraLookAt(vector3(7000.0f, 15.0f, 6000.0f));
    }
    else
    {
        game.setCameraPosition(vector3(5200.0f, 450.0f, 3500.0f));
        game.cameraLookAt(vector3(7000.0f, 0.0f, 6000.0f));
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
    game.log("[RoR|CW2|NeoQ20Runtime] FAIL reason=" + reason);
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
        "[RoR|CW2|NeoQ20Runtime] START cameras=4 "
        "telepoint=6773.92,0,4216.68 grounding=35");
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
        gReadyFrames ==
            FIRST_CAPTURE_FRAME + gCaptures * CAPTURE_INTERVAL)
    {
        game.log(
            "[RoR|CW2|NeoQ20Runtime] CAPTURE index=" + gCaptures);
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
        "[RoR|CW2|NeoQ20Runtime] PASS cameras=4 frames=" +
        gReadyFrames + " physics_steps=" + steps);
    game.quitGame();
}
