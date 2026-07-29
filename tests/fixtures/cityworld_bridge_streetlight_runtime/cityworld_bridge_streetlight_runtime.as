/// \title CityWorld Next collisionless bridge streetlight native runtime gate
/// \brief Loads one visual-only fixture, creates its point light, and captures RGB.

const int APP_STATE_SIMULATION = 2;
const uint CAPTURE_FRAME = 30;
const uint PASS_FRAME = 45;

CVarClass@ gAppState;
bool gCaptured = false;
bool gFinished = false;
uint gReadyFrames = 0;

void HoldCamera()
{
    game.setCameraPosition(vector3(521.0f, 8.5f, 484.0f));
    game.cameraLookAt(vector3(512.0f, 4.0f, 500.0f));
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
    game.log(
        "[RoR|CW1|BridgeStreetlightRuntime] FAIL reason=" + reason);
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

    // Keep the collision subsystem active. The fixture is collisionless because
    // its authenticated ODEF owns no collision directive, not because the scene
    // disabled world collisions.
    console.cVarSet("sim_deterministic_fixed_steps_per_frame", "4");
    console.cVarSet("sim_no_collisions", "false");
    console.cVarSet("sim_no_self_collisions", "false");
    console.cVarSet("ui_hide_gui", "true");
    game.log(
        "[RoR|CW1|BridgeStreetlightRuntime] START fixtures=1 "
        "collision_objects=0 point_lights=1");
}

void frameStep(float dt)
{
    if (gFinished)
        return;

    HoldCamera();
    if (gAppState.getInt() != APP_STATE_SIMULATION)
        return;

    gReadyFrames++;
    if (gReadyFrames == 1)
    {
        game.log(
            "[RoR|CW1|BridgeStreetlightRuntime] LOADED "
            "collision_subsystem=enabled");
    }
    if (gReadyFrames == CAPTURE_FRAME)
    {
        game.log("[RoR|CW1|BridgeStreetlightRuntime] CAPTURE");
        game.pushMessage(MSG_APP_SCREENSHOT_REQUESTED, {});
        gCaptured = true;
    }
    if (gReadyFrames < PASS_FRAME)
        return;
    if (!gCaptured)
    {
        Fail("capture-missing");
        return;
    }

    const uint64 steps = game.getCompletedPhysicsSteps();
    Finish();
    game.log(
        "[RoR|CW1|BridgeStreetlightRuntime] PASS fixtures=1 "
        "collision_objects=0 point_lights=1 frames=" + gReadyFrames +
        " physics_steps=" + steps);
    game.quitGame();
}
