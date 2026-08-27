/// \title authenticated JBeam pressureWheel to native Wheel2 spawn smoke
/// \brief Proves product spawn/topology plus sampled finite-state boundaries.
///
/// Invoke through tools/run_jbeam_wheel2_spawn.py. This clean-room fixture is
/// unpropelled and unbraked. It does not prove tyre pressure, friction,
/// braking, propulsion, steering, rolling, driveability, source parity,
/// playability, settle behavior, gravity response, contact behavior, or
/// per-step numeric state bounds.

const int APP_STATE_SIMULATION = 2;
const int SIM_STATE_RUNNING = 1;
const int SIM_STATE_PAUSED = 2;

const int EXPECTED_NODES = 73;
const int EXPECTED_RUNTIME_BEAMS = 392;
const int EXPECTED_WHEEL_TIRE_NODES = 32;
const int EXPECTED_WHEEL_RIM_NODES = 32;
const int EXPECTED_STRUCTURAL_NODES = 9;
const int EXPECTED_CONTACTERS = 32;
const int EXPECTED_GROUND_CONTACT_NODES = 73;
const uint64 EXPECTED_PHYSICS_STEPS = 20000;
const uint64 EXPECTED_AUDIT_STRIDE = 100;
const int EXPECTED_AUDIT_SAMPLES = 201;
const string SCENARIO_ID = "2026082702";
const string VEHICLE = "ror_jbeam_wheel2_fixture.jbeam";
const vector3 INITIAL_TEST_TRANSLATION(0.0f, 0.75f, 0.0f);
const vector3 INITIAL_TEST_VELOCITY(0.0f, -1.0f, 0.0f);

enum ScenarioState
{
    WAITING_FOR_TERRAIN = 0,
    WAITING_FOR_PAUSE,
    WAITING_FOR_ACTOR,
    RUNNING_BATCH_BOUNDARY_INTERVAL,
    FINISHED
}

ScenarioState gState = WAITING_FOR_TERRAIN;
bool gActorSpawned = false;
bool gForcedActive = false;
int64 gActorId = -1;
double gInitialCenterOfMassY = 0.0;
double gMinimumCenterOfMassY = 0.0;
double gInitialTotalMass = 0.0;
double gMinimumNodeMass = 1.0e100;
double gMaximumAbsPosition = 0.0;
double gMaximumAbsVelocity = 0.0;
int gAuditSamples = 0;
uint64 gLastAuditedStep = 0;
CVarClass@ gAppState;
CVarClass@ gSimState;

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
    game.log("[RoR|J3|Wheel2Spawn] FAIL reason=" + reason);
    game.quitGame();
}

bool IsFiniteComponent(double value)
{
    return value == value && value > -1.0e100 && value < 1.0e100;
}

double Maximum3(double a, double b, double c)
{
    double result = abs(a);
    if (abs(b) > result)
        result = abs(b);
    if (abs(c) > result)
        result = abs(c);
    return result;
}

vector3 CenterOfMassPosition(BeamClass@ actor)
{
    vector3 weighted(0.0f, 0.0f, 0.0f);
    double totalMass = 0.0;
    for (int index = 0; index < actor.getNodeCount(); ++index)
    {
        const float mass = actor.getNodeMass(index);
        weighted += actor.getNodePosition(index) * mass;
        totalMass += double(mass);
    }
    if (!IsFiniteComponent(totalMass) || totalMass <= 0.0)
    {
        FailScenario("center-of-mass-position-invalid-total-mass");
        return vector3();
    }
    return weighted / float(totalMass);
}

vector3 CenterOfMassVelocity(BeamClass@ actor)
{
    vector3 weighted(0.0f, 0.0f, 0.0f);
    double totalMass = 0.0;
    for (int index = 0; index < actor.getNodeCount(); ++index)
    {
        const float mass = actor.getNodeMass(index);
        weighted += actor.getNodeVelocity(index) * mass;
        totalMass += double(mass);
    }
    if (!IsFiniteComponent(totalMass) || totalMass <= 0.0)
    {
        FailScenario("center-of-mass-velocity-invalid-total-mass");
        return vector3();
    }
    return weighted / float(totalMass);
}

bool AuditActor(BeamClass@ actor, const string &in phase)
{
    if (actor is null)
    {
        FailScenario(phase + "-actor-null");
        return false;
    }
    if (actor.getNodeCount() != EXPECTED_NODES ||
        actor.getBeamCount() != EXPECTED_RUNTIME_BEAMS ||
        actor.getWheelNodeCount() != EXPECTED_WHEEL_TIRE_NODES ||
        actor.getCabTriangleCount() != 0 ||
        actor.getCollisionCabTriangleCount() != 0 ||
        actor.getContacterCount() != EXPECTED_CONTACTERS ||
        actor.getGroundContactEnabledNodeCount() !=
            EXPECTED_GROUND_CONTACT_NODES)
    {
        FailScenario(
            phase + "-topology-drift-nodes-" + actor.getNodeCount() +
            "-beams-" + actor.getBeamCount() + "-wheel-tire-nodes-" +
            actor.getWheelNodeCount() + "-cabs-" +
            actor.getCabTriangleCount() + "-collision-cabs-" +
            actor.getCollisionCabTriangleCount() + "-contacters-" +
            actor.getContacterCount() + "-ground-contact-" +
            actor.getGroundContactEnabledNodeCount());
        return false;
    }

    int rimNodes = 0;
    int tireNodes = 0;
    int structuralNodes = 0;
    double totalMass = 0.0;
    for (int index = 0; index < actor.getNodeCount(); ++index)
    {
        const bool rim = actor.isNodeWheelRim(index);
        const bool tire = actor.isNodeWheelTire(index);
        if (rim && tire)
        {
            FailScenario(phase + "-overlapping-wheel-classification-" + index);
            return false;
        }
        if (rim)
            ++rimNodes;
        else if (tire)
            ++tireNodes;
        else
            ++structuralNodes;

        const float mass = actor.getNodeMass(index);
        const vector3 position = actor.getNodePosition(index);
        const vector3 velocity = actor.getNodeVelocity(index);
        const vector3 force = actor.getNodeForces(index);
        if (!IsFiniteComponent(double(mass)) || mass <= 0.0f ||
            !IsFiniteComponent(double(position.x)) ||
            !IsFiniteComponent(double(position.y)) ||
            !IsFiniteComponent(double(position.z)) ||
            !IsFiniteComponent(double(velocity.x)) ||
            !IsFiniteComponent(double(velocity.y)) ||
            !IsFiniteComponent(double(velocity.z)) ||
            !IsFiniteComponent(double(force.x)) ||
            !IsFiniteComponent(double(force.y)) ||
            !IsFiniteComponent(double(force.z)))
        {
            FailScenario(phase + "-nonfinite-or-nonpositive-node-" + index);
            return false;
        }
        const double positionMaximum = Maximum3(
            double(position.x), double(position.y), double(position.z));
        const double velocityMaximum = Maximum3(
            double(velocity.x), double(velocity.y), double(velocity.z));
        if (positionMaximum > 1.0e6 || velocityMaximum > 1.0e4)
        {
            FailScenario(phase + "-node-domain-" + index);
            return false;
        }
        if (double(mass) < gMinimumNodeMass)
            gMinimumNodeMass = double(mass);
        if (positionMaximum > gMaximumAbsPosition)
            gMaximumAbsPosition = positionMaximum;
        if (velocityMaximum > gMaximumAbsVelocity)
            gMaximumAbsVelocity = velocityMaximum;
        totalMass += double(mass);
    }
    if (rimNodes != EXPECTED_WHEEL_RIM_NODES ||
        tireNodes != EXPECTED_WHEEL_TIRE_NODES ||
        structuralNodes != EXPECTED_STRUCTURAL_NODES)
    {
        FailScenario(
            phase + "-wheel-classification-drift-rim-" + rimNodes +
            "-tire-" + tireNodes + "-structural-" + structuralNodes);
        return false;
    }
    const double actorMass = double(actor.getTotalMass(false));
    if (!IsFiniteComponent(totalMass) || totalMass <= 0.0 ||
        !IsFiniteComponent(actorMass) || actorMass <= 0.0 ||
        abs(totalMass - actorMass) > 1.0e-3)
    {
        FailScenario(phase + "-positive-mass-accounting-failed");
        return false;
    }
    if (gInitialTotalMass > 0.0 &&
        abs(totalMass - gInitialTotalMass) > 1.0e-3)
    {
        FailScenario(phase + "-total-mass-drift");
        return false;
    }
    if (actor.getBrokenBeamCount() != 0)
    {
        FailScenario(phase + "-broken-beams-" + actor.getBrokenBeamCount());
        return false;
    }
    return true;
}

double MaximumNodeSpeed(BeamClass@ actor)
{
    double maximum = 0.0;
    for (int index = 0; index < actor.getNodeCount(); ++index)
    {
        const double speed = double(actor.getNodeVelocity(index).length());
        if (!IsFiniteComponent(speed))
        {
            FailScenario("final-node-speed-nonfinite-" + index);
            return 1.0e100;
        }
        if (speed > maximum)
            maximum = speed;
    }
    return maximum;
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

    console.cVarSet(
        "sim_deterministic_fixed_steps_per_frame", "" + EXPECTED_AUDIT_STRIDE);
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
        "[RoR|J3|Wheel2Spawn] START scenario=" + SCENARIO_ID +
        " vehicle=" + VEHICLE + " steps=" + EXPECTED_PHYSICS_STEPS +
        " pressure_volume=false friction=false braking=false " +
        "propulsion=false steering=false rolling=false driveability=false " +
        "source_parity=false playability=false gravity_response=false " +
        "contact_behavior=false per_step_numeric_bounds=false");
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
        if (!AuditActor(actor, "spawn"))
            return;
        gInitialTotalMass = double(actor.getTotalMass(false));
        const double spawnCenterY = double(CenterOfMassPosition(actor).y);
        if (gState == FINISHED)
            return;

        game.setTrucksForcedActive(true);
        gForcedActive = true;
        actor.wakeUp();
        if (!actor.trySetDeterministicImpactPlacementAndVelocity(
                INITIAL_TEST_TRANSLATION, INITIAL_TEST_VELOCITY))
        {
            FailScenario("initial-test-placement-or-velocity-rejected");
            return;
        }
        gInitialCenterOfMassY = double(CenterOfMassPosition(actor).y);
        gMinimumCenterOfMassY = gInitialCenterOfMassY;
        const vector3 armedVelocity = CenterOfMassVelocity(actor);
        if (gState == FINISHED ||
            abs((gInitialCenterOfMassY - spawnCenterY) - 0.75) > 1.0e-5 ||
            abs(double(armedVelocity.x)) > 1.0e-5 ||
            abs(double(armedVelocity.y) + 1.0) > 1.0e-5 ||
            abs(double(armedVelocity.z)) > 1.0e-5)
        {
            FailScenario("initial-test-state-drift");
            return;
        }
        gMinimumNodeMass = 1.0e100;
        gMaximumAbsPosition = 0.0;
        gMaximumAbsVelocity = 0.0;
        if (!AuditActor(actor, "batch-boundary-step-0"))
            return;
        gAuditSamples = 1;
        gLastAuditedStep = 0;
        console.cVarSet("sim_deterministic_state_trace", "true");
        game.pushMessage(MSG_SIM_UNPAUSE_REQUESTED, {});
        gState = RUNNING_BATCH_BOUNDARY_INTERVAL;
        game.log(
            "[RoR|J3|Wheel2Spawn] ARMED actors=1 nodes=73 beams=392 " +
            "structural_nodes=9 rim_nodes=32 tire_nodes=32 " +
            "wheel_tire_nodes=32 contacters=32 ground_contact_nodes=73 " +
            "cab_triangles=0 collision_cab_triangles=0 first_step=0 " +
            "audit_stride=100 audit_samples_expected=201");
        return;
    }

    if (gState == RUNNING_BATCH_BOUNDARY_INTERVAL)
    {
        const uint64 completed = game.getCompletedPhysicsSteps();
        if (completed > EXPECTED_PHYSICS_STEPS)
        {
            FailScenario("physics-step-overshoot-" + completed);
            return;
        }
        BeamClass@ actor = game.getTruckByNum(gActorId);
        if (actor is null)
        {
            FailScenario("batch-boundary-actor-null-" + completed);
            return;
        }
        if (completed == gLastAuditedStep)
        {
            actor.wakeUp();
            return;
        }
        if (completed % EXPECTED_AUDIT_STRIDE != 0 ||
            completed != gLastAuditedStep + EXPECTED_AUDIT_STRIDE)
        {
            FailScenario("batch-boundary-stride-drift-" + completed);
            return;
        }
        if (!AuditActor(actor, "batch-boundary-step-" + completed))
            return;
        ++gAuditSamples;
        gLastAuditedStep = completed;
        if (gAuditSamples != int(completed / EXPECTED_AUDIT_STRIDE) + 1)
        {
            FailScenario("batch-boundary-count-drift-" + gAuditSamples);
            return;
        }
        const vector3 center = CenterOfMassPosition(actor);
        if (gState == FINISHED)
            return;
        if (double(center.y) < gMinimumCenterOfMassY)
            gMinimumCenterOfMassY = double(center.y);
        actor.wakeUp();

        if (completed == EXPECTED_PHYSICS_STEPS)
        {
            if (gSimState.getInt() != SIM_STATE_RUNNING)
            {
                FailScenario("simulation-not-running-at-completion");
                return;
            }
            const vector3 centerVelocity = CenterOfMassVelocity(actor);
            const double finalCenterSpeed = double(centerVelocity.length());
            const double finalMaximumNodeSpeed = MaximumNodeSpeed(actor);
            const double centerDrop =
                gInitialCenterOfMassY - gMinimumCenterOfMassY;
            const int brokenBeams = actor.getBrokenBeamCount();
            if (gState == FINISHED ||
                !IsFiniteComponent(centerDrop) || centerDrop < 0.0 ||
                centerDrop > 100.0 ||
                !IsFiniteComponent(finalCenterSpeed) ||
                finalCenterSpeed > 1.0e4 ||
                !IsFiniteComponent(finalMaximumNodeSpeed) ||
                finalMaximumNodeSpeed > 1.0e4 ||
                !IsFiniteComponent(gMinimumNodeMass) ||
                gMinimumNodeMass <= 0.0 || brokenBeams != 0 ||
                gAuditSamples != EXPECTED_AUDIT_SAMPLES)
            {
                FailScenario(
                    "batch-boundary-envelope-failed-center-drop-" +
                    formatFloat(centerDrop, "e", 0, 17) +
                    "-final-center-speed-" +
                    formatFloat(finalCenterSpeed, "e", 0, 17) +
                    "-final-node-speed-" +
                    formatFloat(finalMaximumNodeSpeed, "e", 0, 17) +
                    "-minimum-node-mass-" +
                    formatFloat(gMinimumNodeMass, "e", 0, 17) +
                    "-broken-" + brokenBeams + "-audit-samples-" +
                    gAuditSamples);
                return;
            }
            console.cVarSet("sim_deterministic_state_trace", "false");
            console.cVarSet("sim_deterministic_fixed_steps_per_frame", "0");
            actor.clearEventSimulatedValues();
            game.setTrucksForcedActive(false);
            gForcedActive = false;
            gState = FINISHED;
            game.log(
                "[RoR|J3|Wheel2Spawn] PASS actors=1 nodes=73 beams=392 " +
                "structural_nodes=9 rim_nodes=32 tire_nodes=32 " +
                "wheel_tire_nodes=32 contacters=32 ground_contact_nodes=73 " +
                "cab_triangles=0 collision_cab_triangles=0 steps=" +
                completed + " audit_stride=100 audit_samples=" +
                gAuditSamples + " total_mass=" +
                formatFloat(gInitialTotalMass, "e", 0, 17) +
                " minimum_node_mass=" +
                formatFloat(gMinimumNodeMass, "e", 0, 17) +
                " sampled_center_drop=" +
                formatFloat(centerDrop, "e", 0, 17) +
                " final_center_speed=" +
                formatFloat(finalCenterSpeed, "e", 0, 17) +
                " final_maximum_node_speed=" +
                formatFloat(finalMaximumNodeSpeed, "e", 0, 17) +
                " maximum_sampled_abs_position=" +
                formatFloat(gMaximumAbsPosition, "e", 0, 17) +
                " maximum_sampled_abs_velocity=" +
                formatFloat(gMaximumAbsVelocity, "e", 0, 17) +
                " broken_beams=" + brokenBeams +
                " pressure_volume=false friction=false braking=false " +
                "propulsion=false steering=false rolling=false " +
                "driveability=false source_parity=false playability=false " +
                "gravity_response=false contact_behavior=false " +
                "per_step_numeric_bounds=false");
            game.quitGame();
        }
    }
}
