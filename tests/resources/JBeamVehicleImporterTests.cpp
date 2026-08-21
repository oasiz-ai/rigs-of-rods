#include "JBeamVehicleImporter.h"
#include "JBeamWheel2Approximation.h"

// Keep this focused importer target independent from Actor/CacheSystem while
// using the production RigDef layout consumed by JBeamVehicleImporter.cpp.
namespace RoR {
class CacheEntry
{
public:
    void AddRef() {}
    void Release() {}
};
}

#include "RigDef_File.h"

#include <zlib.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace RigDef {

const char* ROOT_MODULE_NAME = "_Root_";

Node::Id::Id()
    : m_id_num(0U)
    , m_flags(0U)
{
}

Node::Id::Id(const std::string& id)
    : m_id_num(0U)
    , m_flags(0U)
{
    setStr(id);
}

void Node::Id::setStr(const std::string& id)
{
    m_id_num = 0U;
    m_id_str = id;
    BITMASK_SET_0(m_flags, IS_TYPE_NUMBERED);
    BITMASK_SET_1(m_flags, IS_TYPE_NAMED | IS_VALID);
}

Node::Ref::Ref()
    : m_id_as_number(0U)
    , m_flags(0U)
    , m_line_number(0U)
{
}

Node::Ref::Ref(
    const std::string& id,
    unsigned int number,
    unsigned int flags,
    unsigned int line)
    : m_id(id)
    , m_id_as_number(number)
    , m_flags(0U)
    , m_line_number(line)
{
    BITMASK_SET_1(m_flags, flags);
}

NodeDefaults::NodeDefaults()
    : load_weight(-1.0f)
    , friction(1.0f)
    , volume(1.0f)
    , surface(1.0f)
    , options(0U)
{
}

Document::Module::Module(const Ogre::String& module_name)
    : name(module_name)
{
}

Document::Document()
    : hide_in_chooser(false)
    , enable_advanced_deformation(false)
    , slide_nodes_connect_instantly(false)
    , rollon(false)
    , forward_commands(false)
    , import_commands(false)
    , lockgroup_default_nolock(false)
    , rescuer(false)
    , disable_default_sounds(false)
    , root_module(std::make_shared<Document::Module>(ROOT_MODULE_NAME))
{
}

} // namespace RigDef

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

constexpr std::uint32_t LOCAL_SIGNATURE = UINT32_C(0x04034b50);
constexpr std::uint32_t CENTRAL_SIGNATURE = UINT32_C(0x02014b50);
constexpr std::uint32_t EOCD_SIGNATURE = UINT32_C(0x06054b50);

void Append16(std::vector<std::uint8_t>& bytes, std::uint16_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value & UINT16_C(0xff)));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & UINT16_C(0xff)));
}

void Append32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value & UINT32_C(0xff)));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & UINT32_C(0xff)));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16U) & UINT32_C(0xff)));
    bytes.push_back(static_cast<std::uint8_t>((value >> 24U) & UINT32_C(0xff)));
}

void AppendString(std::vector<std::uint8_t>& bytes, const std::string& value)
{
    bytes.insert(bytes.end(), value.begin(), value.end());
}

struct ZipMember
{
    std::string path;
    std::string expanded;
    std::uint16_t method = 0U;
    bool corrupt_crc = false;
};

std::vector<std::uint8_t> DeflateRaw(const std::string& input)
{
    CHECK(input.size() <= (std::numeric_limits<uInt>::max)());
    z_stream stream = {};
    const int initialized = deflateInit2(
        &stream,
        Z_BEST_COMPRESSION,
        Z_DEFLATED,
        -MAX_WBITS,
        8,
        Z_DEFAULT_STRATEGY);
    CHECK(initialized == Z_OK);
    if (initialized != Z_OK)
    {
        return {};
    }
    std::vector<std::uint8_t> output(
        static_cast<std::size_t>(deflateBound(
            &stream, static_cast<uLong>(input.size()))));
    stream.next_in = reinterpret_cast<Bytef*>(
        const_cast<char*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = reinterpret_cast<Bytef*>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());
    const int encoded = deflate(&stream, Z_FINISH);
    CHECK(encoded == Z_STREAM_END);
    CHECK(stream.total_in == input.size());
    CHECK(stream.total_out <= output.size());
    const std::size_t encoded_size =
        static_cast<std::size_t>(stream.total_out);
    CHECK(deflateEnd(&stream) == Z_OK);
    if (encoded != Z_STREAM_END)
    {
        return {};
    }
    output.resize(encoded_size);
    return output;
}

std::uint32_t Crc32(const std::string& value)
{
    CHECK(value.size() <= (std::numeric_limits<uInt>::max)());
    return static_cast<std::uint32_t>(::crc32(
        ::crc32(0L, Z_NULL, 0),
        reinterpret_cast<const Bytef*>(value.data()),
        static_cast<uInt>(value.size())));
}

std::vector<std::uint8_t> BuildArchive(
    const std::vector<ZipMember>& members)
{
    struct EncodedMember
    {
        const ZipMember* source = nullptr;
        std::vector<std::uint8_t> payload;
        std::uint32_t crc = 0U;
        std::uint32_t local_offset = 0U;
    };

    std::vector<EncodedMember> encoded;
    std::vector<std::uint8_t> bytes;
    for (const ZipMember& member : members)
    {
        CHECK(!member.path.empty());
        CHECK(member.path.size() <= (std::numeric_limits<std::uint16_t>::max)());
        CHECK(member.expanded.size() <=
            (std::numeric_limits<std::uint32_t>::max)());
        EncodedMember entry;
        entry.source = &member;
        entry.payload = member.method == 8U
            ? DeflateRaw(member.expanded)
            : std::vector<std::uint8_t>(
                member.expanded.begin(), member.expanded.end());
        CHECK(entry.payload.size() <=
            (std::numeric_limits<std::uint32_t>::max)());
        entry.crc = Crc32(member.expanded);
        if (member.corrupt_crc)
        {
            entry.crc ^= UINT32_C(0x00000001);
        }
        entry.local_offset = static_cast<std::uint32_t>(bytes.size());

        Append32(bytes, LOCAL_SIGNATURE);
        Append16(bytes, UINT16_C(20));
        Append16(bytes, UINT16_C(0));
        Append16(bytes, member.method);
        Append16(bytes, UINT16_C(0));
        Append16(bytes, UINT16_C(0));
        Append32(bytes, entry.crc);
        Append32(bytes, static_cast<std::uint32_t>(entry.payload.size()));
        Append32(bytes, static_cast<std::uint32_t>(member.expanded.size()));
        Append16(bytes, static_cast<std::uint16_t>(member.path.size()));
        Append16(bytes, UINT16_C(0));
        AppendString(bytes, member.path);
        bytes.insert(bytes.end(), entry.payload.begin(), entry.payload.end());
        encoded.push_back(std::move(entry));
    }

    const std::uint32_t central_offset =
        static_cast<std::uint32_t>(bytes.size());
    for (const EncodedMember& entry : encoded)
    {
        const ZipMember& member = *entry.source;
        Append32(bytes, CENTRAL_SIGNATURE);
        Append16(bytes, UINT16_C(20));
        Append16(bytes, UINT16_C(20));
        Append16(bytes, UINT16_C(0));
        Append16(bytes, member.method);
        Append16(bytes, UINT16_C(0));
        Append16(bytes, UINT16_C(0));
        Append32(bytes, entry.crc);
        Append32(bytes, static_cast<std::uint32_t>(entry.payload.size()));
        Append32(bytes, static_cast<std::uint32_t>(member.expanded.size()));
        Append16(bytes, static_cast<std::uint16_t>(member.path.size()));
        Append16(bytes, UINT16_C(0));
        Append16(bytes, UINT16_C(0));
        Append16(bytes, UINT16_C(0));
        Append16(bytes, UINT16_C(0));
        Append32(bytes, UINT32_C(0));
        Append32(bytes, entry.local_offset);
        AppendString(bytes, member.path);
    }
    const std::uint32_t central_size =
        static_cast<std::uint32_t>(bytes.size()) - central_offset;
    CHECK(encoded.size() <=
        (std::numeric_limits<std::uint16_t>::max)());
    Append32(bytes, EOCD_SIGNATURE);
    Append16(bytes, UINT16_C(0));
    Append16(bytes, UINT16_C(0));
    Append16(bytes, static_cast<std::uint16_t>(encoded.size()));
    Append16(bytes, static_cast<std::uint16_t>(encoded.size()));
    Append32(bytes, central_size);
    Append32(bytes, central_offset);
    Append16(bytes, UINT16_C(0));
    return bytes;
}

class TempArchive final
{
public:
    explicit TempArchive(const std::vector<std::uint8_t>& bytes)
    {
        const std::int64_t nonce =
            std::chrono::high_resolution_clock::now()
                .time_since_epoch().count();
        m_path = std::filesystem::temp_directory_path() /
            ("ror-jbeam-import-" + std::to_string(nonce) + ".zip");
        std::ofstream output(m_path, std::ios::binary | std::ios::trunc);
        output.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        output.close();
        CHECK(output.good());
    }

    ~TempArchive()
    {
        std::error_code error;
        std::filesystem::remove(m_path, error);
    }

    std::string path() const
    {
        return m_path.string();
    }

private:
    std::filesystem::path m_path;
};

RoR::TerrainBundleAuthenticatedArchiveSnapshot LoadSnapshot(
    const std::vector<std::uint8_t>& archive,
    const TempArchive& file)
{
    std::string digest;
    std::string error;
    CHECK(RoR::ComputeTerrainBundleArchiveSha256(
        file.path(), digest, error));
    CHECK(error.empty());
    CHECK(digest.size() == 64U);
    RoR::TerrainBundleAuthenticatedArchiveSnapshot snapshot;
    std::string observed;
    CHECK(RoR::LoadAndVerifyTerrainBundleArchiveSnapshot(
        file.path(),
        digest,
        static_cast<std::uint64_t>(archive.size()),
        snapshot,
        observed,
        error));
    CHECK(error.empty());
    CHECK(observed == digest);
    CHECK(snapshot.initialized());
    CHECK(snapshot.size() == archive.size());
    return snapshot;
}

std::string SupportedVehicle()
{
    return R"JBEAM({
        "test_vehicle": {
            "slotType": "main",
            "nodes": [
                ["id", "posX", "posY", "posZ"],
                ["ref", 0, 0, 0],
                ["back", 0, 1, 0],
                ["left", 1, 0, 0],
                ["up", 0, 0, 1],
                ["leftCorner", 1, -1, 0],
                ["rightCorner", -1, -1, 0]
            ],
            "refNodes": [
                ["ref:", "back:", "left:", "up:",
                 "leftCorner:", "rightCorner:"],
                ["ref", "back", "left", "up",
                 "leftCorner", "rightCorner"]
            ],
            "beams": [
                ["id1:", "id2:"],
                ["ref", "back"],
                ["ref", "left"],
                ["ref", "up"],
                ["left", "up"]
            ],
            "triangles": [
                ["id1:", "id2:", "id3:"],
                ["ref", "back", "left"]
            ],
            "hydros": [
                ["id1:", "id2:"],
                ["left", "up", {"factor": 0.14,
                    "inRate": 1.25, "outRate": 1.25}]
            ]
        }
    })JBEAM";
}

std::string PressureWheelVehicle()
{
    return R"JBEAM({
        "pressure_vehicle": {
            "slotType": "main",
            "nodes": [
                ["id", "posX", "posY", "posZ", "nodeWeight"],
                ["ref", 0, 0, 1, 20],
                ["back", 0, 1, 1, 20],
                ["left", 1, 0, 1, 20],
                ["up", 0, 0, 2, 20],
                ["leftCorner", 1, -1, 1, 20],
                ["rightCorner", -1, -1, 1, 20],
                ["axle1", 0.125, 0, 1, 5],
                ["axle2", -0.125, 0, 1, 5],
                ["arm", 0, 0.25, 1, 5]
            ],
            "refNodes": [
                ["ref:", "back:", "left:", "up:",
                 "leftCorner:", "rightCorner:"],
                ["ref", "back", "left", "up",
                 "leftCorner", "rightCorner"]
            ],
            "beams": [
                ["id1:", "id2:", "beamSpring", "beamDamp"],
                ["ref", "back", 4300000, 580],
                ["ref", "left", 4300000, 580],
                ["ref", "up", 4300000, 580],
                ["ref", "leftCorner", 4300000, 580],
                ["ref", "rightCorner", 4300000, 580],
                ["ref", "axle1", 4300000, 580],
                ["ref", "axle2", 4300000, 580],
                ["ref", "arm", 4300000, 580]
            ],
            "pressureWheels": [
                ["name", "hubGroup", "group", "node1:",
                 "node2:", "nodeS", "nodeArm:", "wheelDir"],
                {"radius": 0.5, "hubRadius": 0.25,
                 "wheelOffset": 0, "tireWidth": 0.25,
                 "hubWidth": 0.25, "hasTire": true, "numRays": 16,
                 "nodeWeight": 0.5, "hubNodeWeight": 0.25,
                 "hubBeamSpring": 1000000, "hubBeamDamp": 100,
                 "wheelSideBeamSpring": 500000,
                 "wheelSideBeamDamp": 50},
                ["FL", "hub_FL", "tire_FL", "axle1",
                 "axle2", 9999, "arm", -1]
            ]
        }
    })JBEAM";
}

void TestPressureWheelProductImport()
{
    const std::vector<std::uint8_t> archive = BuildArchive({
        {"vehicles/pressure/main.jbeam",
         PressureWheelVehicle(), 8U, false}});
    const TempArchive file(archive);
    const RoR::TerrainBundleAuthenticatedArchiveSnapshot snapshot =
        LoadSnapshot(archive, file);
    const RoR::BeamNG::JBeamVehicleImportResult imported =
        RoR::BeamNG::ImportJBeamVehicleFromArchiveSnapshot(
            snapshot, "beamng-wheel-group", "pressure_vehicle");
    if (!imported.IsAdmitted())
    {
        std::cerr << "pressure-wheel fixture returned "
                  << RoR::BeamNG::JBeamVehicleImportCodeToString(
                         imported.code)
                  << ": " << imported.detail << '\n';
    }
    CHECK(imported.IsAdmitted());
    CHECK(imported.authority != nullptr);
    CHECK(imported.authority &&
        imported.authority->wheel2_plan_sha256().size() == 64U);
    CHECK(imported.authority &&
        imported.authority->wheel2_plan_count() == 1U);
    CHECK(imported.authority &&
        imported.authority->wheel2_approximated_semantics() ==
            RoR::BeamNG::JBEAM_WHEEL2_APPROXIMATION_SEMANTICS);
    CHECK(imported.document != nullptr);
    CHECK(imported.document && imported.document->root_module != nullptr);
    if (imported.document && imported.document->root_module)
    {
        CHECK(imported.document->root_module->wheels2.size() == 1U);
        if (imported.document->root_module->wheels2.size() == 1U)
        {
            const RigDef::Wheel2& wheel =
                imported.document->root_module->wheels2[0];
            CHECK(wheel.nodes[0].Str() == "axle1");
            CHECK(wheel.nodes[1].Str() == "axle2");
            CHECK(wheel.reference_arm_node.Str() == "arm");
            CHECK(wheel.rim_radius == 0.25f);
            CHECK(wheel.tyre_radius == 0.5f);
            CHECK(wheel.width == 0.25f);
            CHECK(wheel.num_rays == 16U);
            CHECK(wheel.mass == 24.0f);
            CHECK(wheel.braking == RoR::WheelBraking::NONE);
            CHECK(wheel.propulsion == RoR::WheelPropulsion::NONE);
        }
    }

    std::string unsupported = PressureWheelVehicle();
    const std::string needle = "\"wheelOffset\": 0";
    const std::size_t offset = unsupported.find(needle);
    CHECK(offset != std::string::npos);
    if (offset != std::string::npos)
    {
        unsupported.replace(offset, needle.size(),
            "\"wheelOffset\": 0.125");
    }
    const std::vector<std::uint8_t> rejected_archive = BuildArchive({
        {"vehicles/pressure/main.jbeam", unsupported, 0U, false}});
    const TempArchive rejected_file(rejected_archive);
    const RoR::TerrainBundleAuthenticatedArchiveSnapshot rejected_snapshot =
        LoadSnapshot(rejected_archive, rejected_file);
    const RoR::BeamNG::JBeamVehicleImportResult rejected =
        RoR::BeamNG::ImportJBeamVehicleFromArchiveSnapshot(
            rejected_snapshot, "beamng-wheel-group", "pressure_vehicle");
    CHECK(!rejected.IsAdmitted());
    CHECK(rejected.code ==
        RoR::BeamNG::JBeamVehicleImportCode::WHEEL2_PLAN_REJECTED);
    CHECK(rejected.document == nullptr);
    CHECK(rejected.authority == nullptr);
}

void TestAdmissionAndAuthority()
{
    const std::vector<std::uint8_t> archive = BuildArchive({
        {"vehicles/test/main.jbeam", SupportedVehicle(), 8U, false}});
    const TempArchive file(archive);
    const RoR::TerrainBundleAuthenticatedArchiveSnapshot snapshot =
        LoadSnapshot(archive, file);

    const RoR::BeamNG::JBeamVehiclePackageInspection inspection =
        RoR::BeamNG::InspectJBeamVehicleArchiveSnapshot(snapshot);
    CHECK(inspection.IsValid());
    CHECK(inspection.code == RoR::BeamNG::JBeamVehicleImportCode::ADMITTED);
    CHECK(inspection.candidates.size() == 1U);
    CHECK(inspection.candidates[0].root_part_name == "test_vehicle");
    CHECK(inspection.candidates[0].package_path ==
        "vehicles/test/main.jbeam");
    CHECK(inspection.jbeam_member_count == 1U);
    CHECK(inspection.retained_jbeam_bytes == SupportedVehicle().size());
    CHECK(inspection.archive_sha256 == snapshot.archive_sha256());
    CHECK(inspection.package_index_sha256.size() == 64U);

    const RoR::BeamNG::JBeamVehicleImportResult imported =
        RoR::BeamNG::ImportJBeamVehicleFromArchiveSnapshot(
            snapshot, "beamng-test-group", "test_vehicle");
    CHECK(imported.IsAdmitted());
    CHECK(imported.document != nullptr);
    CHECK(imported.authority != nullptr);
    CHECK(imported.document->_jbeam_import_authority == imported.authority);
    CHECK(imported.document->root_module != nullptr);
    CHECK(imported.document->root_module->hydros.size() == 1U);
    CHECK(imported.document->root_module->hydros[0]
        ._jbeam_runtime_plan != nullptr);
    CHECK(imported.authority->version() ==
        RoR::BeamNG::JBEAM_VEHICLE_IMPORT_AUTHORITY_VERSION);
    CHECK(imported.authority->resource_group() == "beamng-test-group");
    CHECK(imported.authority->root_part_name() == "test_vehicle");
    CHECK(imported.authority->archive_sha256() == snapshot.archive_sha256());
    CHECK(imported.authority->package_index_sha256() ==
        inspection.package_index_sha256);
    CHECK(imported.authority->resolved_graph_sha256().size() == 64U);
    CHECK(imported.authority->wheel2_plan_sha256().size() == 64U);
    CHECK(imported.authority->wheel2_plan_count() == 0U);
    CHECK(imported.authority->wheel2_approximated_semantics() == 0U);
    CHECK(imported.authority->jbeam_member_count() == 1U);
    CHECK(imported.authority->retained_jbeam_bytes() ==
        SupportedVehicle().size());
    CHECK(imported.authority->Matches(
        "beamng-test-group", "test_vehicle", snapshot));
    CHECK(!imported.authority->Matches(
        "wrong-group", "test_vehicle", snapshot));
    CHECK(!imported.authority->Matches(
        "beamng-test-group", "wrong-root", snapshot));

    const TempArchive second_file(archive);
    const RoR::TerrainBundleAuthenticatedArchiveSnapshot second_snapshot =
        LoadSnapshot(archive, second_file);
    CHECK(second_snapshot.archive_sha256() == snapshot.archive_sha256());
    CHECK(!second_snapshot.SharesImmutableStateWith(snapshot));
    CHECK(!imported.authority->Matches(
        "beamng-test-group", "test_vehicle", second_snapshot));

    const RoR::BeamNG::JBeamVehicleImportResult wrong_root =
        RoR::BeamNG::ImportJBeamVehicleFromArchiveSnapshot(
            snapshot, "beamng-test-group", "missing");
    CHECK(!wrong_root.IsAdmitted());
    CHECK(wrong_root.code ==
        RoR::BeamNG::JBeamVehicleImportCode::ROOT_PART_NOT_FOUND);
    CHECK(wrong_root.document == nullptr);
    CHECK(wrong_root.authority == nullptr);
}

void TestHostileArchives()
{
    std::string unsupported_vehicle = SupportedVehicle();
    std::size_t closing = unsupported_vehicle.rfind('}');
    CHECK(closing != std::string::npos && closing != 0U);
    closing = unsupported_vehicle.rfind('}', closing - 1U);
    CHECK(closing != std::string::npos);
    unsupported_vehicle.insert(closing, ",\"wheels\":[]");
    const std::vector<std::uint8_t> unsupported_archive = BuildArchive({
        {"vehicles/test/main.jbeam",
         unsupported_vehicle,
         0U,
         false}});
    const TempArchive unsupported_file(unsupported_archive);
    const RoR::TerrainBundleAuthenticatedArchiveSnapshot unsupported =
        LoadSnapshot(unsupported_archive, unsupported_file);
    const RoR::BeamNG::JBeamVehicleImportResult rejected =
        RoR::BeamNG::ImportJBeamVehicleFromArchiveSnapshot(
            unsupported, "beamng-test-group", "test_vehicle");
    if (rejected.code !=
        RoR::BeamNG::JBeamVehicleImportCode::UNSUPPORTED_ACTIVE_SECTION)
    {
        std::cerr << "unsupported fixture returned "
                  << RoR::BeamNG::JBeamVehicleImportCodeToString(
                         rejected.code)
                  << ": " << rejected.detail << '\n';
    }
    CHECK(!rejected.IsAdmitted());
    CHECK(rejected.code ==
        RoR::BeamNG::JBeamVehicleImportCode::UNSUPPORTED_ACTIVE_SECTION);
    CHECK(rejected.document == nullptr);
    CHECK(rejected.authority == nullptr);

    const std::vector<std::uint8_t> ogre_script_archive = BuildArchive({
        {"vehicles/test/main.jbeam", SupportedVehicle(), 0U, false},
        {"vehicles/test/hostile.material",
         "material MustNeverParse {}\n",
         0U,
         false}});
    const TempArchive ogre_script_file(ogre_script_archive);
    const RoR::TerrainBundleAuthenticatedArchiveSnapshot
        ogre_script_snapshot =
            LoadSnapshot(ogre_script_archive, ogre_script_file);
    const RoR::BeamNG::JBeamVehiclePackageInspection
        ogre_script_inspection =
            RoR::BeamNG::InspectJBeamVehicleArchiveSnapshot(
                ogre_script_snapshot);
    CHECK(!ogre_script_inspection.IsValid());
    CHECK(ogre_script_inspection.code ==
        RoR::BeamNG::JBeamVehicleImportCode::UNSAFE_OGRE_SCRIPT_MEMBER);
    CHECK(ogre_script_inspection.candidates.empty());
    const RoR::BeamNG::JBeamVehicleImportResult ogre_script_import =
        RoR::BeamNG::ImportJBeamVehicleFromArchiveSnapshot(
            ogre_script_snapshot, "beamng-test-group", "test_vehicle");
    CHECK(!ogre_script_import.IsAdmitted());
    CHECK(ogre_script_import.code ==
        RoR::BeamNG::JBeamVehicleImportCode::UNSAFE_OGRE_SCRIPT_MEMBER);
    CHECK(ogre_script_import.document == nullptr);
    CHECK(ogre_script_import.authority == nullptr);

    const std::vector<std::uint8_t> corrupt_archive = BuildArchive({
        {"vehicles/test/main.jbeam", SupportedVehicle(), 0U, true}});
    const TempArchive corrupt_file(corrupt_archive);
    const RoR::TerrainBundleAuthenticatedArchiveSnapshot corrupt =
        LoadSnapshot(corrupt_archive, corrupt_file);
    const RoR::BeamNG::JBeamVehiclePackageInspection corrupt_inspection =
        RoR::BeamNG::InspectJBeamVehicleArchiveSnapshot(corrupt);
    CHECK(!corrupt_inspection.IsValid());
    CHECK(corrupt_inspection.code ==
        RoR::BeamNG::JBeamVehicleImportCode::JBEAM_MEMBER_DECODE_REJECTED);
    CHECK(corrupt_inspection.candidates.empty());

    RoR::BeamNG::JBeamVehicleImportLimits zero_members;
    zero_members.max_jbeam_members = 0U;
    const std::vector<std::uint8_t> valid_archive = BuildArchive({
        {"vehicles/test/main.jbeam", SupportedVehicle(), 0U, false}});
    const TempArchive valid_file(valid_archive);
    const RoR::TerrainBundleAuthenticatedArchiveSnapshot valid =
        LoadSnapshot(valid_archive, valid_file);
    const RoR::BeamNG::JBeamVehiclePackageInspection limited =
        RoR::BeamNG::InspectJBeamVehicleArchiveSnapshot(
            valid, zero_members);
    CHECK(!limited.IsValid());
    CHECK(limited.code ==
        RoR::BeamNG::JBeamVehicleImportCode::JBEAM_MEMBER_LIMIT);
    CHECK(limited.candidates.empty());

    const std::string no_main =
        "{\"aux_part\":{\"slotType\":\"auxiliary\"}}";
    const std::vector<std::uint8_t> no_main_archive = BuildArchive({
        {"vehicles/test/secondary.jbeam", no_main, 0U, false}});
    const TempArchive no_main_file(no_main_archive);
    const RoR::TerrainBundleAuthenticatedArchiveSnapshot no_main_snapshot =
        LoadSnapshot(no_main_archive, no_main_file);
    const RoR::BeamNG::JBeamVehiclePackageInspection no_main_result =
        RoR::BeamNG::InspectJBeamVehicleArchiveSnapshot(no_main_snapshot);
    if (no_main_result.code !=
        RoR::BeamNG::JBeamVehicleImportCode::NO_MAIN_PART)
    {
        std::cerr << "no-main fixture returned "
                  << RoR::BeamNG::JBeamVehicleImportCodeToString(
                         no_main_result.code)
                  << ": " << no_main_result.detail << '\n';
    }
    CHECK(!no_main_result.IsValid());
    CHECK(no_main_result.code ==
        RoR::BeamNG::JBeamVehicleImportCode::NO_MAIN_PART);

    const RoR::BeamNG::JBeamVehicleImportAuthorityReceipt empty_authority;
    CHECK(!empty_authority.initialized());
    CHECK(empty_authority.version() == 0U);
    CHECK(std::string(RoR::BeamNG::JBeamVehicleImportCodeToString(
        RoR::BeamNG::JBeamVehicleImportCode::UNSUPPORTED_ACTIVE_SECTION)) ==
        "unsupported-active-section");
    CHECK(std::string(RoR::BeamNG::JBeamVehicleImportCodeToString(
        RoR::BeamNG::JBeamVehicleImportCode::UNSAFE_OGRE_SCRIPT_MEMBER)) ==
        "unsafe-ogre-script-member");
}

} // namespace

int main()
{
    TestAdmissionAndAuthority();
    TestPressureWheelProductImport();
    TestHostileArchives();
    if (g_failures != 0)
    {
        std::cerr << g_failures << " JBeam vehicle importer checks failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "JBeam vehicle importer checks passed\n";
    return EXIT_SUCCESS;
}
