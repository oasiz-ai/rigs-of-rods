/// \title CityWorld Neo-to-NeoQ2.0 bridge fixed-camera visual gate
/// \brief Captures both joins, outboard side piers, driver view, and underside.

const int APP_STATE_SIMULATION = 2;
const uint FIRST_CAPTURE_FRAME = 40;
const uint CAPTURE_INTERVAL = 40;
const uint CAPTURE_COUNT = 6;
const uint PASS_FRAME = 265;

CVarClass@ gAppState;
bool gFinished = false;
uint gReadyFrames = 0;
uint gCaptures = 0;

void HoldCamera(uint captureIndex)
{
    if (captureIndex == 0)
    {
        // NeoQueretaro east distributor and the zero-overlap flush source seam.
        game.setCameraPosition(vector3(3735.0f, 18.0f, 3955.0f));
        game.cameraLookAt(vector3(3860.0f, 3.0f, 3993.5f));
    }
    else if (captureIndex == 1)
    {
        // Driver-height deck view proves the paired columns leave the road
        // surface and its heavy-truck swept envelope fully unobstructed.
        game.setCameraPosition(vector3(4300.0f, 10.2f, 3996.7f));
        game.cameraLookAt(vector3(4470.0f, 9.8f, 3998.1f));
    }
    else if (captureIndex == 2)
    {
        // Source ramp underside and its first paired, outboard supports.
        game.setCameraPosition(vector3(4000.0f, 2.5f, 3960.0f));
        game.cameraLookAt(vector3(4150.0f, 7.5f, 3996.0f));
    }
    else if (captureIndex == 3)
    {
        // Mid-span underside; the camera remains below the 8 m deck.
        game.setCameraPosition(vector3(5250.0f, 2.5f, 3950.0f));
        game.cameraLookAt(vector3(5400.0f, 8.2f, 4006.0f));
    }
    else if (captureIndex == 4)
    {
        // High oblique view of the complete nearly east-west alignment.
        game.setCameraPosition(vector3(5325.0f, 300.0f, 3650.0f));
        game.cameraLookAt(vector3(5325.0f, 3.0f, 4005.0f));
    }
    else
    {
        // Flush NeoQ2.0 merge: the generated deck ends at the mesh edge and
        // leaves the independently authored median and both lanes untouched.
        game.setCameraPosition(vector3(6950.0f, 20.0f, 4070.0f));
        game.cameraLookAt(vector3(6800.0f, 3.0f, 4018.0f));
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
    game.log("[RoR|CW2|NeoBridgeRuntime] FAIL reason=" + reason);
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
        "[RoR|CW2|NeoBridgeRuntime] START cameras=6 "
        "route_m=3076.132100441 width_m=24 supports=74 lights=33");
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
            "[RoR|CW2|NeoBridgeRuntime] CAPTURE index=" + gCaptures);
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
        "[RoR|CW2|NeoBridgeRuntime] PASS cameras=6 frames=" +
        gReadyFrames + " physics_steps=" + steps +
        " route_m=3076.132100441 supports=74 lights=33");
    game.quitGame();
}
