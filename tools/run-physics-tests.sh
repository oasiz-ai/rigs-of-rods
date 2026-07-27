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

"${physics_test_compiler}" \
    -std=c++11 \
    -Wall \
    -Wextra \
    -Werror \
    -pedantic \
    -I"${repository_dir}/source/main/physics" \
    "${repository_dir}/tests/physics/BeamAxialResponseTests.cpp" \
    -o "${test_build_dir}/beam_axial_response_tests"

"${test_build_dir}/beam_axial_response_tests"
