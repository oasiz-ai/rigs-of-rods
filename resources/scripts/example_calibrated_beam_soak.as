/// \title calibrated-beam P1 starter-content soak
/// \brief Runs 120,000 exact solver steps on an authenticated DAF-derived fixture.
///
/// Invoke through tools/run_calibrated_beam_soak.py. The injected material is
/// a numerical integration fixture, not a claim of physically calibrated DAF
/// material properties.

const int APP_STATE_SIMULATION = 2;
const int SIM_STATE_RUNNING = 1;
const int SIM_STATE_PAUSED = 2;

const int EXPECTED_NODES = 176;
const int EXPECTED_CALIBRATED_BEAMS = 15;
const uint64 EXPECTED_PHYSICS_STEPS = 120000;
const string SCENARIO_ID = "2026081302";
const string VEHICLE = "P1CalibratedDAF.truck";

enum ScenarioState
{
    WAITING_FOR_TERRAIN = 0,
    WAITING_FOR_PAUSE,
    WAITING_FOR_ACTOR,
    RUNNING_SOAK,
    FINISHED
}

ScenarioState gState = WAITING_FOR_TERRAIN;
bool gActorSpawned = false;
int64 gActorId = -1;
CVarClass@ gAppState;
CVarClass@ gSimState;

void FailScenario(const string &in reason)
{
    if (gState == FINISHED)
        return;
    gState = FINISHED;
    console.cVarSet("sim_deterministic_state_trace", "false");
    console.cVarSet("sim_deterministic_fixed_steps_per_frame", "0");
    game.log("[RoR|P1|CalibratedBeamSoak] FAIL reason=" + reason);
    game.quitGame();
}

bool AuditActor(BeamClass@ actor, const string &in phase)
{
    if (actor is null)
    {
        FailScenario(phase + "-actor-null");
        return false;
    }
    if (actor.getNodeCount() != EXPECTED_NODES)
    {
        FailScenario(phase + "-node-count-drift-" + actor.getNodeCount());
        return false;
    }
    if (actor.getCalibratedBeamCount() != EXPECTED_CALIBRATED_BEAMS)
    {
        FailScenario(
            phase + "-calibrated-count-drift-" +
            actor.getCalibratedBeamCount());
        return false;
    }
    if (actor.getCalibratedBeamFaultCount() != 0 ||
        actor.getCalibratedBeamFractureCount() != 0 ||
        actor.getCalibratedBeamDisabledCount() != 0)
    {
        FailScenario(
            phase + "-fault-fracture-disabled-" +
            actor.getCalibratedBeamFaultCount() + "-" +
            actor.getCalibratedBeamFractureCount() + "-" +
            actor.getCalibratedBeamDisabledCount());
        return false;
    }
    if (!actor.hasFiniteCalibratedBeamState())
    {
        FailScenario(phase + "-nonfinite-history");
        return false;
    }
    if (!actor.hasValidCalibratedBeamState())
    {
        FailScenario(phase + "-unreachable-history");
        return false;
    }
    if (actor.getCalibratedBeamMaxAbsTotalStrain() < 0.0 ||
        actor.getCalibratedBeamMaxAbsTotalStrain() > 0.5 ||
        actor.getCalibratedBeamMaxAccumulatedPlasticStrain() < 0.0 ||
        actor.getCalibratedBeamMaxDamage() < 0.0 ||
        actor.getCalibratedBeamMaxDamage() > 1.0)
    {
        FailScenario(phase + "-audit-range");
        return false;
    }
    return true;
}

void main()
{
    @gAppState = console.cVarFind("app_state");
    @gSimState = console.cVarFind("sim_state");
    if (gAppState is null || gSimState is null)
    {
        FailScenario("required-cvar-missing");
        return;
    }

    console.cVarSet("sim_deterministic_fixed_steps_per_frame", "100");
    console.cVarSet("sim_deterministic_sleeping_engine", "true");
    console.cVarSet("sim_deterministic_state_trace", "false");
    console.cVarSet(
        "sim_deterministic_state_trace_scenario_id",
        SCENARIO_ID);
    console.cVarSet(
        "sim_deterministic_state_trace_step_limit",
        "" + EXPECTED_PHYSICS_STEPS);
    console.cVarSet("sim_no_collisions", "false");
    console.cVarSet("sim_no_self_collisions", "false");
    game.registerForEvent(SE_GENERIC_NEW_TRUCK);
    game.log(
        "[RoR|P1|CalibratedBeamSoak] START scenario=" + SCENARIO_ID +
        " vehicle=" + VEHICLE + " steps=" + EXPECTED_PHYSICS_STEPS);
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
    if (gActorSpawned || arg1 < 0)
    {
        FailScenario("unexpected-actor-event-" + arg1);
        return;
    }
    gActorId = arg1;
    gActorSpawned = true;
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
        if (game.getCompletedPhysicsSteps() != 0)
        {
            FailScenario("terrain-advanced-before-arm");
            return;
        }
        gState = WAITING_FOR_ACTOR;
        return;
    }

    if (gState == WAITING_FOR_ACTOR)
    {
        if (!gActorSpawned)
            return;
        if (game.getCompletedPhysicsSteps() != 0)
        {
            FailScenario("physics-advanced-during-paused-spawn");
            return;
        }
        BeamClass@ actor = game.getTruckByNum(gActorId);
        if (!AuditActor(actor, "arm"))
            return;

        console.cVarSet("sim_deterministic_state_trace", "true");
        game.pushMessage(MSG_SIM_UNPAUSE_REQUESTED, {});
        gState = RUNNING_SOAK;
        game.log(
            "[RoR|P1|CalibratedBeamSoak] ARMED actors=1 nodes=176 "
            "calibrated_beams=15 first_step=0 batch=100");
        return;
    }

    if (gState == RUNNING_SOAK)
    {
        const uint64 completed = game.getCompletedPhysicsSteps();
        if (completed > EXPECTED_PHYSICS_STEPS)
        {
            FailScenario("physics-step-overshoot-" + completed);
            return;
        }
        BeamClass@ actor = game.getTruckByNum(gActorId);
        if (!AuditActor(actor, "step-" + completed))
            return;

        if (completed == EXPECTED_PHYSICS_STEPS)
        {
            if (gSimState.getInt() != SIM_STATE_RUNNING)
            {
                FailScenario("simulation-not-running-at-completion");
                return;
            }
            if (actor.getCalibratedBeamActiveHistoryCount() == 0 ||
                actor.getCalibratedBeamMaxAbsTotalStrain() == 0.0)
            {
                FailScenario("no-executed-material-history");
                return;
            }
            console.cVarSet("sim_deterministic_state_trace", "false");
            console.cVarSet("sim_deterministic_fixed_steps_per_frame", "0");
            gState = FINISHED;
            game.log(
                "[RoR|P1|CalibratedBeamSoak] PASS actors=1 nodes=176 "
                "calibrated_beams=15 steps=" + completed +
                " active_history=" +
                actor.getCalibratedBeamActiveHistoryCount() +
                " max_abs_strain=" +
                actor.getCalibratedBeamMaxAbsTotalStrain() +
                " max_plastic_strain=" +
                actor.getCalibratedBeamMaxAccumulatedPlasticStrain() +
                " max_damage=" + actor.getCalibratedBeamMaxDamage());
            game.quitGame();
        }
    }
}
