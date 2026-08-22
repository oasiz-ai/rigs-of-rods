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

/// @file JBeamToRigDef.cpp
/// @brief Fail-closed JBeam structural IR to spawn-ready RigDef adapter.

#include "JBeamToRigDef.h"

#include "BeamRestLengthScale.h"
#include "JBeamAdvancedStructureIR.h"
#include "JBeamCoordinateTransform.h"
#include "JBeamWheel2Approximation.h"

#if !defined(ROR_JBEAM_TO_RIGDEF_PREFLIGHT_ONLY)
#include "RigDef_File.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <set>
#include <utility>
#include <vector>

namespace RoR {
namespace BeamNG {

const float JBEAM_RIGDEF_DEFAULT_BEAM_SPRING = 4300000.0f;
const float JBEAM_RIGDEF_DEFAULT_BEAM_DAMPING = 580.0f;
const float JBEAM_RIGDEF_DEFAULT_BEAM_DEFORM = 220000.0f;
const std::size_t JBEAM_RIGDEF_RUNTIME_NODE_LIMIT = 65535U;
const std::size_t JBEAM_RIGDEF_RUNTIME_BEAM_LIMIT = 1000000U;
const std::size_t JBEAM_RIGDEF_RUNTIME_CAB_LIMIT = 3000U;
const std::size_t JBEAM_RIGDEF_INPUT_RECORD_LIMIT =
    JBEAM_RIGDEF_RUNTIME_NODE_LIMIT +
    JBEAM_RIGDEF_RUNTIME_BEAM_LIMIT +
    1000000U;
const std::size_t JBEAM_RIGDEF_WORK_UNIT_LIMIT = 5000000U;
const std::size_t JBEAM_RIGDEF_DIAGNOSTIC_LIMIT = 4096U;
const std::size_t JBEAM_RIGDEF_DIAGNOSTIC_DETAIL_BYTE_LIMIT =
    4U * 1024U * 1024U;

namespace AdapterDetail {

static const std::size_t INVALID_SOURCE_INDEX =
    std::numeric_limits<std::size_t>::max();

struct BoundedPreflightResult : public JBeamToRigDefPreflightResult
{
    std::size_t diagnostic_payload_limit;
    std::size_t diagnostic_detail_byte_limit;
    std::size_t diagnostic_detail_bytes;
    bool diagnostics_truncated;

    explicit BoundedPreflightResult(const JBeamToRigDefLimits& limits)
        : diagnostic_payload_limit(std::min(
              limits.max_diagnostics,
              JBEAM_RIGDEF_DIAGNOSTIC_LIMIT))
        , diagnostic_detail_byte_limit(std::min(
              limits.max_diagnostic_detail_bytes,
              JBEAM_RIGDEF_DIAGNOSTIC_DETAIL_BYTE_LIMIT))
        , diagnostic_detail_bytes(0U)
        , diagnostics_truncated(false)
    {
    }
};

bool IsFiniteBinary32(float value)
{
    static_assert(sizeof(float) == sizeof(std::uint32_t),
        "JBeam RigDef adapter requires binary32 floats");
    static_assert(std::numeric_limits<float>::is_iec559,
        "JBeam RigDef adapter requires IEC 60559 floats");

    std::uint32_t bits = 0U;
    const volatile unsigned char* const source =
        reinterpret_cast<const volatile unsigned char*>(&value);
    unsigned char* const destination =
        reinterpret_cast<unsigned char*>(&bits);
    for (std::size_t i = 0U; i < sizeof(bits); ++i)
    {
        destination[i] = source[i];
    }
    return (bits & UINT32_C(0x7f800000)) !=
        UINT32_C(0x7f800000);
}

bool IsNormalBinary32(float value)
{
    std::uint32_t bits = 0U;
    const volatile unsigned char* const source =
        reinterpret_cast<const volatile unsigned char*>(&value);
    unsigned char* const destination =
        reinterpret_cast<unsigned char*>(&bits);
    for (std::size_t i = 0U; i < sizeof(bits); ++i)
    {
        destination[i] = source[i];
    }
    const std::uint32_t exponent = bits & UINT32_C(0x7f800000);
    return exponent != 0U && exponent != UINT32_C(0x7f800000);
}

double Absolute(double value)
{
    return value < 0.0 ? -value : value;
}

float Absolute(float value)
{
    return value < 0.0f ? -value : value;
}

bool TryNarrowFinite(
    double value,
    bool reject_nonzero_subnormal,
    float* output)
{
    if (output == NULL)
    {
        return false;
    }
    *output = 0.0f;
    if (!Detail::IsFiniteBinary64(value) ||
        value > static_cast<double>(std::numeric_limits<float>::max()) ||
        value < -static_cast<double>(std::numeric_limits<float>::max()))
    {
        return false;
    }

    const volatile float narrowed = static_cast<float>(value);
    const float result = narrowed;
    if (!IsFiniteBinary32(result))
    {
        return false;
    }
    if (reject_nonzero_subnormal &&
        value != 0.0 &&
        (result == 0.0f || !IsNormalBinary32(result)))
    {
        return false;
    }
    *output = result;
    return true;
}

bool TryNarrowNonnegative(double value, float* output)
{
    return Detail::IsFiniteBinary64(value) &&
        value >= 0.0 &&
        TryNarrowFinite(value, value > 0.0, output);
}

bool TryNarrowPositive(double value, float* output)
{
    return Detail::IsFiniteBinary64(value) &&
        value > 0.0 &&
        TryNarrowFinite(value, true, output) &&
        *output > 0.0f;
}

bool TryRuntimeAdd(float first, float second, float* output)
{
    if (output == NULL)
    {
        return false;
    }
    *output = 0.0f;
    if (!IsFiniteBinary32(first) || !IsFiniteBinary32(second))
    {
        return false;
    }
    const volatile float sum = first + second;
    const float result = sum;
    if (!IsFiniteBinary32(result))
    {
        return false;
    }
    *output = result;
    return true;
}

bool TryRuntimeSubtract(float first, float second, float* output)
{
    if (output == NULL)
    {
        return false;
    }
    *output = 0.0f;
    if (!IsFiniteBinary32(first) || !IsFiniteBinary32(second))
    {
        return false;
    }
    const volatile float difference = first - second;
    const float result = difference;
    if (!IsFiniteBinary32(result))
    {
        return false;
    }
    *output = result;
    return true;
}

bool TryRuntimeLength(
    const JBeamRigDefPoint3& first,
    const JBeamRigDefPoint3& second,
    float* output)
{
    if (output == NULL)
    {
        return false;
    }
    *output = 0.0f;

    volatile float dx_volatile = first.x - second.x;
    volatile float dy_volatile = first.y - second.y;
    volatile float dz_volatile = first.z - second.z;
    const float dx = dx_volatile;
    const float dy = dy_volatile;
    const float dz = dz_volatile;
    if (!IsFiniteBinary32(dx) ||
        !IsFiniteBinary32(dy) ||
        !IsFiniteBinary32(dz))
    {
        return false;
    }

    volatile float xx_volatile = dx * dx;
    volatile float yy_volatile = dy * dy;
    volatile float zz_volatile = dz * dz;
    const float xx = xx_volatile;
    const float yy = yy_volatile;
    const float zz = zz_volatile;
    if (!IsFiniteBinary32(xx) ||
        !IsFiniteBinary32(yy) ||
        !IsFiniteBinary32(zz))
    {
        return false;
    }

    volatile float xy_volatile = xx + yy;
    const float xy = xy_volatile;
    if (!IsFiniteBinary32(xy))
    {
        return false;
    }
    volatile float squared_volatile = xy + zz;
    const float squared = squared_volatile;
    if (!IsFiniteBinary32(squared) ||
        squared < std::numeric_limits<float>::min())
    {
        return false;
    }

    volatile float length_volatile = std::sqrt(squared);
    const float length = length_volatile;
    if (!IsFiniteBinary32(length) ||
        length < std::numeric_limits<float>::min())
    {
        return false;
    }
    *output = length;
    return true;
}

bool IsRuntimeTriangleNondegenerate(
    const JBeamRigDefPoint3& first,
    const JBeamRigDefPoint3& second,
    const JBeamRigDefPoint3& third)
{
    volatile float ux_volatile = second.x - first.x;
    volatile float uy_volatile = second.y - first.y;
    volatile float uz_volatile = second.z - first.z;
    volatile float vx_volatile = third.x - first.x;
    volatile float vy_volatile = third.y - first.y;
    volatile float vz_volatile = third.z - first.z;
    const float ux = ux_volatile;
    const float uy = uy_volatile;
    const float uz = uz_volatile;
    const float vx = vx_volatile;
    const float vy = vy_volatile;
    const float vz = vz_volatile;
    if (!IsFiniteBinary32(ux) ||
        !IsFiniteBinary32(uy) ||
        !IsFiniteBinary32(uz) ||
        !IsFiniteBinary32(vx) ||
        !IsFiniteBinary32(vy) ||
        !IsFiniteBinary32(vz))
    {
        return false;
    }

    volatile float cx_volatile = uy * vz - uz * vy;
    volatile float cy_volatile = uz * vx - ux * vz;
    volatile float cz_volatile = ux * vy - uy * vx;
    const float cx = cx_volatile;
    const float cy = cy_volatile;
    const float cz = cz_volatile;
    if (!IsFiniteBinary32(cx) ||
        !IsFiniteBinary32(cy) ||
        !IsFiniteBinary32(cz))
    {
        return false;
    }

    // Reject topology whose only surviving area components are subnormal:
    // release builds may flush those components to zero.
    return (cx != 0.0f && IsNormalBinary32(cx)) ||
        (cy != 0.0f && IsNormalBinary32(cy)) ||
        (cz != 0.0f && IsNormalBinary32(cz));
}

bool TryRuntimeDifference(
    const JBeamRigDefPoint3& point,
    const JBeamRigDefPoint3& origin,
    JBeamRigDefPoint3* output)
{
    if (output == NULL)
    {
        return false;
    }
    volatile float x_volatile = point.x - origin.x;
    volatile float y_volatile = point.y - origin.y;
    volatile float z_volatile = point.z - origin.z;
    output->x = x_volatile;
    output->y = y_volatile;
    output->z = z_volatile;
    return IsFiniteBinary32(output->x) &&
        IsFiniteBinary32(output->y) &&
        IsFiniteBinary32(output->z);
}

bool TryRuntimeDot(
    const JBeamRigDefPoint3& first,
    const JBeamRigDefPoint3& second,
    float* output)
{
    if (output == NULL)
    {
        return false;
    }
    *output = 0.0f;
    volatile float x_volatile = first.x * second.x;
    volatile float y_volatile = first.y * second.y;
    volatile float z_volatile = first.z * second.z;
    const float x = x_volatile;
    const float y = y_volatile;
    const float z = z_volatile;
    if (!IsFiniteBinary32(x) ||
        !IsFiniteBinary32(y) ||
        !IsFiniteBinary32(z))
    {
        return false;
    }
    volatile float xy_volatile = x + y;
    const float xy = xy_volatile;
    if (!IsFiniteBinary32(xy))
    {
        return false;
    }
    volatile float dot_volatile = xy + z;
    const float dot = dot_volatile;
    if (!IsFiniteBinary32(dot) || !IsNormalBinary32(dot))
    {
        return false;
    }
    *output = dot;
    return true;
}

bool AreRuntimeFrameAxesViable(
    const JBeamRigDefPoint3& reference,
    const JBeamRigDefPoint3& back,
    const JBeamRigDefPoint3& left,
    const JBeamRigDefPoint3& up)
{
    float length = 0.0f;
    return TryRuntimeLength(reference, back, &length) &&
        TryRuntimeLength(reference, left, &length) &&
        TryRuntimeLength(reference, up, &length) &&
        IsRuntimeTriangleNondegenerate(reference, back, left) &&
        IsRuntimeTriangleNondegenerate(reference, back, up) &&
        IsRuntimeTriangleNondegenerate(reference, left, up);
}

bool AreRuntimeCornerDirectionsViable(
    const JBeamRigDefPoint3& reference,
    const JBeamRigDefPoint3& back,
    const JBeamRigDefPoint3& left,
    const JBeamRigDefPoint3& left_corner,
    const JBeamRigDefPoint3& right_corner)
{
    float length = 0.0f;
    if (!TryRuntimeLength(reference, left_corner, &length) ||
        !TryRuntimeLength(reference, right_corner, &length))
    {
        return false;
    }

    JBeamRigDefPoint3 back_direction;
    JBeamRigDefPoint3 left_direction;
    JBeamRigDefPoint3 left_corner_direction;
    JBeamRigDefPoint3 right_corner_direction;
    if (!TryRuntimeDifference(back, reference, &back_direction) ||
        !TryRuntimeDifference(left, reference, &left_direction) ||
        !TryRuntimeDifference(
            left_corner, reference, &left_corner_direction) ||
        !TryRuntimeDifference(
            right_corner, reference, &right_corner_direction))
    {
        return false;
    }

    float left_front = 0.0f;
    float right_front = 0.0f;
    float left_side = 0.0f;
    float right_side = 0.0f;
    return TryRuntimeDot(
            left_corner_direction, back_direction, &left_front) &&
        TryRuntimeDot(
            right_corner_direction, back_direction, &right_front) &&
        TryRuntimeDot(
            left_corner_direction, left_direction, &left_side) &&
        TryRuntimeDot(
            right_corner_direction, left_direction, &right_side) &&
        left_front < 0.0f &&
        right_front < 0.0f &&
        left_side > 0.0f &&
        right_side < 0.0f;
}

void ReplaceWithTerminalDiagnostic(
    BoundedPreflightResult& result,
    JBeamToRigDefDiagnosticCode code,
    JBeamToRigDefEntityKind kind,
    std::size_t source_index,
    const JBeamStructuralProvenance& provenance)
{
    JBeamToRigDefDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.entity_kind = kind;
    diagnostic.source_index = source_index;
    diagnostic.provenance = provenance;
    diagnostic.detail.clear();

    if (result.diagnostics.empty())
    {
        result.diagnostics.push_back(diagnostic);
    }
    else
    {
        const std::size_t old_detail_size =
            result.diagnostics.back().detail.size();
        if (old_detail_size <= result.diagnostic_detail_bytes)
        {
            result.diagnostic_detail_bytes -= old_detail_size;
        }
        else
        {
            result.diagnostic_detail_bytes = 0U;
        }
        result.diagnostics.back() = diagnostic;
    }
    result.diagnostics_truncated = true;
}

void PushDiagnostic(
    BoundedPreflightResult& result,
    JBeamToRigDefDiagnosticCode code,
    JBeamToRigDefEntityKind kind,
    std::size_t source_index,
    const JBeamStructuralProvenance& provenance,
    const std::string& detail)
{
    if (result.diagnostics_truncated)
    {
        return;
    }
    if (result.diagnostics.size() >=
        result.diagnostic_payload_limit)
    {
        ReplaceWithTerminalDiagnostic(
            result,
            JBeamToRigDefDiagnosticCode::DIAGNOSTIC_LIMIT,
            kind,
            source_index,
            provenance);
        return;
    }
    if (detail.size() >
            result.diagnostic_detail_byte_limit -
                std::min(
                    result.diagnostic_detail_bytes,
                    result.diagnostic_detail_byte_limit))
    {
        ReplaceWithTerminalDiagnostic(
            result,
            JBeamToRigDefDiagnosticCode::DIAGNOSTIC_DETAIL_LIMIT,
            kind,
            source_index,
            provenance);
        return;
    }

    JBeamToRigDefDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.entity_kind = kind;
    diagnostic.source_index = source_index;
    diagnostic.provenance = provenance;
    diagnostic.detail = detail;
    result.diagnostics.push_back(std::move(diagnostic));
    result.diagnostic_detail_bytes += detail.size();
}

void PushDiagnostic(
    JBeamToRigDefPreflightResult& result,
    JBeamToRigDefDiagnosticCode code,
    JBeamToRigDefEntityKind kind,
    std::size_t source_index,
    const JBeamStructuralProvenance& provenance,
    const std::string& detail)
{
    JBeamToRigDefDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.entity_kind = kind;
    diagnostic.source_index = source_index;
    diagnostic.provenance = provenance;
    diagnostic.detail = detail;
    result.diagnostics.push_back(std::move(diagnostic));
}

void PushDiagnostic(
    std::vector<JBeamToRigDefDiagnostic>& diagnostics,
    JBeamToRigDefDiagnosticCode code,
    const JBeamStructuralProvenance& provenance,
    const std::string& detail)
{
    JBeamToRigDefDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.entity_kind = JBeamToRigDefEntityKind::DOCUMENT;
    diagnostic.source_index = INVALID_SOURCE_INDEX;
    diagnostic.provenance = provenance;
    diagnostic.detail = detail;
    diagnostics.push_back(std::move(diagnostic));
}

void PushDiagnostic(
    std::vector<JBeamToRigDefDiagnostic>& diagnostics,
    JBeamToRigDefDiagnosticCode code,
    JBeamToRigDefEntityKind kind,
    std::size_t source_index,
    const JBeamStructuralProvenance& provenance,
    const std::string& detail)
{
    JBeamToRigDefDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.entity_kind = kind;
    diagnostic.source_index = source_index;
    diagnostic.provenance = provenance;
    diagnostic.detail = detail;
    diagnostics.push_back(std::move(diagnostic));
}

void ClearPlan(JBeamToRigDefPreflightResult& result)
{
    result.node_source_order.clear();
    result.transformed_nodes.clear();
    result.node_masses.clear();
    result.beams.clear();
    result.triangle_source_indices.clear();
    result.metrics = JBeamToRigDefMetrics();
}

bool HasEmbeddedNull(const std::string& value)
{
    return value.find('\0') != std::string::npos;
}

bool SourceLineFits(const JBeamStructuralProvenance& provenance)
{
    return provenance.begin.line <=
        static_cast<std::uint64_t>(
            std::numeric_limits<unsigned int>::max());
}

bool IsValidTriangleOrigin(JBeamStructuralTriangleOrigin origin)
{
    switch (origin)
    {
    case JBeamStructuralTriangleOrigin::TRIANGLE:
    case JBeamStructuralTriangleOrigin::QUAD_FIRST:
    case JBeamStructuralTriangleOrigin::QUAD_SECOND:
        return true;
    }
    return false;
}

bool IsValidTriangleType(JBeamStructuralTriangleType type)
{
    switch (type)
    {
    case JBeamStructuralTriangleType::NORMALTYPE:
    case JBeamStructuralTriangleType::NONCOLLIDABLE:
        return true;
    }
    return false;
}

enum class ReferenceState
{
    VALID,
    MISSING,
    INCONSISTENT
};

typedef std::map<std::string, std::size_t> NodeIndexMap;

ReferenceState ResolveReferenceState(
    const JBeamStructuralIR& ir,
    const NodeIndexMap& node_indices,
    const std::string& id,
    std::size_t index)
{
    const NodeIndexMap::const_iterator found = node_indices.find(id);
    if (found == node_indices.end())
    {
        return index >= ir.nodes.size()
            ? ReferenceState::MISSING
            : ReferenceState::INCONSISTENT;
    }
    if (index >= ir.nodes.size() ||
        found->second != index ||
        ir.nodes[index].id != id)
    {
        return ReferenceState::INCONSISTENT;
    }
    return ReferenceState::VALID;
}

bool IsValidRequiredReference(
    const JBeamStructuralIR& ir,
    const NodeIndexMap& node_indices,
    const std::string& id,
    std::size_t index)
{
    return ResolveReferenceState(ir, node_indices, id, index) ==
        ReferenceState::VALID;
}

struct PointVector
{
    double x;
    double y;
    double z;
};

PointVector Difference(
    const JBeamRigDefPoint3& point,
    const JBeamRigDefPoint3& origin)
{
    PointVector output;
    output.x =
        static_cast<double>(point.x) - static_cast<double>(origin.x);
    output.y =
        static_cast<double>(point.y) - static_cast<double>(origin.y);
    output.z =
        static_cast<double>(point.z) - static_cast<double>(origin.z);
    return output;
}

double Dot(const PointVector& first, const PointVector& second)
{
    return first.x * second.x +
        first.y * second.y +
        first.z * second.z;
}

double LengthSquared(const PointVector& vector)
{
    return Dot(vector, vector);
}

bool IsFrameNondegenerate(
    const PointVector& back,
    const PointVector& left,
    const PointVector& up)
{
    const PointVector cross = {
        back.y * left.z - back.z * left.y,
        back.z * left.x - back.x * left.z,
        back.x * left.y - back.y * left.x
    };
    const double determinant = Dot(cross, up);
    const double product =
        LengthSquared(back) * LengthSquared(left) * LengthSquared(up);
    return Detail::IsFiniteBinary64(determinant) &&
        Detail::IsFiniteBinary64(product) &&
        product > 0.0 &&
        determinant * determinant >
            product * (64.0 * std::numeric_limits<float>::epsilon()) *
            (64.0 * std::numeric_limits<float>::epsilon());
}

bool IsAxisAligned(
    const PointVector& axis,
    int primary_component)
{
    const double components[3] = { axis.x, axis.y, axis.z };
    const double primary = components[primary_component];
    if (!(primary > 0.0) || !Detail::IsFiniteBinary64(primary))
    {
        return false;
    }
    const double tolerance =
        64.0 * static_cast<double>(std::numeric_limits<float>::epsilon());
    for (int component = 0; component < 3; ++component)
    {
        if (component != primary_component &&
            Absolute(components[component]) > tolerance * primary)
        {
            return false;
        }
    }
    return true;
}

bool AreFrontCornersAligned(
    const PointVector& back,
    const PointVector& left,
    const PointVector& left_corner,
    const PointVector& right_corner)
{
    double scale = Absolute(back.x);
    const PointVector* const vectors[] = {
        &back, &left, &left_corner, &right_corner
    };
    for (std::size_t i = 0U; i < 4U; ++i)
    {
        scale = std::max(scale, Absolute(vectors[i]->x));
        scale = std::max(scale, Absolute(vectors[i]->y));
        scale = std::max(scale, Absolute(vectors[i]->z));
    }
    if (!(scale > 0.0) || !Detail::IsFiniteBinary64(scale))
    {
        return false;
    }
    const PointVector normalized_back = {
        back.x / scale, back.y / scale, back.z / scale
    };
    const PointVector normalized_left = {
        left.x / scale, left.y / scale, left.z / scale
    };
    const PointVector normalized_left_corner = {
        left_corner.x / scale,
        left_corner.y / scale,
        left_corner.z / scale
    };
    const PointVector normalized_right_corner = {
        right_corner.x / scale,
        right_corner.y / scale,
        right_corner.z / scale
    };
    return Dot(normalized_left_corner, normalized_back) < 0.0 &&
        Dot(normalized_right_corner, normalized_back) < 0.0 &&
        Dot(normalized_left_corner, normalized_left) > 0.0 &&
        Dot(normalized_right_corner, normalized_left) < 0.0;
}

bool ValidateFrame(
    const JBeamStructuralIR& ir,
    const NodeIndexMap& node_indices,
    BoundedPreflightResult& result)
{
    const JBeamStructuralRefFrame& frame = ir.ref_frame;
    const std::string* const ids[] = {
        &frame.reference,
        &frame.back,
        &frame.left,
        &frame.up,
        &frame.left_corner,
        &frame.right_corner
    };
    const std::size_t indices[] = {
        frame.reference_index,
        frame.back_index,
        frame.left_index,
        frame.up_index,
        frame.left_corner_index,
        frame.right_corner_index
    };
    bool valid = true;
    for (std::size_t i = 0U; i < 6U; ++i)
    {
        if (!IsValidRequiredReference(
                ir, node_indices, *ids[i], indices[i]))
        {
            PushDiagnostic(
                result,
                JBeamToRigDefDiagnosticCode::INVALID_NODE_REFERENCE,
                JBeamToRigDefEntityKind::REF_FRAME,
                INVALID_SOURCE_INDEX,
                frame.provenance,
                "refNodes identifier/index pair is inconsistent");
            valid = false;
        }
    }
    for (std::size_t i = 0U; i < 6U; ++i)
    {
        for (std::size_t j = i + 1U; j < 6U; ++j)
        {
            if (indices[i] == indices[j])
            {
                PushDiagnostic(
                    result,
                    JBeamToRigDefDiagnosticCode::INVALID_REF_FRAME,
                    JBeamToRigDefEntityKind::REF_FRAME,
                    INVALID_SOURCE_INDEX,
                    frame.provenance,
                    "The six refNodes roles must reference distinct nodes");
                valid = false;
                i = 6U;
                break;
            }
        }
    }
    if (!SourceLineFits(frame.provenance))
    {
        PushDiagnostic(
            result,
            JBeamToRigDefDiagnosticCode::SOURCE_LINE_LIMIT,
            JBeamToRigDefEntityKind::REF_FRAME,
            INVALID_SOURCE_INDEX,
            frame.provenance,
            "refNodes source line does not fit RigDef::Node::Ref");
        valid = false;
    }
    if (!valid)
    {
        return false;
    }

    const JBeamRigDefPoint3& reference =
        result.transformed_nodes[frame.reference_index];
    const JBeamRigDefPoint3& back_point =
        result.transformed_nodes[frame.back_index];
    const JBeamRigDefPoint3& left_point =
        result.transformed_nodes[frame.left_index];
    const JBeamRigDefPoint3& up_point =
        result.transformed_nodes[frame.up_index];
    const JBeamRigDefPoint3& left_corner_point =
        result.transformed_nodes[frame.left_corner_index];
    const JBeamRigDefPoint3& right_corner_point =
        result.transformed_nodes[frame.right_corner_index];
    const PointVector back = Difference(
        back_point, reference);
    const PointVector left = Difference(
        left_point, reference);
    const PointVector up = Difference(
        up_point, reference);
    const PointVector left_corner = Difference(
        left_corner_point, reference);
    const PointVector right_corner = Difference(
        right_corner_point, reference);

    if (!AreRuntimeFrameAxesViable(
            reference, back_point, left_point, up_point) ||
        !IsFrameNondegenerate(back, left, up))
    {
        PushDiagnostic(
            result,
            JBeamToRigDefDiagnosticCode::INVALID_REF_FRAME,
            JBeamToRigDefEntityKind::REF_FRAME,
            INVALID_SOURCE_INDEX,
            frame.provenance,
            "refNodes axes cannot be normalized or crossed safely by the "
            "binary32 runtime");
        valid = false;
    }
    // In transformed RoR coordinates, back is +X, up is +Y, and left is +Z.
    if (!IsAxisAligned(back, 0) ||
        !IsAxisAligned(up, 1) ||
        !IsAxisAligned(left, 2))
    {
        PushDiagnostic(
            result,
            JBeamToRigDefDiagnosticCode::MISALIGNED_REF_FRAME,
            JBeamToRigDefEntityKind::REF_FRAME,
            INVALID_SOURCE_INDEX,
            frame.provenance,
            "Transformed refNodes must align with RoR +X back, +Y up, "
            "and +Z left");
        valid = false;
    }
    if (!AreRuntimeCornerDirectionsViable(
            reference,
            back_point,
            left_point,
            left_corner_point,
            right_corner_point) ||
        !AreFrontCornersAligned(
            back, left, left_corner, right_corner))
    {
        PushDiagnostic(
            result,
            JBeamToRigDefDiagnosticCode::MISALIGNED_REF_CORNERS,
            JBeamToRigDefEntityKind::REF_FRAME,
            INVALID_SOURCE_INDEX,
            frame.provenance,
            "Transformed corner directions must survive binary32 dot "
            "products and remain forward on their documented sides");
        valid = false;
    }
    return valid;
}

bool ValidateBeamParameterState(const JBeamStructuralBeam& beam)
{
    if (beam.has_spring &&
        (!Detail::IsFiniteBinary64(beam.spring) || beam.spring < 0.0))
    {
        return false;
    }
    if (beam.has_damping &&
        (!Detail::IsFiniteBinary64(beam.damping) || beam.damping < 0.0))
    {
        return false;
    }
    if (beam.deform_unbounded && !beam.has_deform)
    {
        return false;
    }
    if (beam.has_deform &&
        !beam.deform_unbounded &&
        (!Detail::IsFiniteBinary64(beam.deform) || beam.deform < 0.0))
    {
        return false;
    }
    if (beam.strength_unbounded && !beam.has_strength)
    {
        return false;
    }
    if (beam.has_strength &&
        !beam.strength_unbounded &&
        (!Detail::IsFiniteBinary64(beam.strength) || beam.strength < 0.0))
    {
        return false;
    }
    if (beam.has_precompression &&
        (!Detail::IsFiniteBinary64(beam.precompression) ||
         !(beam.precompression > 0.0)))
    {
        return false;
    }
    if (beam.has_long_bound &&
        (!Detail::IsFiniteBinary64(beam.long_bound) ||
         beam.long_bound < 0.0))
    {
        return false;
    }
    return true;
}

bool BuildBeamPlan(
    const JBeamStructuralIR& ir,
    std::size_t source_index,
    const NodeIndexMap& node_indices,
    BoundedPreflightResult& result)
{
    const JBeamStructuralBeam& beam = ir.beams[source_index];
    bool valid = true;
    if (beam.node_a == beam.node_b)
    {
        PushDiagnostic(
            result,
            JBeamToRigDefDiagnosticCode::DUPLICATE_VERTEX,
            JBeamToRigDefEntityKind::BEAM,
            source_index,
            beam.provenance,
            "Beam endpoints must be distinct");
        valid = false;
    }
    if (!ValidateBeamParameterState(beam))
    {
        PushDiagnostic(
            result,
            JBeamToRigDefDiagnosticCode::INVALID_BEAM_PARAMETER,
            JBeamToRigDefEntityKind::BEAM,
            source_index,
            beam.provenance,
            "Beam parameter flags and values are inconsistent");
        valid = false;
    }

    const ReferenceState first = ResolveReferenceState(
        ir, node_indices, beam.node_a, beam.node_a_index);
    const ReferenceState second = ResolveReferenceState(
        ir, node_indices, beam.node_b, beam.node_b_index);

    switch (beam.status)
    {
    case JBeamStructuralBeamStatus::ENABLED:
        if ((beam.beam_type != "NORMAL" &&
             beam.beam_type != "SUPPORT") ||
            first != ReferenceState::VALID ||
            second != ReferenceState::VALID)
        {
            PushDiagnostic(
                result,
                JBeamToRigDefDiagnosticCode::INVALID_NODE_REFERENCE,
                JBeamToRigDefEntityKind::BEAM,
                source_index,
                beam.provenance,
                "Enabled NORMAL or SUPPORT beam has an inconsistent node "
                "reference");
            valid = false;
        }
        break;
    case JBeamStructuralBeamStatus::PRESERVED_DISABLED_SPECIAL_TYPE:
        if (beam.beam_type == "NORMAL" ||
            beam.beam_type == "SUPPORT" ||
            first != ReferenceState::VALID ||
            second != ReferenceState::VALID)
        {
            PushDiagnostic(
                result,
                JBeamToRigDefDiagnosticCode::INVALID_ENTITY_STATE,
                JBeamToRigDefEntityKind::BEAM,
                source_index,
                beam.provenance,
                "Disabled special beam state is inconsistent");
            valid = false;
        }
        return valid;
    case JBeamStructuralBeamStatus::
        PRESERVED_DISABLED_OPTIONAL_REFERENCE:
        if (!beam.optional ||
            first == ReferenceState::INCONSISTENT ||
            second == ReferenceState::INCONSISTENT ||
            (first != ReferenceState::MISSING &&
             second != ReferenceState::MISSING))
        {
            PushDiagnostic(
                result,
                JBeamToRigDefDiagnosticCode::INVALID_ENTITY_STATE,
                JBeamToRigDefEntityKind::BEAM,
                source_index,
                beam.provenance,
                "Disabled optional beam must have a genuinely missing "
                "reference");
            valid = false;
        }
        return valid;
    default:
        PushDiagnostic(
            result,
            JBeamToRigDefDiagnosticCode::INVALID_ENTITY_STATE,
            JBeamToRigDefEntityKind::BEAM,
            source_index,
            beam.provenance,
            "Beam has an unknown enabled/disabled state");
        return false;
    }

    if (!SourceLineFits(beam.provenance))
    {
        PushDiagnostic(
            result,
            JBeamToRigDefDiagnosticCode::SOURCE_LINE_LIMIT,
            JBeamToRigDefEntityKind::BEAM,
            source_index,
            beam.provenance,
            "Beam source line does not fit RigDef::Node::Ref");
        valid = false;
    }
    if (!valid)
    {
        return false;
    }

    JBeamRigDefBeamPlan plan;
    plan.source_index = source_index;
    plan.support = beam.beam_type == "SUPPORT";
    const double spring = beam.has_spring
        ? beam.spring
        : static_cast<double>(JBEAM_RIGDEF_DEFAULT_BEAM_SPRING);
    const double damping = beam.has_damping
        ? beam.damping
        : static_cast<double>(JBEAM_RIGDEF_DEFAULT_BEAM_DAMPING);
    if (!TryNarrowNonnegative(spring, &plan.spring) ||
        !TryNarrowNonnegative(damping, &plan.damping))
    {
        PushDiagnostic(
            result,
            JBeamToRigDefDiagnosticCode::FLOAT_NARROWING,
            JBeamToRigDefEntityKind::BEAM,
            source_index,
            beam.provenance,
            "Beam spring or damping cannot be represented safely in "
            "binary32");
        valid = false;
    }

    if (beam.has_deform && beam.deform_unbounded)
    {
        plan.deform = std::numeric_limits<float>::max();
    }
    else
    {
        const double deform = beam.has_deform
            ? beam.deform
            : static_cast<double>(JBEAM_RIGDEF_DEFAULT_BEAM_DEFORM);
        if (!TryNarrowNonnegative(deform, &plan.deform))
        {
            PushDiagnostic(
                result,
                JBeamToRigDefDiagnosticCode::FLOAT_NARROWING,
                JBeamToRigDefEntityKind::BEAM,
                source_index,
                beam.provenance,
                "Beam deformation threshold cannot be represented "
                "safely in binary32");
            valid = false;
        }
    }

    if (!beam.has_strength || beam.strength_unbounded)
    {
        plan.strength = std::numeric_limits<float>::max();
    }
    else if (!TryNarrowNonnegative(beam.strength, &plan.strength))
    {
        PushDiagnostic(
            result,
            JBeamToRigDefDiagnosticCode::FLOAT_NARROWING,
            JBeamToRigDefEntityKind::BEAM,
            source_index,
            beam.provenance,
            "Beam strength cannot be represented safely in binary32");
        valid = false;
    }

    const double scale = beam.has_precompression
        ? beam.precompression
        : 1.0;
    if (!TryNarrowPositive(scale, &plan.rest_length_scale))
    {
        PushDiagnostic(
            result,
            JBeamToRigDefDiagnosticCode::FLOAT_NARROWING,
            JBeamToRigDefEntityKind::BEAM,
            source_index,
            beam.provenance,
            "Beam precompression cannot be represented safely in binary32");
        valid = false;
    }

    const double extension_break_limit = beam.has_long_bound
        ? beam.long_bound
        : 1.0;
    if (plan.support &&
        !TryNarrowNonnegative(
            extension_break_limit, &plan.extension_break_limit))
    {
        PushDiagnostic(
            result,
            JBeamToRigDefDiagnosticCode::FLOAT_NARROWING,
            JBeamToRigDefEntityKind::BEAM,
            source_index,
            beam.provenance,
            "SUPPORT beamLongBound cannot be represented safely in "
            "binary32");
        valid = false;
    }

    if (valid &&
        !TryRuntimeLength(
            result.transformed_nodes[beam.node_a_index],
            result.transformed_nodes[beam.node_b_index],
            &plan.geometric_length))
    {
        PushDiagnostic(
            result,
            JBeamToRigDefDiagnosticCode::DEGENERATE_BEAM,
            JBeamToRigDefEntityKind::BEAM,
            source_index,
            beam.provenance,
            "Beam becomes zero, subnormal, or non-finite in ActorSpawner's "
            "binary32 length calculation");
        valid = false;
    }
    if (valid &&
        !Physics::TryScaleBeamRestLength(
            plan.geometric_length,
            plan.rest_length_scale,
            &plan.scaled_rest_length))
    {
        PushDiagnostic(
            result,
            JBeamToRigDefDiagnosticCode::BEAM_LENGTH_OVERFLOW,
            JBeamToRigDefEntityKind::BEAM,
            source_index,
            beam.provenance,
            "Scaled beam rest length is not a positive normal binary32 "
            "value");
        valid = false;
    }
    if (valid)
    {
        float total_length = 0.0f;
        if (!TryRuntimeAdd(
                result.metrics.runtime_total_beam_length,
                plan.scaled_rest_length,
                &total_length))
        {
            PushDiagnostic(
                result,
                JBeamToRigDefDiagnosticCode::BEAM_LENGTH_OVERFLOW,
                JBeamToRigDefEntityKind::BEAM,
                source_index,
                beam.provenance,
                "Total beam length overflows Actor's binary32 mass pass");
            valid = false;
        }
        else
        {
            result.metrics.runtime_total_beam_length = total_length;
        }
    }
    if (valid)
    {
        result.beams.push_back(plan);
    }
    return valid;
}

bool ValidateTriangle(
    const JBeamStructuralIR& ir,
    std::size_t source_index,
    const NodeIndexMap& node_indices,
    BoundedPreflightResult& result)
{
    const JBeamStructuralTriangle& triangle =
        ir.triangles[source_index];
    bool valid = true;
    if (!IsValidTriangleOrigin(triangle.origin))
    {
        PushDiagnostic(
            result,
            JBeamToRigDefDiagnosticCode::INVALID_ENTITY_STATE,
            JBeamToRigDefEntityKind::TRIANGLE,
            source_index,
            triangle.provenance,
            "Triangle has an unknown source topology state");
        valid = false;
    }
    if (!IsValidTriangleType(triangle.triangle_type))
    {
        PushDiagnostic(
            result,
            JBeamToRigDefDiagnosticCode::INVALID_ENTITY_STATE,
            JBeamToRigDefEntityKind::TRIANGLE,
            source_index,
            triangle.provenance,
            "Triangle has an unknown collision type state");
        valid = false;
    }
    if (triangle.node_a == triangle.node_b ||
        triangle.node_a == triangle.node_c ||
        triangle.node_b == triangle.node_c)
    {
        PushDiagnostic(
            result,
            JBeamToRigDefDiagnosticCode::DUPLICATE_VERTEX,
            JBeamToRigDefEntityKind::TRIANGLE,
            source_index,
            triangle.provenance,
            "Triangle vertices must be distinct");
        valid = false;
    }

    const ReferenceState first = ResolveReferenceState(
        ir, node_indices, triangle.node_a, triangle.node_a_index);
    const ReferenceState second = ResolveReferenceState(
        ir, node_indices, triangle.node_b, triangle.node_b_index);
    const ReferenceState third = ResolveReferenceState(
        ir, node_indices, triangle.node_c, triangle.node_c_index);
    switch (triangle.status)
    {
    case JBeamStructuralTriangleStatus::ENABLED:
        if (first != ReferenceState::VALID ||
            second != ReferenceState::VALID ||
            third != ReferenceState::VALID)
        {
            PushDiagnostic(
                result,
                JBeamToRigDefDiagnosticCode::INVALID_NODE_REFERENCE,
                JBeamToRigDefEntityKind::TRIANGLE,
                source_index,
                triangle.provenance,
                "Enabled triangle has an inconsistent node reference");
            valid = false;
        }
        break;
    case JBeamStructuralTriangleStatus::
        PRESERVED_DISABLED_OPTIONAL_REFERENCE:
        if (!triangle.optional ||
            first == ReferenceState::INCONSISTENT ||
            second == ReferenceState::INCONSISTENT ||
            third == ReferenceState::INCONSISTENT ||
            (first != ReferenceState::MISSING &&
             second != ReferenceState::MISSING &&
             third != ReferenceState::MISSING))
        {
            PushDiagnostic(
                result,
                JBeamToRigDefDiagnosticCode::INVALID_ENTITY_STATE,
                JBeamToRigDefEntityKind::TRIANGLE,
                source_index,
                triangle.provenance,
                "Disabled optional triangle must have a genuinely missing "
                "reference");
            valid = false;
        }
        return valid;
    default:
        PushDiagnostic(
            result,
            JBeamToRigDefDiagnosticCode::INVALID_ENTITY_STATE,
            JBeamToRigDefEntityKind::TRIANGLE,
            source_index,
            triangle.provenance,
            "Triangle has an unknown enabled/disabled state");
        return false;
    }

    if (!SourceLineFits(triangle.provenance))
    {
        PushDiagnostic(
            result,
            JBeamToRigDefDiagnosticCode::SOURCE_LINE_LIMIT,
            JBeamToRigDefEntityKind::TRIANGLE,
            source_index,
            triangle.provenance,
            "Triangle source line does not fit RigDef::Node::Ref");
        valid = false;
    }
    if (valid &&
        !IsRuntimeTriangleNondegenerate(
            result.transformed_nodes[triangle.node_a_index],
            result.transformed_nodes[triangle.node_b_index],
            result.transformed_nodes[triangle.node_c_index]))
    {
        PushDiagnostic(
            result,
            JBeamToRigDefDiagnosticCode::DEGENERATE_TRIANGLE,
            JBeamToRigDefEntityKind::TRIANGLE,
            source_index,
            triangle.provenance,
            "Triangle becomes degenerate or non-finite after binary32 "
            "narrowing");
        valid = false;
    }
    if (valid)
    {
        result.triangle_source_indices.push_back(source_index);
    }
    return valid;
}

bool BuildMassAndBounds(
    const JBeamStructuralIR& ir,
    BoundedPreflightResult& result)
{
    if (result.node_source_order.empty())
    {
        return false;
    }
    bool valid = true;
    const std::size_t first_index = result.node_source_order[0];
    JBeamRigDefPoint3 minimum = result.transformed_nodes[first_index];
    JBeamRigDefPoint3 maximum = minimum;
    double total_mass = 0.0;
    double center_x = 0.0;
    double center_y = 0.0;
    double center_z = 0.0;
    float runtime_total = 0.0f;

    for (std::size_t order_index = 0U;
         order_index < result.node_source_order.size();
         ++order_index)
    {
        const std::size_t source_index =
            result.node_source_order[order_index];
        const float mass = result.node_masses[source_index];
        const JBeamRigDefPoint3& point =
            result.transformed_nodes[source_index];
        float next_runtime_total = 0.0f;
        if (!TryRuntimeAdd(
                runtime_total, mass, &next_runtime_total) ||
            !(next_runtime_total > 0.0f))
        {
            PushDiagnostic(
                result,
                JBeamToRigDefDiagnosticCode::TOTAL_MASS_OVERFLOW,
                JBeamToRigDefEntityKind::NODE,
                source_index,
                ir.nodes[source_index].provenance,
                "Actor's binary32 total mass accumulation overflows");
            valid = false;
            break;
        }
        runtime_total = next_runtime_total;

        const double next_total =
            total_mass + static_cast<double>(mass);
        if (!Detail::IsFiniteBinary64(next_total) ||
            !(next_total > 0.0))
        {
            PushDiagnostic(
                result,
                JBeamToRigDefDiagnosticCode::TOTAL_MASS_OVERFLOW,
                JBeamToRigDefEntityKind::NODE,
                source_index,
                ir.nodes[source_index].provenance,
                "Total emitted node mass is not finite");
            valid = false;
            break;
        }
        if (order_index == 0U)
        {
            center_x = point.x;
            center_y = point.y;
            center_z = point.z;
        }
        else
        {
            const double old_weight = total_mass / next_total;
            const double new_weight =
                static_cast<double>(mass) / next_total;
            center_x =
                center_x * old_weight +
                static_cast<double>(point.x) * new_weight;
            center_y =
                center_y * old_weight +
                static_cast<double>(point.y) * new_weight;
            center_z =
                center_z * old_weight +
                static_cast<double>(point.z) * new_weight;
        }
        total_mass = next_total;

        minimum.x = std::min(minimum.x, point.x);
        minimum.y = std::min(minimum.y, point.y);
        minimum.z = std::min(minimum.z, point.z);
        maximum.x = std::max(maximum.x, point.x);
        maximum.y = std::max(maximum.y, point.y);
        maximum.z = std::max(maximum.z, point.z);
    }

    if (!valid)
    {
        return false;
    }
    if (!Detail::IsFiniteBinary64(center_x) ||
        !Detail::IsFiniteBinary64(center_y) ||
        !Detail::IsFiniteBinary64(center_z) ||
        !TryNarrowFinite(center_x, false, &result.metrics.center_of_mass.x) ||
        !TryNarrowFinite(center_y, false, &result.metrics.center_of_mass.y) ||
        !TryNarrowFinite(center_z, false, &result.metrics.center_of_mass.z))
    {
        PushDiagnostic(
            result,
            JBeamToRigDefDiagnosticCode::INVALID_CENTER_OF_MASS,
            JBeamToRigDefEntityKind::DOCUMENT,
            INVALID_SOURCE_INDEX,
            ir.ref_frame.provenance,
            "Mass-weighted center of mass is not representable in binary32");
        valid = false;
    }

    float extent = 0.0f;
    if (!TryRuntimeSubtract(maximum.x, minimum.x, &extent) ||
        !TryRuntimeSubtract(maximum.y, minimum.y, &extent) ||
        !TryRuntimeSubtract(maximum.z, minimum.z, &extent))
    {
        PushDiagnostic(
            result,
            JBeamToRigDefDiagnosticCode::INVALID_BOUNDS,
            JBeamToRigDefEntityKind::DOCUMENT,
            INVALID_SOURCE_INDEX,
            ir.ref_frame.provenance,
            "Transformed bounds overflow binary32 extent calculations");
        valid = false;
    }

    result.metrics.total_mass_kg = total_mass;
    result.metrics.runtime_total_mass_kg = runtime_total;
    result.metrics.bounds_min = minimum;
    result.metrics.bounds_max = maximum;
    return valid;
}

bool TryAddSize(std::size_t value, std::size_t* total)
{
    if (total == NULL ||
        value > std::numeric_limits<std::size_t>::max() - *total)
    {
        return false;
    }
    *total += value;
    return true;
}

bool TryAddWeightedSize(
    std::size_t value,
    std::size_t multiplier,
    std::size_t* total)
{
    if (multiplier != 0U &&
        value > std::numeric_limits<std::size_t>::max() / multiplier)
    {
        return false;
    }
    return TryAddSize(value * multiplier, total);
}

std::size_t DecimalDigitCount(std::size_t value)
{
    std::size_t digits = 1U;
    while (value >= 10U)
    {
        value /= 10U;
        ++digits;
    }
    return digits;
}

bool TryMeasureInputEnvelope(
    const JBeamStructuralIR& ir,
    std::size_t* input_records,
    std::size_t* work_units)
{
    if (input_records == NULL || work_units == NULL)
    {
        return false;
    }
    *input_records = 0U;
    *work_units = 2U;
    if (!TryAddSize(ir.nodes.size(), input_records) ||
        !TryAddSize(ir.beams.size(), input_records) ||
        !TryAddSize(ir.triangles.size(), input_records) ||
        !TryAddWeightedSize(ir.nodes.size(), 2U, work_units) ||
        !TryAddWeightedSize(ir.beams.size(), 2U, work_units) ||
        !TryAddWeightedSize(ir.triangles.size(), 2U, work_units) ||
        !TryAddWeightedSize(
            ir.diagnostics.size(), 2U, work_units))
    {
        return false;
    }
    return true;
}

bool TryMeasureStructuralErrorDetail(
    const JBeamStructuralDiagnostic& source,
    std::size_t* output)
{
    if (output == NULL)
    {
        return false;
    }
    *output = sizeof("Structural IR error ") - 1U;
    const char* const code =
        JBeamStructuralDiagnosticCodeToString(source.code);
    if (!TryAddSize(std::strlen(code), output))
    {
        return false;
    }
    if (!source.section.empty() &&
        (!TryAddSize(sizeof(" in ") - 1U, output) ||
         !TryAddSize(source.section.size(), output) ||
         !TryAddSize(sizeof(" row ") - 1U, output) ||
         !TryAddSize(DecimalDigitCount(source.row_index), output)))
    {
        return false;
    }
    if (!source.field_name.empty() &&
        (!TryAddSize(sizeof(" field ") - 1U, output) ||
         !TryAddSize(source.field_name.size(), output)))
    {
        return false;
    }
    if (!source.detail.empty() &&
        (!TryAddSize(sizeof(": ") - 1U, output) ||
         !TryAddSize(source.detail.size(), output)))
    {
        return false;
    }
    return true;
}

struct StructuralDiagnosticStats
{
    bool has_error;
    std::size_t retained_detail_bytes;

    StructuralDiagnosticStats()
        : has_error(false)
        , retained_detail_bytes(0U)
    {
    }
};

bool TryMeasureStructuralErrors(
    const JBeamStructuralIR& ir,
    std::size_t detail_byte_limit,
    StructuralDiagnosticStats* stats)
{
    if (stats == NULL)
    {
        return false;
    }
    *stats = StructuralDiagnosticStats();
    for (std::size_t i = 0U; i < ir.diagnostics.size(); ++i)
    {
        const JBeamStructuralDiagnostic& source = ir.diagnostics[i];
        if (source.severity != JBeamStructuralSeverity::ERROR_SEVERITY)
        {
            continue;
        }
        stats->has_error = true;
        std::size_t detail_size = 0U;
        if (!TryMeasureStructuralErrorDetail(source, &detail_size) ||
            !TryAddSize(detail_size, &stats->retained_detail_bytes) ||
            stats->retained_detail_bytes > detail_byte_limit)
        {
            return false;
        }
    }
    return true;
}

void AppendDecimal(std::string& output, std::size_t value)
{
    char reversed[3U * sizeof(std::size_t) + 1U];
    std::size_t count = 0U;
    do
    {
        reversed[count++] =
            static_cast<char>('0' + static_cast<char>(value % 10U));
        value /= 10U;
    }
    while (value != 0U);
    while (count != 0U)
    {
        output.push_back(reversed[--count]);
    }
}

std::string BuildStructuralErrorDetail(
    const JBeamStructuralDiagnostic& source)
{
    std::size_t detail_size = 0U;
    if (!TryMeasureStructuralErrorDetail(source, &detail_size))
    {
        throw std::length_error(
            "Structural diagnostic detail length overflow");
    }
    std::string detail;
    detail.reserve(detail_size);
    detail.append("Structural IR error ");
    detail.append(JBeamStructuralDiagnosticCodeToString(source.code));
    if (!source.section.empty())
    {
        detail.append(" in ");
        detail.append(source.section);
        detail.append(" row ");
        AppendDecimal(detail, source.row_index);
    }
    if (!source.field_name.empty())
    {
        detail.append(" field ");
        detail.append(source.field_name);
    }
    if (!source.detail.empty())
    {
        detail.append(": ");
        detail.append(source.detail);
    }
    return detail;
}

void CopyStructuralErrors(
    const JBeamStructuralIR& ir,
    BoundedPreflightResult& result)
{
    for (std::size_t i = 0U; i < ir.diagnostics.size(); ++i)
    {
        const JBeamStructuralDiagnostic& source = ir.diagnostics[i];
        if (source.severity == JBeamStructuralSeverity::ERROR_SEVERITY)
        {
            PushDiagnostic(
                result,
                JBeamToRigDefDiagnosticCode::INVALID_STRUCTURAL_IR,
                JBeamToRigDefEntityKind::STRUCTURAL_IR,
                INVALID_SOURCE_INDEX,
                source.provenance,
                BuildStructuralErrorDetail(source));
        }
    }
}

JBeamToRigDefPreflightResult PreflightImpl(
    const JBeamStructuralIR& ir,
    const std::string& document_name,
    const JBeamToRigDefLimits& limits)
{
    BoundedPreflightResult result(limits);
    const JBeamStructuralProvenance document_provenance =
        ir.has_ref_frame
            ? ir.ref_frame.provenance
            : JBeamStructuralProvenance();

    const std::size_t input_record_limit = std::min(
        limits.max_input_records,
        JBEAM_RIGDEF_INPUT_RECORD_LIMIT);
    const std::size_t work_unit_limit = std::min(
        limits.max_work_units,
        JBEAM_RIGDEF_WORK_UNIT_LIMIT);
    std::size_t input_records = 0U;
    std::size_t work_units = 0U;
    if (!TryMeasureInputEnvelope(
            ir, &input_records, &work_units))
    {
        PushDiagnostic(
            result,
            JBeamToRigDefDiagnosticCode::INPUT_RECORD_LIMIT,
            JBeamToRigDefEntityKind::STRUCTURAL_IR,
            INVALID_SOURCE_INDEX,
            document_provenance,
            "Input record count overflows the adapter admission envelope");
        ClearPlan(result);
        return std::move(
            static_cast<JBeamToRigDefPreflightResult&>(result));
    }
    if (input_records > input_record_limit)
    {
        PushDiagnostic(
            result,
            JBeamToRigDefDiagnosticCode::INPUT_RECORD_LIMIT,
            JBeamToRigDefEntityKind::STRUCTURAL_IR,
            INVALID_SOURCE_INDEX,
            document_provenance,
            "Total node, beam, and triangle records exceed the bounded "
            "adapter input envelope");
        ClearPlan(result);
        return std::move(
            static_cast<JBeamToRigDefPreflightResult&>(result));
    }
    if (work_units > work_unit_limit)
    {
        PushDiagnostic(
            result,
            JBeamToRigDefDiagnosticCode::WORK_LIMIT,
            JBeamToRigDefEntityKind::STRUCTURAL_IR,
            INVALID_SOURCE_INDEX,
            document_provenance,
            "Adapter record-scan work exceeds the bounded preflight "
            "envelope");
        ClearPlan(result);
        return std::move(
            static_cast<JBeamToRigDefPreflightResult&>(result));
    }
    if (ir.diagnostics.size() >
        result.diagnostic_payload_limit)
    {
        PushDiagnostic(
            result,
            JBeamToRigDefDiagnosticCode::DIAGNOSTIC_LIMIT,
            JBeamToRigDefEntityKind::STRUCTURAL_IR,
            INVALID_SOURCE_INDEX,
            document_provenance,
            "Upstream diagnostic count exceeds the bounded adapter "
            "envelope");
        ClearPlan(result);
        return std::move(
            static_cast<JBeamToRigDefPreflightResult&>(result));
    }

    StructuralDiagnosticStats structural_diagnostics;
    if (!TryMeasureStructuralErrors(
            ir,
            result.diagnostic_detail_byte_limit,
            &structural_diagnostics))
    {
        PushDiagnostic(
            result,
            JBeamToRigDefDiagnosticCode::DIAGNOSTIC_DETAIL_LIMIT,
            JBeamToRigDefEntityKind::STRUCTURAL_IR,
            INVALID_SOURCE_INDEX,
            document_provenance,
            std::string());
        ClearPlan(result);
        return std::move(
            static_cast<JBeamToRigDefPreflightResult&>(result));
    }

    if (document_name.empty() || HasEmbeddedNull(document_name))
    {
        PushDiagnostic(
            result,
            JBeamToRigDefDiagnosticCode::INVALID_DOCUMENT_NAME,
            JBeamToRigDefEntityKind::DOCUMENT,
            INVALID_SOURCE_INDEX,
            document_provenance,
            "RigDef document name must be non-empty and contain no NUL");
    }
    if (!ir.has_ref_frame)
    {
        PushDiagnostic(
            result,
            JBeamToRigDefDiagnosticCode::MISSING_REF_FRAME,
            JBeamToRigDefEntityKind::REF_FRAME,
            INVALID_SOURCE_INDEX,
            document_provenance,
            "A six-node refNodes frame is required for spawning");
    }
    CopyStructuralErrors(ir, result);
    if (!ir.has_ref_frame || structural_diagnostics.has_error)
    {
        if (result.diagnostics.empty())
        {
            PushDiagnostic(
                result,
                JBeamToRigDefDiagnosticCode::INVALID_STRUCTURAL_IR,
                JBeamToRigDefEntityKind::STRUCTURAL_IR,
                INVALID_SOURCE_INDEX,
                document_provenance,
                "Structural IR rejected its own validity invariant");
        }
        ClearPlan(result);
        return std::move(
            static_cast<JBeamToRigDefPreflightResult&>(result));
    }

    const std::size_t node_limit =
        std::min(limits.max_nodes, JBEAM_RIGDEF_RUNTIME_NODE_LIMIT);
    const std::size_t beam_limit =
        std::min(limits.max_beams, JBEAM_RIGDEF_RUNTIME_BEAM_LIMIT);
    const std::size_t cab_limit =
        std::min(
            limits.max_cab_triangles,
            JBEAM_RIGDEF_RUNTIME_CAB_LIMIT);
    if (ir.nodes.size() > node_limit)
    {
        PushDiagnostic(
            result,
            JBeamToRigDefDiagnosticCode::NODE_LIMIT,
            JBeamToRigDefEntityKind::DOCUMENT,
            INVALID_SOURCE_INDEX,
            document_provenance,
            "Node count exceeds the configured or 65535-node runtime "
            "limit");
    }

    std::size_t enabled_beams = 0U;
    for (std::size_t i = 0U; i < ir.beams.size(); ++i)
    {
        const JBeamStructuralBeam& beam = ir.beams[i];
        switch (beam.status)
        {
        case JBeamStructuralBeamStatus::ENABLED:
            if (beam.beam_type != "NORMAL" &&
                beam.beam_type != "SUPPORT")
            {
                PushDiagnostic(
                    result,
                    JBeamToRigDefDiagnosticCode::INVALID_ENTITY_STATE,
                    JBeamToRigDefEntityKind::BEAM,
                    i,
                    beam.provenance,
                    "Only NORMAL or SUPPORT beams may be enabled");
            }
            ++enabled_beams;
            break;
        case JBeamStructuralBeamStatus::PRESERVED_DISABLED_SPECIAL_TYPE:
            if (beam.beam_type == "NORMAL" ||
                beam.beam_type == "SUPPORT")
            {
                PushDiagnostic(
                    result,
                    JBeamToRigDefDiagnosticCode::INVALID_ENTITY_STATE,
                    JBeamToRigDefEntityKind::BEAM,
                    i,
                    beam.provenance,
                    "NORMAL or SUPPORT beam cannot use the disabled special "
                    "state");
            }
            break;
        case JBeamStructuralBeamStatus::
            PRESERVED_DISABLED_OPTIONAL_REFERENCE:
            if (!beam.optional)
            {
                PushDiagnostic(
                    result,
                    JBeamToRigDefDiagnosticCode::INVALID_ENTITY_STATE,
                    JBeamToRigDefEntityKind::BEAM,
                    i,
                    beam.provenance,
                    "Disabled optional beam must set optional=true");
            }
            break;
        default:
            PushDiagnostic(
                result,
                JBeamToRigDefDiagnosticCode::INVALID_ENTITY_STATE,
                JBeamToRigDefEntityKind::BEAM,
                i,
                beam.provenance,
                "Beam has an unknown enabled/disabled state");
            break;
        }
    }
    if (enabled_beams > beam_limit)
    {
        PushDiagnostic(
            result,
            JBeamToRigDefDiagnosticCode::BEAM_LIMIT,
            JBeamToRigDefEntityKind::DOCUMENT,
            INVALID_SOURCE_INDEX,
            document_provenance,
            "Enabled NORMAL/SUPPORT beam count exceeds the configured or "
            "1000000-beam runtime limit");
    }
    else if (enabled_beams == 0U)
    {
        PushDiagnostic(
            result,
            JBeamToRigDefDiagnosticCode::MISSING_STRUCTURAL_BEAM,
            JBeamToRigDefEntityKind::DOCUMENT,
            INVALID_SOURCE_INDEX,
            document_provenance,
            "At least one supported NORMAL or SUPPORT beam is required to "
            "keep Actor's mass redistribution finite");
    }

    std::size_t enabled_triangles = 0U;
    for (std::size_t i = 0U; i < ir.triangles.size(); ++i)
    {
        const JBeamStructuralTriangle& triangle = ir.triangles[i];
        switch (triangle.status)
        {
        case JBeamStructuralTriangleStatus::ENABLED:
            ++enabled_triangles;
            break;
        case JBeamStructuralTriangleStatus::
            PRESERVED_DISABLED_OPTIONAL_REFERENCE:
            if (!triangle.optional)
            {
                PushDiagnostic(
                    result,
                    JBeamToRigDefDiagnosticCode::INVALID_ENTITY_STATE,
                    JBeamToRigDefEntityKind::TRIANGLE,
                    i,
                    triangle.provenance,
                    "Disabled optional triangle must set optional=true");
            }
            break;
        default:
            PushDiagnostic(
                result,
                JBeamToRigDefDiagnosticCode::INVALID_ENTITY_STATE,
                JBeamToRigDefEntityKind::TRIANGLE,
                i,
                triangle.provenance,
                "Triangle has an unknown enabled/disabled state");
            break;
        }
    }
    if (enabled_triangles > cab_limit)
    {
        PushDiagnostic(
            result,
            JBeamToRigDefDiagnosticCode::CAB_LIMIT,
            JBeamToRigDefEntityKind::DOCUMENT,
            INVALID_SOURCE_INDEX,
            document_provenance,
            "Enabled triangle count exceeds the configured or 3000-cab "
            "runtime limit");
    }

    // Counts and status tags are checked before any plan storage is reserved.
    if (!result.diagnostics.empty())
    {
        ClearPlan(result);
        return std::move(
            static_cast<JBeamToRigDefPreflightResult&>(result));
    }

    result.transformed_nodes.resize(ir.nodes.size());
    result.node_masses.resize(ir.nodes.size(), 0.0f);
    result.node_source_order.reserve(ir.nodes.size());
    result.beams.reserve(enabled_beams);
    result.triangle_source_indices.reserve(enabled_triangles);
    NodeIndexMap node_indices;

    for (std::size_t i = 0U; i < ir.nodes.size(); ++i)
    {
        const JBeamStructuralNode& node = ir.nodes[i];
        if (node.id.empty() || HasEmbeddedNull(node.id))
        {
            PushDiagnostic(
                result,
                JBeamToRigDefDiagnosticCode::INVALID_NODE_ID,
                JBeamToRigDefEntityKind::NODE,
                i,
                node.provenance,
                "Named node ID must be non-empty and contain no NUL");
        }
        else if (!node_indices.insert(
                    std::make_pair(node.id, i)).second)
        {
            PushDiagnostic(
                result,
                JBeamToRigDefDiagnosticCode::DUPLICATE_NODE_ID,
                JBeamToRigDefEntityKind::NODE,
                i,
                node.provenance,
                "Named node ID is duplicated");
        }

        JBeamPoint3 transformed;
        if (!TryTransformBeamNGPointToRoR(
                JBeamPoint3(node.x, node.y, node.z),
                &transformed))
        {
            PushDiagnostic(
                result,
                JBeamToRigDefDiagnosticCode::NON_FINITE_VALUE,
                JBeamToRigDefEntityKind::NODE,
                i,
                node.provenance,
                "Node position is not finite");
        }
        else if (!TryNarrowFinite(
                    transformed.x, true,
                    &result.transformed_nodes[i].x) ||
                 !TryNarrowFinite(
                    transformed.y, true,
                    &result.transformed_nodes[i].y) ||
                 !TryNarrowFinite(
                    transformed.z, true,
                    &result.transformed_nodes[i].z))
        {
            PushDiagnostic(
                result,
                JBeamToRigDefDiagnosticCode::FLOAT_NARROWING,
                JBeamToRigDefEntityKind::NODE,
                i,
                node.provenance,
                "Transformed node position cannot be represented safely "
                "in binary32");
        }

        if (!TryNarrowPositive(
                node.node_weight, &result.node_masses[i]))
        {
            PushDiagnostic(
                result,
                JBeamToRigDefDiagnosticCode::INVALID_NODE_MASS,
                JBeamToRigDefEntityKind::NODE,
                i,
                node.provenance,
                "Node weight must narrow to a positive normal binary32 "
                "mass");
        }

        // BeamNG's top-level collision=false makes self/static flags
        // ineffective and maps exactly to RoR's no-ground/contactable node.
        // With collision enabled, RoR cannot presently preserve either
        // group-aware self-collision=true or staticCollision=false without
        // changing external dynamic-contact semantics. Reject those modes
        // rather than approximating them with a generic contacter.
        if (node.collision &&
            (node.self_collision || !node.static_collision))
        {
            PushDiagnostic(
                result,
                JBeamToRigDefDiagnosticCode::
                    UNSUPPORTED_NODE_COLLISION_MODE,
                JBeamToRigDefEntityKind::NODE,
                i,
                node.provenance,
                node.self_collision
                    ? "Enabled BeamNG selfCollision requires native "
                      "group-aware node/triangle behavior"
                    : "BeamNG staticCollision=false cannot preserve "
                      "external dynamic contact in the current RoR node "
                      "model");
        }
    }
    if (!result.diagnostics.empty())
    {
        ClearPlan(result);
        return std::move(
            static_cast<JBeamToRigDefPreflightResult&>(result));
    }

    const bool valid_frame = ValidateFrame(ir, node_indices, result);
    if (valid_frame)
    {
        result.node_source_order.push_back(
            ir.ref_frame.reference_index);
        for (std::size_t i = 0U; i < ir.nodes.size(); ++i)
        {
            if (i != ir.ref_frame.reference_index)
            {
                result.node_source_order.push_back(i);
            }
        }
    }

    for (std::size_t i = 0U; i < ir.beams.size(); ++i)
    {
        BuildBeamPlan(ir, i, node_indices, result);
    }
    for (std::size_t i = 0U; i < ir.triangles.size(); ++i)
    {
        ValidateTriangle(ir, i, node_indices, result);
    }
    if (valid_frame)
    {
        BuildMassAndBounds(ir, result);
    }
    result.metrics.enabled_beam_count = result.beams.size();
    result.metrics.enabled_cab_triangle_count =
        result.triangle_source_indices.size();

    if (!result.diagnostics.empty())
    {
        ClearPlan(result);
    }
    return std::move(
        static_cast<JBeamToRigDefPreflightResult&>(result));
}

#if !defined(ROR_JBEAM_TO_RIGDEF_PREFLIGHT_ONLY)

unsigned int SourceLine(const JBeamStructuralProvenance& provenance)
{
    return static_cast<unsigned int>(provenance.begin.line);
}

RigDef::Node::Ref NamedReference(
    const std::string& id,
    const JBeamStructuralProvenance& provenance)
{
    return RigDef::Node::Ref(
        id,
        0U,
        RigDef::Node::Ref::REGULAR_STATE_IS_VALID |
            RigDef::Node::Ref::REGULAR_STATE_IS_NAMED,
        SourceLine(provenance));
}

bool SameBinary32(float first, float second)
{
    std::uint32_t first_bits = 0U;
    std::uint32_t second_bits = 0U;
    std::memcpy(&first_bits, &first, sizeof(first_bits));
    std::memcpy(&second_bits, &second, sizeof(second_bits));
    return first_bits == second_bits;
}

bool SameBinary64(double first, double second)
{
    std::uint64_t first_bits = 0U;
    std::uint64_t second_bits = 0U;
    std::memcpy(&first_bits, &first, sizeof(first_bits));
    std::memcpy(&second_bits, &second, sizeof(second_bits));
    return first_bits == second_bits;
}

bool SameHydroConfig(
    const HydroActuatorConfig& first,
    const HydroActuatorConfig& second)
{
    return first.has_factor == second.has_factor &&
        SameBinary64(first.factor, second.factor) &&
        SameBinary64(first.in_limit, second.in_limit) &&
        SameBinary64(first.out_limit, second.out_limit) &&
        SameBinary64(first.input_factor, second.input_factor) &&
        SameBinary64(first.input_center, second.input_center) &&
        SameBinary64(first.input_in_limit, second.input_in_limit) &&
        SameBinary64(first.input_out_limit, second.input_out_limit) &&
        SameBinary64(first.in_rate, second.in_rate) &&
        SameBinary64(first.out_rate, second.out_rate) &&
        SameBinary64(
            first.auto_center_rate, second.auto_center_rate);
}

bool SameHydroRuntimeStep(
    const JBeamHydroRuntimeStep& first,
    const JBeamHydroRuntimeStep& second)
{
    return first.valid == second.valid &&
        first.state.accepted_step_count ==
            second.state.accepted_step_count &&
        first.state.fault_latched == second.state.fault_latched &&
        first.state.fault == second.state.fault &&
        SameBinary64(
            first.state.response.length_ratio,
            second.state.response.length_ratio) &&
        SameBinary64(first.rest_length, second.rest_length) &&
        SameBinary32(
            first.runtime_rest_length,
            second.runtime_rest_length) &&
        SameBinary64(first.target_ratio, second.target_ratio) &&
        first.input_was_clamped == second.input_was_clamped;
}

bool TrySourceLength(
    const JBeamStructuralNode& first,
    const JBeamStructuralNode& second,
    double* output)
{
    if (output == NULL)
    {
        return false;
    }
    *output = 0.0;
    if (!Detail::IsFiniteBinary64(first.x) ||
        !Detail::IsFiniteBinary64(first.y) ||
        !Detail::IsFiniteBinary64(first.z) ||
        !Detail::IsFiniteBinary64(second.x) ||
        !Detail::IsFiniteBinary64(second.y) ||
        !Detail::IsFiniteBinary64(second.z))
    {
        return false;
    }
    const double dx = first.x - second.x;
    const double dy = first.y - second.y;
    const double dz = first.z - second.z;
    if (!Detail::IsFiniteBinary64(dx) ||
        !Detail::IsFiniteBinary64(dy) ||
        !Detail::IsFiniteBinary64(dz))
    {
        return false;
    }
    const double maximum = std::max(
        Absolute(dx), std::max(Absolute(dy), Absolute(dz)));
    if (!Detail::IsFiniteBinary64(maximum) || !(maximum > 0.0))
    {
        return false;
    }
    const double sx = dx / maximum;
    const double sy = dy / maximum;
    const double sz = dz / maximum;
    const double squared = sx * sx + sy * sy + sz * sz;
    if (!Detail::IsFiniteBinary64(squared) || !(squared > 0.0))
    {
        return false;
    }
    const double length = maximum * std::sqrt(squared);
    if (!Detail::IsFiniteBinary64(length) || !(length > 0.0))
    {
        return false;
    }
    *output = length;
    return true;
}

bool ValidateHydroRuntimePlans(
    const JBeamStructuralIR& ir,
    const JBeamToRigDefPreflightResult& structural,
    const JBeamHydroRuntimePlanSet& plan_set,
    const JBeamToRigDefLimits& limits,
    std::vector<JBeamToRigDefDiagnostic>& diagnostics)
{
    const JBeamStructuralProvenance document_provenance =
        ir.has_ref_frame
            ? ir.ref_frame.provenance
            : JBeamStructuralProvenance();
    if (plan_set.code != JBeamHydroRuntimePlanSetCode::ADMITTED ||
        plan_set.source_hydro_count != plan_set.plans.size())
    {
        PushDiagnostic(
            diagnostics,
            JBeamToRigDefDiagnosticCode::INVALID_HYDRO_RUNTIME_PLAN,
            JBeamToRigDefEntityKind::DOCUMENT,
            INVALID_SOURCE_INDEX,
            document_provenance,
            "Hydro plan set is not an admitted exact-size transaction");
        return false;
    }

    const std::size_t total_limit = std::min(
        std::min(limits.max_beams, JBEAM_RIGDEF_RUNTIME_BEAM_LIMIT),
        static_cast<std::size_t>(
            std::numeric_limits<std::uint16_t>::max()));
    if (structural.beams.size() > total_limit ||
        plan_set.plans.size() > total_limit - structural.beams.size())
    {
        PushDiagnostic(
            diagnostics,
            JBeamToRigDefDiagnosticCode::HYDRO_RUNTIME_LIMIT,
            JBeamToRigDefEntityKind::DOCUMENT,
            INVALID_SOURCE_INDEX,
            document_provenance,
            "Structural beams plus hydros exceed the uint16 runtime index "
            "boundary");
        return false;
    }

    for (std::size_t i = 0U; i < plan_set.plans.size(); ++i)
    {
        const JBeamHydroRuntimePlan& plan = plan_set.plans[i];
        const JBeamStructuralProvenance provenance =
            plan.node1_source_index < ir.nodes.size()
                ? ir.nodes[plan.node1_source_index].provenance
                : document_provenance;
        bool valid =
            plan.code == JBeamHydroRuntimePlanCode::ADMITTED &&
            plan.source_hydro_index == i &&
            plan.properties.code ==
                JBeamHydroBeamPropertyAdmissionCode::ADMITTED &&
            plan.properties.source_hydro_index == i &&
            plan.properties.actuator.code ==
                JBeamHydroActuatorAdmissionCode::ADMITTED &&
            plan.properties.actuator.source_hydro_index == i &&
            plan.node1_source_index < ir.nodes.size() &&
            plan.node2_source_index < ir.nodes.size() &&
            plan.node1_source_index != plan.node2_source_index &&
            plan.runtime_config.input_route ==
                JBeamHydroInputRoute::STEERING_INPUT &&
            plan.properties.actuator.input_source == "steering_input" &&
            IsValidJBeamHydroControlBinding(
                plan.control_binding,
                plan.runtime_config) &&
            SameHydroConfig(
                plan.runtime_config.response,
                plan.properties.actuator.config) &&
            plan.runtime_config.has_steering_wheel_lock ==
                plan.properties.actuator.has_steering_wheel_lock &&
            SameBinary64(
                plan.runtime_config.steering_wheel_lock,
                plan.properties.actuator.steering_wheel_lock);
        if (valid)
        {
            valid =
                ir.nodes[plan.node1_source_index].id ==
                    plan.properties.actuator.node1 &&
                ir.nodes[plan.node2_source_index].id ==
                    plan.properties.actuator.node2 &&
                IsFiniteBinary32(plan.properties.beam.spring) &&
                plan.properties.beam.spring >= 0.0f &&
                IsFiniteBinary32(plan.properties.beam.damping) &&
                plan.properties.beam.damping >= 0.0f &&
                IsFiniteBinary32(plan.properties.beam.deform) &&
                plan.properties.beam.deform >= 0.0f &&
                IsFiniteBinary32(plan.properties.beam.strength) &&
                plan.properties.beam.strength >= 0.0f &&
                IsNormalBinary32(plan.properties.beam.precompression) &&
                plan.properties.beam.precompression > 0.0f;
        }

        double source_length = 0.0;
        if (valid)
        {
            valid = TrySourceLength(
                ir.nodes[plan.node1_source_index],
                ir.nodes[plan.node2_source_index],
                &source_length) &&
                SameBinary64(source_length, plan.geometric_length);
        }
        if (valid)
        {
            const double source_initial_length = source_length *
                static_cast<double>(plan.properties.beam.precompression);
            const JBeamHydroRuntimeStep initialized =
                InitializeJBeamHydroRuntime(
                    plan.runtime_config, source_initial_length);
            valid = SameBinary64(
                    source_initial_length, plan.initial_rest_length) &&
                initialized.valid &&
                SameHydroRuntimeStep(
                    initialized, plan.initialized_runtime);
        }
        if (valid)
        {
            float runtime_length = 0.0f;
            valid = TryRuntimeLength(
                structural.transformed_nodes[plan.node1_source_index],
                structural.transformed_nodes[plan.node2_source_index],
                &runtime_length);
            if (valid)
            {
                const double runtime_initial_length =
                    static_cast<double>(runtime_length) *
                    static_cast<double>(
                        plan.properties.beam.precompression);
                valid = InitializeJBeamHydroRuntime(
                    plan.runtime_config,
                    runtime_initial_length).valid;
            }
        }
        if (!valid)
        {
            PushDiagnostic(
                diagnostics,
                JBeamToRigDefDiagnosticCode::INVALID_HYDRO_RUNTIME_PLAN,
                JBeamToRigDefEntityKind::HYDRO,
                i,
                provenance,
                "Hydro plan does not exactly match structural identity, "
                "configuration, or spawn geometry");
            return false;
        }
    }
    return true;
}

bool ValidateWheel2ApproximationPlans(
    const JBeamStructuralIR& ir,
    const JBeamToRigDefPreflightResult& structural,
    const JBeamHydroRuntimePlanSet* hydro_plans,
    const JBeamWheel2ApproximationPlanSet& plan_set,
    const JBeamToRigDefLimits& limits,
    std::vector<JBeamToRigDefDiagnostic>& diagnostics)
{
    const JBeamStructuralProvenance document_provenance =
        ir.has_ref_frame
            ? ir.ref_frame.provenance
            : JBeamStructuralProvenance();
    const std::string canonical =
        SerializeCanonicalJBeamWheel2ApproximationPlanSet(plan_set);
    if (!plan_set.IsAdmitted() || canonical.empty() ||
        plan_set.plans.size() > 64U)
    {
        PushDiagnostic(
            diagnostics,
            JBeamToRigDefDiagnosticCode::INVALID_WHEEL2_APPROXIMATION_PLAN,
            JBeamToRigDefEntityKind::DOCUMENT,
            INVALID_SOURCE_INDEX,
            document_provenance,
            "Wheel2 plan set is not an admitted canonical transaction");
        return false;
    }

    std::size_t expected_nodes = 0U;
    std::size_t expected_beams = 0U;
    std::set<std::string> names;
    std::set<std::pair<std::size_t, std::size_t>> source_rows;
    for (std::size_t i = 0U; i < plan_set.plans.size(); ++i)
    {
        const JBeamWheel2ApproximationPlan& plan = plan_set.plans[i];
        std::size_t node1_index = INVALID_SOURCE_INDEX;
        std::size_t node2_index = INVALID_SOURCE_INDEX;
        std::size_t arm_index = INVALID_SOURCE_INDEX;
        for (std::size_t node_index = 0U;
             node_index < ir.nodes.size(); ++node_index)
        {
            const std::string& id = ir.nodes[node_index].id;
            if (id == plan.node1)
            {
                if (node1_index != INVALID_SOURCE_INDEX)
                {
                    node1_index = INVALID_SOURCE_INDEX;
                    break;
                }
                node1_index = node_index;
            }
            if (id == plan.node2)
            {
                if (node2_index != INVALID_SOURCE_INDEX)
                {
                    node2_index = INVALID_SOURCE_INDEX;
                    break;
                }
                node2_index = node_index;
            }
            if (id == plan.node_arm)
            {
                if (arm_index != INVALID_SOURCE_INDEX)
                {
                    arm_index = INVALID_SOURCE_INDEX;
                    break;
                }
                arm_index = node_index;
            }
        }
        const JBeamStructuralProvenance provenance =
            node1_index < ir.nodes.size()
                ? ir.nodes[node1_index].provenance
                : document_provenance;
        bool valid =
            plan.source_wheel_index == i &&
            names.insert(plan.name).second && !plan.name.empty() &&
            source_rows.insert(std::make_pair(
                plan.source_record_index,
                plan.source_entry_index)).second &&
            (plan.wheel_direction == -1 || plan.wheel_direction == 1) &&
            plan.num_rays >= 10U && plan.num_rays <= 20U &&
            plan.num_rays % 2U == 0U &&
            plan.approximated_semantics ==
                JBEAM_WHEEL2_APPROXIMATION_SEMANTICS &&
            node1_index < ir.nodes.size() &&
            node2_index < ir.nodes.size() &&
            arm_index < ir.nodes.size() &&
            node1_index != node2_index &&
            node1_index != arm_index && node2_index != arm_index &&
            IsFiniteBinary32(plan.rim_radius) &&
            IsFiniteBinary32(plan.tyre_radius) &&
            IsFiniteBinary32(plan.width) &&
            IsFiniteBinary32(plan.mass) &&
            IsFiniteBinary32(plan.rim_spring) &&
            IsFiniteBinary32(plan.rim_damping) &&
            IsFiniteBinary32(plan.tyre_spring) &&
            IsFiniteBinary32(plan.tyre_damping) &&
            plan.rim_radius > 0.0f &&
            plan.tyre_radius > plan.rim_radius &&
            plan.width > 0.0f && plan.mass > 0.0f &&
            plan.rim_spring > 0.0f && plan.rim_damping >= 0.0f &&
            plan.tyre_spring > 0.0f && plan.tyre_damping >= 0.0f;
        if (valid)
        {
            float axis_length = 0.0f;
            valid = TryRuntimeLength(
                    structural.transformed_nodes[node1_index],
                    structural.transformed_nodes[node2_index],
                    &axis_length) &&
                SameBinary32(axis_length, plan.width) &&
                IsRuntimeTriangleNondegenerate(
                    structural.transformed_nodes[node1_index],
                    structural.transformed_nodes[node2_index],
                    structural.transformed_nodes[arm_index]);
        }
        const std::size_t nodes =
            static_cast<std::size_t>(plan.num_rays) * 4U;
        const std::size_t beams =
            static_cast<std::size_t>(plan.num_rays) * 24U;
        if (valid)
        {
            valid = TryAddSize(nodes, &expected_nodes) &&
                TryAddSize(beams, &expected_beams);
        }
        if (!valid)
        {
            PushDiagnostic(
                diagnostics,
                JBeamToRigDefDiagnosticCode::INVALID_WHEEL2_APPROXIMATION_PLAN,
                JBeamToRigDefEntityKind::WHEEL,
                i,
                provenance,
                "Wheel2 plan does not exactly match structural identity, "
                "binary32 geometry, or the bounded J3 profile");
            return false;
        }
    }
    if (expected_nodes != plan_set.generated_node_count ||
        expected_beams != plan_set.generated_beam_count)
    {
        PushDiagnostic(
            diagnostics,
            JBeamToRigDefDiagnosticCode::INVALID_WHEEL2_APPROXIMATION_PLAN,
            JBeamToRigDefEntityKind::DOCUMENT,
            INVALID_SOURCE_INDEX,
            document_provenance,
            "Wheel2 generated topology receipt does not match its rows");
        return false;
    }

    const std::size_t node_limit = std::min(
        limits.max_nodes, JBEAM_RIGDEF_RUNTIME_NODE_LIMIT);
    const std::size_t beam_limit = std::min(
        std::min(limits.max_beams, JBEAM_RIGDEF_RUNTIME_BEAM_LIMIT),
        static_cast<std::size_t>(
            std::numeric_limits<std::uint16_t>::max()));
    const std::size_t hydro_count =
        hydro_plans == NULL ? 0U : hydro_plans->plans.size();
    if (structural.node_source_order.size() > node_limit ||
        expected_nodes > node_limit - structural.node_source_order.size() ||
        structural.beams.size() > beam_limit ||
        hydro_count > beam_limit - structural.beams.size() ||
        expected_beams >
            beam_limit - structural.beams.size() - hydro_count)
    {
        PushDiagnostic(
            diagnostics,
            JBeamToRigDefDiagnosticCode::WHEEL2_RUNTIME_LIMIT,
            JBeamToRigDefEntityKind::DOCUMENT,
            INVALID_SOURCE_INDEX,
            document_provenance,
            "Structural, hydro, and generated Wheel2 topology exceed native "
            "ActorSpawner limits");
        return false;
    }
    return true;
}

#endif

} // namespace AdapterDetail

JBeamToRigDefDiagnostic::JBeamToRigDefDiagnostic()
    : code(JBeamToRigDefDiagnosticCode::INVALID_STRUCTURAL_IR)
    , entity_kind(JBeamToRigDefEntityKind::STRUCTURAL_IR)
    , source_index(AdapterDetail::INVALID_SOURCE_INDEX)
{
}

JBeamToRigDefLimits::JBeamToRigDefLimits()
    : max_input_records(JBEAM_RIGDEF_INPUT_RECORD_LIMIT)
    , max_work_units(JBEAM_RIGDEF_WORK_UNIT_LIMIT)
    , max_diagnostics(JBEAM_RIGDEF_DIAGNOSTIC_LIMIT)
    , max_diagnostic_detail_bytes(
          JBEAM_RIGDEF_DIAGNOSTIC_DETAIL_BYTE_LIMIT)
    , max_nodes(JBEAM_RIGDEF_RUNTIME_NODE_LIMIT)
    , max_beams(JBEAM_RIGDEF_RUNTIME_BEAM_LIMIT)
    , max_cab_triangles(JBEAM_RIGDEF_RUNTIME_CAB_LIMIT)
{
}

JBeamRigDefPoint3::JBeamRigDefPoint3()
    : x(0.0f)
    , y(0.0f)
    , z(0.0f)
{
}

JBeamRigDefPoint3::JBeamRigDefPoint3(
    float x_value,
    float y_value,
    float z_value)
    : x(x_value)
    , y(y_value)
    , z(z_value)
{
}

JBeamRigDefBeamPlan::JBeamRigDefBeamPlan()
    : source_index(0U)
    , spring(0.0f)
    , damping(0.0f)
    , deform(0.0f)
    , strength(0.0f)
    , rest_length_scale(1.0f)
    , support(false)
    , extension_break_limit(1.0f)
    , geometric_length(0.0f)
    , scaled_rest_length(0.0f)
{
}

JBeamToRigDefMetrics::JBeamToRigDefMetrics()
    : enabled_beam_count(0U)
    , enabled_cab_triangle_count(0U)
    , total_mass_kg(0.0)
    , runtime_total_mass_kg(0.0f)
    , runtime_total_beam_length(0.0f)
{
}

bool JBeamToRigDefPreflightResult::IsValid() const
{
    return diagnostics.empty() &&
        !node_source_order.empty() &&
        node_source_order.size() == transformed_nodes.size() &&
        transformed_nodes.size() == node_masses.size();
}

JBeamToRigDefPreflightResult PreflightJBeamToRigDef(
    const JBeamStructuralIR& ir,
    const std::string& document_name,
    const JBeamToRigDefLimits& limits)
{
    try
    {
        return AdapterDetail::PreflightImpl(ir, document_name, limits);
    }
    catch (const std::bad_alloc&)
    {
        JBeamToRigDefPreflightResult result;
        const JBeamStructuralProvenance provenance =
            ir.has_ref_frame
                ? ir.ref_frame.provenance
                : JBeamStructuralProvenance();
        AdapterDetail::PushDiagnostic(
            result,
            JBeamToRigDefDiagnosticCode::ALLOCATION_FAILURE,
            JBeamToRigDefEntityKind::DOCUMENT,
            AdapterDetail::INVALID_SOURCE_INDEX,
            provenance,
            "Adapter preflight allocation failed");
        return result;
    }
    catch (const std::length_error&)
    {
        JBeamToRigDefPreflightResult result;
        const JBeamStructuralProvenance provenance =
            ir.has_ref_frame
                ? ir.ref_frame.provenance
                : JBeamStructuralProvenance();
        AdapterDetail::PushDiagnostic(
            result,
            JBeamToRigDefDiagnosticCode::ALLOCATION_FAILURE,
            JBeamToRigDefEntityKind::DOCUMENT,
            AdapterDetail::INVALID_SOURCE_INDEX,
            provenance,
            "Adapter preflight vector length exceeded its implementation "
            "limit");
        return result;
    }
}

#if !defined(ROR_JBEAM_TO_RIGDEF_PREFLIGHT_ONLY)

namespace {

RigDef::DocumentPtr ConvertJBeamToRigDefImpl(
    const JBeamStructuralIR& ir,
    const JBeamHydroRuntimePlanSet* hydro_plans,
    const JBeamWheel2ApproximationPlanSet* wheel_plans,
    const std::string& document_name,
    std::vector<JBeamToRigDefDiagnostic>& diagnostics,
    const JBeamToRigDefLimits& limits)
{
    diagnostics.clear();
    JBeamToRigDefPreflightResult preflight =
        PreflightJBeamToRigDef(ir, document_name, limits);
    if (!preflight.IsValid())
    {
        diagnostics.swap(preflight.diagnostics);
        return RigDef::DocumentPtr();
    }
    if (hydro_plans != NULL &&
        !AdapterDetail::ValidateHydroRuntimePlans(
            ir, preflight, *hydro_plans, limits, diagnostics))
    {
        return RigDef::DocumentPtr();
    }
    if (wheel_plans != NULL &&
        !AdapterDetail::ValidateWheel2ApproximationPlans(
            ir, preflight, hydro_plans, *wheel_plans, limits, diagnostics))
    {
        return RigDef::DocumentPtr();
    }

    try
    {
        // No RigDef or OGRE allocation occurs until every imported value and
        // runtime limit has passed the dependency-light preflight above.
        RigDef::DocumentPtr document =
            std::make_shared<RigDef::Document>();
        document->name = document_name;
        document->enable_advanced_deformation = true;
        if (!document->root_module)
        {
            throw std::runtime_error(
                "RigDef::Document did not create a root module");
        }
        RigDef::Document::Module& module = *document->root_module;
        module.nodes.reserve(preflight.node_source_order.size());
        module.beams.reserve(preflight.beams.size());
        if (hydro_plans != NULL)
        {
            module.hydros.reserve(hydro_plans->plans.size());
        }
        if (wheel_plans != NULL)
        {
            module.wheels2.reserve(wheel_plans->plans.size());
        }
        module.cameras.reserve(1U);
        module.globals.reserve(1U);
        if (!preflight.triangle_source_indices.empty())
        {
            module.submeshes.reserve(1U);
        }

        const std::shared_ptr<RigDef::NodeDefaults> node_defaults =
            std::make_shared<RigDef::NodeDefaults>();
        node_defaults->load_weight = -1.0f;
        node_defaults->options = 0U;
        const std::shared_ptr<RigDef::DefaultMinimass> minimass =
            std::make_shared<RigDef::DefaultMinimass>();
        minimass->min_mass_Kg = 0.0f;

        for (std::size_t order_index = 0U;
             order_index < preflight.node_source_order.size();
             ++order_index)
        {
            const std::size_t source_index =
                preflight.node_source_order[order_index];
            const JBeamStructuralNode& source = ir.nodes[source_index];
            const JBeamRigDefPoint3& position =
                preflight.transformed_nodes[source_index];
            RigDef::Node node;
            node.id = RigDef::Node::Id(source.id);
            node.position =
                Ogre::Vector3(position.x, position.y, position.z);
            node.options = RigDef::Node::OPTION_l_LOAD_WEIGHT;
            if (!source.collision)
            {
                node.options |=
                    RigDef::Node::OPTION_c_NO_GROUND_CONTACT;
            }
            node.load_weight_override =
                preflight.node_masses[source_index];
            node._has_load_weight_override = true;
            node.node_defaults = node_defaults;
            node.default_minimass = minimass;
            module.nodes.push_back(node);
        }

        for (std::size_t plan_index = 0U;
             plan_index < preflight.beams.size();
             ++plan_index)
        {
            const JBeamRigDefBeamPlan& plan =
                preflight.beams[plan_index];
            const JBeamStructuralBeam& source =
                ir.beams[plan.source_index];
            RigDef::Beam beam;
            beam.nodes[0] =
                AdapterDetail::NamedReference(
                    source.node_a, source.provenance);
            beam.nodes[1] =
                AdapterDetail::NamedReference(
                    source.node_b, source.provenance);
            beam.options = RigDef::Beam::OPTION_i_INVISIBLE;
            if (plan.support)
            {
                beam.options |=
                    RigDef::Beam::OPTION_COMPRESSION_ONLY_SUPPORT;
                beam.extension_break_limit =
                    plan.extension_break_limit;
                beam._has_extension_break_limit = true;
            }
            beam._rest_length_scale = plan.rest_length_scale;
            beam.defaults = std::make_shared<RigDef::BeamDefaults>();
            beam.defaults->springiness = plan.spring;
            beam.defaults->damping_constant = plan.damping;
            beam.defaults->deformation_threshold = plan.deform;
            beam.defaults->breaking_threshold = plan.strength;
            beam.defaults->visual_beam_diameter = 0.0f;
            // ActorSpawner otherwise applies legacy truck defaults and raises
            // BeamNG deformation values to RoR's 100 kN creak floor (or its
            // 400 kN non-advanced floor). An explicitly authored neutral
            // plastic coefficient is the existing RigDef signal that disables
            // that legacy creak clamp without changing BeamNG's threshold.
            beam.defaults->plastic_deform_coef =
                BEAM_PLASTIC_COEF_DEFAULT;
            beam.defaults->_is_plastic_deform_coef_user_defined = true;
            beam.defaults->_is_user_defined = true;
            beam.defaults->_enable_advanced_deformation = true;
            module.beams.push_back(beam);
        }

        if (hydro_plans != NULL)
        {
            for (std::size_t plan_index = 0U;
                 plan_index < hydro_plans->plans.size();
                 ++plan_index)
            {
                const JBeamHydroRuntimePlan& plan =
                    hydro_plans->plans[plan_index];
                RigDef::Hydro hydro;
                hydro.nodes[0] = AdapterDetail::NamedReference(
                    ir.nodes[plan.node1_source_index].id,
                    ir.nodes[plan.node1_source_index].provenance);
                hydro.nodes[1] = AdapterDetail::NamedReference(
                    ir.nodes[plan.node2_source_index].id,
                    ir.nodes[plan.node2_source_index].provenance);
                hydro.lenghtening_factor = 0.0f;
                hydro.options = 0U;
                hydro.inertia_defaults =
                    std::make_shared<RigDef::Inertia>();
                hydro.beam_defaults =
                    std::make_shared<RigDef::BeamDefaults>();
                hydro.beam_defaults->springiness =
                    plan.properties.beam.spring;
                hydro.beam_defaults->damping_constant =
                    plan.properties.beam.damping;
                hydro.beam_defaults->deformation_threshold =
                    plan.properties.beam.deform;
                hydro.beam_defaults->breaking_threshold =
                    plan.properties.beam.strength;
                hydro.beam_defaults->visual_beam_diameter = 0.0f;
                hydro.beam_defaults->plastic_deform_coef =
                    BEAM_PLASTIC_COEF_DEFAULT;
                hydro.beam_defaults->
                    _is_plastic_deform_coef_user_defined = true;
                hydro.beam_defaults->_is_user_defined = true;
                hydro.beam_defaults->_enable_advanced_deformation = true;
                hydro._jbeam_runtime_plan =
                    std::make_shared<const JBeamHydroRuntimePlan>(plan);
                module.hydros.push_back(std::move(hydro));
            }
        }

        if (wheel_plans != NULL)
        {
            for (std::size_t plan_index = 0U;
                 plan_index < wheel_plans->plans.size(); ++plan_index)
            {
                const JBeamWheel2ApproximationPlan& plan =
                    wheel_plans->plans[plan_index];
                const JBeamStructuralNode* node1 = NULL;
                const JBeamStructuralNode* node2 = NULL;
                const JBeamStructuralNode* arm = NULL;
                for (std::size_t node_index = 0U;
                     node_index < ir.nodes.size(); ++node_index)
                {
                    if (ir.nodes[node_index].id == plan.node1)
                    {
                        node1 = &ir.nodes[node_index];
                    }
                    if (ir.nodes[node_index].id == plan.node2)
                    {
                        node2 = &ir.nodes[node_index];
                    }
                    if (ir.nodes[node_index].id == plan.node_arm)
                    {
                        arm = &ir.nodes[node_index];
                    }
                }
                if (node1 == NULL || node2 == NULL || arm == NULL)
                {
                    throw std::runtime_error(
                        "Validated Wheel2 node identity disappeared");
                }

                RigDef::Wheel2 wheel;
                wheel.rim_radius = plan.rim_radius;
                wheel.tyre_radius = plan.tyre_radius;
                wheel.width = plan.width;
                wheel.num_rays = plan.num_rays;
                wheel.nodes[0] = AdapterDetail::NamedReference(
                    plan.node1, node1->provenance);
                wheel.nodes[1] = AdapterDetail::NamedReference(
                    plan.node2, node2->provenance);
                wheel.braking = RoR::WheelBraking::NONE;
                wheel.propulsion = RoR::WheelPropulsion::NONE;
                wheel.reference_arm_node = AdapterDetail::NamedReference(
                    plan.node_arm, arm->provenance);
                wheel.mass = plan.mass;
                wheel.rim_springiness = plan.rim_spring;
                wheel.rim_damping = plan.rim_damping;
                wheel.tyre_springiness = plan.tyre_spring;
                wheel.tyre_damping = plan.tyre_damping;
                wheel.node_defaults = node_defaults;
                wheel.beam_defaults =
                    std::make_shared<RigDef::BeamDefaults>();
                wheel.beam_defaults->springiness = plan.rim_spring;
                wheel.beam_defaults->damping_constant = plan.rim_damping;
                wheel.beam_defaults->deformation_threshold =
                    JBEAM_RIGDEF_DEFAULT_BEAM_DEFORM;
                wheel.beam_defaults->breaking_threshold = 1000000.0f;
                wheel.beam_defaults->visual_beam_diameter = 0.0f;
                wheel.beam_defaults->plastic_deform_coef =
                    BEAM_PLASTIC_COEF_DEFAULT;
                wheel.beam_defaults->
                    _is_plastic_deform_coef_user_defined = true;
                wheel.beam_defaults->_is_user_defined = true;
                wheel.beam_defaults->_enable_advanced_deformation = true;
                module.wheels2.push_back(std::move(wheel));
            }
        }

        if (!preflight.triangle_source_indices.empty())
        {
            RigDef::Submesh submesh;
            submesh.backmesh = false;
            submesh.cab_triangles.reserve(
                preflight.triangle_source_indices.size());
            for (std::size_t plan_index = 0U;
                 plan_index <
                    preflight.triangle_source_indices.size();
                 ++plan_index)
            {
                const std::size_t source_index =
                    preflight.triangle_source_indices[plan_index];
                const JBeamStructuralTriangle& source =
                    ir.triangles[source_index];
                RigDef::Cab cab;
                // The proper (+1 determinant) axis permutation preserves the
                // authored winding. BeamNG NORMALTYPE is a two-sided
                // node-to-triangle collision surface and maps to RoR's
                // contact cab. NONCOLLIDABLE remains visual/anti-clip-only at
                // this boundary and therefore receives no contact option.
                cab.nodes[0] =
                    AdapterDetail::NamedReference(
                        source.node_a, source.provenance);
                cab.nodes[1] =
                    AdapterDetail::NamedReference(
                        source.node_b, source.provenance);
                cab.nodes[2] =
                    AdapterDetail::NamedReference(
                        source.node_c, source.provenance);
                cab.options =
                    source.triangle_type ==
                            JBeamStructuralTriangleType::NORMALTYPE
                        ? RigDef::Cab::OPTION_c_CONTACT
                        : 0U;
                submesh.cab_triangles.push_back(cab);
            }
            module.submeshes.push_back(submesh);
        }

        RigDef::Camera camera;
        camera.center_node =
            AdapterDetail::NamedReference(
                ir.ref_frame.reference,
                ir.ref_frame.provenance);
        camera.back_node =
            AdapterDetail::NamedReference(
                ir.ref_frame.back,
                ir.ref_frame.provenance);
        camera.left_node =
            AdapterDetail::NamedReference(
                ir.ref_frame.left,
                ir.ref_frame.provenance);
        module.cameras.push_back(camera);

        RigDef::Globals globals;
        globals.dry_mass = 0.0f;
        globals.cargo_mass = 0.0f;
        globals.material_name.clear();
        module.globals.push_back(globals);

        return document;
    }
    catch (const std::bad_alloc&)
    {
        AdapterDetail::PushDiagnostic(
            diagnostics,
            JBeamToRigDefDiagnosticCode::ALLOCATION_FAILURE,
            ir.ref_frame.provenance,
            "RigDef document allocation failed");
    }
    catch (const std::exception&)
    {
        AdapterDetail::PushDiagnostic(
            diagnostics,
            JBeamToRigDefDiagnosticCode::RIGDEF_CONSTRUCTION_FAILURE,
            ir.ref_frame.provenance,
            "RigDef construction failed");
    }
    catch (...)
    {
        AdapterDetail::PushDiagnostic(
            diagnostics,
            JBeamToRigDefDiagnosticCode::RIGDEF_CONSTRUCTION_FAILURE,
            ir.ref_frame.provenance,
            "RigDef construction failed with an unknown exception");
    }
    return RigDef::DocumentPtr();
}

} // namespace

RigDef::DocumentPtr ConvertJBeamToRigDef(
    const JBeamStructuralIR& ir,
    const std::string& document_name,
    std::vector<JBeamToRigDefDiagnostic>& diagnostics,
    const JBeamToRigDefLimits& limits)
{
    return ConvertJBeamToRigDefImpl(
        ir, NULL, NULL, document_name, diagnostics, limits);
}

RigDef::DocumentPtr ConvertJBeamToRigDefWithHydroRuntimePlans(
    const JBeamStructuralIR& ir,
    const JBeamHydroRuntimePlanSet& hydro_plans,
    const std::string& document_name,
    std::vector<JBeamToRigDefDiagnostic>& diagnostics,
    const JBeamToRigDefLimits& limits)
{
    return ConvertJBeamToRigDefImpl(
        ir, &hydro_plans, NULL, document_name, diagnostics, limits);
}

RigDef::DocumentPtr ConvertJBeamToRigDefWithRuntimePlans(
    const JBeamStructuralIR& ir,
    const JBeamHydroRuntimePlanSet& hydro_plans,
    const JBeamWheel2ApproximationPlanSet& wheel_plans,
    const std::string& document_name,
    std::vector<JBeamToRigDefDiagnostic>& diagnostics,
    const JBeamToRigDefLimits& limits)
{
    return ConvertJBeamToRigDefImpl(
        ir,
        &hydro_plans,
        &wheel_plans,
        document_name,
        diagnostics,
        limits);
}

#endif

const char* ToString(JBeamToRigDefDiagnosticCode code)
{
    switch (code)
    {
    case JBeamToRigDefDiagnosticCode::INVALID_DOCUMENT_NAME:
        return "invalid-document-name";
    case JBeamToRigDefDiagnosticCode::INVALID_STRUCTURAL_IR:
        return "invalid-structural-ir";
    case JBeamToRigDefDiagnosticCode::MISSING_REF_FRAME:
        return "missing-ref-frame";
    case JBeamToRigDefDiagnosticCode::INPUT_RECORD_LIMIT:
        return "input-record-limit";
    case JBeamToRigDefDiagnosticCode::WORK_LIMIT:
        return "work-limit";
    case JBeamToRigDefDiagnosticCode::DIAGNOSTIC_LIMIT:
        return "diagnostic-limit";
    case JBeamToRigDefDiagnosticCode::DIAGNOSTIC_DETAIL_LIMIT:
        return "diagnostic-detail-limit";
    case JBeamToRigDefDiagnosticCode::NODE_LIMIT:
        return "node-limit";
    case JBeamToRigDefDiagnosticCode::BEAM_LIMIT:
        return "beam-limit";
    case JBeamToRigDefDiagnosticCode::MISSING_STRUCTURAL_BEAM:
        return "missing-structural-beam";
    case JBeamToRigDefDiagnosticCode::CAB_LIMIT:
        return "cab-limit";
    case JBeamToRigDefDiagnosticCode::INVALID_NODE_ID:
        return "invalid-node-id";
    case JBeamToRigDefDiagnosticCode::DUPLICATE_NODE_ID:
        return "duplicate-node-id";
    case JBeamToRigDefDiagnosticCode::INVALID_ENTITY_STATE:
        return "invalid-entity-state";
    case JBeamToRigDefDiagnosticCode::INVALID_NODE_REFERENCE:
        return "invalid-node-reference";
    case JBeamToRigDefDiagnosticCode::DUPLICATE_VERTEX:
        return "duplicate-vertex";
    case JBeamToRigDefDiagnosticCode::SOURCE_LINE_LIMIT:
        return "source-line-limit";
    case JBeamToRigDefDiagnosticCode::NON_FINITE_VALUE:
        return "non-finite-value";
    case JBeamToRigDefDiagnosticCode::FLOAT_NARROWING:
        return "float-narrowing";
    case JBeamToRigDefDiagnosticCode::INVALID_NODE_MASS:
        return "invalid-node-mass";
    case JBeamToRigDefDiagnosticCode::UNSUPPORTED_NODE_COLLISION_MODE:
        return "unsupported-node-collision-mode";
    case JBeamToRigDefDiagnosticCode::TOTAL_MASS_OVERFLOW:
        return "total-mass-overflow";
    case JBeamToRigDefDiagnosticCode::INVALID_CENTER_OF_MASS:
        return "invalid-center-of-mass";
    case JBeamToRigDefDiagnosticCode::INVALID_BOUNDS:
        return "invalid-bounds";
    case JBeamToRigDefDiagnosticCode::DEGENERATE_BEAM:
        return "degenerate-beam";
    case JBeamToRigDefDiagnosticCode::INVALID_BEAM_PARAMETER:
        return "invalid-beam-parameter";
    case JBeamToRigDefDiagnosticCode::BEAM_LENGTH_OVERFLOW:
        return "beam-length-overflow";
    case JBeamToRigDefDiagnosticCode::DEGENERATE_TRIANGLE:
        return "degenerate-triangle";
    case JBeamToRigDefDiagnosticCode::INVALID_REF_FRAME:
        return "invalid-ref-frame";
    case JBeamToRigDefDiagnosticCode::MISALIGNED_REF_FRAME:
        return "misaligned-ref-frame";
    case JBeamToRigDefDiagnosticCode::MISALIGNED_REF_CORNERS:
        return "misaligned-ref-corners";
    case JBeamToRigDefDiagnosticCode::INVALID_HYDRO_RUNTIME_PLAN:
        return "invalid-hydro-runtime-plan";
    case JBeamToRigDefDiagnosticCode::HYDRO_RUNTIME_LIMIT:
        return "hydro-runtime-limit";
    case JBeamToRigDefDiagnosticCode::INVALID_WHEEL2_APPROXIMATION_PLAN:
        return "invalid-wheel2-approximation-plan";
    case JBeamToRigDefDiagnosticCode::WHEEL2_RUNTIME_LIMIT:
        return "wheel2-runtime-limit";
    case JBeamToRigDefDiagnosticCode::ALLOCATION_FAILURE:
        return "allocation-failure";
    case JBeamToRigDefDiagnosticCode::RIGDEF_CONSTRUCTION_FAILURE:
        return "rigdef-construction-failure";
    }
    return "unknown";
}

const char* ToString(JBeamToRigDefEntityKind kind)
{
    switch (kind)
    {
    case JBeamToRigDefEntityKind::DOCUMENT:
        return "document";
    case JBeamToRigDefEntityKind::STRUCTURAL_IR:
        return "structural-ir";
    case JBeamToRigDefEntityKind::NODE:
        return "node";
    case JBeamToRigDefEntityKind::BEAM:
        return "beam";
    case JBeamToRigDefEntityKind::HYDRO:
        return "hydro";
    case JBeamToRigDefEntityKind::WHEEL:
        return "wheel";
    case JBeamToRigDefEntityKind::TRIANGLE:
        return "triangle";
    case JBeamToRigDefEntityKind::REF_FRAME:
        return "ref-frame";
    }
    return "unknown";
}

} // namespace BeamNG
} // namespace RoR
