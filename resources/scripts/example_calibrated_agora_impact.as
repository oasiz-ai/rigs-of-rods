/// \title calibrated-beam P1 Agora impact regression
/// \brief Measures one fixed-speed full-vehicle impact at exact solver steps.
///
/// Invoke through tools/run_agora_impact_regression.py. The derived material is
/// a numerical regression fixture, not a claim of physical Agora calibration.

const int APP_STATE_SIMULATION = 2;
const int SIM_STATE_RUNNING = 1;
const int SIM_STATE_PAUSED = 2;

const int EXPECTED_NODES = 297;
const int AUTHORED_NODES = 151;
const int EXPECTED_CALIBRATED_BEAMS = 675;
const uint64 EXPECTED_PHYSICS_STEPS = 6000;
const double FIXED_STEP_SECONDS = 0.0005;
const string SCENARIO_ID = "2026082001";
const string VEHICLE = "P1CalibratedAgoraImpact.truck";
const vector3 INITIAL_VELOCITY(0.0f, -12.0f, 0.0f);

enum ScenarioState
{
    WAITING_FOR_TERRAIN = 0,
    WAITING_FOR_PAUSE,
    WAITING_FOR_ACTOR,
    RUNNING_IMPACT,
    FINISHED
}

ScenarioState gState = WAITING_FOR_TERRAIN;
bool gActorSpawned = false;
int64 gActorId = -1;
CVarClass@ gAppState;
CVarClass@ gSimState;
array<float> gInitialPairDistances;
vector3 gPreviousComVelocity;
uint64 gLastCompletedStep = 0;
double gInitialMechanicalEnergy = 0.0;
double gPeakDeceleration = 0.0;
float gGravity = 0.0f;

void FailScenario(const string &in reason)
{
    if (gState == FINISHED)
        return;
    gState = FINISHED;
    console.cVarSet("sim_deterministic_state_trace", "false");
    console.cVarSet("sim_deterministic_fixed_steps_per_frame", "0");
    game.log("[RoR|P1|AgoraImpact] FAIL reason=" + reason);
    game.quitGame();
}

bool IsFiniteMetric(double value)
{
    return value == value && value > -1.0e300 && value < 1.0e300;
}

string FormatMetric(double value)
{
    return formatFloat(value, "e", 0, 17);
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
    if (actor.getCalibratedBeamFaultCount() != 0)
    {
        FailScenario(
            phase + "-calibrated-faults-" +
            actor.getCalibratedBeamFaultCount());
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
    return true;
}

vector3 CenterOfMassVelocity(BeamClass@ actor)
{
    vector3 weightedVelocity(0.0f, 0.0f, 0.0f);
    double totalMass = 0.0;
    for (int i = 0; i < actor.getNodeCount(); ++i)
    {
        const float mass = actor.getNodeMass(i);
        weightedVelocity += actor.getNodeVelocity(i) * mass;
        totalMass += double(mass);
    }
    if (!IsFiniteMetric(totalMass) || totalMass <= 0.0)
    {
        FailScenario("center-of-mass-invalid-total-mass");
        return vector3();
    }
    return weightedVelocity / float(totalMass);
}

double MechanicalEnergy(BeamClass@ actor)
{
    double kinetic = 0.0;
    double potential = 0.0;
    for (int i = 0; i < actor.getNodeCount(); ++i)
    {
        const double mass = double(actor.getNodeMass(i));
        const vector3 velocity = actor.getNodeVelocity(i);
        const vector3 position = actor.getNodePosition(i);
        kinetic += 0.5 * mass * double(velocity.squaredLength());
        potential += -double(gGravity) * mass * double(position.y);
    }
    const double total = kinetic + potential;
    if (!IsFiniteMetric(total))
    {
        FailScenario("nonfinite-mechanical-energy");
        return 0.0;
    }
    return total;
}

bool CaptureInitialShape(BeamClass@ actor)
{
    const int expectedPairs = AUTHORED_NODES * (AUTHORED_NODES - 1) / 2;
    gInitialPairDistances.resize(expectedPairs);
    int pair = 0;
    for (int i = 0; i < AUTHORED_NODES; ++i)
    {
        const vector3 first = actor.getNodePosition(i);
        for (int j = i + 1; j < AUTHORED_NODES; ++j)
        {
            const float distance = first.distance(actor.getNodePosition(j));
            if (distance != distance || distance < 0.0f)
            {
                FailScenario("invalid-initial-pair-distance-" + pair);
                return false;
            }
            gInitialPairDistances[pair++] = distance;
        }
    }
    if (pair != expectedPairs)
    {
        FailScenario("initial-pair-count-drift-" + pair);
        return false;
    }
    return true;
}

bool MeasurePermanentShape(
    BeamClass@ actor,
    float &out rms,
    float &out maximum)
{
    double squaredSum = 0.0;
    maximum = 0.0f;
    int pair = 0;
    for (int i = 0; i < AUTHORED_NODES; ++i)
    {
        const vector3 first = actor.getNodePosition(i);
        for (int j = i + 1; j < AUTHORED_NODES; ++j)
        {
            const float distance = first.distance(actor.getNodePosition(j));
            const float delta = distance - gInitialPairDistances[pair++];
            const float magnitude = abs(delta);
            squaredSum += double(delta) * double(delta);
            if (magnitude > maximum)
                maximum = magnitude;
        }
    }
    if (pair == 0 || pair != int(gInitialPairDistances.length()) ||
        !IsFiniteMetric(squaredSum))
    {
        FailScenario("invalid-final-pair-census-" + pair);
        return false;
    }
    rms = sqrt(float(squaredSum / double(pair)));
    if (rms != rms || maximum != maximum)
    {
        FailScenario("nonfinite-permanent-deformation");
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

    console.cVarSet("sim_deterministic_fixed_steps_per_frame", "1");
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
        "[RoR|P1|AgoraImpact] START scenario=" + SCENARIO_ID +
        " vehicle=" + VEHICLE + " steps=" + EXPECTED_PHYSICS_STEPS +
        " velocity=0,-12,0");
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
        if (!CaptureInitialShape(actor))
            return;
        if (!actor.trySetDeterministicImpactVelocity(INITIAL_VELOCITY))
        {
            FailScenario("initial-velocity-rejected");
            return;
        }
        gGravity = game.getGravity();
        if (gGravity >= 0.0f)
        {
            FailScenario("invalid-gravity-" + gGravity);
            return;
        }
        gPreviousComVelocity = CenterOfMassVelocity(actor);
        gInitialMechanicalEnergy = MechanicalEnergy(actor);
        if (gState == FINISHED)
            return;

        console.cVarSet("sim_deterministic_state_trace", "true");
        game.pushMessage(MSG_SIM_UNPAUSE_REQUESTED, {});
        gState = RUNNING_IMPACT;
        game.log(
            "[RoR|P1|AgoraImpact] ARMED actors=1 nodes=297 "
            "calibrated_beams=675 first_step=0 batch=1");
        return;
    }

    if (gState == RUNNING_IMPACT)
    {
        const uint64 completed = game.getCompletedPhysicsSteps();
        if (completed == gLastCompletedStep)
            return;
        if (completed != gLastCompletedStep + 1)
        {
            FailScenario(
                "physics-step-discontinuity-" + gLastCompletedStep + "-" +
                completed);
            return;
        }
        if (completed > EXPECTED_PHYSICS_STEPS)
        {
            FailScenario("physics-step-overshoot-" + completed);
            return;
        }

        BeamClass@ actor = game.getTruckByNum(gActorId);
        if (!AuditActor(actor, "step-" + completed))
            return;
        const vector3 currentComVelocity = CenterOfMassVelocity(actor);
        const double acceleration =
            double((currentComVelocity - gPreviousComVelocity).length()) /
            FIXED_STEP_SECONDS;
        if (!IsFiniteMetric(acceleration))
        {
            FailScenario("nonfinite-deceleration-" + completed);
            return;
        }
        if (acceleration > gPeakDeceleration)
            gPeakDeceleration = acceleration;
        gPreviousComVelocity = currentComVelocity;
        gLastCompletedStep = completed;

        if (completed == EXPECTED_PHYSICS_STEPS)
        {
            if (gSimState.getInt() != SIM_STATE_RUNNING)
            {
                FailScenario("simulation-not-running-at-completion");
                return;
            }
            const double finalEnergy = MechanicalEnergy(actor);
            const double absorbedEnergy =
                gInitialMechanicalEnergy - finalEnergy;
            float permanentRms = 0.0f;
            float permanentMaximum = 0.0f;
            if (!MeasurePermanentShape(actor, permanentRms, permanentMaximum))
                return;
            const int broken = actor.getBrokenBeamCount();
            const int fractures = actor.getCalibratedBeamFractureCount();
            const int disabled = actor.getCalibratedBeamDisabledCount();
            if (!IsFiniteMetric(absorbedEnergy) ||
                gPeakDeceleration <= 0.0 ||
                absorbedEnergy <= 0.0 ||
                permanentRms <= 0.0f ||
                permanentMaximum <= 0.0f ||
                broken <= 0 || fractures <= 0 || disabled < fractures)
            {
                FailScenario("impact-did-not-produce-complete-response");
                return;
            }

            console.cVarSet("sim_deterministic_state_trace", "false");
            console.cVarSet("sim_deterministic_fixed_steps_per_frame", "0");
            gState = FINISHED;
            game.log(
                "[RoR|P1|AgoraImpact] PASS actors=1 nodes=297 "
                "calibrated_beams=675 steps=" + completed +
                " peak_deceleration=" + FormatMetric(gPeakDeceleration) +
                " initial_energy=" + FormatMetric(gInitialMechanicalEnergy) +
                " final_energy=" + FormatMetric(finalEnergy) +
                " absorbed_energy=" + FormatMetric(absorbedEnergy) +
                " permanent_rms=" + FormatMetric(permanentRms) +
                " permanent_max=" + FormatMetric(permanentMaximum) +
                " broken_beams=" + broken +
                " fractures=" + fractures +
                " disabled=" + disabled +
                " final_com_speed=" +
                FormatMetric(currentComVelocity.length()));
            game.quitGame();
        }
    }
}
