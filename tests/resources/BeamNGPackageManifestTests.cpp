#include "BeamNGPackageManifest.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void Check(bool condition, const char* expression, int line)
{
    if (!condition)
    {
        std::cerr << "line " << line << ": check failed: "
                  << expression << '\n';
        ++g_failures;
    }
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

using RoR::BeamNG::ManifestErrorCode;
using RoR::BeamNG::PackageFormatProfile;
using RoR::BeamNG::PackageEntryInput;
using RoR::BeamNG::PackageEntryKind;
using RoR::BeamNG::PackageManifestResult;
using RoR::BeamNG::PackageScanLimits;

PackageEntryInput File(
    const std::string& path,
    std::uint64_t compressed_size = 10,
    std::uint64_t expanded_size = 20)
{
    PackageEntryInput entry;
    entry.path = path;
    entry.kind = PackageEntryKind::REGULAR_FILE;
    entry.compressed_size = compressed_size;
    entry.expanded_size = expanded_size;
    return entry;
}

PackageEntryInput Directory(const std::string& path)
{
    PackageEntryInput entry;
    entry.path = path;
    entry.kind = PackageEntryKind::DIRECTORY;
    return entry;
}

ManifestErrorCode ErrorFor(const PackageEntryInput& entry)
{
    const std::vector<PackageEntryInput> entries(1, entry);
    return RoR::BeamNG::BuildPackageManifest(entries).error.code;
}

void CheckPathError(
    const std::string& path,
    ManifestErrorCode expected_error)
{
    const ManifestErrorCode actual_error = ErrorFor(File(path));
    if (actual_error != expected_error)
    {
        std::cerr
            << "path test failed for byte length " << path.size()
            << ": expected "
            << RoR::BeamNG::ManifestErrorCodeToString(expected_error)
            << ", got "
            << RoR::BeamNG::ManifestErrorCodeToString(actual_error)
            << '\n';
        ++g_failures;
    }
}

void TestCanonicalOrderingAndSerialization()
{
    std::vector<PackageEntryInput> entries;
    entries.push_back(File("vehicles/test/main.jbeam", 50, 100));
    entries.push_back(File("lua/vehicle/controller.lua", 5, 10));
    entries.push_back(File("assets//textures/./body.dds", 25, 40));
    entries.push_back(Directory("vehicles/test/"));

    PackageFormatProfile profile;
    profile.identifier = "beamng-docs";
    profile.version = "2026-04-22";
    const PackageManifestResult forward =
        RoR::BeamNG::BuildPackageManifest(
            entries, PackageScanLimits(), profile);
    CHECK(forward.IsValid());
    CHECK(forward.manifest.entries.size() == 4);
    CHECK(forward.manifest.total_expanded_bytes == 150);
    CHECK(
        forward.manifest.entries[0].path ==
        "assets/textures/body.dds");
    CHECK(
        forward.manifest.entries[1].path ==
        "lua/vehicle/controller.lua");
    CHECK(forward.manifest.entries[2].path == "vehicles/test");
    CHECK(
        forward.manifest.entries[2].kind ==
        PackageEntryKind::DIRECTORY);
    CHECK(
        forward.manifest.entries[3].path ==
        "vehicles/test/main.jbeam");

    std::reverse(entries.begin(), entries.end());
    const PackageManifestResult reversed =
        RoR::BeamNG::BuildPackageManifest(
            entries, PackageScanLimits(), profile);
    CHECK(reversed.IsValid());
    CHECK(
        RoR::BeamNG::SerializeCanonicalManifest(forward.manifest) ==
        RoR::BeamNG::SerializeCanonicalManifest(reversed.manifest));

    RoR::BeamNG::PackageManifest manually_reordered = forward.manifest;
    std::reverse(
        manually_reordered.entries.begin(),
        manually_reordered.entries.end());
    CHECK(
        RoR::BeamNG::SerializeCanonicalManifest(forward.manifest) ==
        RoR::BeamNG::SerializeCanonicalManifest(manually_reordered));

    const std::string expected =
        "ror-beamng-package-manifest-v1\n"
        "format-profile\t11:beamng-docs\t10:2026-04-22\n"
        "F\tassets/textures/body.dds\tassets\t25\t40\n"
        "F\tlua/vehicle/controller.lua\tlua\t5\t10\n"
        "D\tvehicles/test\tvehicles\t0\t0\n"
        "F\tvehicles/test/main.jbeam\tvehicles\t50\t100\n"
        "total-expanded\t150\n";
    CHECK(
        RoR::BeamNG::SerializeCanonicalManifest(forward.manifest) ==
        expected);
}

void TestDefaultDocumentationProfile()
{
    const PackageManifestResult result =
        RoR::BeamNG::BuildPackageManifest(
            std::vector<PackageEntryInput>(
                1,
                File("vehicles/test/main.jbeam")));
    CHECK(result.IsValid());
    CHECK(result.manifest.format_profile.identifier == "beamng-docs");
    CHECK(
        result.manifest.format_profile.version ==
        "0.38.5.0-2026-07-27");
}

void TestCompatibilityReportIsVersionedAndCanonical()
{
    RoR::BeamNG::CompatibilityReport report;
    report.format_profile.identifier = "beamng-docs";
    report.format_profile.version = "2026-04-22";
    report.package_identity = "sha256:synthetic";
    report.importer_version = "j0-test";
    report.configuration = "synthetic_main";

    RoR::BeamNG::FeatureAssessment lua;
    lua.feature_id = "package.lua";
    lua.status =
        RoR::BeamNG::CompatibilityStatus::PRESERVED_BUT_DISABLED;
    lua.reason = "Imported scripts are data and are never executed";
    lua.source.package_path = "lua/vehicle/controller.lua";
    lua.source.line = 1;
    report.features.push_back(lua);

    RoR::BeamNG::FeatureAssessment nodes;
    nodes.feature_id = "jbeam.nodes";
    nodes.status = RoR::BeamNG::CompatibilityStatus::NATIVE;
    nodes.reason = "Syntax recognized";
    nodes.source.package_path = "vehicles/test/main.jbeam";
    nodes.source.section = "nodes";
    nodes.source.row = 2;
    nodes.source.line = 8;
    nodes.source.column = 5;
    report.features.push_back(nodes);

    RoR::BeamNG::CompatibilityDiagnostic diagnostic;
    diagnostic.code = "J0_LUA_DISABLED";
    diagnostic.severity = RoR::BeamNG::DiagnosticSeverity::WARNING;
    diagnostic.message = "Lua retained in manifest only";
    diagnostic.source = lua.source;
    report.diagnostics.push_back(diagnostic);

    const std::string first =
        RoR::BeamNG::SerializeCanonicalCompatibilityReport(report);
    std::reverse(report.features.begin(), report.features.end());
    const std::string second =
        RoR::BeamNG::SerializeCanonicalCompatibilityReport(report);
    CHECK(first == second);
    CHECK(
        first.find("ror-beamng-compatibility-report-v1\n") == 0);
    CHECK(
        first.find("preserved-but-disabled") != std::string::npos);
    CHECK(first.find("vehicles/test/main.jbeam") != std::string::npos);
    CHECK(first.find("\t5:nodes\t2\t8\t5\n") != std::string::npos);
}

void TestHostilePathsFailClosed()
{
    CheckPathError("", ManifestErrorCode::EMPTY_PATH);
    CheckPathError("/vehicles/test.jbeam", ManifestErrorCode::ABSOLUTE_PATH);
    CheckPathError("\\\\server\\share", ManifestErrorCode::ABSOLUTE_PATH);
    CheckPathError("C:/vehicle.jbeam", ManifestErrorCode::ABSOLUTE_PATH);
    CheckPathError("C:vehicle.jbeam", ManifestErrorCode::ABSOLUTE_PATH);
    CheckPathError(
        "vehicles\\test\\main.jbeam",
        ManifestErrorCode::BACKSLASH);
    CheckPathError(
        "../vehicles/test.jbeam",
        ManifestErrorCode::PARENT_TRAVERSAL);
    CheckPathError(
        "vehicles/test/../../escape",
        ManifestErrorCode::PARENT_TRAVERSAL);
    CheckPathError(".", ManifestErrorCode::EMPTY_NORMALIZED_PATH);
    CheckPathError(
        "vehicles/bad:name.jbeam",
        ManifestErrorCode::INVALID_PATH_CHARACTER);
    CheckPathError(
        "vehicles/line\nbreak.jbeam",
        ManifestErrorCode::INVALID_PATH_CHARACTER);
    CheckPathError(
        "vehicles/trailing./main.jbeam",
        ManifestErrorCode::TRAILING_DOT_OR_SPACE);
    CheckPathError(
        "vehicles/NUL/main.jbeam",
        ManifestErrorCode::WINDOWS_RESERVED_NAME);
    CheckPathError(
        "vehicles/com1.txt/main.jbeam",
        ManifestErrorCode::WINDOWS_RESERVED_NAME);
    CheckPathError(
        "vehicles/CON .txt/main.jbeam",
        ManifestErrorCode::WINDOWS_RESERVED_NAME);
    CheckPathError(
        "vehicles/conin$/main.jbeam",
        ManifestErrorCode::WINDOWS_RESERVED_NAME);
    CheckPathError(
        "vehicles/CONOUT$.log/main.jbeam",
        ManifestErrorCode::WINDOWS_RESERVED_NAME);

    std::string embedded_nul("vehicles/a", 10);
    embedded_nul.push_back('\0');
    embedded_nul += "b.jbeam";
    CheckPathError(embedded_nul, ManifestErrorCode::EMBEDDED_NUL);

    std::string non_ascii = "vehicles/";
    non_ascii.push_back(static_cast<char>(0xc3));
    non_ascii.push_back(static_cast<char>(0xa9));
    non_ascii += ".jbeam";
    CheckPathError(non_ascii, ManifestErrorCode::NON_ASCII_PATH);
}

void TestCollisionsAreRejected()
{
    {
        std::vector<PackageEntryInput> entries;
        entries.push_back(File("vehicles/test/main.jbeam"));
        entries.push_back(File("vehicles/test/main.jbeam"));
        CHECK(
            RoR::BeamNG::BuildPackageManifest(entries).error.code ==
            ManifestErrorCode::DUPLICATE_ENTRY);
    }
    {
        std::vector<PackageEntryInput> entries;
        entries.push_back(File("vehicles/test/main.jbeam"));
        entries.push_back(File("vehicles//test/./main.jbeam"));
        CHECK(
            RoR::BeamNG::BuildPackageManifest(entries).error.code ==
            ManifestErrorCode::NORMALIZATION_COLLISION);
    }
    {
        std::vector<PackageEntryInput> entries;
        entries.push_back(File("vehicles/Test/main.jbeam"));
        entries.push_back(File("vehicles/test/main.jbeam"));
        CHECK(
            RoR::BeamNG::BuildPackageManifest(entries).error.code ==
            ManifestErrorCode::CASE_COLLISION);
    }
    {
        std::vector<PackageEntryInput> entries;
        entries.push_back(File("vehicles"));
        entries.push_back(File("vehicles/test/main.jbeam"));
        CHECK(
            RoR::BeamNG::BuildPackageManifest(entries).error.code ==
            ManifestErrorCode::PATH_PREFIX_COLLISION);
    }
    {
        std::vector<PackageEntryInput> entries;
        entries.push_back(File("a"));
        entries.push_back(File("a-other"));
        entries.push_back(File("a/b"));
        CHECK(
            RoR::BeamNG::BuildPackageManifest(entries).error.code ==
            ManifestErrorCode::PATH_PREFIX_COLLISION);
    }
    {
        std::vector<PackageEntryInput> entries;
        entries.push_back(File("vehicles"));
        entries.push_back(File("Vehicles/test/main.jbeam"));
        CHECK(
            RoR::BeamNG::BuildPackageManifest(entries).error.code ==
            ManifestErrorCode::PATH_PREFIX_COLLISION);
    }
    {
        std::vector<PackageEntryInput> entries;
        entries.push_back(File("Vehicles"));
        entries.push_back(File("vehicles/test/main.jbeam"));
        CHECK(
            RoR::BeamNG::BuildPackageManifest(entries).error.code ==
            ManifestErrorCode::PATH_PREFIX_COLLISION);
    }
    {
        std::vector<PackageEntryInput> entries;
        entries.push_back(Directory("vehicles/"));
        entries.push_back(File("vehicles/test/main.jbeam"));
        CHECK(RoR::BeamNG::BuildPackageManifest(entries).IsValid());
    }
}

void TestMetadataAndResourceLimits()
{
    CHECK(
        RoR::BeamNG::BuildPackageManifest(
            std::vector<PackageEntryInput>()).error.code ==
        ManifestErrorCode::EMPTY_PACKAGE);

    PackageEntryInput symlink = File("vehicles/link");
    symlink.kind = PackageEntryKind::SYMLINK;
    CHECK(ErrorFor(symlink) == ManifestErrorCode::SYMLINK);

    PackageEntryInput unsupported = File("vehicles/device");
    unsupported.kind = PackageEntryKind::OTHER;
    CHECK(
        ErrorFor(unsupported) ==
        ManifestErrorCode::UNSUPPORTED_ENTRY_TYPE);

    PackageEntryInput encrypted = File("vehicles/secret.jbeam");
    encrypted.encrypted = true;
    CHECK(
        ErrorFor(encrypted) == ManifestErrorCode::ENCRYPTED_ENTRY);

    PackageEntryInput sized_directory = Directory("vehicles/test/");
    sized_directory.expanded_size = 1;
    CHECK(
        ErrorFor(sized_directory) ==
        ManifestErrorCode::DIRECTORY_SIZE_MISMATCH);

    CHECK(
        ErrorFor(File("vehicles/test/")) ==
        ManifestErrorCode::DIRECTORY_MARKER_MISMATCH);

    {
        const PackageScanLimits defaults;
        CHECK(defaults.max_compression_ratio == UINT64_C(1024));
        CHECK(
            RoR::BeamNG::BuildPackageManifest(
                std::vector<PackageEntryInput>(
                    1,
                    File(
                        "vehicles/test/uniform.data.dds",
                        UINT64_C(3334),
                        UINT64_C(2796344))),
                defaults).IsValid());
        CHECK(
            RoR::BeamNG::BuildPackageManifest(
                std::vector<PackageEntryInput>(
                    1,
                    File(
                        "vehicles/test/ratio-over-limit.bin",
                        UINT64_C(1000),
                        UINT64_C(1024001))),
                defaults).error.code ==
            ManifestErrorCode::COMPRESSION_RATIO_LIMIT);
    }
    {
        PackageScanLimits limits;
        limits.max_entries = 1;
        std::vector<PackageEntryInput> entries;
        entries.push_back(File("a"));
        entries.push_back(File("b"));
        CHECK(
            RoR::BeamNG::BuildPackageManifest(entries, limits).error.code ==
            ManifestErrorCode::ENTRY_COUNT_LIMIT);
    }
    {
        PackageScanLimits limits;
        limits.max_path_bytes = 4;
        CHECK(
            RoR::BeamNG::BuildPackageManifest(
                std::vector<PackageEntryInput>(1, File("12345")),
                limits).error.code ==
            ManifestErrorCode::PATH_LENGTH_LIMIT);
    }
    {
        PackageScanLimits limits;
        limits.max_path_depth = 2;
        CHECK(
            RoR::BeamNG::BuildPackageManifest(
                std::vector<PackageEntryInput>(1, File("a/b/c")),
                limits).error.code ==
            ManifestErrorCode::PATH_DEPTH_LIMIT);
    }
    {
        PackageScanLimits limits;
        limits.max_entry_expanded_bytes = 99;
        CHECK(
            RoR::BeamNG::BuildPackageManifest(
                std::vector<PackageEntryInput>(1, File("a", 10, 100)),
                limits).error.code ==
            ManifestErrorCode::ENTRY_SIZE_LIMIT);
    }
    {
        PackageScanLimits limits;
        limits.max_total_expanded_bytes = 150;
        std::vector<PackageEntryInput> entries;
        entries.push_back(File("a", 10, 100));
        entries.push_back(File("b", 10, 51));
        CHECK(
            RoR::BeamNG::BuildPackageManifest(entries, limits).error.code ==
            ManifestErrorCode::TOTAL_SIZE_LIMIT);
    }
    {
        PackageScanLimits limits;
        limits.max_compression_ratio = 10;
        CHECK(
            RoR::BeamNG::BuildPackageManifest(
                std::vector<PackageEntryInput>(
                    1,
                    File("bomb", 10, 101)),
                limits).error.code ==
            ManifestErrorCode::COMPRESSION_RATIO_LIMIT);
        CHECK(
            RoR::BeamNG::BuildPackageManifest(
                std::vector<PackageEntryInput>(
                    1,
                    File("boundary", 10, 100)),
                limits).IsValid());
        CHECK(
            RoR::BeamNG::BuildPackageManifest(
                std::vector<PackageEntryInput>(
                    1,
                    File("zero-compressed", 0, 1)),
                limits).error.code ==
            ManifestErrorCode::COMPRESSION_RATIO_LIMIT);
    }
}

void TestErrorNamesAreComplete()
{
    CHECK(
        std::string(
            RoR::BeamNG::ManifestErrorCodeToString(
                ManifestErrorCode::PATH_PREFIX_COLLISION)) ==
        "path-prefix-collision");
    CHECK(
        std::string(
            RoR::BeamNG::ManifestErrorCodeToString(
                ManifestErrorCode::NONE)) ==
        "none");
    CHECK(
        std::string(
            RoR::BeamNG::CompatibilityStatusToString(
                RoR::BeamNG::CompatibilityStatus::NATIVE)) ==
        "native");
    CHECK(
        std::string(
            RoR::BeamNG::CompatibilityStatusToString(
                RoR::BeamNG::CompatibilityStatus::APPROXIMATED)) ==
        "approximated");
    CHECK(
        std::string(
            RoR::BeamNG::CompatibilityStatusToString(
                RoR::BeamNG::CompatibilityStatus::
                    PRESERVED_BUT_DISABLED)) ==
        "preserved-but-disabled");
    CHECK(
        std::string(
            RoR::BeamNG::CompatibilityStatusToString(
                RoR::BeamNG::CompatibilityStatus::UNSUPPORTED)) ==
        "unsupported");
    CHECK(
        std::string(
            RoR::BeamNG::CompatibilityStatusToString(
                RoR::BeamNG::CompatibilityStatus::REJECTED)) ==
        "rejected");
}

} // anonymous namespace

int main()
{
    TestCanonicalOrderingAndSerialization();
    TestDefaultDocumentationProfile();
    TestCompatibilityReportIsVersionedAndCanonical();
    TestHostilePathsFailClosed();
    TestCollisionsAreRejected();
    TestMetadataAndResourceLimits();
    TestErrorNamesAreComplete();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "BeamNG package manifest tests passed\n";
    return EXIT_SUCCESS;
}
