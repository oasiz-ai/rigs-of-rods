/// \title Authenticated CityWorld legacy-material runtime smoke
/// \brief Waits for the local overlay terrain, then exits cleanly.

const int APP_STATE_SIMULATION = 2;
const uint WARMUP_FRAMES = 120;
const uint MAX_FRAMES = 2400;

CVarClass@ gAppState;
uint gFrames = 0;
uint gSimulationFrames = 0;
bool gFinished = false;

void Finish(const string &in result)
{
    if (gFinished)
        return;

    gFinished = true;
    console.cVarSet("ui_hide_gui", "false");
    game.log(
        "[RoR|CW2|LegacyMaterialRuntime] " + result +
        " frames=" + gFrames +
        " simulation_frames=" + gSimulationFrames);
    game.quitGame();
}

void main()
{
    @gAppState = console.cVarFind("app_state");
    if (@gAppState == null)
    {
        Finish("FAIL reason=app-state-cvar-missing");
        return;
    }

    console.cVarSet("ui_hide_gui", "true");
    game.log("[RoR|CW2|LegacyMaterialRuntime] START");
}

void frameStep(float dt)
{
    if (gFinished)
        return;

    gFrames++;
    if (gAppState.getInt() == APP_STATE_SIMULATION)
    {
        gSimulationFrames++;
        if (gSimulationFrames >= WARMUP_FRAMES)
        {
            Finish("PASS");
            return;
        }
    }

    if (gFrames >= MAX_FRAMES)
        Finish("FAIL reason=terrain-load-timeout");
}
