/// \title deterministic input savegame checkpoint resume
/// \brief Continues the authenticated step-120 record through step 240.
///
/// The runtime, not this script, imports and revalidates the input
/// continuation. The script only arms the independent state trace and checks
/// the published identity before allowing the resumed physics step.

const int APP_STATE_SIMULATION = 2;
const int SIM_STATE_PAUSED = 2;

const uint64 SCENARIO_ID = 2026082001;
const uint64 TARGET_ID = 2026082001001;
const uint64 SAVE_STEP = 120;
const uint64 FINAL_STEP = 240;
const uint64 MAX_RESTORE_WAIT_FRAMES = 600;
const string CHECKPOINT = "d0_input_checkpoint.sav";

enum ScenarioState
{
    WAITING_FOR_RESTORE = 0,
    RESUMED,
    FINISHED
}

ScenarioState gState = WAITING_FOR_RESTORE;
uint64 gRestoreWaitFrames = 0;
CVarClass@ gAppState;
CVarClass@ gInputMode;
CVarClass@ gInputScenario;
CVarClass@ gInputTarget;
CVarClass@ gInputLimit;

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
    game.log("[RoR|D0|InputResume] FAIL reason=" + reason);
    game.quitGame();
}

void main()
{
    @gAppState = console.cVarFind("app_state");
    @gInputMode = console.cVarFind("sim_deterministic_input_mode");
    @gInputScenario =
        console.cVarFind("sim_deterministic_input_scenario_id");
    @gInputTarget =
        console.cVarFind("sim_deterministic_input_target_id");
    @gInputLimit =
        console.cVarFind("sim_deterministic_input_step_limit");
    if (gAppState is null || gInputMode is null ||
        gInputScenario is null || gInputTarget is null ||
        gInputLimit is null)
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
    if (!game.pushMessage(
            MSG_SIM_LOAD_SAVEGAME_REQUESTED,
            {{"filename", CHECKPOINT}}))
    {
        FailScenario("checkpoint-load-request-rejected");
        return;
    }
    game.log(
        "[RoR|D0|InputResume] START checkpoint=" + CHECKPOINT +
        " expected_step=" + SAVE_STEP + " final_step=" + FINAL_STEP);
}

void frameStep(float dt)
{
    if (gState == FINISHED)
        return;

    if (gState == WAITING_FOR_RESTORE)
    {
        if (gAppState.getInt() != APP_STATE_SIMULATION)
            return;
        BeamClass@ actor = game.getCurrentTruck();
        if (actor is null)
            return;

        const uint64 completed = game.getCompletedPhysicsSteps();
        if (completed != SAVE_STEP)
        {
            FailScenario("restore-published-at-step-" + completed);
            return;
        }
        const string mode = gInputMode.getStr();
        if (mode == "off")
        {
            ++gRestoreWaitFrames;
            if (gRestoreWaitFrames > MAX_RESTORE_WAIT_FRAMES)
                FailScenario("restored-input-activation-timeout");
            return;
        }
        if (mode != "record" ||
            gInputScenario.getStr() != "" + SCENARIO_ID ||
            gInputTarget.getStr() != "" + TARGET_ID ||
            gInputLimit.getStr() != "" + FINAL_STEP)
        {
            FailScenario(
                "restored-input-identity-mismatch mode=" + mode +
                " scenario=" + gInputScenario.getStr() +
                " target=" + gInputTarget.getStr() +
                " limit=" + gInputLimit.getStr());
            return;
        }

        console.cVarSet("sim_deterministic_state_trace", "true");
        gState = RESUMED;
        game.log(
            "[RoR|D0|InputResume] ARMED first_step=" + completed +
            " mode=record batch=1");
        return;
    }

    const uint64 completed = game.getCompletedPhysicsSteps();
    if (completed > FINAL_STEP)
    {
        FailScenario("physics-step-overshoot-" + completed);
        return;
    }
    if (completed == FINAL_STEP)
    {
        StopRuntime();
        gState = FINISHED;
        game.log(
            "[RoR|D0|InputResume] PASS completed_step=" + completed);
        game.quitGame();
    }
}
