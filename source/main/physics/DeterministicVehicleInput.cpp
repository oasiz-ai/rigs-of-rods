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

#include "DeterministicVehicleInput.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace RoR {
namespace DeterministicVehicleInput {
namespace {

const char REGISTRY_MANIFEST[] =
    "ror-deterministic-vehicle-input-registry\n"
    "schema=1\n"
    "semantics=restricted-applied-control-state\n"
    "stream=single-target\n"
    "target_identity=stream_id\n"
    "storage=binary32-exact\n"
    "1=steering_command[-1,1]\n"
    "2=service_brake[0,1]\n"
    "3=throttle[0,1]\n"
    "4=clutch[0,1]\n"
    "5=parking_brake{0,1}\n"
    "6=engine_contact{0,1}\n"
    "7=engine_starter{0,1}\n"
    "8=gear:int[-1,255]\n"
    "9=gear_range:int[0,255]\n"
    "10=hydro_speed_coupling{0,1}\n"
    "11=trailer_parking_brake{0,1}\n"
    "1024..1107=command_key_1..84[0,1]\n";

const char REGISTRY_SOURCE_NAME[] =
    "ror-restricted-applied-control-state-v1+sha256:"
    "5368675b48c68ee2804455ed0577bc5069aab2fa50a210c3dbe9d28785057f95";

const char COMPOSITE_REGISTRY_MANIFEST[] =
    "ror-deterministic-vehicle-input-composite-registry\n"
    "schema=2\n"
    "snapshot_schema=1\n"
    "semantics=atomic-multi-target-applied-control-state\n"
    "stream=scenario\n"
    "roster=sorted-unique-nonzero\n"
    "roster_count=uint32be[1,32]\n"
    "roster_target=uint64be\n"
    "identity_payload=composite_manifest+snapshot_manifest+roster\n";

const char COMPOSITE_SOURCE_PREFIX[] =
    "ror-atomic-applied-control-batch-v2+sha256:";

std::uint64_t DoubleBits(const double& value)
{
    // Read the object representation through volatile bytes. Optimized
    // fast-math builds may otherwise prove a by-value floating argument
    // finite and fold away the exponent check, even though this validation
    // boundary deliberately accepts hostile serialized values.
    unsigned char representation[sizeof(value)];
    const volatile unsigned char* source =
        reinterpret_cast<const volatile unsigned char*>(&value);
    for (std::size_t index = 0; index < sizeof(value); ++index)
        representation[index] = source[index];

    std::uint64_t bits = 0;
    std::memcpy(&bits, representation, sizeof(bits));
    return bits;
}

bool IsFiniteBits(double value)
{
    return
        (DoubleBits(value) & UINT64_C(0x7ff0000000000000)) !=
        UINT64_C(0x7ff0000000000000);
}

bool IsNegativeZero(double value)
{
    return DoubleBits(value) == UINT64_C(0x8000000000000000);
}

bool IsPositiveZero(double value)
{
    return DoubleBits(value) == 0;
}

bool SameBits(double first, double second)
{
    return DoubleBits(first) == DoubleBits(second);
}

bool SameSnapshot(
    const Snapshot& first,
    const Snapshot& second)
{
    if (first.schema_version != second.schema_version ||
        first.target_id != second.target_id)
    {
        return false;
    }
    for (std::size_t slot = 0;
        slot < first.values.size();
        ++slot)
    {
        if (!SameBits(
                first.values[slot],
                second.values[slot]))
        {
            return false;
        }
    }
    return true;
}

bool TryControlSlot(
    std::uint32_t control_id,
    std::size_t& slot)
{
    if (control_id >= CONTROL_STEERING_COMMAND &&
        control_id <= CONTROL_TRAILER_PARKING_BRAKE)
    {
        slot = static_cast<std::size_t>(
            control_id - CONTROL_STEERING_COMMAND);
        return true;
    }
    if (control_id >= CONTROL_COMMAND_1 &&
        control_id <= CONTROL_COMMAND_84)
    {
        slot =
            STANDARD_CONTROL_COUNT +
            static_cast<std::size_t>(
                control_id - CONTROL_COMMAND_1);
        return true;
    }
    return false;
}

std::uint32_t SlotControlId(std::size_t slot)
{
    if (slot < STANDARD_CONTROL_COUNT)
    {
        return
            CONTROL_STEERING_COMMAND +
            static_cast<std::uint32_t>(slot);
    }
    return
        CONTROL_COMMAND_1 +
        static_cast<std::uint32_t>(
            slot - STANDARD_CONTROL_COUNT);
}

bool IsBooleanControl(std::uint32_t control_id)
{
    return
        control_id == CONTROL_PARKING_BRAKE ||
        control_id == CONTROL_ENGINE_CONTACT ||
        control_id == CONTROL_ENGINE_STARTER ||
        control_id == CONTROL_HYDRO_SPEED_COUPLING ||
        control_id == CONTROL_TRAILER_PARKING_BRAKE;
}

bool IsUnitControl(std::uint32_t control_id)
{
    return
        control_id == CONTROL_SERVICE_BRAKE ||
        control_id == CONTROL_THROTTLE ||
        control_id == CONTROL_CLUTCH ||
        IsCommandControl(control_id);
}

bool ValidateControlValue(
    std::uint32_t control_id,
    double value,
    Status& status)
{
    status.control_id = control_id;
    if (!IsFiniteBits(value))
    {
        status.error = Error::NONFINITE_VALUE;
        return false;
    }
    if (IsNegativeZero(value))
    {
        status.error = Error::NEGATIVE_ZERO;
        return false;
    }
    if (control_id == CONTROL_STEERING_COMMAND)
    {
        if (value < -1.0 || value > 1.0)
        {
            status.error = Error::VALUE_OUT_OF_RANGE;
            return false;
        }
    }
    else if (IsUnitControl(control_id))
    {
        if (value < 0.0 || value > 1.0)
        {
            status.error = Error::VALUE_OUT_OF_RANGE;
            return false;
        }
    }
    else if (IsBooleanControl(control_id))
    {
        if (value != 0.0 && value != 1.0)
        {
            status.error = Error::VALUE_OUT_OF_RANGE;
            return false;
        }
    }
    else if (control_id == CONTROL_GEAR)
    {
        if (value < -1.0 || value > 255.0)
        {
            status.error = Error::VALUE_OUT_OF_RANGE;
            return false;
        }
        const int integer_value = static_cast<int>(value);
        if (value != static_cast<double>(integer_value))
        {
            status.error = Error::NONINTEGRAL_VALUE;
            return false;
        }
    }
    else if (control_id == CONTROL_GEAR_RANGE)
    {
        if (value < 0.0 || value > 255.0)
        {
            status.error = Error::VALUE_OUT_OF_RANGE;
            return false;
        }
        const int integer_value = static_cast<int>(value);
        if (value != static_cast<double>(integer_value))
        {
            status.error = Error::NONINTEGRAL_VALUE;
            return false;
        }
    }
    else
    {
        status.error = Error::UNKNOWN_CONTROL;
        return false;
    }

    const float narrowed = static_cast<float>(value);
    if (static_cast<double>(narrowed) != value)
    {
        status.error = Error::NON_BINARY32_VALUE;
        return false;
    }
    return true;
}

bool ValidatePersistentOrderAndValues(
    const std::vector<
        DeterministicInputTrace::PersistentControl>& controls,
    std::uint64_t target_id,
    Status& status)
{
    std::uint64_t previous_target = 0;
    std::uint32_t previous_control = 0;
    bool have_previous = false;
    for (std::size_t index = 0; index < controls.size(); ++index)
    {
        const DeterministicInputTrace::PersistentControl& control =
            controls[index];
        status.control_id = control.control_id;
        if (control.target_id != target_id)
        {
            status.error = Error::TARGET_MISMATCH;
            return false;
        }
        if (have_previous)
        {
            if (control.target_id < previous_target ||
                (control.target_id == previous_target &&
                 control.control_id < previous_control))
            {
                status.error = Error::NONCANONICAL_ORDER;
                return false;
            }
            if (control.target_id == previous_target &&
                control.control_id == previous_control)
            {
                status.error = Error::DUPLICATE_CONTROL;
                return false;
            }
        }
        if (!IsKnownControl(control.control_id))
        {
            status.error = Error::UNKNOWN_CONTROL;
            return false;
        }
        if (!ValidateControlValue(
                control.control_id,
                control.value,
                status))
        {
            return false;
        }
        if (IsPositiveZero(control.value))
        {
            status.error = Error::NONCANONICAL_ACTIVE_ZERO;
            return false;
        }
        previous_target = control.target_id;
        previous_control = control.control_id;
        have_previous = true;
    }
    return true;
}

bool ValidateDeltaOrderAndValues(
    const std::vector<DeterministicInputTrace::Event>& events,
    std::uint64_t target_id,
    Status& status)
{
    std::uint64_t previous_target = 0;
    std::uint32_t previous_control = 0;
    bool have_previous = false;
    for (std::size_t index = 0; index < events.size(); ++index)
    {
        const DeterministicInputTrace::Event& event = events[index];
        status.control_id = event.control_id;
        if (event.target_id != target_id)
        {
            status.error = Error::TARGET_MISMATCH;
            return false;
        }
        if (event.kind != DeterministicInputTrace::EventKind::STATE)
        {
            status.error = Error::UNSUPPORTED_IMPULSE;
            return false;
        }
        if (have_previous)
        {
            if (event.target_id < previous_target ||
                (event.target_id == previous_target &&
                 event.control_id < previous_control))
            {
                status.error = Error::NONCANONICAL_ORDER;
                return false;
            }
            if (event.target_id == previous_target &&
                event.control_id == previous_control)
            {
                status.error = Error::DUPLICATE_CONTROL;
                return false;
            }
        }
        if (!IsKnownControl(event.control_id))
        {
            status.error = Error::UNKNOWN_CONTROL;
            return false;
        }
        if (!ValidateControlValue(
                event.control_id,
                event.value,
                status))
        {
            return false;
        }
        previous_target = event.target_id;
        previous_control = event.control_id;
        have_previous = true;
    }
    return true;
}

bool ApplyDeltas(
    const std::vector<DeterministicInputTrace::Event>& events,
    Snapshot& snapshot,
    Status& status)
{
    for (std::size_t index = 0; index < events.size(); ++index)
    {
        const DeterministicInputTrace::Event& event = events[index];
        double previous = 0.0;
        if (!snapshot.Get(event.control_id, previous))
        {
            status.error = Error::UNKNOWN_CONTROL;
            status.control_id = event.control_id;
            return false;
        }
        if (SameBits(previous, event.value))
        {
            status.error = Error::REDUNDANT_DELTA;
            status.control_id = event.control_id;
            return false;
        }
        if (!snapshot.Set(event.control_id, event.value))
        {
            status.error = Error::UNKNOWN_CONTROL;
            status.control_id = event.control_id;
            return false;
        }
    }
    return true;
}

bool CanonicalizeTargetRosterInternal(
    const std::vector<std::uint64_t>& target_roster,
    std::vector<std::uint64_t>& canonical_roster,
    Status& status)
{
    if (target_roster.empty())
    {
        status.error = Error::EMPTY_TARGET_ROSTER;
        return false;
    }
    if (target_roster.size() > MAX_COMPOSITE_TARGETS)
    {
        status.error = Error::TARGET_LIMIT_EXCEEDED;
        return false;
    }
    std::vector<std::uint64_t> candidate = target_roster;
    for (std::size_t index = 0; index < candidate.size(); ++index)
    {
        const std::uint64_t target_id = candidate[index];
        status.target_id = target_id;
        if (target_id == 0)
        {
            status.error = Error::INVALID_TARGET;
            return false;
        }
    }
    std::sort(candidate.begin(), candidate.end());
    for (std::size_t index = 1; index < candidate.size(); ++index)
    {
        if (candidate[index] == candidate[index - 1U])
        {
            status.error = Error::DUPLICATE_TARGET;
            status.target_id = candidate[index];
            return false;
        }
    }
    canonical_roster = candidate;
    return true;
}

bool FindRosterIndex(
    const std::vector<std::uint64_t>& target_roster,
    std::uint64_t target_id,
    std::size_t& roster_index)
{
    const std::vector<std::uint64_t>::const_iterator found =
        std::lower_bound(
            target_roster.begin(),
            target_roster.end(),
            target_id);
    if (found == target_roster.end() ||
        *found != target_id)
    {
        return false;
    }
    roster_index = static_cast<std::size_t>(
        found - target_roster.begin());
    return true;
}

bool ValidateCompositePersistentOrderAndValues(
    const std::vector<
        DeterministicInputTrace::PersistentControl>& controls,
    const std::vector<std::uint64_t>& target_roster,
    Status& status)
{
    std::uint64_t previous_target = 0;
    std::uint32_t previous_control = 0;
    bool have_previous = false;
    for (std::size_t index = 0; index < controls.size(); ++index)
    {
        const DeterministicInputTrace::PersistentControl& control =
            controls[index];
        status.target_id = control.target_id;
        status.control_id = control.control_id;
        if (control.target_id == 0)
        {
            status.error = Error::INVALID_TARGET;
            return false;
        }
        std::size_t roster_index = 0;
        if (!FindRosterIndex(
                target_roster,
                control.target_id,
                roster_index))
        {
            status.error = Error::EXTRA_TARGET;
            return false;
        }
        if (have_previous)
        {
            if (control.target_id < previous_target ||
                (control.target_id == previous_target &&
                 control.control_id < previous_control))
            {
                status.error = Error::NONCANONICAL_ORDER;
                return false;
            }
            if (control.target_id == previous_target &&
                control.control_id == previous_control)
            {
                status.error = Error::DUPLICATE_CONTROL;
                return false;
            }
        }
        if (!IsKnownControl(control.control_id))
        {
            status.error = Error::UNKNOWN_CONTROL;
            return false;
        }
        if (!ValidateControlValue(
                control.control_id,
                control.value,
                status))
        {
            return false;
        }
        if (IsPositiveZero(control.value))
        {
            status.error = Error::NONCANONICAL_ACTIVE_ZERO;
            return false;
        }
        previous_target = control.target_id;
        previous_control = control.control_id;
        have_previous = true;
    }
    return true;
}

bool ValidateCompositeDeltaOrderAndValues(
    const std::vector<DeterministicInputTrace::Event>& events,
    const std::vector<std::uint64_t>& target_roster,
    Status& status)
{
    std::uint64_t previous_target = 0;
    std::uint32_t previous_control = 0;
    bool have_previous = false;
    for (std::size_t index = 0; index < events.size(); ++index)
    {
        const DeterministicInputTrace::Event& event = events[index];
        status.target_id = event.target_id;
        status.control_id = event.control_id;
        if (event.target_id == 0)
        {
            status.error = Error::INVALID_TARGET;
            return false;
        }
        std::size_t roster_index = 0;
        if (!FindRosterIndex(
                target_roster,
                event.target_id,
                roster_index))
        {
            status.error = Error::EXTRA_TARGET;
            return false;
        }
        if (event.kind != DeterministicInputTrace::EventKind::STATE)
        {
            status.error = Error::UNSUPPORTED_IMPULSE;
            return false;
        }
        if (have_previous)
        {
            if (event.target_id < previous_target ||
                (event.target_id == previous_target &&
                 event.control_id < previous_control))
            {
                status.error = Error::NONCANONICAL_ORDER;
                return false;
            }
            if (event.target_id == previous_target &&
                event.control_id == previous_control)
            {
                status.error = Error::DUPLICATE_CONTROL;
                return false;
            }
        }
        if (!IsKnownControl(event.control_id))
        {
            status.error = Error::UNKNOWN_CONTROL;
            return false;
        }
        if (!ValidateControlValue(
                event.control_id,
                event.value,
                status))
        {
            return false;
        }
        previous_target = event.target_id;
        previous_control = event.control_id;
        have_previous = true;
    }
    return true;
}

bool SameSnapshotBatch(
    const SnapshotBatch& first,
    const SnapshotBatch& second)
{
    if (first.schema_version != second.schema_version ||
        first.snapshots.size() != second.snapshots.size())
    {
        return false;
    }
    for (std::size_t index = 0;
        index < first.snapshots.size();
        ++index)
    {
        if (!SameSnapshot(
                first.snapshots[index],
                second.snapshots[index]))
        {
            return false;
        }
    }
    return true;
}

bool ApplyCompositeDeltas(
    const std::vector<DeterministicInputTrace::Event>& events,
    const std::vector<std::uint64_t>& target_roster,
    SnapshotBatch& batch,
    Status& status)
{
    for (std::size_t index = 0; index < events.size(); ++index)
    {
        const DeterministicInputTrace::Event& event = events[index];
        std::size_t roster_index = 0;
        if (!FindRosterIndex(
                target_roster,
                event.target_id,
                roster_index) ||
            roster_index >= batch.snapshots.size())
        {
            status.error = Error::EXTRA_TARGET;
            status.target_id = event.target_id;
            status.control_id = event.control_id;
            return false;
        }
        Snapshot& snapshot = batch.snapshots[roster_index];
        double previous = 0.0;
        if (!snapshot.Get(event.control_id, previous))
        {
            status.error = Error::UNKNOWN_CONTROL;
            status.target_id = event.target_id;
            status.control_id = event.control_id;
            return false;
        }
        if (SameBits(previous, event.value))
        {
            status.error = Error::REDUNDANT_DELTA;
            status.target_id = event.target_id;
            status.control_id = event.control_id;
            return false;
        }
        if (!snapshot.Set(event.control_id, event.value))
        {
            status.error = Error::UNKNOWN_CONTROL;
            status.target_id = event.target_id;
            status.control_id = event.control_id;
            return false;
        }
    }
    return true;
}

void AppendUint32BigEndian(
    std::vector<std::uint8_t>& bytes,
    std::uint32_t value)
{
    for (unsigned int shift = 32; shift != 0; shift -= 8)
    {
        bytes.push_back(
            static_cast<std::uint8_t>(
                value >> (shift - 8U)));
    }
}

void AppendUint64BigEndian(
    std::vector<std::uint8_t>& bytes,
    std::uint64_t value)
{
    for (unsigned int shift = 64; shift != 0; shift -= 8)
    {
        bytes.push_back(
            static_cast<std::uint8_t>(
                value >> (shift - 8U)));
    }
}

} // namespace

Status::Status():
    error(Error::NONE),
    target_id(0),
    control_id(0)
{
}

Snapshot::Snapshot():
    schema_version(SNAPSHOT_SCHEMA_VERSION),
    target_id(0),
    values()
{
}

bool Snapshot::Set(
    std::uint32_t control_id,
    double value)
{
    std::size_t slot = 0;
    if (!TryControlSlot(control_id, slot))
        return false;
    values[slot] = value;
    return true;
}

bool Snapshot::Get(
    std::uint32_t control_id,
    double& value) const
{
    std::size_t slot = 0;
    if (!TryControlSlot(control_id, slot))
        return false;
    value = values[slot];
    return true;
}

bool IsKnownControl(std::uint32_t control_id)
{
    std::size_t slot = 0;
    return TryControlSlot(control_id, slot);
}

bool IsCommandControl(std::uint32_t control_id)
{
    return
        control_id >= CONTROL_COMMAND_1 &&
        control_id <= CONTROL_COMMAND_84;
}

std::uint32_t CommandControlId(std::uint32_t command_index)
{
    if (command_index == 0 ||
        command_index > COMMAND_CONTROL_COUNT)
    {
        return 0;
    }
    return
        CONTROL_COMMAND_1 +
        command_index - 1;
}

bool CommandIndex(
    std::uint32_t control_id,
    std::uint32_t& command_index)
{
    if (!IsCommandControl(control_id))
        return false;
    command_index = control_id - CONTROL_COMMAND_1 + 1;
    return true;
}

const char* ControlName(std::uint32_t control_id)
{
    switch (control_id)
    {
    case CONTROL_STEERING_COMMAND:
        return "steering_command";
    case CONTROL_SERVICE_BRAKE:
        return "service_brake";
    case CONTROL_THROTTLE:
        return "throttle";
    case CONTROL_CLUTCH:
        return "clutch";
    case CONTROL_PARKING_BRAKE:
        return "parking_brake";
    case CONTROL_ENGINE_CONTACT:
        return "engine_contact";
    case CONTROL_ENGINE_STARTER:
        return "engine_starter";
    case CONTROL_GEAR:
        return "gear";
    case CONTROL_GEAR_RANGE:
        return "gear_range";
    case CONTROL_HYDRO_SPEED_COUPLING:
        return "hydro_speed_coupling";
    case CONTROL_TRAILER_PARKING_BRAKE:
        return "trailer_parking_brake";
    default:
        return
            IsCommandControl(control_id) ?
                "command_key" :
                "unknown";
    }
}

const char* RegistryManifest()
{
    return REGISTRY_MANIFEST;
}

const char* RegistrySourceName()
{
    return REGISTRY_SOURCE_NAME;
}

bool IsRegistryMetadata(
    const DeterministicInputTrace::Metadata& metadata,
    std::uint64_t target_id)
{
    return
        target_id != 0 &&
        metadata.source_name == REGISTRY_SOURCE_NAME &&
        metadata.stream_id == target_id;
}

bool ValidateSnapshot(
    const Snapshot& snapshot,
    Status& status)
{
    status = Status();
    if (snapshot.schema_version != SNAPSHOT_SCHEMA_VERSION)
    {
        status.error = Error::INVALID_SCHEMA;
        return false;
    }
    if (snapshot.target_id == 0)
    {
        status.error = Error::INVALID_TARGET;
        return false;
    }
    for (std::size_t slot = 0;
        slot < snapshot.values.size();
        ++slot)
    {
        const std::uint32_t control_id = SlotControlId(slot);
        if (!ValidateControlValue(
                control_id,
                snapshot.values[slot],
                status))
        {
            return false;
        }
    }
    status = Status();
    return true;
}

bool BuildSnapshotFromPersistentState(
    std::uint64_t target_id,
    const std::vector<
        DeterministicInputTrace::PersistentControl>& persistent_state,
    Snapshot& snapshot,
    Status& status)
{
    status = Status();
    if (target_id == 0)
    {
        status.error = Error::INVALID_TARGET;
        return false;
    }
    if (!ValidatePersistentOrderAndValues(
            persistent_state,
            target_id,
            status))
    {
        return false;
    }

    Snapshot candidate;
    candidate.target_id = target_id;
    for (std::size_t index = 0;
        index < persistent_state.size();
        ++index)
    {
        const DeterministicInputTrace::PersistentControl& control =
            persistent_state[index];
        if (!candidate.Set(control.control_id, control.value))
        {
            status.error = Error::UNKNOWN_CONTROL;
            status.control_id = control.control_id;
            return false;
        }
    }
    if (!ValidateSnapshot(candidate, status))
        return false;
    snapshot = candidate;
    status = Status();
    return true;
}

SnapshotBatch::SnapshotBatch():
    schema_version(COMPOSITE_BATCH_SCHEMA_VERSION),
    snapshots()
{
}

const char* CompositeRegistryManifest()
{
    return COMPOSITE_REGISTRY_MANIFEST;
}

bool BuildCompositeRegistrySourceName(
    const std::vector<std::uint64_t>& target_roster,
    std::string& source_name,
    Status& status)
{
    status = Status();
    std::vector<std::uint64_t> canonical_roster;
    if (!CanonicalizeTargetRosterInternal(
            target_roster,
            canonical_roster,
            status))
    {
        return false;
    }

    std::vector<std::uint8_t> identity_payload;
    identity_payload.reserve(
        std::strlen(COMPOSITE_REGISTRY_MANIFEST) +
        std::strlen(REGISTRY_MANIFEST) +
        4U +
        canonical_roster.size() * 8U);
    identity_payload.insert(
        identity_payload.end(),
        COMPOSITE_REGISTRY_MANIFEST,
        COMPOSITE_REGISTRY_MANIFEST +
            std::strlen(COMPOSITE_REGISTRY_MANIFEST));
    identity_payload.insert(
        identity_payload.end(),
        REGISTRY_MANIFEST,
        REGISTRY_MANIFEST +
            std::strlen(REGISTRY_MANIFEST));
    AppendUint32BigEndian(
        identity_payload,
        static_cast<std::uint32_t>(
            canonical_roster.size()));
    for (std::size_t index = 0;
        index < canonical_roster.size();
        ++index)
    {
        AppendUint64BigEndian(
            identity_payload,
            canonical_roster[index]);
    }

    const DeterministicInputTrace::Digest digest =
        DeterministicInputTrace::ComputeSha256(
            identity_payload.data(),
            identity_payload.size());
    const std::string candidate =
        std::string(COMPOSITE_SOURCE_PREFIX) +
        digest.ToHex();
    if (candidate.size() >
        DeterministicInputTrace::MAX_IDENTITY_STRING_BYTES)
    {
        status.error = Error::METADATA_SCHEMA_MISMATCH;
        return false;
    }
    source_name = candidate;
    status = Status();
    return true;
}

bool IsCompositeRegistryMetadata(
    const DeterministicInputTrace::Metadata& metadata,
    std::uint64_t scenario_stream_id,
    const std::vector<std::uint64_t>& target_roster)
{
    if (scenario_stream_id == 0 ||
        metadata.stream_id != scenario_stream_id)
    {
        return false;
    }
    std::string expected_source_name;
    Status status;
    return
        BuildCompositeRegistrySourceName(
            target_roster,
            expected_source_name,
            status) &&
        metadata.source_name == expected_source_name;
}

bool CanonicalizeSnapshotBatch(
    const SnapshotBatch& batch,
    const std::vector<std::uint64_t>& target_roster,
    SnapshotBatch& canonical_batch,
    Status& status)
{
    status = Status();
    std::vector<std::uint64_t> canonical_roster;
    if (!CanonicalizeTargetRosterInternal(
            target_roster,
            canonical_roster,
            status))
    {
        return false;
    }
    if (batch.schema_version !=
        COMPOSITE_BATCH_SCHEMA_VERSION)
    {
        status.error = Error::INVALID_SCHEMA;
        return false;
    }
    if (batch.snapshots.size() > MAX_COMPOSITE_TARGETS)
    {
        status.error = Error::TARGET_LIMIT_EXCEEDED;
        return false;
    }

    SnapshotBatch candidate = batch;
    for (std::size_t index = 0;
        index < candidate.snapshots.size();
        ++index)
    {
        if (candidate.snapshots[index].target_id == 0)
        {
            status.error = Error::INVALID_TARGET;
            return false;
        }
    }
    std::sort(
        candidate.snapshots.begin(),
        candidate.snapshots.end(),
        [](const Snapshot& left, const Snapshot& right)
        {
            return left.target_id < right.target_id;
        });
    for (std::size_t index = 1;
        index < candidate.snapshots.size();
        ++index)
    {
        if (candidate.snapshots[index - 1U].target_id ==
            candidate.snapshots[index].target_id)
        {
            status.error = Error::DUPLICATE_TARGET;
            status.target_id =
                candidate.snapshots[index].target_id;
            return false;
        }
    }

    std::size_t expected_index = 0;
    std::size_t actual_index = 0;
    while (expected_index < canonical_roster.size() &&
        actual_index < candidate.snapshots.size())
    {
        const std::uint64_t expected =
            canonical_roster[expected_index];
        const std::uint64_t actual =
            candidate.snapshots[actual_index].target_id;
        if (actual < expected)
        {
            status.error = Error::EXTRA_TARGET;
            status.target_id = actual;
            return false;
        }
        if (expected < actual)
        {
            status.error = Error::MISSING_TARGET;
            status.target_id = expected;
            return false;
        }
        ++expected_index;
        ++actual_index;
    }
    if (actual_index < candidate.snapshots.size())
    {
        status.error = Error::EXTRA_TARGET;
        status.target_id =
            candidate.snapshots[actual_index].target_id;
        return false;
    }
    if (expected_index < canonical_roster.size())
    {
        status.error = Error::MISSING_TARGET;
        status.target_id =
            canonical_roster[expected_index];
        return false;
    }

    for (std::size_t index = 0;
        index < candidate.snapshots.size();
        ++index)
    {
        if (!ValidateSnapshot(
                candidate.snapshots[index],
                status))
        {
            status.target_id =
                candidate.snapshots[index].target_id;
            return false;
        }
    }
    canonical_batch = candidate;
    status = Status();
    return true;
}

bool BuildSnapshotBatchFromPersistentState(
    const std::vector<std::uint64_t>& target_roster,
    const std::vector<
        DeterministicInputTrace::PersistentControl>& persistent_state,
    SnapshotBatch& batch,
    Status& status)
{
    status = Status();
    std::vector<std::uint64_t> canonical_roster;
    if (!CanonicalizeTargetRosterInternal(
            target_roster,
            canonical_roster,
            status))
    {
        return false;
    }
    if (!ValidateCompositePersistentOrderAndValues(
            persistent_state,
            canonical_roster,
            status))
    {
        return false;
    }

    SnapshotBatch candidate;
    candidate.snapshots.reserve(canonical_roster.size());
    for (std::size_t index = 0;
        index < canonical_roster.size();
        ++index)
    {
        Snapshot snapshot;
        snapshot.target_id = canonical_roster[index];
        candidate.snapshots.push_back(snapshot);
    }
    for (std::size_t index = 0;
        index < persistent_state.size();
        ++index)
    {
        const DeterministicInputTrace::PersistentControl& control =
            persistent_state[index];
        std::size_t roster_index = 0;
        if (!FindRosterIndex(
                canonical_roster,
                control.target_id,
                roster_index) ||
            !candidate.snapshots[roster_index].Set(
                control.control_id,
                control.value))
        {
            status.error = Error::EXTRA_TARGET;
            status.target_id = control.target_id;
            status.control_id = control.control_id;
            return false;
        }
    }
    for (std::size_t index = 0;
        index < candidate.snapshots.size();
        ++index)
    {
        if (!ValidateSnapshot(
                candidate.snapshots[index],
                status))
        {
            status.target_id =
                candidate.snapshots[index].target_id;
            return false;
        }
    }
    batch = candidate;
    status = Status();
    return true;
}

RecordingSource::RecordingSource(
    const DeterministicInputTrace::Runtime& runtime,
    std::uint64_t target_id,
    SnapshotProvider& provider):
    m_runtime(&runtime),
    m_target_id(target_id),
    m_provider(provider),
    m_previous(),
    m_status(),
    m_initial_status(),
    m_next_physics_step(runtime.GetNextPhysicsStep()),
    m_ready(false)
{
    m_previous.target_id = target_id;
    if (runtime.GetMode() !=
        DeterministicInputTrace::RuntimeMode::RECORD)
    {
        m_initial_status.error = Error::RUNTIME_MODE_MISMATCH;
        m_status = m_initial_status;
        return;
    }
    if (target_id == 0)
    {
        m_initial_status.error = Error::INVALID_TARGET;
        m_status = m_initial_status;
        return;
    }
    if (!IsRegistryMetadata(runtime.GetIdentity(), target_id))
    {
        m_initial_status.error =
            Error::METADATA_SCHEMA_MISMATCH;
        m_status = m_initial_status;
        return;
    }
    if (!BuildSnapshotFromPersistentState(
            target_id,
            runtime.GetPersistentState().GetControls(),
            m_previous,
            m_initial_status))
    {
        m_status = m_initial_status;
        return;
    }
    m_ready = true;
}

bool RecordingSource::AcceptsRuntime(
    const DeterministicInputTrace::Runtime& runtime) const
{
    return m_runtime == &runtime;
}

bool RecordingSource::SampleFixedStepStart(
    std::uint64_t physics_step,
    DeterministicInputTrace::SampleCollector& collector)
{
    m_status = Status();
    if (!m_ready)
    {
        m_status = m_initial_status;
        return false;
    }
    if (physics_step != m_next_physics_step)
    {
        m_status.error = Error::FIXED_STEP_MISMATCH;
        return false;
    }
    if (m_target_id == 0)
    {
        m_status.error = Error::INVALID_TARGET;
        return false;
    }

    Snapshot captured;
    captured.target_id = m_target_id;
    if (!m_provider.CaptureAppliedControls(
            physics_step,
            captured))
    {
        m_status.error = Error::PROVIDER_REJECTED;
        return false;
    }
    if (captured.target_id != m_target_id)
    {
        m_status.error = Error::TARGET_MISMATCH;
        return false;
    }
    if (!ValidateSnapshot(captured, m_status))
        return false;

    for (std::size_t slot = 0;
        slot < captured.values.size();
        ++slot)
    {
        if (SameBits(
                captured.values[slot],
                m_previous.values[slot]))
        {
            continue;
        }
        const std::uint32_t control_id = SlotControlId(slot);
        if (!collector.AddPersistentDelta(
                m_target_id,
                control_id,
                captured.values[slot]))
        {
            m_status.error = Error::COLLECTOR_REJECTED;
            m_status.control_id = control_id;
            return false;
        }
    }

    m_previous = captured;
    ++m_next_physics_step;
    m_status = Status();
    return true;
}

const Snapshot& RecordingSource::GetPreviousSnapshot() const
{
    return m_previous;
}

const Status& RecordingSource::GetStatus() const
{
    return m_status;
}

ReplaySink::ReplaySink(
    const DeterministicInputTrace::Runtime& runtime,
    std::uint64_t target_id,
    SnapshotConsumer& consumer):
    m_runtime(&runtime),
    m_target_id(target_id),
    m_consumer(consumer),
    m_previous(),
    m_status(),
    m_initial_status(),
    m_next_physics_step(runtime.GetNextPhysicsStep()),
    m_ready(false)
{
    m_previous.target_id = target_id;
    if (runtime.GetMode() !=
        DeterministicInputTrace::RuntimeMode::REPLAY)
    {
        m_initial_status.error = Error::RUNTIME_MODE_MISMATCH;
        m_status = m_initial_status;
        return;
    }
    if (target_id == 0)
    {
        m_initial_status.error = Error::INVALID_TARGET;
        m_status = m_initial_status;
        return;
    }
    if (!IsRegistryMetadata(runtime.GetIdentity(), target_id))
    {
        m_initial_status.error =
            Error::METADATA_SCHEMA_MISMATCH;
        m_status = m_initial_status;
        return;
    }
    if (!BuildSnapshotFromPersistentState(
            target_id,
            runtime.GetPersistentState().GetControls(),
            m_previous,
            m_initial_status))
    {
        m_status = m_initial_status;
        return;
    }
    m_ready = true;
}

bool ReplaySink::AcceptsRuntime(
    const DeterministicInputTrace::Runtime& runtime) const
{
    return m_runtime == &runtime;
}

bool ReplaySink::InjectFixedStepStart(
    const DeterministicInputTrace::StepInjection& injection)
{
    m_status = Status();
    if (!m_ready)
    {
        m_status = m_initial_status;
        return false;
    }
    if (injection.physics_step != m_next_physics_step)
    {
        m_status.error = Error::FIXED_STEP_MISMATCH;
        return false;
    }
    if (m_target_id == 0)
    {
        m_status.error = Error::INVALID_TARGET;
        return false;
    }
    if (!injection.impulses.empty())
    {
        m_status.error = Error::UNSUPPORTED_IMPULSE;
        m_status.control_id =
            injection.impulses.front().control_id;
        return false;
    }
    if (!ValidateDeltaOrderAndValues(
            injection.persistent_deltas,
            m_target_id,
            m_status))
    {
        return false;
    }
    if (!ValidatePersistentOrderAndValues(
            injection.persistent_state,
            m_target_id,
            m_status))
    {
        return false;
    }

    Snapshot snapshot;
    if (!BuildSnapshotFromPersistentState(
            m_target_id,
            injection.persistent_state,
            snapshot,
            m_status))
    {
        return false;
    }

    Snapshot delta_result = m_previous;
    if (!ApplyDeltas(
            injection.persistent_deltas,
            delta_result,
            m_status))
    {
        return false;
    }
    if (!SameSnapshot(delta_result, snapshot))
    {
        m_status.error = Error::STATE_DELTA_MISMATCH;
        return false;
    }
    if (!m_consumer.ApplyAppliedControls(
            injection.physics_step,
            snapshot))
    {
        m_status.error = Error::CONSUMER_REJECTED;
        return false;
    }

    m_previous = snapshot;
    ++m_next_physics_step;
    m_status = Status();
    return true;
}

const Snapshot& ReplaySink::GetPreviousSnapshot() const
{
    return m_previous;
}

const Status& ReplaySink::GetStatus() const
{
    return m_status;
}

CompositeRecordingSource::CompositeRecordingSource(
    const DeterministicInputTrace::Runtime& runtime,
    std::uint64_t scenario_stream_id,
    const std::vector<std::uint64_t>& target_roster,
    SnapshotBatchProvider& provider):
    m_runtime(&runtime),
    m_scenario_stream_id(scenario_stream_id),
    m_target_roster(),
    m_provider(provider),
    m_previous(),
    m_status(),
    m_initial_status(),
    m_next_physics_step(runtime.GetNextPhysicsStep()),
    m_ready(false)
{
    if (runtime.GetMode() !=
        DeterministicInputTrace::RuntimeMode::RECORD)
    {
        m_initial_status.error = Error::RUNTIME_MODE_MISMATCH;
        m_status = m_initial_status;
        return;
    }
    if (scenario_stream_id == 0)
    {
        m_initial_status.error = Error::INVALID_TARGET;
        m_status = m_initial_status;
        return;
    }
    if (!CanonicalizeTargetRosterInternal(
            target_roster,
            m_target_roster,
            m_initial_status))
    {
        m_status = m_initial_status;
        return;
    }
    if (!IsCompositeRegistryMetadata(
            runtime.GetIdentity(),
            scenario_stream_id,
            m_target_roster))
    {
        m_initial_status.error =
            Error::METADATA_SCHEMA_MISMATCH;
        m_status = m_initial_status;
        return;
    }
    if (!BuildSnapshotBatchFromPersistentState(
            m_target_roster,
            runtime.GetPersistentState().GetControls(),
            m_previous,
            m_initial_status))
    {
        m_status = m_initial_status;
        return;
    }
    m_ready = true;
}

bool CompositeRecordingSource::AcceptsRuntime(
    const DeterministicInputTrace::Runtime& runtime) const
{
    return m_runtime == &runtime;
}

bool CompositeRecordingSource::SampleFixedStepStart(
    std::uint64_t physics_step,
    DeterministicInputTrace::SampleCollector& collector)
{
    m_status = Status();
    if (!m_ready)
    {
        m_status = m_initial_status;
        return false;
    }
    if (physics_step != m_next_physics_step)
    {
        m_status.error = Error::FIXED_STEP_MISMATCH;
        return false;
    }
    if (m_scenario_stream_id == 0)
    {
        m_status.error = Error::INVALID_TARGET;
        return false;
    }

    SnapshotBatch captured;
    if (!m_provider.CaptureAppliedControlBatch(
            physics_step,
            captured))
    {
        m_status.error = Error::PROVIDER_REJECTED;
        return false;
    }
    SnapshotBatch canonical;
    if (!CanonicalizeSnapshotBatch(
            captured,
            m_target_roster,
            canonical,
            m_status))
    {
        return false;
    }

    std::vector<DeterministicInputTrace::Event> deltas;
    deltas.reserve(
        canonical.snapshots.size() *
        CONTROL_SLOT_COUNT);
    for (std::size_t target = 0;
        target < canonical.snapshots.size();
        ++target)
    {
        const Snapshot& captured_snapshot =
            canonical.snapshots[target];
        const Snapshot& previous_snapshot =
            m_previous.snapshots[target];
        for (std::size_t slot = 0;
            slot < captured_snapshot.values.size();
            ++slot)
        {
            if (SameBits(
                    captured_snapshot.values[slot],
                    previous_snapshot.values[slot]))
            {
                continue;
            }
            DeterministicInputTrace::Event event;
            event.target_id = captured_snapshot.target_id;
            event.control_id = SlotControlId(slot);
            event.kind =
                DeterministicInputTrace::EventKind::STATE;
            event.value = captured_snapshot.values[slot];
            deltas.push_back(event);
        }
    }

    if (deltas.size() >
        m_runtime->GetLimits().max_events_per_step)
    {
        m_status.error = Error::COLLECTOR_REJECTED;
        if (!deltas.empty())
        {
            m_status.target_id = deltas.front().target_id;
            m_status.control_id = deltas.front().control_id;
        }
        return false;
    }
    for (std::size_t index = 0; index < deltas.size(); ++index)
    {
        const DeterministicInputTrace::Event& event =
            deltas[index];
        if (!collector.AddPersistentDelta(
                event.target_id,
                event.control_id,
                event.value))
        {
            m_status.error = Error::COLLECTOR_REJECTED;
            m_status.target_id = event.target_id;
            m_status.control_id = event.control_id;
            return false;
        }
    }

    m_previous = canonical;
    ++m_next_physics_step;
    m_status = Status();
    return true;
}

const SnapshotBatch&
CompositeRecordingSource::GetPreviousBatch() const
{
    return m_previous;
}

const std::vector<std::uint64_t>&
CompositeRecordingSource::GetTargetRoster() const
{
    return m_target_roster;
}

const Status& CompositeRecordingSource::GetStatus() const
{
    return m_status;
}

CompositeReplaySink::CompositeReplaySink(
    const DeterministicInputTrace::Runtime& runtime,
    std::uint64_t scenario_stream_id,
    const std::vector<std::uint64_t>& target_roster,
    SnapshotBatchConsumer& consumer):
    m_runtime(&runtime),
    m_scenario_stream_id(scenario_stream_id),
    m_target_roster(),
    m_consumer(consumer),
    m_previous(),
    m_status(),
    m_initial_status(),
    m_next_physics_step(runtime.GetNextPhysicsStep()),
    m_ready(false)
{
    if (runtime.GetMode() !=
        DeterministicInputTrace::RuntimeMode::REPLAY)
    {
        m_initial_status.error = Error::RUNTIME_MODE_MISMATCH;
        m_status = m_initial_status;
        return;
    }
    if (scenario_stream_id == 0)
    {
        m_initial_status.error = Error::INVALID_TARGET;
        m_status = m_initial_status;
        return;
    }
    if (!CanonicalizeTargetRosterInternal(
            target_roster,
            m_target_roster,
            m_initial_status))
    {
        m_status = m_initial_status;
        return;
    }
    if (!IsCompositeRegistryMetadata(
            runtime.GetIdentity(),
            scenario_stream_id,
            m_target_roster))
    {
        m_initial_status.error =
            Error::METADATA_SCHEMA_MISMATCH;
        m_status = m_initial_status;
        return;
    }
    if (!BuildSnapshotBatchFromPersistentState(
            m_target_roster,
            runtime.GetPersistentState().GetControls(),
            m_previous,
            m_initial_status))
    {
        m_status = m_initial_status;
        return;
    }
    m_ready = true;
}

bool CompositeReplaySink::AcceptsRuntime(
    const DeterministicInputTrace::Runtime& runtime) const
{
    return m_runtime == &runtime;
}

bool CompositeReplaySink::InjectFixedStepStart(
    const DeterministicInputTrace::StepInjection& injection)
{
    m_status = Status();
    if (!m_ready)
    {
        m_status = m_initial_status;
        return false;
    }
    if (injection.physics_step != m_next_physics_step)
    {
        m_status.error = Error::FIXED_STEP_MISMATCH;
        return false;
    }
    if (m_scenario_stream_id == 0)
    {
        m_status.error = Error::INVALID_TARGET;
        return false;
    }
    if (!injection.impulses.empty())
    {
        m_status.error = Error::UNSUPPORTED_IMPULSE;
        m_status.target_id =
            injection.impulses.front().target_id;
        m_status.control_id =
            injection.impulses.front().control_id;
        return false;
    }
    if (!ValidateCompositeDeltaOrderAndValues(
            injection.persistent_deltas,
            m_target_roster,
            m_status))
    {
        return false;
    }
    if (!ValidateCompositePersistentOrderAndValues(
            injection.persistent_state,
            m_target_roster,
            m_status))
    {
        return false;
    }

    SnapshotBatch snapshot;
    if (!BuildSnapshotBatchFromPersistentState(
            m_target_roster,
            injection.persistent_state,
            snapshot,
            m_status))
    {
        return false;
    }
    SnapshotBatch delta_result = m_previous;
    if (!ApplyCompositeDeltas(
            injection.persistent_deltas,
            m_target_roster,
            delta_result,
            m_status))
    {
        return false;
    }
    if (!SameSnapshotBatch(delta_result, snapshot))
    {
        m_status.error = Error::STATE_DELTA_MISMATCH;
        return false;
    }
    if (!m_consumer.ApplyAppliedControlBatch(
            injection.physics_step,
            snapshot))
    {
        m_status.error = Error::CONSUMER_REJECTED;
        return false;
    }

    m_previous = snapshot;
    ++m_next_physics_step;
    m_status = Status();
    return true;
}

const SnapshotBatch&
CompositeReplaySink::GetPreviousBatch() const
{
    return m_previous;
}

const std::vector<std::uint64_t>&
CompositeReplaySink::GetTargetRoster() const
{
    return m_target_roster;
}

const Status& CompositeReplaySink::GetStatus() const
{
    return m_status;
}

const char* ToString(Error error)
{
    switch (error)
    {
    case Error::NONE:
        return "none";
    case Error::INVALID_SCHEMA:
        return "invalid schema";
    case Error::INVALID_TARGET:
        return "invalid target";
    case Error::UNKNOWN_CONTROL:
        return "unknown control";
    case Error::NONFINITE_VALUE:
        return "non-finite value";
    case Error::NEGATIVE_ZERO:
        return "negative zero";
    case Error::NON_BINARY32_VALUE:
        return "value is not exactly binary32";
    case Error::VALUE_OUT_OF_RANGE:
        return "value out of range";
    case Error::NONINTEGRAL_VALUE:
        return "non-integral value";
    case Error::NONCANONICAL_ACTIVE_ZERO:
        return "noncanonical active zero";
    case Error::PROVIDER_REJECTED:
        return "provider rejected";
    case Error::COLLECTOR_REJECTED:
        return "collector rejected";
    case Error::TARGET_MISMATCH:
        return "target mismatch";
    case Error::NONCANONICAL_ORDER:
        return "noncanonical order";
    case Error::DUPLICATE_CONTROL:
        return "duplicate control";
    case Error::UNSUPPORTED_IMPULSE:
        return "unsupported impulse";
    case Error::REDUNDANT_DELTA:
        return "redundant delta";
    case Error::STATE_DELTA_MISMATCH:
        return "state and delta mismatch";
    case Error::RUNTIME_MODE_MISMATCH:
        return "runtime mode mismatch";
    case Error::METADATA_SCHEMA_MISMATCH:
        return "metadata schema mismatch";
    case Error::FIXED_STEP_MISMATCH:
        return "fixed-step mismatch";
    case Error::CONSUMER_REJECTED:
        return "consumer rejected";
    case Error::EMPTY_TARGET_ROSTER:
        return "empty target roster";
    case Error::TARGET_LIMIT_EXCEEDED:
        return "target limit exceeded";
    case Error::DUPLICATE_TARGET:
        return "duplicate target";
    case Error::MISSING_TARGET:
        return "missing target";
    case Error::EXTRA_TARGET:
        return "extra target";
    }
    return "unknown";
}

} // namespace DeterministicVehicleInput
} // namespace RoR
