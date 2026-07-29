/*
    This source file is part of Rigs of Rods

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

/// @file BeamNGMaterialInventory.h
/// @brief Bounded, non-extracting inventory for BeamNG *.materials.json data.

#pragma once

#include "BeamNGPackageManifest.h"
#include "JBeamSyntax.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace RoR {
namespace BeamNG {

/// Immutable documentation snapshot used to classify fields. Classification
/// is deliberately narrower than runtime support: this pass never creates an
/// Ogre material, shader, texture, script, or resource location.
struct BeamNGMaterialDocumentationProfile
{
    std::string profile_id;
    std::string beamng_version;
    std::string source_url;
    std::string last_modified;
    std::string texture_cooker_url;
    std::string texture_cooker_last_modified;

    BeamNGMaterialDocumentationProfile();
};

const BeamNGMaterialDocumentationProfile&
GetBeamNGMaterialDocumentationProfile();

/// The package path must name an exact regular-file entry in the supplied
/// validated manifest. Bytes are supplied by the caller; this module performs
/// no archive extraction or filesystem access.
struct BeamNGMaterialSource
{
    std::string package_path;
    std::string source_bytes;

    BeamNGMaterialSource();
};

enum class BeamNGMaterialSeverity
{
    WARNING,
    ERROR_SEVERITY
};

enum class BeamNGMaterialDiagnosticCode
{
    INVALID_PACKAGE_NAMESPACE,
    MANIFEST_ENTRY_LIMIT,
    INVALID_MANIFEST_ENTRY,
    MANIFEST_DUPLICATE_PATH,
    MANIFEST_CASE_COLLISION,
    SOURCE_LIMIT,
    SOURCE_BYTE_LIMIT,
    INVALID_SOURCE_PATH,
    SOURCE_NOT_IN_MANIFEST,
    SOURCE_NOT_REGULAR_FILE,
    SOURCE_NOT_MATERIAL_JSON,
    MATERIAL_SOURCE_NOT_SUPPLIED,
    DUPLICATE_SOURCE_PATH,
    SOURCE_PARSE_DIAGNOSTIC,
    SOURCE_ROOT_NOT_OBJECT,
    MATERIAL_LIMIT,
    FIELD_LIMIT,
    STAGE_LIMIT,
    TEXTURE_LIMIT,
    RETAINED_BYTE_LIMIT,
    WORK_LIMIT,
    DIAGNOSTIC_LIMIT,
    SCOPED_RESOURCE_NAME_LIMIT,
    SCOPED_RESOURCE_NAME_COLLISION,
    CANONICAL_OUTPUT_LIMIT,
    MATERIAL_DEFINITION_NOT_OBJECT,
    INVALID_CLASS_FIELD,
    UNSUPPORTED_CLASS,
    INVALID_NAME_FIELD,
    INVALID_MAP_TO_FIELD,
    INVALID_STAGES_FIELD,
    INVALID_STAGE,
    INVALID_PBR_INPUT,
    DUPLICATE_MATERIAL_KEY,
    DUPLICATE_MATERIAL_NAME,
    INVALID_TEXTURE_PATH,
    MISSING_TEXTURE,
    TEXTURE_CASE_MISMATCH
};

struct BeamNGMaterialDiagnostic
{
    BeamNGMaterialDiagnosticCode code;
    BeamNGMaterialSeverity severity;
    JBeamSourceSpan span;
    std::size_t source_index;
    std::size_t material_index;
    std::string field_name;
    std::string detail;

    BeamNGMaterialDiagnostic();
};

struct BeamNGMaterialLimits
{
    std::size_t max_manifest_entries;
    std::size_t max_sources;
    std::size_t max_total_source_bytes;
    std::size_t max_source_bytes;
    std::size_t max_tokens_per_source;
    std::size_t max_nodes_per_source;
    std::size_t max_value_depth;
    std::size_t max_string_bytes;
    std::size_t max_materials;
    /// Counts every object field in every retained material definition,
    /// including fields unknown to this importer.
    std::size_t max_fields;
    /// Counts every item in every authored Stages assignment, including
    /// superseded duplicate assignments.
    std::size_t max_stages;
    std::size_t max_texture_references;
    /// Zero still permits one terminal diagnostic explaining rejection.
    std::size_t max_diagnostics;
    /// Portable logical accounting rather than sizeof()-dependent accounting.
    std::size_t max_retained_bytes;
    std::size_t max_work_units;
    std::size_t max_scoped_resource_name_bytes;
    std::size_t max_canonical_output_bytes;
    std::size_t max_canonical_work_units;

    BeamNGMaterialLimits();
};

enum class BeamNGMaterialDisposition
{
    /// Parsed and classified only. This does not mean runtime renderer parity.
    INVENTORY_ONLY,
    /// A future renderer adapter must use an explicit warning/placeholder
    /// representation because an input is absent, invalid, or ambiguous.
    PLACEHOLDER,
    /// Retained byte-for-byte in the AST but intentionally not activated.
    PRESERVED_DISABLED
};

enum class BeamNGMaterialFieldScope
{
    ROOT_LAYER,
    STAGE
};

enum class BeamNGMaterialFieldKind
{
    CLASS_NAME,
    MATERIAL_NAME,
    MAP_TO,
    STAGES,
    BASE_COLOR_MAP,
    NORMAL_MAP,
    ROUGHNESS_MAP,
    METALLIC_MAP,
    EMISSIVE_MAP,
    AMBIENT_OCCLUSION_MAP,
    OPACITY_MAP,
    CLEAR_COAT_MAP,
    COLOR_PALETTE_MAP,
    BASE_COLOR_FACTOR,
    ROUGHNESS_FACTOR,
    METALLIC_FACTOR,
    EMISSIVE_FACTOR,
    CLEAR_COAT_FACTOR,
    CLEAR_COAT_ROUGHNESS_FACTOR,
    ALPHA_REF,
    TRANSLUCENT,
    USE_ANISOTROPIC
};

struct BeamNGMaterialStringField
{
    bool present;
    bool type_valid;
    std::string value;
    JBeamSourceSpan span;
    std::size_t assignment_count;

    BeamNGMaterialStringField();
};

/// One occurrence of a recognized field. raw_value aliases the retained raw
/// definition so duplicate assignments and array shapes remain observable.
struct BeamNGMaterialFieldObservation
{
    BeamNGMaterialFieldKind kind;
    BeamNGMaterialFieldScope scope;
    bool has_stages_assignment;
    std::size_t stages_assignment_index;
    std::size_t stage_index;
    std::string authored_name;
    JBeamSourceSpan field_span;
    std::shared_ptr<const JBeamValue> raw_value;
    bool is_effective_assignment;

    BeamNGMaterialFieldObservation();
};

enum class BeamNGTextureReferenceStatus
{
    LOCAL_FOUND,
    /// The authored *.color.png, *.data.png, or *.normal.png path is
    /// represented by its package-local cooked *.dds counterpart, matching
    /// BeamNG's documented mod-publishing behavior.
    LOCAL_COOKED_DDS,
    DYNAMIC_TEXTURE,
    LOCAL_MISSING,
    LOCAL_CASE_MISMATCH,
    INVALID_PATH
};

struct BeamNGMaterialTextureReference
{
    BeamNGMaterialFieldKind field_kind;
    BeamNGMaterialFieldScope scope;
    bool has_stages_assignment;
    std::size_t stages_assignment_index;
    std::size_t stage_index;
    /// Index path within a texture array; empty means the authored value was a
    /// scalar string.
    std::vector<std::size_t> array_indices;
    std::string raw_path;
    JBeamSourceSpan value_span;
    BeamNGTextureReferenceStatus status;
    /// Canonical package-relative candidate, empty when the raw path is
    /// dynamic or invalid.
    std::string candidate_path;
    /// Exact manifest path for LOCAL_FOUND, LOCAL_COOKED_DDS, and
    /// LOCAL_CASE_MISMATCH.
    std::string resolved_manifest_path;

    BeamNGMaterialTextureReference();
};

struct BeamNGMaterialSourceRecord
{
    std::string package_path;
    JBeamSourceSpan root_span;
    std::vector<std::size_t> material_indices;

    BeamNGMaterialSourceRecord();
};

struct BeamNGMaterialRecord
{
    std::size_t source_index;
    std::size_t source_material_index;
    std::string material_key;
    JBeamSourceSpan material_key_span;
    /// Every package-wide record index with the same exact case-sensitive
    /// top-level key, including this record, in canonical source order.
    std::vector<std::size_t> same_key_material_indices;
    /// Hex-encoded source/key identity plus duplicate ordinal. No lossy name
    /// sanitization is used, so distinct authored identities cannot alias.
    std::string scoped_resource_name;
    BeamNGMaterialDisposition disposition;
    std::shared_ptr<const JBeamValue> raw_definition;

    BeamNGMaterialStringField class_name;
    BeamNGMaterialStringField name;
    BeamNGMaterialStringField map_to;
    std::vector<BeamNGMaterialFieldObservation> recognized_fields;
    std::vector<BeamNGMaterialTextureReference> texture_references;
    std::size_t authored_stage_count;

    BeamNGMaterialRecord();
};

struct BeamNGMaterialInventory
{
    std::string documentation_profile_id;
    PackageFormatProfile package_format_profile;
    std::string package_namespace;
    std::vector<BeamNGMaterialSourceRecord> sources;
    std::vector<BeamNGMaterialRecord> materials;
    std::size_t authored_field_count;
    std::size_t authored_stage_count;
    std::size_t texture_reference_count;
    std::size_t retained_byte_count;
    std::size_t work_unit_count;
    std::size_t canonical_output_byte_limit;
    std::size_t canonical_work_unit_limit;
    std::size_t canonical_value_depth_limit;
    std::vector<BeamNGMaterialDiagnostic> diagnostics;

    BeamNGMaterialInventory();
    bool IsValid() const;
};

/// Builds a deterministic package inventory. Source inputs are canonicalized
/// by validated package path and then sorted before parsing. Any syntax,
/// namespace, manifest, or resource-limit error rejects the whole inventory
/// transactionally; semantic material defects remain inert records with an
/// explicit PLACEHOLDER or PRESERVED_DISABLED disposition.
BeamNGMaterialInventory BuildBeamNGMaterialInventory(
    const std::string& package_namespace,
    const PackageManifest& manifest,
    const std::vector<BeamNGMaterialSource>& sources,
    const BeamNGMaterialLimits& limits = BeamNGMaterialLimits());

/// Stable binary identity material for the exact duplicate-preserving AST,
/// recognized observations, texture resolutions, dispositions, and
/// diagnostics. Returns empty rather than exceeding configured byte, work, or
/// depth limits.
std::string SerializeCanonicalBeamNGMaterialInventory(
    const BeamNGMaterialInventory& inventory);

const char* BeamNGMaterialDiagnosticCodeToString(
    BeamNGMaterialDiagnosticCode code);
const char* BeamNGMaterialDispositionToString(
    BeamNGMaterialDisposition disposition);
const char* BeamNGTextureReferenceStatusToString(
    BeamNGTextureReferenceStatus status);

} // namespace BeamNG
} // namespace RoR
