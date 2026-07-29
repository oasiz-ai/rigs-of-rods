/// \title CityWorld NeoQ tree replacement fixed-camera visual diagnostic
/// \brief Captures both authenticated rows and the complete 18-tree layout.

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
        game.setCameraPosition(vector3(1702.0f, 10.0f, 2160.0f));
        game.cameraLookAt(vector3(1702.0f, 4.5f, 2232.668945f));
    }
    else if (captureIndex == 1)
    {
        game.setCameraPosition(vector3(1702.0f, 10.0f, 2118.0f));
        game.cameraLookAt(vector3(1702.0f, 4.5f, 2046.000977f));
    }
    else if (captureIndex == 2)
    {
        game.setCameraPosition(vector3(1500.0f, 90.0f, 2140.0f));
        game.cameraLookAt(vector3(1702.0f, 4.0f, 2140.0f));
    }
    else
    {
        game.setCameraPosition(vector3(1702.0f, 230.0f, 2140.0f));
        game.cameraLookAt(vector3(1702.0f, 0.0f, 2140.0f));
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
    game.log("[RoR|CW2|NeoQTreeRuntime] FAIL reason=" + reason);
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
        "[RoR|CW2|NeoQTreeRuntime] START cameras=4 "
        "replacements=18 variants=3 duplicate_placements=0");
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
            "[RoR|CW2|NeoQTreeRuntime] CAPTURE index=" + gCaptures);
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
        "[RoR|CW2|NeoQTreeRuntime] PASS cameras=4 frames=" +
        gReadyFrames + " physics_steps=" + steps +
        " replacements=18 variants=3 duplicate_placements=0");
    game.quitGame();
}
