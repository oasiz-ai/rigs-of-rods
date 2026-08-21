/*
    This source file is part of Rigs of Rods
    For more information, see http://www.rigsofrods.org/

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Bounded savegame envelope for authenticated deterministic input.

#pragma once

#include "DeterministicInputTraceRuntime.h"

#include <cstdint>
#include <string>

namespace RoR {
namespace DeterministicInputContinuationSavegame {

static const std::uint32_t PAYLOAD_SCHEMA_VERSION = 1U;
static const std::uint64_t MAX_SAVEGAME_STEPS = UINT64_C(120000);
static const std::uint64_t MAX_SAVEGAME_EVENTS = UINT64_C(2000000);
static const std::uint64_t MAX_SAVEGAME_TRACE_BYTES =
    UINT64_C(128) * UINT64_C(1024) * UINT64_C(1024);

/// Savegame-owned state outside RuntimeContinuation's authenticated envelope.
///
/// The outer wire digest binds the resume decision and production configuration
/// to the continuation. It is still an unkeyed integrity receipt; the runtime
/// must rebuild the expected identity from the live Actor before importing.
struct Payload
{
    std::uint32_t schema_version;
    bool resume_after_load;
    std::uint64_t scenario_id;
    std::uint64_t target_id;
    std::uint64_t step_limit;
    std::uint64_t completed_physics_steps;
    std::uint64_t actor_physics_step;
    DeterministicInputTrace::RuntimeContinuation continuation;

    Payload();
    void Swap(Payload& other);
};

enum class Error
{
    NONE = 0,
    INVALID_PAYLOAD,
    SIZE_LIMIT_EXCEEDED,
    INVALID_BASE64,
    INVALID_WIRE_FORMAT,
    INTEGRITY_MISMATCH,
    ALLOCATION_FAILURE
};

struct Status
{
    Error error;
    std::uint64_t byte_offset;

    Status();
};

/// Encodes one canonical base64url string without padding. `output` is
/// unchanged on failure.
bool Encode(
    const Payload& payload,
    std::string& output,
    Status& status);

/// Strictly decodes, authenticates, and structurally validates one envelope.
/// `output` is unchanged on failure; Runtime::ImportContinuation performs the
/// subsequent trace authentication and exact live-identity check.
bool Decode(
    const std::string& encoded,
    Payload& output,
    Status& status);

const char* ToString(Error error);

} // namespace DeterministicInputContinuationSavegame
} // namespace RoR
