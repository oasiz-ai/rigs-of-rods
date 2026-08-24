/// \title full-runtime JBeam multi-actor ThreadSanitizer soak
/// \brief Cycles native inter-actor collision pairs for ten wall-clock minutes.
///
/// Invoke only through tools/run_jbeam_multi_actor_tsan_soak.py. The fixture is
/// clean-room project content. This is a concurrency/lifetime gate, not
/// BeamNG.drive force-parity or third-party-mod compatibility evidence.

const int APP_STATE_SIMULATION = 2;
const int SIM_STATE_RUNNING = 1;
const int SIM_STATE_PAUSED = 2;

const int64 FIRST_ACTOR_ID = 3101;
const int EXPECTED_NODES = 6;
const int EXPECTED_BEAMS = 16;
const int EXPECTED_CAB_TRIANGLES = 5;
const int EXPECTED_COLLISION_CABS = 5;
const int EXPECTED_CONTACTERS = 0;
const int EXPECTED_HYDROS = 1;
const uint64 CYCLE_PHYSICS_STEPS = 2000;
const uint64 MINIMUM_TOTAL_PHYSICS_STEPS = 20000;
const int MINIMUM_COMPLETED_CYCLES = 10;
const float TARGET_SECONDS = 600.0f;
const string VEHICLE = "ror_jbeam_spawn_fixture.jbeam";
const vector3 LOWER_POSITION(512.0f, 100.0f, 512.0f);
const vector3 UPPER_POSITION(512.0f, 100.01f, 512.0f);
const vector3 LOWER_VELOCITY(0.0f, 0.5f, 0.0f);
const vector3 UPPER_VELOCITY(0.0f, -0.5f, 0.0f);

enum SoakState
{
    WAITING_FOR_TERRAIN = 0,
    WAITING_FOR_PAUSE,
    WAITING_FOR_ACTORS,
    RUNNING_COLLISION,
    WAITING_FOR_DELETE,
    FINISHED
}

SoakState gState = WAITING_FOR_TERRAIN;
int gPairGeneration = 0;
int64 gLowerActorId = FIRST_ACTOR_ID;
int64 gUpperActorId = FIRST_ACTOR_ID + 1;
bool gLowerSpawned = false;
bool gUpperSpawned = false;
bool gForcedActive = false;
bool gObservedCycleResponse = false;
uint64 gCycleStartPhysicsStep = 0;
float gLastProgressTime = -1.0f;
Timer gSoakWallClock;
bool gSoakWallClockStarted = false;
int gCompletedCycles = 0;
int gCollisionResponses = 0;
int gActorSpawns = 0;
int gActorDeletes = 0;
CVarClass@ gAppState;
CVarClass@ gSimState;

bool IsFinite(double value)
{
    return value == value && value > -1.0e100 && value < 1.0e100;
}

void FinishForcedActive()
{
    if (gForcedActive)
    {
        game.setTrucksForcedActive(false);
        gForcedActive = false;
    }
}

void FailSoak(const string &in reason)
{
    if (gState == FINISHED)
        return;
    gState = FINISHED;
    FinishForcedActive();
    console.cVarSet("sim_deterministic_fixed_steps_per_frame", "0");
    game.log("[RoR|D0|TSanSoak] FAIL reason=" + reason);
    game.quitGame();
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
        FailSoak("invalid-center-velocity-mass");
        return vector3();
    }
    return sum / float(mass);
}

bool AuditActor(BeamClass@ actor, const string &in label, uint64 localSteps)
{
    if (actor is null)
    {
        FailSoak(label + "-actor-null");
        return false;
    }
    if (actor.getNodeCount() != EXPECTED_NODES ||
        actor.getBeamCount() != EXPECTED_BEAMS ||
        actor.getCabTriangleCount() != EXPECTED_CAB_TRIANGLES ||
        actor.getCollisionCabTriangleCount() != EXPECTED_COLLISION_CABS ||
        actor.getContacterCount() != EXPECTED_CONTACTERS ||
        actor.getJBeamHydroRuntimeCount() != EXPECTED_HYDROS ||
        actor.getJBeamHydroRuntimeFaultCount() != 0 ||
        !actor.hasFiniteJBeamHydroRuntimeState() ||
        actor.getJBeamHydroMinimumAcceptedStepCount() != localSteps ||
        actor.getJBeamHydroMaximumAcceptedStepCount() != localSteps ||
        actor.getBrokenBeamCount() != 0)
    {
        FailSoak(label + "-topology-or-runtime-drift-" + localSteps);
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
            FailSoak(label + "-nonfinite-or-domain-node-" + index);
            return false;
        }
    }
    return true;
}

bool SpawnActor(int64 instanceId, const vector3 &in position)
{
    return game.pushMessage(
        MSG_SIM_SPAWN_ACTOR_REQUESTED,
        {
            {"filename", VEHICLE},
            {"instance_id", instanceId},
            {"free_position", true},
            {"enter", false},
            {"position", position},
            {"rotation", quaternion()}
        });
}

bool SpawnPair()
{
    gLowerActorId = FIRST_ACTOR_ID + int64(gPairGeneration * 2);
    gUpperActorId = gLowerActorId + 1;
    gLowerSpawned = false;
    gUpperSpawned = false;
    if (!SpawnActor(gLowerActorId, LOWER_POSITION) ||
        !SpawnActor(gUpperActorId, UPPER_POSITION))
    {
        return false;
    }
    gActorSpawns += 2;
    gState = WAITING_FOR_ACTORS;
    return true;
}

bool DeletePair()
{
    if (!game.pushMessage(
            MSG_SIM_DELETE_ACTOR_REQUESTED,
            {{"instance_id", gLowerActorId}}) ||
        !game.pushMessage(
            MSG_SIM_DELETE_ACTOR_REQUESTED,
            {{"instance_id", gUpperActorId}}))
    {
        return false;
    }
    gState = WAITING_FOR_DELETE;
    return true;
}

void main()
{
    @gAppState = console.cVarFind("app_state");
    @gSimState = console.cVarFind("sim_state");
    if (gAppState is null || gSimState is null)
    {
        FailSoak("required-cvar-missing");
        return;
    }
    console.cVarSet("sim_deterministic_fixed_steps_per_frame", "10");
    console.cVarSet("sim_deterministic_sleeping_engine", "true");
    console.cVarSet("sim_deterministic_state_trace", "false");
    console.cVarSet("sim_no_collisions", "false");
    console.cVarSet("sim_no_self_collisions", "false");
    game.registerForEvent(SE_GENERIC_NEW_TRUCK);
    game.log(
        "[RoR|D0|TSanSoak] START vehicle=" + VEHICLE +
        " target_seconds=600 cycle_steps=2000 minimum_cycles=10 " +
        "minimum_total_steps=20000 actors_per_cycle=2");
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
    if (int64(arg1) == gLowerActorId && !gLowerSpawned)
        gLowerSpawned = true;
    else if (int64(arg1) == gUpperActorId && !gUpperSpawned)
        gUpperSpawned = true;
    else
        FailSoak("unexpected-or-duplicate-actor-event-" + arg1);
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
            FailSoak("terrain-advanced-before-arm");
            return;
        }
        if (!SpawnPair())
            FailSoak("initial-spawn-request-rejected");
        return;
    }

    if (gState == WAITING_FOR_DELETE)
    {
        if (gSimState.getInt() != SIM_STATE_PAUSED)
        {
            FailSoak("delete-wait-not-paused");
            return;
        }
        if (game.getTruckByNum(gLowerActorId) !is null ||
            game.getTruckByNum(gUpperActorId) !is null)
        {
            return;
        }
        gActorDeletes += 2;
        ++gPairGeneration;
        if (!SpawnPair())
            FailSoak("replacement-spawn-request-rejected");
        return;
    }

    if (gState == WAITING_FOR_ACTORS)
    {
        if (!gLowerSpawned || !gUpperSpawned)
            return;
        if (gSimState.getInt() != SIM_STATE_PAUSED)
        {
            FailSoak("pair-arm-not-paused");
            return;
        }
        BeamClass@ lower = game.getTruckByNum(gLowerActorId);
        BeamClass@ upper = game.getTruckByNum(gUpperActorId);
        if (!AuditActor(lower, "arm-lower", 0) ||
            !AuditActor(upper, "arm-upper", 0))
        {
            return;
        }
        if (!gForcedActive)
        {
            game.setTrucksForcedActive(true);
            gForcedActive = true;
        }
        lower.wakeUp();
        upper.wakeUp();
        if (!lower.trySetDeterministicImpactVelocity(LOWER_VELOCITY) ||
            !upper.trySetDeterministicImpactVelocity(UPPER_VELOCITY))
        {
            FailSoak("pair-impact-velocity-rejected");
            return;
        }
        const vector3 lowerVelocity = CenterOfMassVelocity(lower);
        const vector3 upperVelocity = CenterOfMassVelocity(upper);
        if (gState == FINISHED)
            return;
        if (abs(double(lowerVelocity.y) - 0.5) > 1.0e-5 ||
            abs(double(upperVelocity.y) + 0.5) > 1.0e-5)
        {
            FailSoak("pair-impact-velocity-drift");
            return;
        }
        gCycleStartPhysicsStep = game.getCompletedPhysicsSteps();
        gObservedCycleResponse = false;
        if (!gSoakWallClockStarted)
        {
            // `game.getTime()` is authoritative simulation time. This gate
            // instead promises a continuous ten-minute wall-clock exposure,
            // so use Ogre's monotonic Timer and retain fixed-step counters as
            // a separate minimum-work requirement.
            gSoakWallClock.reset();
            gSoakWallClockStarted = true;
            gLastProgressTime = 0.0f;
            game.log(
                "[RoR|D0|TSanSoak] ARMED actors=2 nodes=12 beams=32 " +
                "cab_triangles=10 collision_cabs=10 hydros=2 batch=10");
        }
        game.pushMessage(MSG_SIM_UNPAUSE_REQUESTED, {});
        gState = RUNNING_COLLISION;
        return;
    }

    if (gState == RUNNING_COLLISION)
    {
        const uint64 completed = game.getCompletedPhysicsSteps();
        if (gSimState.getInt() != SIM_STATE_RUNNING)
        {
            // The unpause request is queued. Permit only its zero-step
            // handoff; a pause after the cycle has advanced is a hard fault.
            if (gSimState.getInt() == SIM_STATE_PAUSED &&
                completed == gCycleStartPhysicsStep)
            {
                return;
            }
            FailSoak("collision-cycle-not-running");
            return;
        }
        if (completed < gCycleStartPhysicsStep)
        {
            FailSoak("global-step-regressed");
            return;
        }
        const uint64 localSteps = completed - gCycleStartPhysicsStep;
        if (localSteps > CYCLE_PHYSICS_STEPS)
        {
            FailSoak("cycle-step-overshoot-" + localSteps);
            return;
        }
        BeamClass@ lower = game.getTruckByNum(gLowerActorId);
        BeamClass@ upper = game.getTruckByNum(gUpperActorId);
        if (!AuditActor(lower, "cycle-lower", localSteps) ||
            !AuditActor(upper, "cycle-upper", localSteps))
        {
            return;
        }
        lower.wakeUp();
        upper.wakeUp();
        const vector3 lowerVelocity = CenterOfMassVelocity(lower);
        const vector3 upperVelocity = CenterOfMassVelocity(upper);
        if (gState == FINISHED)
            return;
        const double relativeVelocity =
            double(lowerVelocity.y) - double(upperVelocity.y);
        if (!gObservedCycleResponse && abs(relativeVelocity - 1.0) > 0.1)
        {
            gObservedCycleResponse = true;
            ++gCollisionResponses;
        }

        const float elapsed =
            float(gSoakWallClock.getMilliseconds()) / 1000.0f;
        if (elapsed - gLastProgressTime >= 60.0f)
        {
            gLastProgressTime = elapsed;
            game.log(
                "[RoR|D0|TSanSoak] PROGRESS elapsed_seconds=" +
                formatFloat(elapsed, "f", 0, 3) +
                " physics_steps=" + completed +
                " completed_cycles=" + gCompletedCycles +
                " actor_spawns=" + gActorSpawns +
                " actor_deletes=" + gActorDeletes);
        }

        if (localSteps != CYCLE_PHYSICS_STEPS)
            return;
        if (!gObservedCycleResponse)
        {
            FailSoak("cycle-produced-no-collision-response");
            return;
        }
        ++gCompletedCycles;
        if (elapsed >= TARGET_SECONDS)
        {
            if (gCompletedCycles < MINIMUM_COMPLETED_CYCLES ||
                completed < MINIMUM_TOTAL_PHYSICS_STEPS ||
                gCollisionResponses != gCompletedCycles ||
                gActorSpawns != gCompletedCycles * 2 ||
                gActorDeletes != (gCompletedCycles - 1) * 2)
            {
                FailSoak("minimum-work-or-mutation-receipt-incomplete");
                return;
            }
            FinishForcedActive();
            console.cVarSet("sim_deterministic_fixed_steps_per_frame", "0");
            gState = FINISHED;
            game.log(
                "[RoR|D0|TSanSoak] PASS elapsed_seconds=" +
                formatFloat(elapsed, "f", 0, 3) +
                " physics_steps=" + completed +
                " completed_cycles=" + gCompletedCycles +
                " collision_responses=" + gCollisionResponses +
                " actor_spawns=" + gActorSpawns +
                " actor_deletes=" + gActorDeletes);
            game.quitGame();
            return;
        }

        console.cVarSet("sim_state", "" + SIM_STATE_PAUSED);
        if (!DeletePair())
            FailSoak("delete-request-rejected");
    }
}
