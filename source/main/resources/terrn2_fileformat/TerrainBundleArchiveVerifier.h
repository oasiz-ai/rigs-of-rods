/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#pragma once

#include <string>

namespace RoR {

/// Streams an archive through SHA-256 and compares it with a mandatory,
/// lowercase hexadecimal digest.
///
/// The archive is opened read-only and is never buffered in full. On success,
/// `out_observed_sha256` contains the verified digest and `out_error` is empty.
/// On failure, the observed digest is supplied when hashing completed.
bool VerifyTerrainBundleArchiveSha256(
    const std::string& archive_path,
    const std::string& expected_sha256,
    std::string& out_observed_sha256,
    std::string& out_error);

} // namespace RoR
