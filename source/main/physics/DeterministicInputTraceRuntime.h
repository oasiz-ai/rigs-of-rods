/*
    This source file is part of Rigs of Rods
    For more information, see http://www.rigsofrods.org/

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.

    Rigs of Rods is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Rigs of Rods. If not, see <http://www.gnu.org/licenses/>.
*/

/// @file
/// @brief Bounded production lifecycle around deterministic fixed-step input.

#pragma once

#include "DeterministicInputTrace.h"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

namespace RoR {
namespace DeterministicInputTrace {

class Runtime;

/// Runtime mode is immutable between Begin/Import and terminal completion.
enum class RuntimeMode : std::uint32_t
{
    NONE = 0,
    RECORD = 1,
    REPLAY = 2
};

/// PAUSED consumes neither a wall-clock frame nor a fixed physics step.
enum class RuntimeLifecycle : std::uint32_t
{
    IDLE = 0,
    RUNNING = 1,
    PAUSED = 2,
    FINISHED = 3,
    FAULTED = 4
};

enum class RuntimeError : std::uint32_t
{
    NONE = 0,
    INVALID_TRANSITION,
    IDENTITY_MISMATCH,
    FIXED_STEP_MISMATCH,
    SOURCE_REJECTED,
    SOURCE_EXCEPTION,
    SINK_REJECTED,
    SINK_EXCEPTION,
    TRACE_FAILURE,
    INVALID_CONTINUATION,
    IO_FAILURE,
    ALLOCATION_FAILURE
};

/// First-failure status. Once FAULTED, no source or sink is called again.
struct RuntimeStatus
{
    RuntimeError error;
    Status trace_status;
    std::uint64_t physics_step;
    std::uint64_t processed_steps;

    RuntimeStatus();
};

/// A quota-aware collector passed to a caller-owned post-aggregation source.
/// Events must be inserted in strict (target_id, control_id) order. A key may
/// appear only once per fixed step, irrespective of event kind.
class SampleCollector
{
public:
    bool AddPersistentDelta(
        std::uint64_t target_id,
        std::uint32_t control_id,
        double absolute_value);
    bool AddImpulse(
        std::uint64_t target_id,
        std::uint32_t control_id,
        double value);

    bool IsValid() const;
    Error GetError() const;
    std::size_t GetEventCount() const;

private:
    friend class Runtime;

    explicit SampleCollector(std::uint32_t max_events);
    bool Add(
        std::uint64_t target_id,
        std::uint32_t control_id,
        EventKind kind,
        double value);

    std::vector<Event> m_events;
    std::uint32_t m_max_events;
    Error m_error;
};

/// Device aggregation stays outside this kernel. Implementations normally map
/// InputEngine event IDs to stable scenario target/control IDs, then add only
/// persistent changes plus this step's impulses. The method is called exactly
/// once for an accepted contiguous fixed step and is never retried.
/// InputEngine's `events` enum is zero-based, whereas the D0 wire contract
/// reserves control ID zero; a production adapter therefore needs an explicit,
/// versioned nonzero mapping (not a raw enum cast). Continuous aggregated
/// values become persistent deltas; bounce/one-shot actions become impulses.
class FixedStepSampleSource
{
public:
    virtual ~FixedStepSampleSource() {}

    /// Optional ownership guard for adapters whose continuation baseline is
    /// derived from one specific Runtime. The default keeps dependency-free
    /// stateless sources compatible; bound adapters override this and reject
    /// accidental use by another runtime before any source state is sampled.
    virtual bool AcceptsRuntime(const Runtime& runtime) const;

    virtual bool SampleFixedStepStart(
        std::uint64_t physics_step,
        SampleCollector& collector) = 0;
};

/// Complete replay batch delivered at the beginning of one fixed step.
/// `persistent_state` is the reconstructed, sorted active state after applying
/// `persistent_deltas`. Impulses exist for this step only.
struct StepInjection
{
    std::uint64_t physics_step;
    std::vector<PersistentControl> persistent_state;
    std::vector<Event> persistent_deltas;
    std::vector<Event> impulses;

    StepInjection();
};

/// The sink receives one immutable, complete batch only after the entire input
/// trace has been authenticated. A false return rejects the batch. Callers
/// should apply a batch transactionally because arbitrary external state cannot
/// be rolled back by this dependency-free kernel.
class FixedStepInjectionSink
{
public:
    virtual ~FixedStepInjectionSink() {}

    /// Optional ownership guard matching FixedStepSampleSource. Runtime checks
    /// this before consuming the next authenticated frame.
    virtual bool AcceptsRuntime(const Runtime& runtime) const;

    virtual bool InjectFixedStepStart(const StepInjection& injection) = 0;
};

static const std::uint32_t RUNTIME_CONTINUATION_SCHEMA_VERSION = 1;

/// Copyable savegame-owned continuation. `authenticated_trace` is always a
/// complete D0 stream: a recording stores an authenticated checkpoint of all
/// recorded frames, while replay stores the original fully authenticated
/// stream and the number of already injected frames.
///
/// The base Writer/Reader deliberately expose neither a serializable SHA-chain
/// context nor a restorable stream cursor. Consequently export/import is
/// O(trace): recording regenerates and authenticates a checkpoint, and import
/// authenticates/replays that checkpoint to reconstruct the private state.
/// This retains integrity and stays bounded by Limits instead of weakening the
/// D0 format with an unauthenticated internal-state snapshot.
///
/// `authentication_digest` is the version-1 canonical SHA-256 integrity
/// envelope for every continuation field other than itself. The trace bytes
/// are bound through their independently verified digest and exact byte count.
/// This is an unkeyed integrity check, not a MAC; an application accepting
/// adversarial savegames must still authenticate its savegame envelope.
struct RuntimeContinuation
{
    std::uint32_t schema_version;
    RuntimeMode mode;
    RuntimeLifecycle lifecycle;
    Limits limits;
    std::uint64_t processed_steps;
    std::uint64_t next_physics_step;
    Digest trace_digest;
    std::string authenticated_trace;
    Digest authentication_digest;

    RuntimeContinuation();
    void Swap(RuntimeContinuation& other);
};

/// Bounded lifecycle/bridge for deterministic input recording and replay.
/// It owns no device, Lua, network, actor, or scheduler API.
class Runtime
{
public:
    Runtime();
    ~Runtime();

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    bool BeginRecording(
        const Metadata& identity,
        const Limits& limits = Limits());

    /// String and stream entry points both preflight the complete authenticated
    /// stream before any replay sink may be called.
    bool BeginReplay(
        const std::string& authenticated_trace,
        const Metadata& expected_identity,
        const Limits& limits = Limits());
    bool BeginReplay(
        std::istream& authenticated_trace,
        const Metadata& expected_identity,
        const Limits& limits = Limits());

    bool Pause();
    bool Resume();

    bool RecordFixedStep(
        std::uint64_t physics_step,
        FixedStepSampleSource& source);
    bool ReplayFixedStep(
        std::uint64_t physics_step,
        FixedStepInjectionSink& sink);

    /// Finalization authenticates a fresh snapshot, then publishes it. The
    /// string overload leaves `output` unchanged on every failure.
    bool FinalizeRecording(std::string& output);
    bool FinalizeRecording(std::ostream& output);

    /// Export is non-terminal. Import reconstructs either Writer or Reader
    /// progress from authenticated bytes and verifies exact immutable identity.
    /// `continuation` / `output` are not modified on failure.
    bool ExportContinuation(RuntimeContinuation& output);
    bool ImportContinuation(
        const RuntimeContinuation& continuation,
        const Metadata& expected_identity);

    RuntimeMode GetMode() const;
    RuntimeLifecycle GetLifecycle() const;
    const RuntimeStatus& GetStatus() const;
    const Metadata& GetIdentity() const;
    const Limits& GetLimits() const;
    std::uint64_t GetProcessedStepCount() const;
    std::uint64_t GetNextPhysicsStep() const;
    const ReplayState& GetPersistentState() const;
    const Digest& GetTraceDigest() const;
    const std::string& GetAuthenticatedTrace() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

const char* ToString(RuntimeError error);
const char* ToString(RuntimeMode mode);
const char* ToString(RuntimeLifecycle lifecycle);

} // namespace DeterministicInputTrace
} // namespace RoR
