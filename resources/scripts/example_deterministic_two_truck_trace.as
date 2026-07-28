/// \title deterministic two-truck D0 validation scene
/// \brief Runs the pinned simple2/DAF runtime trace with exact step grouping.
///
/// Invoke through tools/run_deterministic_scene.py. The script is inert during
/// ordinary launches and never downloads or modifies content.

const int APP_STATE_SIMULATION = 2;
const int SIM_STATE_RUNNING = 1;
const int SIM_STATE_PAUSED = 2;

const int64 FIRST_ACTOR_ID = 1001;
const int64 SECOND_ACTOR_ID = 1002;
const int EXPECTED_NODES_PER_ACTOR = 176;
const uint64 EXPECTED_PHYSICS_STEPS = 1000;
const string SCENARIO_ID = "2026072801";
const string VEHICLE = "b6b0UID-semi.truck";

const vector3 FIRST_POSITION(512.0f, 25.0f, 512.0f);
const vector3 SECOND_POSITION(512.0f, 25.0f, 514.0f);

enum ScenarioState
{
    WAITING_FOR_TERRAIN = 0,
    WAITING_FOR_PAUSE,
    WAITING_FOR_ACTORS,
    RUNNING_TRACE,
    FINISHED
}

ScenarioState gState = WAITING_FOR_TERRAIN;
bool gFirstActorSpawned = false;
bool gSecondActorSpawned = false;
CVarClass@ gAppState;
CVarClass@ gSimState;

void FailScenario(const string &in reason)
{
    if (gState == FINISHED)
        return;

    gState = FINISHED;
    console.cVarSet("sim_deterministic_state_trace", "false");
    console.cVarSet("sim_deterministic_fixed_steps_per_frame", "0");
    game.log("[RoR|D0|TwoTruck] FAIL reason=" + reason);
    game.quitGame();
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

    // Ten exact substeps per rendered frame keeps the runtime gate fast while
    // preserving the same grouping across runs and worker counts.
    console.cVarSet("sim_deterministic_fixed_steps_per_frame", "10");
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
        "[RoR|D0|TwoTruck] START scenario=" + SCENARIO_ID +
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

    if (arg1 == FIRST_ACTOR_ID)
        gFirstActorSpawned = true;
    else if (arg1 == SECOND_ACTOR_ID)
        gSecondActorSpawned = true;
    else
        FailScenario("unexpected-actor-id-" + arg1);
}

bool SpawnActor(
    const string &in filename,
    int64 instanceId,
    const vector3 &in position)
{
    return game.pushMessage(
        MSG_SIM_SPAWN_ACTOR_REQUESTED,
        {
            {"filename", filename},
            {"instance_id", instanceId},
            {"free_position", true},
            {"enter", false},
            {"position", position},
            {"rotation", quaternion()}
        });
}

void frameStep(float dt)
{
    if (gState == FINISHED)
        return;

    if (gState == WAITING_FOR_TERRAIN)
    {
        if (gAppState.getInt() == APP_STATE_SIMULATION)
        {
            // A queued pause is not processed until the next rendered frame,
            // which would advance one deterministic batch before actors exist.
            // Set the state synchronously so the scenario owns step zero.
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

        if (!SpawnActor(VEHICLE, FIRST_ACTOR_ID, FIRST_POSITION) ||
            !SpawnActor(VEHICLE, SECOND_ACTOR_ID, SECOND_POSITION))
        {
            FailScenario("spawn-request-rejected");
            return;
        }
        gState = WAITING_FOR_ACTORS;
        return;
    }

    if (gState == WAITING_FOR_ACTORS)
    {
        if (!gFirstActorSpawned || !gSecondActorSpawned)
            return;
        if (game.getCompletedPhysicsSteps() != 0)
        {
            FailScenario("physics-advanced-during-paused-spawn");
            return;
        }

        BeamClass@ first = game.getTruckByNum(FIRST_ACTOR_ID);
        BeamClass@ second = game.getTruckByNum(SECOND_ACTOR_ID);
        if (first is null || second is null)
        {
            FailScenario("spawned-actor-not-addressable");
            return;
        }
        if (first.getNodeCount() != EXPECTED_NODES_PER_ACTOR ||
            second.getNodeCount() != EXPECTED_NODES_PER_ACTOR)
        {
            FailScenario("pinned-content-node-count-drift");
            return;
        }

        console.cVarSet("sim_deterministic_state_trace", "true");
        game.pushMessage(MSG_SIM_UNPAUSE_REQUESTED, {});
        gState = RUNNING_TRACE;
        game.log(
            "[RoR|D0|TwoTruck] ARMED actors=2 nodes=352 "
            "samples_per_step=1056 first_step=0 batch=10");
        return;
    }

    if (gState == RUNNING_TRACE)
    {
        const uint64 completed = game.getCompletedPhysicsSteps();
        if (completed > EXPECTED_PHYSICS_STEPS)
        {
            FailScenario("physics-step-overshoot-" + completed);
            return;
        }
        if (completed == EXPECTED_PHYSICS_STEPS)
        {
            if (gSimState.getInt() != SIM_STATE_RUNNING)
            {
                FailScenario("simulation-not-running-at-completion");
                return;
            }
            console.cVarSet("sim_deterministic_state_trace", "false");
            console.cVarSet(
                "sim_deterministic_fixed_steps_per_frame",
                "0");
            gState = FINISHED;
            game.log(
                "[RoR|D0|TwoTruck] PASS actors=2 nodes=352 steps=" +
                completed);
            game.quitGame();
        }
    }
}
