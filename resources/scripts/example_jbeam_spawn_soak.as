/// \title authenticated JBeam J2 spawn and fixed-step soak
/// \brief Proves the product cache/import/spawn path and 120,000 finite steps.
///
/// Invoke through tools/run_jbeam_spawn_soak.py. The generated vehicle is a
/// clean-room structural fixture, not a BeamNG.drive behavior-parity claim.

const int APP_STATE_SIMULATION = 2;
const int SIM_STATE_RUNNING = 1;
const int SIM_STATE_PAUSED = 2;

const int EXPECTED_NODES = 6;
const int EXPECTED_RUNTIME_BEAMS = 16;
const int EXPECTED_CAB_TRIANGLES = 5;
const int EXPECTED_COLLISION_CAB_TRIANGLES = 5;
const int EXPECTED_CONTACTERS = 0;
const int EXPECTED_GROUND_CONTACT_NODES = 6;
const int EXPECTED_JBEAM_HYDROS = 1;
const int EXPECTED_JBEAM_SUPPORT_BEAMS = 1;
const double EXPECTED_NODE_MASS = 20.0;
const double EXPECTED_TOTAL_MASS = 120.0;
const double EXPECTED_COM_FROM_REFERENCE_SQUARED = 1.0 / 12.0;
const uint64 EXPECTED_PHYSICS_STEPS = 120000;
const string SCENARIO_ID = "2026082105";
const string VEHICLE = "ror_jbeam_spawn_fixture.jbeam";
const vector3 INITIAL_IMPACT_TRANSLATION(0.0f, 2.0f, 0.0f);
const vector3 INITIAL_IMPACT_VELOCITY(0.0f, -4.0f, 0.0f);

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
bool gForcedActive = false;
int64 gActorId = -1;
double gMaxAbsPosition = 0.0;
double gMaxAbsVelocity = 0.0;
double gInitialCenterOfMassY = 0.0;
double gMinimumCenterOfMassY = 0.0;
double gPeakCenterOfMassSpeed = 0.0;
double gMinimumHydroLengthRatio = 1.0;
double gMaximumHydroLengthRatio = 1.0;
bool gObservedTerrainImpactResponse = false;
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
    game.log("[RoR|J2|SpawnSoak] FAIL reason=" + reason);
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

bool SetSteeringForStep(BeamClass@ actor, uint64 completed)
{
    const bool left = ((completed / 1000) % 2) == 0;
    const float command = left ? -0.35f : 0.35f;
    if (!actor.trySetJBeamHydroSteeringCommand(command))
    {
        FailScenario("native-hydro-steering-command-rejected");
        return false;
    }
    return true;
}

bool AuditActor(BeamClass@ actor, const string &in phase, uint64 completed)
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
    if (actor.getBeamCount() != EXPECTED_RUNTIME_BEAMS)
    {
        FailScenario(phase + "-beam-count-drift-" + actor.getBeamCount());
        return false;
    }
    if (actor.getCabTriangleCount() != EXPECTED_CAB_TRIANGLES)
    {
        FailScenario(
            phase + "-cab-triangle-count-drift-" +
            actor.getCabTriangleCount());
        return false;
    }
    if (actor.getCollisionCabTriangleCount() !=
        EXPECTED_COLLISION_CAB_TRIANGLES)
    {
        FailScenario(
            phase + "-collision-cab-triangle-count-drift-" +
            actor.getCollisionCabTriangleCount());
        return false;
    }
    if (actor.getContacterCount() != EXPECTED_CONTACTERS)
    {
        FailScenario(
            phase + "-contacter-count-drift-" +
            actor.getContacterCount());
        return false;
    }
    if (actor.getGroundContactEnabledNodeCount() !=
        EXPECTED_GROUND_CONTACT_NODES)
    {
        FailScenario(
            phase + "-ground-contact-node-count-drift-" +
            actor.getGroundContactEnabledNodeCount());
        return false;
    }
    if (actor.getJBeamHydroRuntimeCount() != EXPECTED_JBEAM_HYDROS)
    {
        FailScenario(
            phase + "-hydro-count-drift-" +
            actor.getJBeamHydroRuntimeCount());
        return false;
    }
    if (actor.getJBeamHydroRuntimeFaultCount() != 0 ||
        !actor.hasFiniteJBeamHydroRuntimeState())
    {
        FailScenario(
            phase + "-hydro-fault-or-nonfinite-" +
            actor.getJBeamHydroRuntimeFaultCount());
        return false;
    }
    if (actor.getJBeamHydroMinimumAcceptedStepCount() != completed ||
        actor.getJBeamHydroMaximumAcceptedStepCount() != completed)
    {
        FailScenario(
            phase + "-hydro-step-mismatch-" +
            actor.getJBeamHydroMinimumAcceptedStepCount() + "-" +
            actor.getJBeamHydroMaximumAcceptedStepCount() + "-" +
            completed);
        return false;
    }
    const double minimumHydroRatio =
        actor.getJBeamHydroMinimumLengthRatio();
    const double maximumHydroRatio =
        actor.getJBeamHydroMaximumLengthRatio();
    if (!IsFiniteComponent(minimumHydroRatio) ||
        !IsFiniteComponent(maximumHydroRatio) ||
        minimumHydroRatio <= 0.0 ||
        maximumHydroRatio < minimumHydroRatio)
    {
        FailScenario(
            phase + "-hydro-ratio-invalid-" +
            formatFloat(minimumHydroRatio, "e", 0, 17) + "-" +
            formatFloat(maximumHydroRatio, "e", 0, 17));
        return false;
    }
    if (minimumHydroRatio < gMinimumHydroLengthRatio)
        gMinimumHydroLengthRatio = minimumHydroRatio;
    if (maximumHydroRatio > gMaximumHydroLengthRatio)
        gMaximumHydroLengthRatio = maximumHydroRatio;
    if (actor.getJBeamSupportRuntimeCount() !=
            EXPECTED_JBEAM_SUPPORT_BEAMS ||
        actor.getJBeamSupportRuntimeFaultCount() != 0 ||
        !actor.hasFiniteJBeamSupportRuntimeState())
    {
        FailScenario(
            phase + "-support-count-fault-or-nonfinite-" +
            actor.getJBeamSupportRuntimeCount() + "-" +
            actor.getJBeamSupportRuntimeFaultCount());
        return false;
    }
    if (actor.getJBeamSupportMinimumAcceptedStepCount() != completed ||
        actor.getJBeamSupportMaximumAcceptedStepCount() != completed)
    {
        FailScenario(
            phase + "-support-step-mismatch-" +
            actor.getJBeamSupportMinimumAcceptedStepCount() + "-" +
            actor.getJBeamSupportMaximumAcceptedStepCount() + "-" +
            completed);
        return false;
    }

    for (int index = 0; index < actor.getNodeCount(); ++index)
    {
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
            FailScenario(phase + "-nonfinite-node-" + index);
            return false;
        }
        const double positionMaximum = Maximum3(
            double(position.x), double(position.y), double(position.z));
        const double velocityMaximum = Maximum3(
            double(velocity.x), double(velocity.y), double(velocity.z));
        if (positionMaximum > 1.0e7 || velocityMaximum > 1.0e7)
        {
            FailScenario(phase + "-node-domain-" + index);
            return false;
        }
        if (positionMaximum > gMaxAbsPosition)
            gMaxAbsPosition = positionMaximum;
        if (velocityMaximum > gMaxAbsVelocity)
            gMaxAbsVelocity = velocityMaximum;
    }
    return true;
}

bool AuditInitialMassDistribution(BeamClass@ actor)
{
    double accumulatedMass = 0.0;
    double weightedX = 0.0;
    double weightedY = 0.0;
    double weightedZ = 0.0;
    for (int index = 0; index < actor.getNodeCount(); ++index)
    {
        const double mass = double(actor.getNodeMass(index));
        if (abs(mass - EXPECTED_NODE_MASS) > 1.0e-6)
        {
            FailScenario("arm-authored-node-mass-drift-" + index);
            return false;
        }
        const vector3 position = actor.getNodePosition(index);
        accumulatedMass += mass;
        weightedX += double(position.x) * mass;
        weightedY += double(position.y) * mass;
        weightedZ += double(position.z) * mass;
    }
    const double actorMass = double(actor.getTotalMass(false));
    if (abs(accumulatedMass - EXPECTED_TOTAL_MASS) > 1.0e-6 ||
        abs(actorMass - EXPECTED_TOTAL_MASS) > 1.0e-5)
    {
        FailScenario("arm-authored-total-mass-drift");
        return false;
    }

    const vector3 reference = actor.getNodePosition(0);
    const double dx = weightedX / accumulatedMass - double(reference.x);
    const double dy = weightedY / accumulatedMass - double(reference.y);
    const double dz = weightedZ / accumulatedMass - double(reference.z);
    const double centerDistanceSquared = dx * dx + dy * dy + dz * dz;
    if (!IsFiniteComponent(centerDistanceSquared) ||
        abs(centerDistanceSquared - EXPECTED_COM_FROM_REFERENCE_SQUARED) >
            2.0e-4)
    {
        FailScenario("arm-center-of-mass-drift");
        return false;
    }
    return true;
}

vector3 CenterOfMassPosition(BeamClass@ actor)
{
    vector3 weightedPosition(0.0f, 0.0f, 0.0f);
    double totalMass = 0.0;
    for (int index = 0; index < actor.getNodeCount(); ++index)
    {
        const float mass = actor.getNodeMass(index);
        weightedPosition += actor.getNodePosition(index) * mass;
        totalMass += double(mass);
    }
    if (!IsFiniteComponent(totalMass) || totalMass <= 0.0)
    {
        FailScenario("center-of-mass-position-invalid-total-mass");
        return vector3();
    }
    return weightedPosition / float(totalMass);
}

vector3 CenterOfMassVelocity(BeamClass@ actor)
{
    vector3 weightedVelocity(0.0f, 0.0f, 0.0f);
    double totalMass = 0.0;
    for (int index = 0; index < actor.getNodeCount(); ++index)
    {
        const float mass = actor.getNodeMass(index);
        weightedVelocity += actor.getNodeVelocity(index) * mass;
        totalMass += double(mass);
    }
    if (!IsFiniteComponent(totalMass) || totalMass <= 0.0)
    {
        FailScenario("center-of-mass-velocity-invalid-total-mass");
        return vector3();
    }
    return weightedVelocity / float(totalMass);
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
        "sim_deterministic_state_trace_scenario_id", SCENARIO_ID);
    console.cVarSet(
        "sim_deterministic_state_trace_step_limit",
        "" + EXPECTED_PHYSICS_STEPS);
    console.cVarSet("sim_no_collisions", "false");
    console.cVarSet("sim_no_self_collisions", "false");
    game.registerForEvent(SE_GENERIC_NEW_TRUCK);
    game.log(
        "[RoR|J2|SpawnSoak] START scenario=" + SCENARIO_ID +
        " vehicle=" + VEHICLE + " steps=" + EXPECTED_PHYSICS_STEPS +
        " impact_translation_y=2 impact_velocity_y=-4");
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
        if (!AuditActor(actor, "arm", 0))
            return;
        if (!AuditInitialMassDistribution(actor))
            return;
        const double spawnCenterOfMassY =
            double(CenterOfMassPosition(actor).y);
        if (gState == FINISHED)
            return;

        // This is an actuator-runtime gate, so keep the fixture scheduled for
        // every requested fixed step instead of allowing the normal ten-second
        // inactive-vehicle sleep policy to end the measurement early. Wake it
        // before applying the paused deterministic impact initial condition.
        game.setTrucksForcedActive(true);
        gForcedActive = true;
        actor.wakeUp();
        if (!actor.trySetDeterministicImpactPlacementAndVelocity(
                INITIAL_IMPACT_TRANSLATION,
                INITIAL_IMPACT_VELOCITY))
        {
            FailScenario("initial-impact-placement-or-velocity-rejected");
            return;
        }
        gInitialCenterOfMassY = double(CenterOfMassPosition(actor).y);
        gMinimumCenterOfMassY = gInitialCenterOfMassY;
        if (gState == FINISHED ||
            abs((gInitialCenterOfMassY - spawnCenterOfMassY) - 2.0) >
                1.0e-5)
        {
            FailScenario("initial-impact-placement-drift");
            return;
        }
        const vector3 armedVelocity = CenterOfMassVelocity(actor);
        if (gState == FINISHED ||
            abs(double(armedVelocity.x)) > 1.0e-5 ||
            abs(double(armedVelocity.y) + 4.0) > 1.0e-5 ||
            abs(double(armedVelocity.z)) > 1.0e-5)
        {
            FailScenario("initial-impact-velocity-drift");
            return;
        }
        gPeakCenterOfMassSpeed = double(armedVelocity.length());

        if (!SetSteeringForStep(actor, 0))
            return;
        console.cVarSet("sim_deterministic_state_trace", "true");
        game.pushMessage(MSG_SIM_UNPAUSE_REQUESTED, {});
        gState = RUNNING_SOAK;
        game.log(
            "[RoR|J2|SpawnSoak] ARMED actors=1 nodes=6 beams=16 "
            "cab_triangles=5 collision_cab_triangles=5 contacters=0 "
            "ground_contact_nodes=6 "
            "hydros=1 support_beams=1 total_mass=120 "
            "translation_y=2 first_step=0 "
            "batch=100");
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
        if (!AuditActor(actor, "step-" + completed, completed))
            return;
        const vector3 center = CenterOfMassPosition(actor);
        const vector3 centerVelocity = CenterOfMassVelocity(actor);
        if (gState == FINISHED)
            return;
        if (double(center.y) < gMinimumCenterOfMassY)
            gMinimumCenterOfMassY = double(center.y);
        const double centerSpeed = double(centerVelocity.length());
        if (centerSpeed > gPeakCenterOfMassSpeed)
            gPeakCenterOfMassSpeed = centerSpeed;
        // Gravity can only make the armed -4 m/s velocity more negative.
        // A later value above -3 m/s therefore proves an upward terrain-contact
        // impulse occurred on the product physics path.
        if (completed > 0 && double(centerVelocity.y) > -3.0)
            gObservedTerrainImpactResponse = true;
        actor.wakeUp();
        if (!SetSteeringForStep(actor, completed))
            return;

        if (completed == EXPECTED_PHYSICS_STEPS)
        {
            if (gSimState.getInt() != SIM_STATE_RUNNING)
            {
                FailScenario("simulation-not-running-at-completion");
                return;
            }
            const double minimumDrop =
                gInitialCenterOfMassY - gMinimumCenterOfMassY;
            const int brokenBeams = actor.getBrokenBeamCount();
            const uint64 supportCompressionSteps =
                actor.getJBeamSupportMinimumCompressionStepCount();
            if (!gObservedTerrainImpactResponse ||
                !IsFiniteComponent(minimumDrop) || minimumDrop <= 1.0 ||
                !IsFiniteComponent(gPeakCenterOfMassSpeed) ||
                gPeakCenterOfMassSpeed < 4.0 || brokenBeams != 0 ||
                supportCompressionSteps == 0 ||
                gMinimumHydroLengthRatio >= 0.995 ||
                gMaximumHydroLengthRatio <= 1.005 ||
                gMinimumHydroLengthRatio <= 0.98 ||
                gMaximumHydroLengthRatio >= 1.02)
            {
                FailScenario(
                    "terrain-impact-response-incomplete-observed-" +
                    gObservedTerrainImpactResponse + "-initial-y-" +
                    formatFloat(gInitialCenterOfMassY, "e", 0, 17) +
                    "-minimum-y-" +
                    formatFloat(gMinimumCenterOfMassY, "e", 0, 17) +
                    "-final-y-" +
                    formatFloat(double(center.y), "e", 0, 17) +
                    "-final-vy-" +
                    formatFloat(double(centerVelocity.y), "e", 0, 17) +
                    "-peak-speed-" +
                    formatFloat(gPeakCenterOfMassSpeed, "e", 0, 17) +
                    "-broken-" + brokenBeams +
                    "-support-compression-" + supportCompressionSteps +
                    "-hydro-min-ratio-" +
                    formatFloat(gMinimumHydroLengthRatio, "e", 0, 17) +
                    "-hydro-max-ratio-" +
                    formatFloat(gMaximumHydroLengthRatio, "e", 0, 17));
                return;
            }
            console.cVarSet("sim_deterministic_state_trace", "false");
            console.cVarSet("sim_deterministic_fixed_steps_per_frame", "0");
            actor.clearEventSimulatedValues();
            game.setTrucksForcedActive(false);
            gForcedActive = false;
            gState = FINISHED;
            game.log(
                "[RoR|J2|SpawnSoak] PASS actors=1 nodes=6 beams=16 "
                "cab_triangles=5 collision_cab_triangles=5 "
                "contacters=0 ground_contact_nodes=6 "
                "hydros=1 support_beams=1 total_mass=120 steps=" +
                completed + " hydro_steps=" +
                actor.getJBeamHydroMinimumAcceptedStepCount() +
                " support_steps=" +
                actor.getJBeamSupportMinimumAcceptedStepCount() +
                " support_compression_steps=" +
                supportCompressionSteps +
                " hydro_min_ratio=" +
                formatFloat(gMinimumHydroLengthRatio, "e", 0, 17) +
                " hydro_max_ratio=" +
                formatFloat(gMaximumHydroLengthRatio, "e", 0, 17) +
                " max_abs_position=" +
                formatFloat(gMaxAbsPosition, "e", 0, 17) +
                " max_abs_velocity=" +
                formatFloat(gMaxAbsVelocity, "e", 0, 17) +
                " minimum_com_drop=" +
                formatFloat(minimumDrop, "e", 0, 17) +
                " peak_com_speed=" +
                formatFloat(gPeakCenterOfMassSpeed, "e", 0, 17) +
                " broken_beams=" + brokenBeams);
            game.quitGame();
        }
    }
}
