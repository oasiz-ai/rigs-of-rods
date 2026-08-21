/// \title deterministic input savegame checkpoint recorder
/// \brief Records one authenticated input stream and saves it at step 120.
///
/// This script is driven by tools/run_deterministic_savegame_resume.py. It is
/// inert until a local player truck is present and never chooses content.

const int APP_STATE_SIMULATION = 2;
const int SIM_STATE_PAUSED = 2;

const uint64 SCENARIO_ID = 2026082001;
const uint64 TARGET_ID = 2026082001001;
const uint64 SAVE_STEP = 120;
const uint64 FINAL_STEP = 240;
const string CHECKPOINT = "d0_input_checkpoint.sav";

enum ScenarioState
{
    WAITING_FOR_PLAYER = 0,
    RECORDING,
    FINISHED
}

ScenarioState gState = WAITING_FOR_PLAYER;
bool gCheckpointSaved = false;
CVarClass@ gAppState;
CVarClass@ gInputMode;

void StopRuntime()
{
    console.cVarSet("sim_state", "" + SIM_STATE_PAUSED);
    console.cVarSet("sim_deterministic_state_trace", "false");
    console.cVarSet("sim_deterministic_input_mode", "off");
    console.cVarSet("sim_deterministic_fixed_steps_per_frame", "0");
}

void FailScenario(const string &in reason)
{
    if (gState == FINISHED)
        return;
    gState = FINISHED;
    StopRuntime();
    game.log("[RoR|D0|InputSave] FAIL reason=" + reason);
    game.quitGame();
}

void main()
{
    @gAppState = console.cVarFind("app_state");
    @gInputMode = console.cVarFind("sim_deterministic_input_mode");
    if (gAppState is null || gInputMode is null)
    {
        FailScenario("required-cvar-missing");
        return;
    }

    console.cVarSet("sim_deterministic_fixed_steps_per_frame", "1");
    console.cVarSet("sim_deterministic_sleeping_engine", "true");
    console.cVarSet("sim_deterministic_state_trace", "false");
    console.cVarSet(
        "sim_deterministic_state_trace_scenario_id",
        "" + SCENARIO_ID);
    console.cVarSet(
        "sim_deterministic_state_trace_step_limit",
        "" + FINAL_STEP);
    console.cVarSet("sim_deterministic_input_mode", "off");
    console.cVarSet("sim_deterministic_input_path", "");
    console.cVarSet(
        "sim_deterministic_input_scenario_id",
        "" + SCENARIO_ID);
    console.cVarSet(
        "sim_deterministic_input_target_id",
        "" + TARGET_ID);
    console.cVarSet(
        "sim_deterministic_input_step_limit",
        "" + FINAL_STEP);
    game.log(
        "[RoR|D0|InputSave] START scenario=" + SCENARIO_ID +
        " target=" + TARGET_ID + " save_step=" + SAVE_STEP +
        " final_step=" + FINAL_STEP);
}

void frameStep(float dt)
{
    if (gState == FINISHED)
        return;

    if (gState == WAITING_FOR_PLAYER)
    {
        if (gAppState.getInt() != APP_STATE_SIMULATION)
            return;

        BeamClass@ actor = game.getCurrentTruck();
        if (actor is null)
            return;
        const uint64 completed = game.getCompletedPhysicsSteps();
        if (completed != 0)
        {
            FailScenario("player-arrived-after-step-" + completed);
            return;
        }

        EngineClass@ engine = actor.getEngine();
        if (engine is null)
        {
            FailScenario("player-has-no-engine");
            return;
        }
        engine.setAutoMode(SimGearboxMode::MANUAL);
        engine.setGear(0);
        engine.setGearRange(0);
        engine.setAcc(0.375f);
        if (actor.getAntiLockBrake())
            actor.antilockbrakeToggle();
        if (actor.getTractionControl())
            actor.tractioncontrolToggle();
        if (actor.getCruiseControl())
            actor.cruisecontrolToggle();

        console.cVarSet("sim_deterministic_state_trace", "true");
        console.cVarSet("sim_deterministic_input_mode", "record");
        gState = RECORDING;
        game.log(
            "[RoR|D0|InputSave] ARMED first_step=0 throttle=0.375 "
            "gear=0 batch=1");
        return;
    }

    const uint64 completed = game.getCompletedPhysicsSteps();
    if (completed > FINAL_STEP)
    {
        FailScenario("physics-step-overshoot-" + completed);
        return;
    }
    if (!gCheckpointSaved && completed == SAVE_STEP)
    {
        if (gInputMode.getStr() != "record")
        {
            FailScenario("record-mode-not-active-at-save");
            return;
        }
        if (!game.saveScene(CHECKPOINT))
        {
            FailScenario("checkpoint-save-rejected");
            return;
        }
        gCheckpointSaved = true;
        game.log(
            "[RoR|D0|InputSave] CHECKPOINT file=" + CHECKPOINT +
            " completed_step=" + completed);
    }
    if (completed == FINAL_STEP)
    {
        if (!gCheckpointSaved)
        {
            FailScenario("checkpoint-was-not-saved");
            return;
        }
        StopRuntime();
        gState = FINISHED;
        game.log(
            "[RoR|D0|InputSave] PASS completed_step=" + completed);
        game.quitGame();
    }
}
