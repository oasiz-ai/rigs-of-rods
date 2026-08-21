/// \title authenticated JBeam inter-actor collision execution gate
/// \brief Executes native node-to-NORMALTYPE-cab contact in the product path.
///
/// Invoke through tools/run_jbeam_inter_actor_collision.py. The generated
/// actors use a clean-room structural fixture; this is not BeamNG.drive
/// behavior-parity evidence.

const int APP_STATE_SIMULATION = 2;
const int SIM_STATE_RUNNING = 1;
const int SIM_STATE_PAUSED = 2;

const int64 LOWER_ACTOR_ID = 2101;
const int64 UPPER_ACTOR_ID = 2102;
const int EXPECTED_NODES = 6;
const int EXPECTED_BEAMS = 16;
const int EXPECTED_CAB_TRIANGLES = 5;
const int EXPECTED_COLLISION_CABS = 5;
const int EXPECTED_CONTACTERS = 0;
const int EXPECTED_HYDROS = 1;
const uint64 EXPECTED_PHYSICS_STEPS = 2000;
const string SCENARIO_ID = "2026082106";
const string VEHICLE = "ror_jbeam_spawn_fixture.jbeam";
const vector3 LOWER_POSITION(512.0f, 100.0f, 512.0f);
const vector3 UPPER_POSITION(512.0f, 100.01f, 512.0f);
const vector3 LOWER_VELOCITY(0.0f, 0.5f, 0.0f);
const vector3 UPPER_VELOCITY(0.0f, -0.5f, 0.0f);

enum ScenarioState
{
    WAITING_FOR_TERRAIN = 0,
    WAITING_FOR_PAUSE,
    WAITING_FOR_ACTORS,
    RUNNING_COLLISION,
    FINISHED
}

ScenarioState gState = WAITING_FOR_TERRAIN;
bool gLowerSpawned = false;
bool gUpperSpawned = false;
bool gForcedActive = false;
double gInitialVerticalSeparation = 0.0;
double gMaximumVerticalSeparation = 0.0;
double gMaximumRelativeVelocityChange = 0.0;
bool gObservedCollisionResponse = false;
CVarClass@ gAppState;
CVarClass@ gSimState;

bool IsFinite(double value)
{
    return value == value && value > -1.0e100 && value < 1.0e100;
}

void FailScenario(const string &in reason)
{
    if (gState == FINISHED)
        return;
    gState = FINISHED;
    if (gForcedActive)
    {
        game.setTrucksForcedActive(false);
        gForcedActive = false;
    }
    console.cVarSet("sim_deterministic_state_trace", "false");
    console.cVarSet("sim_deterministic_fixed_steps_per_frame", "0");
    game.log("[RoR|J2|InterActorCollision] FAIL reason=" + reason);
    game.quitGame();
}

vector3 CenterOfMassPosition(BeamClass@ actor)
{
    vector3 sum(0.0f, 0.0f, 0.0f);
    double mass = 0.0;
    for (int index = 0; index < actor.getNodeCount(); ++index)
    {
        const float nodeMass = actor.getNodeMass(index);
        sum += actor.getNodePosition(index) * nodeMass;
        mass += double(nodeMass);
    }
    if (!IsFinite(mass) || mass <= 0.0)
    {
        FailScenario("invalid-center-mass");
        return vector3();
    }
    return sum / float(mass);
}

vector3 CenterOfMassVelocity(BeamClass@ actor)
{
    vector3 sum(0.0f, 0.0f, 0.0f);
    double mass = 0.0;
    for (int index = 0; index < actor.getNodeCount(); ++index)
    {
        const float nodeMass = actor.getNodeMass(index);
        sum += actor.getNodeVelocity(index) * nodeMass;
        mass += double(nodeMass);
    }
    if (!IsFinite(mass) || mass <= 0.0)
    {
        FailScenario("invalid-center-velocity-mass");
        return vector3();
    }
    return sum / float(mass);
}

bool AuditActor(
    BeamClass@ actor,
    const string &in label,
    uint64 completed)
{
    if (actor is null)
    {
        FailScenario(label + "-actor-null");
        return false;
    }
    if (actor.getNodeCount() != EXPECTED_NODES ||
        actor.getBeamCount() != EXPECTED_BEAMS ||
        actor.getCabTriangleCount() != EXPECTED_CAB_TRIANGLES ||
        actor.getCollisionCabTriangleCount() != EXPECTED_COLLISION_CABS ||
        actor.getContacterCount() != EXPECTED_CONTACTERS ||
        actor.getJBeamHydroRuntimeCount() != EXPECTED_HYDROS)
    {
        FailScenario(label + "-topology-drift");
        return false;
    }
    if (actor.getJBeamHydroRuntimeFaultCount() != 0 ||
        !actor.hasFiniteJBeamHydroRuntimeState() ||
        actor.getJBeamHydroMinimumAcceptedStepCount() != completed ||
        actor.getJBeamHydroMaximumAcceptedStepCount() != completed)
    {
        FailScenario(label + "-hydro-state-drift-" + completed);
        return false;
    }
    for (int index = 0; index < actor.getNodeCount(); ++index)
    {
        const vector3 position = actor.getNodePosition(index);
        const vector3 velocity = actor.getNodeVelocity(index);
        const vector3 force = actor.getNodeForces(index);
        if (!IsFinite(double(position.x)) ||
            !IsFinite(double(position.y)) ||
            !IsFinite(double(position.z)) ||
            !IsFinite(double(velocity.x)) ||
            !IsFinite(double(velocity.y)) ||
            !IsFinite(double(velocity.z)) ||
            !IsFinite(double(force.x)) ||
            !IsFinite(double(force.y)) ||
            !IsFinite(double(force.z)) ||
            abs(double(position.x)) > 1.0e7 ||
            abs(double(position.y)) > 1.0e7 ||
            abs(double(position.z)) > 1.0e7 ||
            abs(double(velocity.x)) > 1.0e7 ||
            abs(double(velocity.y)) > 1.0e7 ||
            abs(double(velocity.z)) > 1.0e7)
        {
            FailScenario(label + "-nonfinite-or-domain-node-" + index);
            return false;
        }
    }
    return true;
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

void main()
{
    @gAppState = console.cVarFind("app_state");
    @gSimState = console.cVarFind("sim_state");
    if (gAppState is null || gSimState is null)
    {
        FailScenario("required-cvar-missing");
        return;
    }

    console.cVarSet("sim_deterministic_fixed_steps_per_frame", "10");
    console.cVarSet("sim_deterministic_sleeping_engine", "true");
    console.cVarSet("sim_deterministic_state_trace", "false");
    console.cVarSet(
        "sim_deterministic_state_trace_scenario_id", SCENARIO_ID);
    console.cVarSet(
        "sim_deterministic_state_trace_step_limit",
        "" + EXPECTED_PHYSICS_STEPS);
    console.cVarSet("sim_no_collisions", "false");
    console.cVarSet("sim_no_self_collisions", "false");
    game.registerForEvent(SE_GENERIC_NEW_TRUCK);
    game.log(
        "[RoR|J2|InterActorCollision] START scenario=" + SCENARIO_ID +
        " vehicle=" + VEHICLE + " actors=2 steps=" +
        EXPECTED_PHYSICS_STEPS + " initial_vertical_gap=0.01 " +
        "closing_speed=1");
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
    if (arg1 == LOWER_ACTOR_ID && !gLowerSpawned)
        gLowerSpawned = true;
    else if (arg1 == UPPER_ACTOR_ID && !gUpperSpawned)
        gUpperSpawned = true;
    else
        FailScenario("unexpected-or-duplicate-actor-event-" + arg1);
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
        if (!SpawnActor(VEHICLE, LOWER_ACTOR_ID, LOWER_POSITION) ||
            !SpawnActor(VEHICLE, UPPER_ACTOR_ID, UPPER_POSITION))
        {
            FailScenario("spawn-request-rejected");
            return;
        }
        gState = WAITING_FOR_ACTORS;
        return;
    }

    if (gState == WAITING_FOR_ACTORS)
    {
        if (!gLowerSpawned || !gUpperSpawned)
            return;
        if (game.getCompletedPhysicsSteps() != 0)
        {
            FailScenario("physics-advanced-during-paused-spawn");
            return;
        }
        BeamClass@ lower = game.getTruckByNum(LOWER_ACTOR_ID);
        BeamClass@ upper = game.getTruckByNum(UPPER_ACTOR_ID);
        if (!AuditActor(lower, "arm-lower", 0) ||
            !AuditActor(upper, "arm-upper", 0))
        {
            return;
        }

        game.setTrucksForcedActive(true);
        gForcedActive = true;
        lower.wakeUp();
        upper.wakeUp();
        // The exact free-position requests already own placement. Apply only
        // the bounded pre-step velocity transaction here so neither actor's
        // spawn-origin bookkeeping is rewritten by the evidence harness.
        if (!lower.trySetDeterministicImpactVelocity(LOWER_VELOCITY) ||
            !upper.trySetDeterministicImpactVelocity(UPPER_VELOCITY))
        {
            FailScenario("initial-collision-velocity-rejected");
            return;
        }

        const vector3 lowerCenter = CenterOfMassPosition(lower);
        const vector3 upperCenter = CenterOfMassPosition(upper);
        const vector3 lowerVelocity = CenterOfMassVelocity(lower);
        const vector3 upperVelocity = CenterOfMassVelocity(upper);
        if (gState == FINISHED)
            return;
        gInitialVerticalSeparation =
            double(upperCenter.y) - double(lowerCenter.y);
        gMaximumVerticalSeparation = abs(gInitialVerticalSeparation);
        if (abs(gInitialVerticalSeparation - 0.01) > 2.0e-5 ||
            abs(double(lowerVelocity.y) - 0.5) > 1.0e-5 ||
            abs(double(upperVelocity.y) + 0.5) > 1.0e-5)
        {
            FailScenario("initial-collision-state-drift");
            return;
        }

        console.cVarSet("sim_deterministic_state_trace", "true");
        game.pushMessage(MSG_SIM_UNPAUSE_REQUESTED, {});
        gState = RUNNING_COLLISION;
        game.log(
            "[RoR|J2|InterActorCollision] ARMED actors=2 nodes=12 " +
            "beams=32 cab_triangles=10 collision_cabs=10 contacters=0 " +
            "hydros=2 initial_vertical_gap=0.01 closing_speed=1 " +
            "first_step=0 batch=10");
        return;
    }

    if (gState == RUNNING_COLLISION)
    {
        const uint64 completed = game.getCompletedPhysicsSteps();
        if (completed > EXPECTED_PHYSICS_STEPS)
        {
            FailScenario("physics-step-overshoot-" + completed);
            return;
        }
        BeamClass@ lower = game.getTruckByNum(LOWER_ACTOR_ID);
        BeamClass@ upper = game.getTruckByNum(UPPER_ACTOR_ID);
        if (!AuditActor(lower, "step-lower", completed) ||
            !AuditActor(upper, "step-upper", completed))
        {
            return;
        }
        lower.wakeUp();
        upper.wakeUp();

        const vector3 lowerCenter = CenterOfMassPosition(lower);
        const vector3 upperCenter = CenterOfMassPosition(upper);
        const vector3 lowerVelocity = CenterOfMassVelocity(lower);
        const vector3 upperVelocity = CenterOfMassVelocity(upper);
        if (gState == FINISHED)
            return;
        const double separation =
            abs(double(upperCenter.y) - double(lowerCenter.y));
        const double relativeVelocity =
            double(lowerVelocity.y) - double(upperVelocity.y);
        const double relativeVelocityChange =
            abs(relativeVelocity - 1.0);
        if (separation > gMaximumVerticalSeparation)
            gMaximumVerticalSeparation = separation;
        if (relativeVelocityChange > gMaximumRelativeVelocityChange)
            gMaximumRelativeVelocityChange = relativeVelocityChange;
        if (relativeVelocityChange > 0.1)
            gObservedCollisionResponse = true;

        if (completed == EXPECTED_PHYSICS_STEPS)
        {
            const int broken =
                lower.getBrokenBeamCount() + upper.getBrokenBeamCount();
            if (gSimState.getInt() != SIM_STATE_RUNNING ||
                !gObservedCollisionResponse ||
                !IsFinite(gMaximumRelativeVelocityChange) ||
                gMaximumRelativeVelocityChange <= 0.1 ||
                !IsFinite(gMaximumVerticalSeparation) ||
                gMaximumVerticalSeparation <= 0.03 ||
                broken != 0)
            {
                FailScenario(
                    "collision-response-incomplete-observed-" +
                    gObservedCollisionResponse + "-relative-change-" +
                    formatFloat(
                        gMaximumRelativeVelocityChange, "e", 0, 17) +
                    "-separation-" +
                    formatFloat(gMaximumVerticalSeparation, "e", 0, 17) +
                    "-broken-" + broken);
                return;
            }

            console.cVarSet("sim_deterministic_state_trace", "false");
            console.cVarSet("sim_deterministic_fixed_steps_per_frame", "0");
            game.setTrucksForcedActive(false);
            gForcedActive = false;
            gState = FINISHED;
            game.log(
                "[RoR|J2|InterActorCollision] PASS actors=2 nodes=12 " +
                "beams=32 cab_triangles=10 collision_cabs=10 " +
                "contacters=0 hydros=2 steps=" + completed +
                " maximum_relative_velocity_change=" +
                formatFloat(
                    gMaximumRelativeVelocityChange, "e", 0, 17) +
                " maximum_vertical_separation=" +
                formatFloat(gMaximumVerticalSeparation, "e", 0, 17) +
                " broken_beams=" + broken);
            game.quitGame();
        }
    }
}
