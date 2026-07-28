#include "BeamNGMaterialInventory.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <set>
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

using RoR::BeamNG::BeamNGMaterialDiagnosticCode;
using RoR::BeamNG::BeamNGMaterialDisposition;
using RoR::BeamNG::BeamNGMaterialInventory;
using RoR::BeamNG::BeamNGMaterialLimits;
using RoR::BeamNG::BeamNGMaterialSource;
using RoR::BeamNG::BeamNGTextureReferenceStatus;
using RoR::BeamNG::PackageEntryInput;
using RoR::BeamNG::PackageEntryKind;
using RoR::BeamNG::PackageManifest;
using RoR::BeamNG::PackageManifestResult;

PackageEntryInput File(const std::string& path)
{
    PackageEntryInput entry;
    entry.path = path;
    entry.kind = PackageEntryKind::REGULAR_FILE;
    entry.compressed_size = 1U;
    entry.expanded_size = 1U;
    return entry;
}

PackageEntryInput Directory(const std::string& path)
{
    PackageEntryInput entry;
    entry.path = path;
    entry.kind = PackageEntryKind::DIRECTORY;
    return entry;
}

PackageManifest Manifest(
    const std::vector<std::string>& file_paths)
{
    std::vector<PackageEntryInput> entries;
    for (std::size_t i = 0U; i < file_paths.size(); ++i)
    {
        entries.push_back(File(file_paths[i]));
    }
    const PackageManifestResult result =
        RoR::BeamNG::BuildPackageManifest(entries);
    CHECK(result.IsValid());
    return result.manifest;
}

BeamNGMaterialSource Source(
    const std::string& path,
    const std::string& bytes)
{
    BeamNGMaterialSource source;
    source.package_path = path;
    source.source_bytes = bytes;
    return source;
}

bool HasCode(
    const BeamNGMaterialInventory& inventory,
    BeamNGMaterialDiagnosticCode code)
{
    for (std::size_t i = 0U;
         i < inventory.diagnostics.size();
         ++i)
    {
        if (inventory.diagnostics[i].code == code)
        {
            return true;
        }
    }
    return false;
}

std::size_t CountCode(
    const BeamNGMaterialInventory& inventory,
    BeamNGMaterialDiagnosticCode code)
{
    std::size_t count = 0U;
    for (std::size_t i = 0U;
         i < inventory.diagnostics.size();
         ++i)
    {
        if (inventory.diagnostics[i].code == code)
        {
            ++count;
        }
    }
    return count;
}

std::size_t CountObjectKey(
    const RoR::BeamNG::JBeamValue& object,
    const std::string& key)
{
    if (object.type != RoR::BeamNG::JBeamValueType::OBJECT)
    {
        return 0U;
    }
    std::size_t count = 0U;
    for (std::size_t i = 0U;
         i < object.object_fields.size();
         ++i)
    {
        if (object.object_fields[i].key == key)
        {
            ++count;
        }
    }
    return count;
}

void TestDocumentationProfile()
{
    const RoR::BeamNG::BeamNGMaterialDocumentationProfile& profile =
        RoR::BeamNG::GetBeamNGMaterialDocumentationProfile();
    CHECK(profile.beamng_version == "0.38.5.0");
    CHECK(profile.last_modified == "2026-07-08");
    CHECK(profile.texture_cooker_last_modified == "2026-04-22");
    CHECK(
        profile.source_url ==
        "https://documentation.beamng.com/modding/file_formats/"
        "materials/");
    CHECK(
        profile.texture_cooker_url ==
        "https://documentation.beamng.com/modding/materials/"
        "texture_cooker/");
}

std::vector<BeamNGMaterialSource> RepresentativeSources()
{
    const std::string first =
        "{\n"
        "  \"formula_body\": {\n"
        "    \"name\": \"old_name\",\n"
        "    \"name\": \"formula_body\",\n"
        "    \"class\": \"Material\",\n"
        "    \"mapTo\": \"formula_body\",\n"
        "    \"baseColorMap\": [\n"
        "      \"/vehicles/formula/Body.color.png\",\n"
        "      \"@DynamicTextureBaseColor\"\n"
        "    ],\n"
        "    \"roughnessFactor\": [1],\n"
        "    \"unknownRendererField\": {\n"
        "      \"shape\": [1, {\"x\": true}],\n"
        "      \"dup\": 1,\n"
        "      \"dup\": 2\n"
        "    },\n"
        "    \"Stages\": [\n"
        "      {\n"
        "        \"normalMap\": "
        "\"/vehicles/formula/Body.normal.png\",\n"
        "        \"metallicFactor\": 1\n"
        "      },\n"
        "      {},\n"
        "      null\n"
        "    ]\n"
        "  }\n"
        "}\n";
    const std::string second =
        "{\n"
        "  \"formula_screen\": {\n"
        "    \"class\": \"Material\",\n"
        "    \"name\": \"formula_screen\",\n"
        "    \"mapTo\": \"formula_screen\",\n"
        "    \"Stages\": [\n"
        "      {\n"
        "        \"baseColorFactor\": [0.09, 0.09, 0.09, 1],\n"
        "        \"emissiveFactor\": [1, 1, 1],\n"
        "        \"emissiveMap\": \"@formula_screen\"\n"
        "      }, {}, {}, {}\n"
        "    ],\n"
        "    \"version\": 1.5\n"
        "  }\n"
        "}\n";
    std::vector<BeamNGMaterialSource> sources;
    sources.push_back(Source(
        "vehicles/formula/z.materials.json",
        second));
    sources.push_back(Source(
        "vehicles/formula/a.materials.json",
        first));
    return sources;
}

PackageManifest RepresentativeManifest()
{
    return Manifest({
        "vehicles/formula/z.materials.json",
        "vehicles/formula/a.materials.json",
        "vehicles/formula/Body.color.png",
        "vehicles/formula/Body.normal.png"
    });
}

void TestSortedEnumerationAndPreservation()
{
    const PackageManifest manifest = RepresentativeManifest();
    const std::vector<BeamNGMaterialSource> reversed =
        RepresentativeSources();
    std::vector<BeamNGMaterialSource> forward = reversed;
    std::reverse(forward.begin(), forward.end());

    const BeamNGMaterialInventory first =
        RoR::BeamNG::BuildBeamNGMaterialInventory(
            "formula-coupe", manifest, reversed);
    const BeamNGMaterialInventory second =
        RoR::BeamNG::BuildBeamNGMaterialInventory(
            "formula-coupe", manifest, forward);
    CHECK(first.IsValid());
    CHECK(second.IsValid());
    CHECK(first.sources.size() == 2U);
    CHECK(first.materials.size() == 2U);
    CHECK(
        first.sources[0].package_path ==
        "vehicles/formula/a.materials.json");
    CHECK(
        first.sources[1].package_path ==
        "vehicles/formula/z.materials.json");
    CHECK(first.materials[0].material_key == "formula_body");
    CHECK(first.materials[0].name.assignment_count == 2U);
    CHECK(first.materials[0].name.type_valid);
    CHECK(first.materials[0].name.value == "formula_body");
    CHECK(
        first.materials[0].disposition ==
        BeamNGMaterialDisposition::INVENTORY_ONLY);
    CHECK(first.materials[0].authored_stage_count == 3U);
    CHECK(first.materials[1].authored_stage_count == 4U);
    CHECK(first.authored_stage_count == 7U);
    CHECK(first.texture_reference_count == 4U);
    CHECK(
        first.materials[0].texture_references[0].status ==
        BeamNGTextureReferenceStatus::LOCAL_FOUND);
    CHECK(
        first.materials[0].texture_references[1].status ==
        BeamNGTextureReferenceStatus::DYNAMIC_TEXTURE);
    CHECK(
        first.materials[0].texture_references[2].status ==
        BeamNGTextureReferenceStatus::LOCAL_FOUND);
    CHECK(
        first.materials[1].texture_references[0].status ==
        BeamNGTextureReferenceStatus::DYNAMIC_TEXTURE);
    CHECK(
        first.materials[0].material_key_span.source_name ==
        "vehicles/formula/a.materials.json");
    CHECK(first.materials[0].material_key_span.begin.line == 2U);
    CHECK(
        first.materials[0].scoped_resource_name !=
        first.materials[1].scoped_resource_name);

    const RoR::BeamNG::JBeamObjectField* unknown =
        RoR::BeamNG::FindLastJBeamObjectField(
            *first.materials[0].raw_definition,
            "unknownRendererField");
    CHECK(unknown != NULL);
    CHECK(static_cast<bool>(unknown->value));
    CHECK(
        unknown->value->type ==
        RoR::BeamNG::JBeamValueType::OBJECT);
    CHECK(CountObjectKey(*unknown->value, "dup") == 2U);
    const RoR::BeamNG::JBeamObjectField* shape =
        RoR::BeamNG::FindLastJBeamObjectField(
            *unknown->value, "shape");
    CHECK(shape != NULL);
    CHECK(static_cast<bool>(shape->value));
    CHECK(
        shape->value->type ==
        RoR::BeamNG::JBeamValueType::ARRAY);
    CHECK(shape->value->array_values.size() == 2U);
    CHECK(
        shape->value->array_values[1].type ==
        RoR::BeamNG::JBeamValueType::OBJECT);

    const std::string canonical_first =
        RoR::BeamNG::
            SerializeCanonicalBeamNGMaterialInventory(first);
    const std::string canonical_second =
        RoR::BeamNG::
            SerializeCanonicalBeamNGMaterialInventory(second);
    CHECK(!canonical_first.empty());
    CHECK(canonical_first == canonical_second);
    CHECK(
        canonical_first ==
        RoR::BeamNG::
            SerializeCanonicalBeamNGMaterialInventory(first));
}

void TestTextureResolutionStatesAndTraversal()
{
    const PackageManifest manifest = Manifest({
        "vehicles/car/main.materials.json",
        "vehicles/car/Exact.COLOR.PNG",
        "vehicles/car/cooked.data.dds"
    });
    const std::string json =
        "{\n"
        " \"material\": {\n"
        "  \"class\": \"Material\",\n"
        "  \"baseColorMap\": [\n"
        "   \"/vehicles/car/exact.color.png\",\n"
        "   \"missing.png\",\n"
        "   \"../escape.png\",\n"
        "   \"@\",\n"
        "   \"/vehicles/car/cooked.data.png\"\n"
        "  ]\n"
        " }\n"
        "}\n";
    const BeamNGMaterialInventory inventory =
        RoR::BeamNG::BuildBeamNGMaterialInventory(
            "texture-test",
            manifest,
            std::vector<BeamNGMaterialSource>(
                1U,
                Source(
                    "vehicles/car/main.materials.json",
                    json)));
    CHECK(inventory.IsValid());
    CHECK(inventory.materials.size() == 1U);
    CHECK(
        inventory.materials[0].disposition ==
        BeamNGMaterialDisposition::PLACEHOLDER);
    CHECK(
        inventory.materials[0].texture_references.size() == 5U);
    CHECK(
        inventory.materials[0].texture_references[0].status ==
        BeamNGTextureReferenceStatus::LOCAL_CASE_MISMATCH);
    CHECK(
        inventory.materials[0].
            texture_references[0].resolved_manifest_path ==
        "vehicles/car/Exact.COLOR.PNG");
    CHECK(
        inventory.materials[0].texture_references[1].status ==
        BeamNGTextureReferenceStatus::LOCAL_MISSING);
    CHECK(
        inventory.materials[0].
            texture_references[1].candidate_path ==
        "vehicles/car/missing.png");
    CHECK(
        inventory.materials[0].texture_references[2].status ==
        BeamNGTextureReferenceStatus::INVALID_PATH);
    CHECK(
        inventory.materials[0].
            texture_references[2].candidate_path.empty());
    CHECK(
        inventory.materials[0].texture_references[3].status ==
        BeamNGTextureReferenceStatus::INVALID_PATH);
    CHECK(
        inventory.materials[0].texture_references[4].status ==
        BeamNGTextureReferenceStatus::LOCAL_COOKED_DDS);
    CHECK(
        inventory.materials[0].
            texture_references[4].resolved_manifest_path ==
        "vehicles/car/cooked.data.dds");
    CHECK(
        HasCode(
            inventory,
            BeamNGMaterialDiagnosticCode::
                TEXTURE_CASE_MISMATCH));
    CHECK(
        HasCode(
            inventory,
            BeamNGMaterialDiagnosticCode::MISSING_TEXTURE));
    CHECK(
        HasCode(
            inventory,
            BeamNGMaterialDiagnosticCode::
                INVALID_TEXTURE_PATH));
}

void TestDuplicateCollisionHistory()
{
    const PackageManifest manifest = Manifest({
        "vehicles/car/a.materials.json",
        "vehicles/car/b.materials.json"
    });
    const std::string first =
        "{\"same\":{\"class\":\"Material\","
        "\"name\":\"same_name\",\"unknown\":[1,2]}}";
    const std::string second =
        "{\"same\":{\"class\":\"Material\","
        "\"name\":\"same_name\",\"unknown\":[3,4]}}";
    std::vector<BeamNGMaterialSource> sources;
    sources.push_back(Source(
        "vehicles/car/b.materials.json", second));
    sources.push_back(Source(
        "vehicles/car/a.materials.json", first));
    const BeamNGMaterialInventory inventory =
        RoR::BeamNG::BuildBeamNGMaterialInventory(
            "duplicates", manifest, sources);
    CHECK(inventory.IsValid());
    CHECK(inventory.materials.size() == 2U);
    CHECK(
        inventory.materials[0].same_key_material_indices.size() ==
        2U);
    CHECK(
        inventory.materials[0].same_key_material_indices[0] == 0U);
    CHECK(
        inventory.materials[0].same_key_material_indices[1] == 1U);
    CHECK(
        inventory.materials[1].same_key_material_indices ==
        inventory.materials[0].same_key_material_indices);
    CHECK(
        inventory.materials[0].scoped_resource_name !=
        inventory.materials[1].scoped_resource_name);
    CHECK(
        inventory.materials[0].disposition ==
        BeamNGMaterialDisposition::PLACEHOLDER);
    CHECK(
        inventory.materials[1].disposition ==
        BeamNGMaterialDisposition::PLACEHOLDER);
    CHECK(
        HasCode(
            inventory,
            BeamNGMaterialDiagnosticCode::
                DUPLICATE_MATERIAL_KEY));
    CHECK(
        HasCode(
            inventory,
            BeamNGMaterialDiagnosticCode::
                DUPLICATE_MATERIAL_NAME));
}

void TestPreservedDisabledAndUnknownShapes()
{
    const PackageManifest manifest = Manifest({
        "levels/test/main.materials.json"
    });
    const std::string json =
        "{"
        "\"terrain\":{"
        "\"class\":\"TerrainMaterial\","
        "\"rendererExtension\":{\"nested\":[1,true,null,{\"x\":2}]}"
        "},"
        "\"notAnObject\":[1,{\"preserved\":true}]"
        "}";
    const BeamNGMaterialInventory inventory =
        RoR::BeamNG::BuildBeamNGMaterialInventory(
            "preserved",
            manifest,
            std::vector<BeamNGMaterialSource>(
                1U,
                Source(
                    "levels/test/main.materials.json",
                    json)));
    CHECK(inventory.IsValid());
    CHECK(inventory.materials.size() == 2U);
    CHECK(
        inventory.materials[0].disposition ==
        BeamNGMaterialDisposition::PRESERVED_DISABLED);
    CHECK(
        inventory.materials[1].disposition ==
        BeamNGMaterialDisposition::PRESERVED_DISABLED);
    CHECK(
        inventory.materials[1].raw_definition->type ==
        RoR::BeamNG::JBeamValueType::ARRAY);
    CHECK(
        HasCode(
            inventory,
            BeamNGMaterialDiagnosticCode::UNSUPPORTED_CLASS));
    CHECK(
        HasCode(
            inventory,
            BeamNGMaterialDiagnosticCode::
                MATERIAL_DEFINITION_NOT_OBJECT));
}

void TestRecognizedInputsRejectNestedArrays()
{
    const PackageManifest manifest = Manifest({
        "vehicles/car/main.materials.json",
        "vehicles/car/texture.png"
    });
    const BeamNGMaterialInventory inventory =
        RoR::BeamNG::BuildBeamNGMaterialInventory(
            "nested-shapes",
            manifest,
            std::vector<BeamNGMaterialSource>(
                1U,
                Source(
                    "vehicles/car/main.materials.json",
                    "{"
                    "\"effective\":{"
                    "\"baseColorFactor\":[[1]],"
                    "\"translucent\":[[true]],"
                    "\"baseColorMap\":[[\"texture.png\"]]"
                    "},"
                    "\"superseded\":{"
                    "\"roughnessFactor\":[[1]],"
                    "\"roughnessFactor\":[1],"
                    "\"baseColorMap\":[[\"missing.png\"]],"
                    "\"baseColorMap\":[\"texture.png\"]"
                    "}"
                    "}")));
    CHECK(inventory.IsValid());
    CHECK(inventory.materials.size() == 2U);
    CHECK(
        inventory.materials[0].disposition ==
        BeamNGMaterialDisposition::PLACEHOLDER);
    CHECK(
        inventory.materials[1].disposition ==
        BeamNGMaterialDisposition::INVENTORY_ONLY);
    CHECK(
        CountCode(
            inventory,
            BeamNGMaterialDiagnosticCode::INVALID_PBR_INPUT) == 5U);
    CHECK(inventory.materials[0].texture_references.empty());
    CHECK(inventory.materials[1].texture_references.size() == 1U);
    CHECK(
        inventory.materials[1].texture_references[0].status ==
        BeamNGTextureReferenceStatus::LOCAL_FOUND);
    CHECK(
        static_cast<bool>(
            inventory.materials[0].raw_definition));
    CHECK(
        CountObjectKey(
            *inventory.materials[0].raw_definition,
            "baseColorFactor") == 1U);
    CHECK(
        CountObjectKey(
            *inventory.materials[1].raw_definition,
            "baseColorMap") == 2U);
}

void TestFatalRollbackScrubsDiagnosticIndices()
{
    const PackageManifest manifest = Manifest({
        "vehicles/car/a.materials.json",
        "vehicles/car/b.materials.json"
    });
    std::vector<BeamNGMaterialSource> sources;
    sources.push_back(Source(
        "vehicles/car/a.materials.json",
        "{\"first\":{\"baseColorMap\":\"missing.png\"}}"));
    sources.push_back(Source(
        "vehicles/car/b.materials.json",
        "{\"broken\":"));
    const BeamNGMaterialInventory inventory =
        RoR::BeamNG::BuildBeamNGMaterialInventory(
            "rollback", manifest, sources);
    CHECK(!inventory.IsValid());
    CHECK(inventory.sources.empty());
    CHECK(inventory.materials.empty());
    CHECK(inventory.authored_field_count == 0U);
    CHECK(inventory.authored_stage_count == 0U);
    CHECK(inventory.texture_reference_count == 0U);
    CHECK(
        HasCode(
            inventory,
            BeamNGMaterialDiagnosticCode::MISSING_TEXTURE));
    CHECK(
        HasCode(
            inventory,
            BeamNGMaterialDiagnosticCode::SOURCE_PARSE_DIAGNOSTIC));
    const std::size_t no_index =
        std::numeric_limits<std::size_t>::max();
    for (std::size_t i = 0U;
         i < inventory.diagnostics.size();
         ++i)
    {
        CHECK(inventory.diagnostics[i].source_index == no_index);
        CHECK(inventory.diagnostics[i].material_index == no_index);
    }
}

void TestNamespaceAndScopedIdentity()
{
    const PackageManifest manifest = Manifest({
        "vehicles/car/main.materials.json"
    });
    const std::string json =
        "{\"a/b\":{},\"a?b\":{}}";
    const std::vector<BeamNGMaterialSource> sources(
        1U,
        Source(
            "vehicles/car/main.materials.json",
            json));

    const BeamNGMaterialInventory invalid =
        RoR::BeamNG::BuildBeamNGMaterialInventory(
            "../escape", manifest, sources);
    CHECK(!invalid.IsValid());
    CHECK(invalid.materials.empty());
    CHECK(
        HasCode(
            invalid,
            BeamNGMaterialDiagnosticCode::
                INVALID_PACKAGE_NAMESPACE));

    const BeamNGMaterialInventory dash =
        RoR::BeamNG::BuildBeamNGMaterialInventory(
            "package-a", manifest, sources);
    const BeamNGMaterialInventory underscore =
        RoR::BeamNG::BuildBeamNGMaterialInventory(
            "package_a", manifest, sources);
    CHECK(dash.IsValid());
    CHECK(underscore.IsValid());
    CHECK(dash.materials.size() == 2U);
    CHECK(
        dash.materials[0].scoped_resource_name !=
        dash.materials[1].scoped_resource_name);
    CHECK(
        dash.materials[0].scoped_resource_name !=
        underscore.materials[0].scoped_resource_name);
}

void CheckRejectedByLimit(
    BeamNGMaterialLimits limits,
    BeamNGMaterialDiagnosticCode expected,
    const PackageManifest& manifest,
    const std::vector<BeamNGMaterialSource>& sources)
{
    const BeamNGMaterialInventory inventory =
        RoR::BeamNG::BuildBeamNGMaterialInventory(
            "quota", manifest, sources, limits);
    CHECK(!inventory.IsValid());
    CHECK(inventory.sources.empty());
    CHECK(inventory.materials.empty());
    CHECK(HasCode(inventory, expected));
}

void TestEveryAggregateQuotaFailsClosed()
{
    const PackageManifest manifest = Manifest({
        "vehicles/car/main.materials.json",
        "vehicles/car/texture.png"
    });
    const std::vector<BeamNGMaterialSource> sources(
        1U,
        Source(
            "vehicles/car/main.materials.json",
            "{\"m\":{\"class\":\"Material\","
            "\"baseColorMap\":[\"texture.png\"],"
            "\"Stages\":[{},{}],\"unknown\":1}}"));

    BeamNGMaterialLimits limits;
    limits.max_manifest_entries = 1U;
    CheckRejectedByLimit(
        limits,
        BeamNGMaterialDiagnosticCode::MANIFEST_ENTRY_LIMIT,
        manifest,
        sources);

    limits = BeamNGMaterialLimits();
    limits.max_sources = 0U;
    CheckRejectedByLimit(
        limits,
        BeamNGMaterialDiagnosticCode::SOURCE_LIMIT,
        manifest,
        sources);

    limits = BeamNGMaterialLimits();
    limits.max_total_source_bytes = 1U;
    CheckRejectedByLimit(
        limits,
        BeamNGMaterialDiagnosticCode::SOURCE_BYTE_LIMIT,
        manifest,
        sources);

    limits = BeamNGMaterialLimits();
    limits.max_materials = 0U;
    CheckRejectedByLimit(
        limits,
        BeamNGMaterialDiagnosticCode::MATERIAL_LIMIT,
        manifest,
        sources);

    limits = BeamNGMaterialLimits();
    limits.max_fields = 1U;
    CheckRejectedByLimit(
        limits,
        BeamNGMaterialDiagnosticCode::FIELD_LIMIT,
        manifest,
        sources);

    limits = BeamNGMaterialLimits();
    limits.max_stages = 1U;
    CheckRejectedByLimit(
        limits,
        BeamNGMaterialDiagnosticCode::STAGE_LIMIT,
        manifest,
        sources);

    limits = BeamNGMaterialLimits();
    limits.max_texture_references = 0U;
    CheckRejectedByLimit(
        limits,
        BeamNGMaterialDiagnosticCode::TEXTURE_LIMIT,
        manifest,
        sources);

    limits = BeamNGMaterialLimits();
    limits.max_retained_bytes = 1U;
    CheckRejectedByLimit(
        limits,
        BeamNGMaterialDiagnosticCode::RETAINED_BYTE_LIMIT,
        manifest,
        sources);

    limits = BeamNGMaterialLimits();
    limits.max_work_units = 1U;
    CheckRejectedByLimit(
        limits,
        BeamNGMaterialDiagnosticCode::WORK_LIMIT,
        manifest,
        sources);

    limits = BeamNGMaterialLimits();
    limits.max_value_depth = 2U;
    CheckRejectedByLimit(
        limits,
        BeamNGMaterialDiagnosticCode::SOURCE_PARSE_DIAGNOSTIC,
        manifest,
        sources);

    limits = BeamNGMaterialLimits();
    limits.max_scoped_resource_name_bytes = 1U;
    CheckRejectedByLimit(
        limits,
        BeamNGMaterialDiagnosticCode::
            SCOPED_RESOURCE_NAME_LIMIT,
        manifest,
        sources);

    limits = BeamNGMaterialLimits();
    limits.max_canonical_output_bytes = 8U;
    CheckRejectedByLimit(
        limits,
        BeamNGMaterialDiagnosticCode::CANONICAL_OUTPUT_LIMIT,
        manifest,
        sources);

    limits = BeamNGMaterialLimits();
    limits.max_canonical_work_units = 1U;
    CheckRejectedByLimit(
        limits,
        BeamNGMaterialDiagnosticCode::CANONICAL_OUTPUT_LIMIT,
        manifest,
        sources);

    const std::vector<BeamNGMaterialSource> missing_texture(
        1U,
        Source(
            "vehicles/car/main.materials.json",
            "{\"m\":{\"baseColorMap\":\"missing.png\"}}"));
    limits = BeamNGMaterialLimits();
    limits.max_diagnostics = 0U;
    CheckRejectedByLimit(
        limits,
        BeamNGMaterialDiagnosticCode::DIAGNOSTIC_LIMIT,
        manifest,
        missing_texture);
}

void TestHostileMalformedInputsFailClosed()
{
    const PackageManifest manifest = Manifest({
        "vehicles/car/main.materials.json"
    });
    BeamNGMaterialInventory inventory =
        RoR::BeamNG::BuildBeamNGMaterialInventory(
            "hostile",
            manifest,
            std::vector<BeamNGMaterialSource>(
                1U,
                Source(
                    "vehicles/car/main.materials.json",
                    "{\"m\":{\"class\":\"Material\"")));
    CHECK(!inventory.IsValid());
    CHECK(inventory.materials.empty());
    CHECK(
        HasCode(
            inventory,
            BeamNGMaterialDiagnosticCode::
                SOURCE_PARSE_DIAGNOSTIC));

    inventory =
        RoR::BeamNG::BuildBeamNGMaterialInventory(
            "hostile",
            manifest,
            std::vector<BeamNGMaterialSource>());
    CHECK(!inventory.IsValid());
    CHECK(
        HasCode(
            inventory,
            BeamNGMaterialDiagnosticCode::
                MATERIAL_SOURCE_NOT_SUPPLIED));

    inventory =
        RoR::BeamNG::BuildBeamNGMaterialInventory(
            "hostile",
            manifest,
            std::vector<BeamNGMaterialSource>(
                1U,
                Source(
                    "vehicles/car/main.materials.json",
                    "[]")));
    CHECK(!inventory.IsValid());
    CHECK(inventory.materials.empty());
    CHECK(
        HasCode(
            inventory,
            BeamNGMaterialDiagnosticCode::
                SOURCE_ROOT_NOT_OBJECT));

    std::vector<BeamNGMaterialSource> duplicate_sources;
    duplicate_sources.push_back(Source(
        "vehicles/car/main.materials.json", "{}"));
    duplicate_sources.push_back(Source(
        "vehicles/car/main.materials.json", "{}"));
    inventory =
        RoR::BeamNG::BuildBeamNGMaterialInventory(
            "hostile", manifest, duplicate_sources);
    CHECK(!inventory.IsValid());
    CHECK(
        HasCode(
            inventory,
            BeamNGMaterialDiagnosticCode::
                DUPLICATE_SOURCE_PATH));

    inventory =
        RoR::BeamNG::BuildBeamNGMaterialInventory(
            "hostile",
            manifest,
            std::vector<BeamNGMaterialSource>(
                1U,
                Source(
                    "vehicles/car/../main.materials.json",
                    "{}")));
    CHECK(!inventory.IsValid());
    CHECK(
        HasCode(
            inventory,
            BeamNGMaterialDiagnosticCode::INVALID_SOURCE_PATH));

    PackageManifest corrupt = manifest;
    corrupt.entries.push_back(corrupt.entries[0]);
    inventory =
        RoR::BeamNG::BuildBeamNGMaterialInventory(
            "hostile",
            corrupt,
            std::vector<BeamNGMaterialSource>());
    CHECK(!inventory.IsValid());
    CHECK(
        HasCode(
            inventory,
            BeamNGMaterialDiagnosticCode::
                MANIFEST_DUPLICATE_PATH));
}

void TestAllTruncationsAndNulMutationsFailClosed()
{
    const PackageManifest manifest = Manifest({
        "vehicles/car/main.materials.json",
        "vehicles/car/texture.png"
    });
    const std::string valid =
        "{\"material\":{\"class\":\"Material\","
        "\"name\":\"material\","
        "\"baseColorMap\":[\"texture.png\"],"
        "\"Stages\":[{\"roughnessFactor\":0.5}]}}";
    for (std::size_t length = 0U;
         length < valid.size();
         ++length)
    {
        const BeamNGMaterialInventory inventory =
            RoR::BeamNG::BuildBeamNGMaterialInventory(
                "truncation",
                manifest,
                std::vector<BeamNGMaterialSource>(
                    1U,
                    Source(
                        "vehicles/car/main.materials.json",
                        valid.substr(0U, length))));
        CHECK(!inventory.IsValid());
        CHECK(inventory.materials.empty());
    }
    for (std::size_t offset = 0U;
         offset < valid.size();
         ++offset)
    {
        std::string mutated(valid);
        mutated[offset] = '\0';
        const BeamNGMaterialInventory inventory =
            RoR::BeamNG::BuildBeamNGMaterialInventory(
                "nul-mutation",
                manifest,
                std::vector<BeamNGMaterialSource>(
                    1U,
                    Source(
                        "vehicles/car/main.materials.json",
                        mutated)));
        CHECK(!inventory.IsValid());
        CHECK(inventory.materials.empty());
    }
}

void TestCanonicalIdentityIsExact()
{
    const PackageManifest manifest = Manifest({
        "vehicles/car/main.materials.json"
    });
    const BeamNGMaterialInventory integer =
        RoR::BeamNG::BuildBeamNGMaterialInventory(
            "identity",
            manifest,
            std::vector<BeamNGMaterialSource>(
                1U,
                Source(
                    "vehicles/car/main.materials.json",
                    "{\"m\":{\"unknown\":1}}")));
    const BeamNGMaterialInventory decimal =
        RoR::BeamNG::BuildBeamNGMaterialInventory(
            "identity",
            manifest,
            std::vector<BeamNGMaterialSource>(
                1U,
                Source(
                    "vehicles/car/main.materials.json",
                    "{\"m\":{\"unknown\":1.0}}")));
    const BeamNGMaterialInventory reordered =
        RoR::BeamNG::BuildBeamNGMaterialInventory(
            "identity",
            manifest,
            std::vector<BeamNGMaterialSource>(
                1U,
                Source(
                    "vehicles/car/main.materials.json",
                    "{\"m\":{\"other\":2,\"unknown\":1}}")));
    CHECK(integer.IsValid());
    CHECK(decimal.IsValid());
    CHECK(reordered.IsValid());
    const std::string integer_identity =
        RoR::BeamNG::
            SerializeCanonicalBeamNGMaterialInventory(integer);
    CHECK(!integer_identity.empty());
    CHECK(
        integer_identity !=
        RoR::BeamNG::
            SerializeCanonicalBeamNGMaterialInventory(decimal));
    CHECK(
        integer_identity !=
        RoR::BeamNG::
            SerializeCanonicalBeamNGMaterialInventory(reordered));
}

std::string ReadFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return std::string();
    }
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

void TestFormulaCoupeOptIn()
{
    const char* root_env =
        std::getenv("ROR_FORMULACOUPE_MATERIAL_ROOT");
    const char* entry_list_env =
        std::getenv("ROR_FORMULACOUPE_ENTRY_LIST");
    if (root_env == NULL || entry_list_env == NULL)
    {
        std::cout
            << "FormulaCOUPE material inventory opt-in skipped; set "
            << "ROR_FORMULACOUPE_MATERIAL_ROOT and "
            << "ROR_FORMULACOUPE_ENTRY_LIST\n";
        return;
    }

    std::ifstream entry_list(entry_list_env);
    CHECK(static_cast<bool>(entry_list));
    if (!entry_list)
    {
        return;
    }
    std::vector<PackageEntryInput> entries;
    std::string path;
    while (std::getline(entry_list, path))
    {
        if (path.empty())
        {
            continue;
        }
        if (path[path.size() - 1U] == '/')
        {
            entries.push_back(Directory(path));
        }
        else
        {
            entries.push_back(File(path));
        }
    }
    const PackageManifestResult manifest =
        RoR::BeamNG::BuildPackageManifest(entries);
    CHECK(manifest.IsValid());
    if (!manifest.IsValid())
    {
        return;
    }

    const std::filesystem::path root(root_env);
    std::vector<BeamNGMaterialSource> sources;
    for (std::filesystem::recursive_directory_iterator it(root), end;
         it != end;
         ++it)
    {
        if (!it->is_regular_file())
        {
            continue;
        }
        const std::filesystem::path relative =
            std::filesystem::relative(it->path(), root);
        const std::string package_path =
            relative.generic_string();
        if (!(
                package_path.size() >= 15U &&
                package_path.compare(
                    package_path.size() - 15U,
                    15U,
                    ".materials.json") == 0))
        {
            continue;
        }
        sources.push_back(Source(
            package_path,
            ReadFile(it->path())));
    }
    CHECK(sources.size() == 3U);
    const BeamNGMaterialInventory inventory =
        RoR::BeamNG::BuildBeamNGMaterialInventory(
            "gd808-formulacoupe-0.9.7",
            manifest.manifest,
            sources);
    CHECK(inventory.IsValid());
    CHECK(inventory.sources.size() == 3U);
    CHECK(inventory.materials.size() == 70U);
    CHECK(inventory.authored_stage_count == 280U);
    CHECK(inventory.texture_reference_count == 335U);
    bool saw_dynamic = false;
    bool saw_stage_input = false;
    std::size_t inventory_only = 0U;
    std::size_t placeholder = 0U;
    std::size_t preserved_disabled = 0U;
    std::size_t found = 0U;
    std::size_t cooked = 0U;
    std::size_t dynamic = 0U;
    std::size_t missing = 0U;
    std::size_t case_mismatch = 0U;
    std::size_t invalid = 0U;
    for (std::size_t i = 0U;
         i < inventory.materials.size();
         ++i)
    {
        switch (inventory.materials[i].disposition)
        {
        case BeamNGMaterialDisposition::INVENTORY_ONLY:
            ++inventory_only;
            break;
        case BeamNGMaterialDisposition::PLACEHOLDER:
            ++placeholder;
            break;
        case BeamNGMaterialDisposition::PRESERVED_DISABLED:
            ++preserved_disabled;
            break;
        }
        for (std::size_t j = 0U;
             j < inventory.materials[i].texture_references.size();
             ++j)
        {
            switch (inventory.materials[i].
                        texture_references[j].status)
            {
            case BeamNGTextureReferenceStatus::LOCAL_FOUND:
                ++found;
                break;
            case BeamNGTextureReferenceStatus::LOCAL_COOKED_DDS:
                ++cooked;
                break;
            case BeamNGTextureReferenceStatus::DYNAMIC_TEXTURE:
                ++dynamic;
                saw_dynamic = true;
                break;
            case BeamNGTextureReferenceStatus::LOCAL_MISSING:
                ++missing;
                break;
            case BeamNGTextureReferenceStatus::LOCAL_CASE_MISMATCH:
                ++case_mismatch;
                break;
            case BeamNGTextureReferenceStatus::INVALID_PATH:
                ++invalid;
                break;
            }
            if (inventory.materials[i].
                    texture_references[j].scope ==
                RoR::BeamNG::BeamNGMaterialFieldScope::STAGE)
            {
                saw_stage_input = true;
            }
        }
    }
    CHECK(saw_dynamic);
    CHECK(saw_stage_input);
    CHECK(inventory_only == 54U);
    CHECK(placeholder == 16U);
    CHECK(preserved_disabled == 0U);
    CHECK(found == 0U);
    CHECK(cooked == 281U);
    CHECK(dynamic == 7U);
    CHECK(missing == 19U);
    CHECK(case_mismatch == 28U);
    CHECK(invalid == 0U);
    CHECK(
        !RoR::BeamNG::
            SerializeCanonicalBeamNGMaterialInventory(inventory).
                empty());
    std::cout
        << "FormulaCOUPE material inventory: "
        << inventory.materials.size() << " materials, "
        << inventory.authored_stage_count << " stages, "
        << inventory.texture_reference_count
        << " texture references; dispositions "
        << inventory_only << " inventory-only, "
        << placeholder << " placeholder, "
        << preserved_disabled << " preserved-disabled; textures "
        << found << " found, "
        << cooked << " cooked-dds, "
        << dynamic << " dynamic, "
        << missing << " missing, "
        << case_mismatch << " case-mismatch, "
        << invalid << " invalid\n";
}

} // namespace

int main()
{
    TestDocumentationProfile();
    TestSortedEnumerationAndPreservation();
    TestTextureResolutionStatesAndTraversal();
    TestDuplicateCollisionHistory();
    TestPreservedDisabledAndUnknownShapes();
    TestRecognizedInputsRejectNestedArrays();
    TestFatalRollbackScrubsDiagnosticIndices();
    TestNamespaceAndScopedIdentity();
    TestEveryAggregateQuotaFailsClosed();
    TestHostileMalformedInputsFailClosed();
    TestAllTruncationsAndNulMutationsFailClosed();
    TestCanonicalIdentityIsExact();
    TestFormulaCoupeOptIn();

    if (g_failures != 0)
    {
        std::cerr << g_failures
                  << " BeamNG material inventory test(s) failed\n";
        return 1;
    }
    std::cout << "BeamNG material inventory tests passed\n";
    return 0;
}
