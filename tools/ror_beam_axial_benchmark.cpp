#include "BeamAxialKinematics.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <new>
#include <string>
#include <vector>

#ifndef ROR_BEAM_BENCHMARK_COMMIT
#define ROR_BEAM_BENCHMARK_COMMIT "unknown"
#endif

#ifndef ROR_BEAM_BENCHMARK_FLAGS
#define ROR_BEAM_BENCHMARK_FLAGS "unknown"
#endif

#ifndef ROR_BEAM_BENCHMARK_SOURCE_MANIFEST_SHA256
#define ROR_BEAM_BENCHMARK_SOURCE_MANIFEST_SHA256 "unknown"
#endif

#ifndef ROR_BEAM_BENCHMARK_SOURCE_STATE
#define ROR_BEAM_BENCHMARK_SOURCE_STATE "unknown"
#endif

namespace {

std::atomic<std::uint64_t> g_allocation_count(0U);
volatile double g_checksum_sink = 0.0;

std::uint64_t NextRandom(std::uint64_t& state)
{
    state ^= state >> 12U;
    state ^= state << 25U;
    state ^= state >> 27U;
    return state * UINT64_C(2685821657736338717);
}

double NextSigned(std::uint64_t& state, double magnitude)
{
    return
        (2.0 * static_cast<double>(NextRandom(state) >> 11U) *
                (1.0 / 9007199254740992.0) -
            1.0) *
        magnitude;
}

std::vector<RoR::BeamAxialKinematics::Input> BuildFixture(
    std::size_t beam_count)
{
    std::vector<RoR::BeamAxialKinematics::Input> fixture;
    fixture.reserve(beam_count);
    std::uint64_t state = UINT64_C(0x243f6a8885a308d3);
    for (std::size_t index = 0U; index < beam_count; ++index)
    {
        RoR::BeamAxialKinematics::Input input;
        input.endpoint_1_position_m = {{
            NextSigned(state, 15.0) + 0.5,
            NextSigned(state, 5.0),
            NextSigned(state, 3.0)
        }};
        input.endpoint_2_position_m = {{
            NextSigned(state, 15.0),
            NextSigned(state, 5.0),
            NextSigned(state, 3.0)
        }};
        input.endpoint_1_velocity_mps = {{
            NextSigned(state, 45.0),
            NextSigned(state, 45.0),
            NextSigned(state, 45.0)
        }};
        input.endpoint_2_velocity_mps = {{
            NextSigned(state, 45.0),
            NextSigned(state, 45.0),
            NextSigned(state, 45.0)
        }};
        fixture.push_back(input);
    }
    return fixture;
}

std::uint64_t HashBytes(
    std::uint64_t hash,
    const void* data,
    std::size_t size)
{
    const unsigned char* bytes =
        static_cast<const unsigned char*>(data);
    for (std::size_t index = 0U; index < size; ++index)
    {
        hash ^= bytes[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

std::uint64_t FixtureDigest(
    const std::vector<RoR::BeamAxialKinematics::Input>& fixture,
    std::size_t* failure_count)
{
    std::uint64_t hash = UINT64_C(1469598103934665603);
    *failure_count = 0U;
    for (std::size_t index = 0U; index < fixture.size(); ++index)
    {
        const RoR::BeamAxialKinematics::Result result =
            RoR::BeamAxialKinematics::Compute(fixture[index]);
        if (!result.IsValid())
        {
            ++*failure_count;
            continue;
        }
        hash = HashBytes(
            hash,
            &result.current_length_m,
            sizeof(result.current_length_m));
        hash = HashBytes(
            hash,
            &result.axial_relative_velocity_mps,
            sizeof(result.axial_relative_velocity_mps));
        hash = HashBytes(
            hash,
            result.unit_direction.data(),
            sizeof(double) * result.unit_direction.size());
    }
    return hash;
}

struct Measurement
{
    std::size_t beam_count = 0U;
    std::size_t sample_count = 0U;
    double p50_ns_per_beam = 0.0;
    double p95_ns_per_beam = 0.0;
    double p99_ns_per_beam = 0.0;
    std::uint64_t hot_loop_allocations = 0U;
    std::size_t failure_count = 0U;
    std::uint64_t digest = 0U;
};

double Percentile(
    const std::vector<double>& sorted,
    std::size_t numerator)
{
    const std::size_t index =
        ((sorted.size() - 1U) * numerator + 99U) / 100U;
    return sorted[std::min(index, sorted.size() - 1U)];
}

Measurement Measure(std::size_t beam_count, std::size_t sample_count)
{
    const std::vector<RoR::BeamAxialKinematics::Input> fixture =
        BuildFixture(beam_count);

    std::size_t warmup_failures = 0U;
    for (int warmup = 0; warmup < 8; ++warmup)
    {
        for (std::size_t index = 0U; index < fixture.size(); ++index)
        {
            const RoR::BeamAxialKinematics::Result result =
                RoR::BeamAxialKinematics::Compute(fixture[index]);
            warmup_failures += result.IsValid() ? 0U : 1U;
            g_checksum_sink += result.current_length_m * 1.0e-30;
        }
    }

    std::vector<double> samples;
    samples.reserve(sample_count);
    std::uint64_t hot_loop_allocations = 0U;
    std::size_t timed_failures = 0U;
    for (std::size_t sample = 0U; sample < sample_count; ++sample)
    {
        const std::uint64_t allocations_before =
            g_allocation_count.load(std::memory_order_relaxed);
        const std::chrono::steady_clock::time_point begin =
            std::chrono::steady_clock::now();
        double checksum = 0.0;
        for (std::size_t index = 0U; index < fixture.size(); ++index)
        {
            const RoR::BeamAxialKinematics::Result result =
                RoR::BeamAxialKinematics::Compute(fixture[index]);
            timed_failures += result.IsValid() ? 0U : 1U;
            checksum += result.current_length_m +
                result.axial_relative_velocity_mps * 1.0e-6 +
                result.unit_direction[index % 3U] * 1.0e-9;
        }
        const std::chrono::steady_clock::time_point end =
            std::chrono::steady_clock::now();
        const std::uint64_t allocations_after =
            g_allocation_count.load(std::memory_order_relaxed);
        hot_loop_allocations += allocations_after - allocations_before;
        g_checksum_sink += checksum * 1.0e-30;
        const double elapsed_ns = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                end - begin).count());
        samples.push_back(elapsed_ns / static_cast<double>(beam_count));
    }
    std::sort(samples.begin(), samples.end());

    Measurement measurement;
    measurement.beam_count = beam_count;
    measurement.sample_count = sample_count;
    measurement.p50_ns_per_beam = Percentile(samples, 50U);
    measurement.p95_ns_per_beam = Percentile(samples, 95U);
    measurement.p99_ns_per_beam = Percentile(samples, 99U);
    measurement.hot_loop_allocations = hot_loop_allocations;
    measurement.failure_count = warmup_failures + timed_failures;
    measurement.digest = FixtureDigest(
        fixture,
        &warmup_failures);
    measurement.failure_count += warmup_failures;
    return measurement;
}

const char* CompilerName()
{
#if defined(__apple_build_version__)
    return "AppleClang";
#elif defined(__clang__)
    return "Clang";
#elif defined(__GNUC__)
    return "GCC";
#elif defined(_MSC_VER)
    return "MSVC";
#else
    return "unknown";
#endif
}

const char* CompilerVersion()
{
#if defined(__VERSION__)
    return __VERSION__;
#else
    return "unknown";
#endif
}

const char* ArchitectureName()
{
#if defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#else
    return "unknown";
#endif
}

void PrintMeasurement(const Measurement& measurement, bool trailing_comma)
{
    std::cout << "    {\n"
              << "      \"beam_count\": " << measurement.beam_count << ",\n"
              << "      \"sample_count\": " << measurement.sample_count << ",\n"
              << "      \"p50_ns_per_beam\": "
              << measurement.p50_ns_per_beam << ",\n"
              << "      \"p95_ns_per_beam\": "
              << measurement.p95_ns_per_beam << ",\n"
              << "      \"p99_ns_per_beam\": "
              << measurement.p99_ns_per_beam << ",\n"
              << "      \"hot_loop_allocations\": "
              << measurement.hot_loop_allocations << ",\n"
              << "      \"failure_count\": "
              << measurement.failure_count << ",\n"
              << "      \"digest_fnv1a64\": \""
              << std::hex << std::setw(16) << std::setfill('0')
              << measurement.digest << std::dec << "\"\n"
              << "    }" << (trailing_comma ? "," : "") << "\n";
}

} // namespace

void* operator new(std::size_t size)
{
    g_allocation_count.fetch_add(1U, std::memory_order_relaxed);
    if (void* pointer = std::malloc(size))
        return pointer;
    throw std::bad_alloc();
}

void* operator new[](std::size_t size)
{
    g_allocation_count.fetch_add(1U, std::memory_order_relaxed);
    if (void* pointer = std::malloc(size))
        return pointer;
    throw std::bad_alloc();
}

void operator delete(void* pointer) noexcept
{
    std::free(pointer);
}

void operator delete[](void* pointer) noexcept
{
    std::free(pointer);
}

int main()
{
    const Measurement agora = Measure(675U, 96U);
    const Measurement repeated = Measure(10800U, 48U);

    std::cout << std::fixed << std::setprecision(3)
              << "{\n"
              << "  \"schema\": \"ror.beam_axial_kinematics_benchmark@1\",\n"
              << "  \"commit\": \"" ROR_BEAM_BENCHMARK_COMMIT "\",\n"
              << "  \"source_state\": \""
              << ROR_BEAM_BENCHMARK_SOURCE_STATE << "\",\n"
              << "  \"source_manifest_sha256\": \""
              << ROR_BEAM_BENCHMARK_SOURCE_MANIFEST_SHA256 << "\",\n"
              << "  \"compiler\": \"" << CompilerName() << "\",\n"
              << "  \"compiler_version\": \""
              << CompilerVersion() << "\",\n"
              << "  \"flags\": \"" ROR_BEAM_BENCHMARK_FLAGS "\",\n"
              << "  \"architecture\": \"" << ArchitectureName() << "\",\n"
              << "  \"measurements\": [\n";
    PrintMeasurement(agora, true);
    PrintMeasurement(repeated, false);
    std::cout << "  ]\n}\n";

    return
        agora.hot_loop_allocations == 0U &&
        repeated.hot_loop_allocations == 0U &&
        agora.failure_count == 0U &&
        repeated.failure_count == 0U
            ? 0
            : 1;
}
