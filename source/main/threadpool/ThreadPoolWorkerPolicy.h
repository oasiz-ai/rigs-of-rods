/*
This source file is part of Rigs of Rods

Rigs of Rods is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License version 3, as
published by the Free Software Foundation.
*/

#pragma once

namespace RoR {

constexpr int THREAD_POOL_MAX_WORKERS = 8;

// A bounded explicit setting is an instruction, not an auto-sizing hint.
// Honoring it also lets deterministic qualification intentionally
// oversubscribe small CI hosts and prove the same state trace at 1 and 8
// workers. Missing or out-of-range settings retain hardware-based sizing.
constexpr int ResolveThreadPoolWorkerCount(
    int configured_workers,
    int logical_cores) noexcept
{
    if (configured_workers >= 1 &&
        configured_workers <= THREAD_POOL_MAX_WORKERS)
    {
        return configured_workers;
    }

    const int detected_workers = logical_cores > 1 ? logical_cores - 1 : 1;
    return detected_workers > THREAD_POOL_MAX_WORKERS
        ? THREAD_POOL_MAX_WORKERS
        : detected_workers;
}

} // namespace RoR
