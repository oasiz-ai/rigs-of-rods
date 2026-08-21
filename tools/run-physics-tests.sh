#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repository_dir="$(cd -- "${script_dir}/.." && pwd)"
test_build_dir="$(mktemp -d)"

cleanup() {
    rm -rf -- "${test_build_dir}"
}
trap cleanup EXIT

physics_test_compiler="${CXX:-c++}"
physics_test_repeat="${ROR_PHYSICS_TEST_REPEAT:-1}"
physics_test_fast_math="${ROR_PHYSICS_TEST_FAST_MATH:-0}"

if [[ ! "${physics_test_repeat}" =~ ^[1-9][0-9]*$ ]]; then
    echo "ROR_PHYSICS_TEST_REPEAT must be a positive integer" >&2
    exit 2
fi

if [[ "${physics_test_fast_math}" != "0" &&
      "${physics_test_fast_math}" != "1" ]]; then
    echo "ROR_PHYSICS_TEST_FAST_MATH must be 0 or 1" >&2
    exit 2
fi

common_test_flags=(
    -std=c++11
    -Wall
    -Wextra
    -Werror
    -pedantic
    -I"${repository_dir}/source/main/physics"
)

if [[ "${physics_test_fast_math}" == "1" ]]; then
    common_test_flags+=(-ffast-math)
fi

"${physics_test_compiler}" \
    "${common_test_flags[@]}" \
    -fno-fast-math \
    -ffp-contract=off \
    -fno-lto \
    -c "${repository_dir}/source/main/physics/BeamAxialKinematics.cpp" \
    -o "${test_build_dir}/beam_axial_kinematics.o"

"${physics_test_compiler}" \
    "${common_test_flags[@]}" \
    -fno-fast-math \
    -ffp-contract=off \
    -fno-lto \
    -c "${repository_dir}/source/main/physics/CalibratedBeamProductionStep.cpp" \
    -o "${test_build_dir}/calibrated_beam_production_step.o"

"${physics_test_compiler}" \
    "${common_test_flags[@]}" \
    "${repository_dir}/tests/physics/BeamAxialKinematicsTests.cpp" \
    "${test_build_dir}/beam_axial_kinematics.o" \
    "${test_build_dir}/calibrated_beam_production_step.o" \
    -o "${test_build_dir}/beam_axial_kinematics_tests"

"${physics_test_compiler}" \
    "${common_test_flags[@]}" \
    "${repository_dir}/tests/physics/BeamAxialResponseTests.cpp" \
    -o "${test_build_dir}/beam_axial_response_tests"

"${physics_test_compiler}" \
    "${common_test_flags[@]}" \
    "${repository_dir}/tests/physics/BeamRestLengthScaleTests.cpp" \
    -o "${test_build_dir}/beam_rest_length_scale_tests"

"${physics_test_compiler}" \
    "${common_test_flags[@]}" \
    -pthread \
    "${repository_dir}/tests/physics/DeterministicCounterNoiseTests.cpp" \
    -o "${test_build_dir}/deterministic_counter_noise_tests"

"${physics_test_compiler}" \
    "${common_test_flags[@]}" \
    "${repository_dir}/tests/physics/DeterministicFixedStepCadenceTests.cpp" \
    -o "${test_build_dir}/deterministic_fixed_step_cadence_tests"

"${physics_test_compiler}" \
    "${common_test_flags[@]}" \
    "${repository_dir}/tests/physics/DeterministicScenarioScheduleTests.cpp" \
    -o "${test_build_dir}/deterministic_scenario_schedule_tests"

"${physics_test_compiler}" \
    "${common_test_flags[@]}" \
    "${repository_dir}/tests/physics/DeterministicImpactInitialConditionTests.cpp" \
    -o "${test_build_dir}/deterministic_impact_initial_condition_tests"

"${physics_test_compiler}" \
    "${common_test_flags[@]}" \
    "${repository_dir}/tests/physics/DeterministicContactOrderTests.cpp" \
    -o "${test_build_dir}/deterministic_contact_order_tests"

"${physics_test_compiler}" \
    "${common_test_flags[@]}" \
    "${repository_dir}/tests/physics/CalibratedBeamMaterialTests.cpp" \
    -o "${test_build_dir}/calibrated_beam_material_tests"

"${physics_test_compiler}" \
    "${common_test_flags[@]}" \
    "${repository_dir}/tests/physics/CalibratedBeamMaterialAdapterTests.cpp" \
    -o "${test_build_dir}/calibrated_beam_material_adapter_tests"

"${physics_test_compiler}" \
    "${common_test_flags[@]}" \
    "${repository_dir}/tests/physics/CalibratedBeamStepSensitivityTests.cpp" \
    -o "${test_build_dir}/calibrated_beam_step_sensitivity_tests"

"${physics_test_compiler}" \
    "${common_test_flags[@]}" \
    "${repository_dir}/tests/physics/CalibratedBeamMeshRefinementTests.cpp" \
    -o "${test_build_dir}/calibrated_beam_mesh_refinement_tests"

"${physics_test_compiler}" \
    "${common_test_flags[@]}" \
    "${repository_dir}/tests/physics/CalibratedBeamSavegameTests.cpp" \
    -o "${test_build_dir}/calibrated_beam_savegame_tests"

"${physics_test_compiler}" \
    "${common_test_flags[@]}" \
    "${repository_dir}/tests/physics/DeterministicStateDigestTests.cpp" \
    "${repository_dir}/source/main/physics/DeterministicStateDigest.cpp" \
    -o "${test_build_dir}/deterministic_state_digest_tests"

"${physics_test_compiler}" \
    "${common_test_flags[@]}" \
    "${repository_dir}/tests/physics/DeterministicStateTraceTests.cpp" \
    "${repository_dir}/source/main/physics/DeterministicStateDigest.cpp" \
    "${repository_dir}/source/main/physics/DeterministicStateTrace.cpp" \
    "${repository_dir}/source/main/physics/DeterministicStateTraceCli.cpp" \
    -o "${test_build_dir}/deterministic_state_trace_tests"

"${physics_test_compiler}" \
    "${common_test_flags[@]}" \
    "${repository_dir}/tests/physics/DeterministicInputTraceTests.cpp" \
    "${repository_dir}/source/main/physics/DeterministicInputTrace.cpp" \
    -o "${test_build_dir}/deterministic_input_trace_tests"

"${physics_test_compiler}" \
    "${common_test_flags[@]}" \
    "${repository_dir}/tests/physics/DeterministicInputTraceRuntimeTests.cpp" \
    "${repository_dir}/source/main/physics/DeterministicInputTrace.cpp" \
    "${repository_dir}/source/main/physics/DeterministicInputTraceRuntime.cpp" \
    -o "${test_build_dir}/deterministic_input_trace_runtime_tests"

"${physics_test_compiler}" \
    "${common_test_flags[@]}" \
    "${repository_dir}/tests/physics/DeterministicVehicleInputTests.cpp" \
    "${repository_dir}/source/main/physics/DeterministicInputTrace.cpp" \
    "${repository_dir}/source/main/physics/DeterministicInputTraceRuntime.cpp" \
    "${repository_dir}/source/main/physics/DeterministicVehicleInput.cpp" \
    -o "${test_build_dir}/deterministic_vehicle_input_tests"

"${physics_test_compiler}" \
    "${common_test_flags[@]}" \
    "${repository_dir}/tools/ror_state_trace.cpp" \
    "${repository_dir}/source/main/physics/DeterministicStateDigest.cpp" \
    "${repository_dir}/source/main/physics/DeterministicStateTrace.cpp" \
    "${repository_dir}/source/main/physics/DeterministicStateTraceCli.cpp" \
    -o "${test_build_dir}/ror_state_trace"

"${physics_test_compiler}" \
    "${common_test_flags[@]}" \
    "${repository_dir}/tests/physics/HydroActuatorResponseTests.cpp" \
    -o "${test_build_dir}/hydro_actuator_response_tests"

physics_test_executables=(
    beam_axial_kinematics_tests
    beam_axial_response_tests
    beam_rest_length_scale_tests
    deterministic_counter_noise_tests
    deterministic_fixed_step_cadence_tests
    deterministic_scenario_schedule_tests
    deterministic_impact_initial_condition_tests
    deterministic_contact_order_tests
    deterministic_input_trace_tests
    deterministic_input_trace_runtime_tests
    deterministic_vehicle_input_tests
    deterministic_state_digest_tests
    deterministic_state_trace_tests
    calibrated_beam_material_adapter_tests
    calibrated_beam_material_tests
    calibrated_beam_mesh_refinement_tests
    calibrated_beam_step_sensitivity_tests
    calibrated_beam_savegame_tests
    hydro_actuator_response_tests
)

for ((run = 1; run <= physics_test_repeat; ++run)); do
    for test_executable in "${physics_test_executables[@]}"; do
        "${test_build_dir}/${test_executable}"
    done
done

"${test_build_dir}/ror_state_trace" --help >/dev/null

echo "physics kernel suite passed ${physics_test_repeat} time(s)"
