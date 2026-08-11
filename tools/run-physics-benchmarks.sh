#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repository_dir="$(cd -- "${script_dir}/.." && pwd)"
benchmark_build_dir="$(mktemp -d)"

cleanup() {
    rm -rf -- "${benchmark_build_dir}"
}
trap cleanup EXIT

benchmark_compiler="${CXX:-c++}"
benchmark_commit="$(git -C "${repository_dir}" rev-parse HEAD)"
benchmark_flags="-O2 -fno-fast-math -ffp-contract=off -fno-lto"
output_path="${1:-}"
benchmark_sources=(
    "source/main/physics/BeamAxialKinematics.h"
    "source/main/physics/BeamAxialKinematics.cpp"
    "source/main/physics/BeamAxialResponse.h"
    "tools/ror_beam_axial_benchmark.cpp"
    "tools/run-physics-benchmarks.sh"
)
benchmark_source_state="clean"
if [[ -n "$(git -C "${repository_dir}" status --porcelain -- "${benchmark_sources[@]}")" ]]; then
    benchmark_source_state="dirty"
fi
benchmark_source_manifest_sha256="$(
    for source_path in "${benchmark_sources[@]}"; do
        source_sha256="$(
            shasum -a 256 "${repository_dir}/${source_path}" |
                awk '{print $1}'
        )"
        printf '%s  %s\n' "${source_sha256}" "${source_path}"
    done | shasum -a 256 | awk '{print $1}'
)"

"${benchmark_compiler}" \
    -std=c++11 \
    -O2 \
    -Wall \
    -Wextra \
    -Werror \
    -pedantic \
    -fno-fast-math \
    -ffp-contract=off \
    -fno-lto \
    -I"${repository_dir}/source/main/physics" \
    -DROR_BEAM_BENCHMARK_COMMIT="\"${benchmark_commit}\"" \
    -DROR_BEAM_BENCHMARK_FLAGS="\"${benchmark_flags}\"" \
    -DROR_BEAM_BENCHMARK_SOURCE_MANIFEST_SHA256="\"${benchmark_source_manifest_sha256}\"" \
    -DROR_BEAM_BENCHMARK_SOURCE_STATE="\"${benchmark_source_state}\"" \
    "${repository_dir}/tools/ror_beam_axial_benchmark.cpp" \
    "${repository_dir}/source/main/physics/BeamAxialKinematics.cpp" \
    -o "${benchmark_build_dir}/ror_beam_axial_benchmark"

if [[ -n "${output_path}" ]]; then
    "${benchmark_build_dir}/ror_beam_axial_benchmark" | tee "${output_path}"
else
    "${benchmark_build_dir}/ror_beam_axial_benchmark"
fi
