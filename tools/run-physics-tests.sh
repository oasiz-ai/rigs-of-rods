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

common_test_flags=(
    -std=c++11
    -Wall
    -Wextra
    -Werror
    -pedantic
    -I"${repository_dir}/source/main/physics"
)

"${physics_test_compiler}" \
    "${common_test_flags[@]}" \
    "${repository_dir}/tests/physics/BeamAxialResponseTests.cpp" \
    -o "${test_build_dir}/beam_axial_response_tests"

"${physics_test_compiler}" \
    "${common_test_flags[@]}" \
    -pthread \
    "${repository_dir}/tests/physics/DeterministicCounterNoiseTests.cpp" \
    -o "${test_build_dir}/deterministic_counter_noise_tests"

"${physics_test_compiler}" \
    "${common_test_flags[@]}" \
    "${repository_dir}/tests/physics/DeterministicContactOrderTests.cpp" \
    -o "${test_build_dir}/deterministic_contact_order_tests"

"${physics_test_compiler}" \
    "${common_test_flags[@]}" \
    "${repository_dir}/tests/physics/CalibratedBeamMaterialTests.cpp" \
    -o "${test_build_dir}/calibrated_beam_material_tests"

"${test_build_dir}/beam_axial_response_tests"
"${test_build_dir}/deterministic_counter_noise_tests"
"${test_build_dir}/deterministic_contact_order_tests"
"${test_build_dir}/calibrated_beam_material_tests"
