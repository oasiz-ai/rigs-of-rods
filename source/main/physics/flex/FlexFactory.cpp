/*
    This source file is part of Rigs of Rods

    Copyright 2015-2020 Petr Ohlidal

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
/// @author Petr Ohlidal
/// @date   05/2015

#include "FlexFactory.h"

#include "Application.h"
#include "Actor.h"
#include "CacheSystem.h"
#include "FlexBody.h"
#include "FlexMeshWheel.h"
#include "GfxScene.h"
#include "PlatformUtils.h"
#include "RigDef_File.h"
#include "ActorSpawner.h"

#include <OgreMeshManager.h>
#include <OgreMeshSerializer.h>
#include <OgreResourceGroupManager.h>
#include <OgreSceneManager.h>
#include <MeshLodGenerator/OgreMeshLodGenerator.h>

#include <algorithm>
#include <cstring>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

namespace {

bool ValidateShadowedFlexbodyVertexData(
    const Ogre::VertexData* vertex_data) noexcept
{
    try
    {
        if (vertex_data == nullptr || vertex_data->vertexStart != 0U ||
            vertex_data->vertexCount == 0U ||
            vertex_data->vertexCount >
                static_cast<std::size_t>(
                    (std::numeric_limits<int>::max)()) ||
            vertex_data->vertexDeclaration == nullptr ||
            vertex_data->vertexBufferBinding == nullptr)
        {
            return false;
        }
        const auto& bindings =
            vertex_data->vertexBufferBinding->getBindings();
        if (bindings.empty())
            return false;
        for (const auto& binding : bindings)
        {
            const Ogre::HardwareVertexBufferSharedPtr& buffer =
                binding.second;
            // Ogre 14 reorganiseBuffers() locks every bound source stream,
            // including streams outside the target declaration. Require a CPU
            // shadow for all of them so Metal never sees a private read lock.
            if (buffer.isNull() || !buffer->hasShadowBuffer() ||
                buffer->getVertexSize() == 0U ||
                vertex_data->vertexCount > buffer->getNumVertices())
            {
                return false;
            }
        }
        return true;
    }
    catch (...)
    {
        return false;
    }
}

template <typename T>
bool AppendShadowedFlexbodyVertexElement(
    const Ogre::VertexData* vertex_data,
    Ogre::VertexElementSemantic semantic,
    Ogre::VertexElementType expected_type,
    std::vector<T>& destination)
{
    const Ogre::VertexElement* const element =
        vertex_data->vertexDeclaration->findElementBySemantic(semantic, 0U);
    if (element == nullptr || element->getType() != expected_type ||
        element->getSize() != sizeof(T))
    {
        return false;
    }
    const Ogre::HardwareVertexBufferSharedPtr buffer =
        vertex_data->vertexBufferBinding->getBuffer(element->getSource());
    if (buffer.isNull() || !buffer->hasShadowBuffer() ||
        vertex_data->vertexCount > buffer->getNumVertices())
    {
        return false;
    }

    const std::size_t stride = buffer->getVertexSize();
    const std::size_t element_offset = element->getOffset();
    const std::size_t element_size = element->getSize();
    if (stride == 0U || element_offset > stride ||
        element_size > stride - element_offset ||
        element_offset >
            (std::numeric_limits<std::size_t>::max)() - element_size)
    {
        return false;
    }
    const std::size_t trailing_vertices = vertex_data->vertexCount - 1U;
    const std::size_t tail_capacity =
        (std::numeric_limits<std::size_t>::max)() -
        element_offset - element_size;
    if (trailing_vertices > tail_capacity / stride)
        return false;
    const std::size_t byte_count =
        trailing_vertices * stride + element_size;
    if (element_offset > buffer->getSizeInBytes() ||
        byte_count > buffer->getSizeInBytes() - element_offset ||
        vertex_data->vertexCount >
            destination.max_size() - destination.size())
    {
        return false;
    }

    const std::size_t old_size = destination.size();
    destination.resize(old_size + vertex_data->vertexCount);
    Ogre::HardwareBufferLockGuard shadow_lock(
        buffer, element_offset, byte_count,
        Ogre::HardwareBuffer::HBL_READ_ONLY);
    if (shadow_lock.pData == nullptr)
        return false;
    const auto* const bytes =
        static_cast<const unsigned char*>(shadow_lock.pData);
    for (std::size_t vertex = 0U;
         vertex < vertex_data->vertexCount; ++vertex)
    {
        std::memcpy(
            &destination[old_size + vertex], bytes + vertex * stride,
            sizeof(T));
    }
    return true;
}

bool CaptureShadowedFlexbodyVertexStreams(
    const Ogre::MeshPtr& mesh,
    RoR::FlexBodyInitialVertexStreams& output) noexcept
{
    try
    {
        if (mesh.isNull() || mesh->getNumSubMeshes() == 0U)
            return false;
        RoR::FlexBodyInitialVertexStreams candidate;
        candidate.has_complete_texcoords0 = true;
        const auto append_vertex_data = [&candidate](
            const Ogre::VertexData* vertex_data) -> bool
        {
            if (!ValidateShadowedFlexbodyVertexData(vertex_data) ||
                !AppendShadowedFlexbodyVertexElement(
                    vertex_data, Ogre::VES_POSITION, Ogre::VET_FLOAT3,
                    candidate.positions) ||
                !AppendShadowedFlexbodyVertexElement(
                    vertex_data, Ogre::VES_NORMAL, Ogre::VET_FLOAT3,
                    candidate.normals))
            {
                return false;
            }
            const Ogre::VertexElement* const texcoord =
                vertex_data->vertexDeclaration->findElementBySemantic(
                    Ogre::VES_TEXTURE_COORDINATES, 0U);
            if (texcoord == nullptr)
            {
                candidate.has_complete_texcoords0 = false;
                return true;
            }
            return AppendShadowedFlexbodyVertexElement(
                vertex_data, Ogre::VES_TEXTURE_COORDINATES,
                Ogre::VET_FLOAT2, candidate.texcoords0);
        };

        if (mesh->sharedVertexData != nullptr &&
            !append_vertex_data(mesh->sharedVertexData))
        {
            return false;
        }
        std::size_t nonshared_section_count = 0U;
        for (std::size_t section_index = 0U;
             section_index < mesh->getNumSubMeshes(); ++section_index)
        {
            const Ogre::SubMesh* const submesh =
                mesh->getSubMesh(section_index);
            if (submesh == nullptr)
                return false;
            if (submesh->useSharedVertices)
            {
                if (mesh->sharedVertexData == nullptr)
                    return false;
                continue;
            }
            ++nonshared_section_count;
            if (nonshared_section_count > 16U ||
                !append_vertex_data(submesh->vertexData))
            {
                return false;
            }
        }
        if (candidate.positions.empty() ||
            candidate.positions.size() != candidate.normals.size())
        {
            return false;
        }
        if (candidate.has_complete_texcoords0)
        {
            if (candidate.texcoords0.size() != candidate.positions.size())
                return false;
        }
        else
        {
            candidate.texcoords0.clear();
        }
        output = std::move(candidate);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

void RemoveFlexbodyMeshResourceNoexcept(const Ogre::MeshPtr& mesh) noexcept
{
    if (mesh.isNull())
        return;
    try
    {
        Ogre::MeshManager::getSingleton().remove(mesh);
    }
    catch (...)
    {
        // Best-effort rollback only; preserve the original construction error.
    }
}

Ogre::MeshPtr ImportShadowedFlexbodyMesh(
    const std::string& source_name,
    const std::string& unique_name,
    const std::string& resource_group_name)
{
    Ogre::MeshManager& mesh_manager = Ogre::MeshManager::getSingleton();
    Ogre::MeshPtr mesh = mesh_manager.createManual(
        unique_name, resource_group_name);
    try
    {
#if OGRE_VERSION_MAJOR >= 14
        constexpr Ogre::HardwareBuffer::Usage shadowed_static_usage =
            Ogre::HBU_GPU_ONLY;
#else
        constexpr Ogre::HardwareBuffer::Usage shadowed_static_usage =
            Ogre::HardwareBuffer::HBU_STATIC_WRITE_ONLY;
#endif
        mesh->setVertexBufferPolicy(shadowed_static_usage, true);
        mesh->setIndexBufferPolicy(shadowed_static_usage, true);
        Ogre::DataStreamPtr source =
            Ogre::ResourceGroupManager::getSingleton().openResource(
                source_name, resource_group_name);
        Ogre::MeshSerializer serializer;
        serializer.setListener(mesh_manager.getListener());
        serializer.importMesh(source, mesh.get());
        // Manual resources are populated above; load() performs the normal
        // Mesh post-load lifecycle and marks the resource ready for Entity.
        mesh->load();
        mesh->touch();
        return mesh;
    }
    catch (...)
    {
        RemoveFlexbodyMeshResourceNoexcept(mesh);
        throw;
    }
}

class OgreFlexbodyTopologySource final
    : public RoR::IFlexMeshTopologySource
{
public:
    explicit OgreFlexbodyTopologySource(const Ogre::MeshPtr& mesh)
        : m_mesh(mesh)
    {
    }

    std::size_t SectionCount() const noexcept override
    {
        return m_mesh.isNull() ? 0U : m_mesh->getNumSubMeshes();
    }

    bool DescribeSection(
        std::size_t section_index,
        RoR::FlexMeshTopologySourceSection& description) const override
    {
        if (m_mesh.isNull() || section_index >= m_mesh->getNumSubMeshes())
            return false;
        const Ogre::SubMesh* const submesh =
            m_mesh->getSubMesh(section_index);
        if (submesh == nullptr || submesh->indexData == nullptr ||
            submesh->indexData->indexBuffer.isNull())
        {
            return false;
        }
        const Ogre::VertexData* const vertices = submesh->useSharedVertices
            ? m_mesh->sharedVertexData
            : submesh->vertexData;
        if (vertices == nullptr)
            return false;

        const Ogre::HardwareIndexBufferSharedPtr& buffer =
            submesh->indexData->indexBuffer;
        RoR::FlexMeshTopologySourceSection candidate;
        if (buffer->getType() == Ogre::HardwareIndexBuffer::IT_16BIT)
        {
            candidate.index_format =
                RoR::FlexMeshTopologySection::IndexFormat::UINT16;
        }
        else if (buffer->getType() == Ogre::HardwareIndexBuffer::IT_32BIT)
        {
            candidate.index_format =
                RoR::FlexMeshTopologySection::IndexFormat::UINT32;
        }
        else
        {
            return false;
        }
        candidate.vertex_count = vertices->vertexCount;
        candidate.buffer_index_count = buffer->getNumIndexes();
        candidate.index_start = submesh->indexData->indexStart;
        candidate.index_count = submesh->indexData->indexCount;
        candidate.has_cpu_shadow = buffer->hasShadowBuffer();
        description = candidate;
        return true;
    }

    bool ReadShadowedIndices(
        std::size_t section_index,
        std::size_t index_start,
        std::size_t index_count,
        RoR::FlexMeshTopologySection::IndexFormat index_format,
        std::vector<std::uint32_t>& indices) const override
    {
        if (m_mesh.isNull() || section_index >= m_mesh->getNumSubMeshes())
            return false;
        const Ogre::SubMesh* const submesh =
            m_mesh->getSubMesh(section_index);
        if (submesh == nullptr || submesh->indexData == nullptr ||
            submesh->indexData->indexBuffer.isNull())
        {
            return false;
        }
        const Ogre::HardwareIndexBufferSharedPtr& buffer =
            submesh->indexData->indexBuffer;
        if (!buffer->hasShadowBuffer() ||
            index_start > buffer->getNumIndexes() ||
            index_count > buffer->getNumIndexes() - index_start)
        {
            return false;
        }

        const bool format_matches =
            (index_format ==
                 RoR::FlexMeshTopologySection::IndexFormat::UINT16 &&
             buffer->getType() == Ogre::HardwareIndexBuffer::IT_16BIT) ||
            (index_format ==
                 RoR::FlexMeshTopologySection::IndexFormat::UINT32 &&
             buffer->getType() == Ogre::HardwareIndexBuffer::IT_32BIT);
        if (!format_matches)
            return false;
        const std::size_t index_size =
            index_format ==
                    RoR::FlexMeshTopologySection::IndexFormat::UINT16
            ? sizeof(std::uint16_t)
            : sizeof(std::uint32_t);
        const std::size_t max_index_count =
            (std::numeric_limits<std::size_t>::max)() / index_size;
        if (buffer->getNumIndexes() > max_index_count ||
            index_start > max_index_count ||
            index_count > max_index_count - index_start)
        {
            return false;
        }

        const std::size_t byte_offset = index_start * index_size;
        const std::size_t byte_count = index_count * index_size;
        if (byte_offset > buffer->getSizeInBytes() ||
            byte_count > buffer->getSizeInBytes() - byte_offset)
        {
            return false;
        }

        std::vector<std::uint32_t> candidate(index_count);
        // Do not call the readData API here. Ogre 14's Metal implementation reads
        // its native delegate even when a CPU shadow exists. The public
        // HardwareBuffer::lock() path is the API that routes HBL_READ_ONLY to
        // mShadowBuffer, and the guard makes the unlock exception-safe.
        Ogre::HardwareBufferLockGuard shadow_lock(
            buffer, byte_offset, byte_count,
            Ogre::HardwareBuffer::HBL_READ_ONLY);
        if (shadow_lock.pData == nullptr)
            return false;
        if (index_format ==
            RoR::FlexMeshTopologySection::IndexFormat::UINT16)
        {
            std::vector<std::uint16_t> source(index_count);
            std::memcpy(source.data(), shadow_lock.pData, byte_count);
            std::copy(source.begin(), source.end(), candidate.begin());
        }
        else
        {
            std::memcpy(candidate.data(), shadow_lock.pData, byte_count);
        }
        indices = std::move(candidate);
        return true;
    }

private:
    Ogre::MeshPtr m_mesh;
};

bool InstallShadowedFlexbodyIndexBuffers(
    const Ogre::MeshPtr& mesh,
    const std::vector<RoR::FlexMeshTopologySection>& topology) noexcept
{
    struct PreparedIndexRange
    {
        Ogre::IndexData* native_range = nullptr;
        Ogre::HardwareIndexBufferSharedPtr buffer;
        std::size_t index_count = 0U;
    };

    if (mesh.isNull() || topology.size() != mesh->getNumSubMeshes())
        return false;
    return RoR::InstallFlexMeshCpuTopologyTransaction<PreparedIndexRange>(
        topology.size(),
        [&mesh, &topology](
            std::size_t section_index,
            PreparedIndexRange& prepared) -> bool
        {
            Ogre::SubMesh* const submesh = mesh->getSubMesh(section_index);
            if (submesh == nullptr || submesh->indexData == nullptr ||
                submesh->indexData->indexBuffer.isNull() ||
                submesh->indexData->indexCount !=
                    topology[section_index].indices.size() ||
                topology[section_index].revision == 0U ||
                topology[section_index].indices.empty() ||
                topology[section_index].indices.size() % 3U != 0U)
            {
                return false;
            }
            const Ogre::VertexData* const vertices =
                submesh->useSharedVertices
                ? mesh->sharedVertexData
                : submesh->vertexData;
            if (vertices == nullptr || vertices->vertexCount !=
                    topology[section_index].vertex_count)
            {
                return false;
            }
            const Ogre::HardwareIndexBufferSharedPtr& source =
                submesh->indexData->indexBuffer;
            const Ogre::HardwareIndexBuffer::IndexType native_format =
                topology[section_index].index_format ==
                        RoR::FlexMeshTopologySection::IndexFormat::UINT16
                ? Ogre::HardwareIndexBuffer::IT_16BIT
                : Ogre::HardwareIndexBuffer::IT_32BIT;
            if (source->getType() != native_format)
                return false;
            const std::size_t index_size =
                native_format == Ogre::HardwareIndexBuffer::IT_16BIT
                ? sizeof(std::uint16_t)
                : sizeof(std::uint32_t);
            if (topology[section_index].indices.size() >
                (std::numeric_limits<std::size_t>::max)() / index_size)
            {
                return false;
            }
            const std::size_t max_index_count =
                (std::numeric_limits<std::size_t>::max)() / index_size;
            const std::size_t source_index_count = source->getNumIndexes();
            const std::size_t source_index_start =
                submesh->indexData->indexStart;
            const std::size_t source_draw_count =
                submesh->indexData->indexCount;
            if (source_index_count > max_index_count ||
                source_index_start > source_index_count ||
                source_draw_count >
                    source_index_count - source_index_start ||
                source->getSizeInBytes() <
                    source_index_count * index_size)
            {
                return false;
            }

            Ogre::HardwareIndexBufferSharedPtr buffer =
                Ogre::HardwareBufferManager::getSingleton().createIndexBuffer(
                    native_format, topology[section_index].indices.size(),
                    source->getUsage(), true);
            const std::size_t byte_count =
                topology[section_index].indices.size() * index_size;
            if (buffer.isNull() || !buffer->hasShadowBuffer() ||
                buffer->getNumIndexes() !=
                    topology[section_index].indices.size() ||
                buffer->getSizeInBytes() < byte_count)
            {
                return false;
            }
            if (native_format == Ogre::HardwareIndexBuffer::IT_16BIT)
            {
                std::vector<std::uint16_t> indices;
                indices.reserve(topology[section_index].indices.size());
                for (std::uint32_t index : topology[section_index].indices)
                {
                    if (index >
                        (std::numeric_limits<std::uint16_t>::max)())
                    {
                        return false;
                    }
                    indices.push_back(static_cast<std::uint16_t>(index));
                }
                buffer->writeData(
                    0U, byte_count,
                    indices.data(), true);
            }
            else
            {
                buffer->writeData(
                    0U, byte_count,
                    topology[section_index].indices.data(), true);
            }
            prepared.native_range = submesh->indexData;
            prepared.buffer = std::move(buffer);
            prepared.index_count = topology[section_index].indices.size();
            return true;
        },
        [](std::size_t, PreparedIndexRange& prepared) noexcept
        {
            // shared_ptr::swap and scalar assignments are non-throwing. No
            // dirty-state callback follows this point, so a completed prepare
            // phase cannot become a partial native rebind.
            prepared.native_range->indexBuffer.swap(prepared.buffer);
            prepared.native_range->indexStart = 0U;
            prepared.native_range->indexCount = prepared.index_count;
        });
}

} // namespace

//#define FLEXFACTORY_DEBUG_LOGGING

#ifdef FLEXFACTORY_DEBUG_LOGGING
#   include "RoRPrerequisites.h"
#   define FLEX_DEBUG_LOG(TEXT) LOG("FlexFactory | " TEXT)
#else
#   define FLEX_DEBUG_LOG(TEXT)    
#endif // FLEXFACTORY_DEBUG_LOGGING

using namespace RoR;

// Static
const char * FlexBodyFileIO::SIGNATURE = "RoR FlexBody";

FlexFactory::FlexFactory(ActorSpawner* rig_spawner):
    m_rig_spawner(rig_spawner),
    m_is_flexbody_cache_loaded(false),
    m_is_flexbody_cache_enabled(App::gfx_flexbody_cache->getBool()),
    m_flexbody_cache_next_index(0)
{
}

FlexBody* FlexFactory::CreateFlexBody(
    FlexbodyID_t flexbody_id,
    const NodeNum_t ref_node, 
    const NodeNum_t x_node, 
    const NodeNum_t y_node, 
    Ogre::Vector3 offset,
    Ogre::Vector3 rotation, 
    std::vector<unsigned int> & node_indices,
    std::vector<ForvertTempData>& forvert_data,
    const std::string& mesh_name,
    const std::string& resource_group_name)
{
    const std::string mesh_unique_name = m_rig_spawner->ComposeName(
        fmt::format("{}_FlexBody", mesh_name).c_str(), flexbody_id);
    Ogre::MeshPtr mesh;
    Ogre::Entity* entity = nullptr;
    bool entity_owned_by_flexbody = false;
    try
    {
        // Every flexbody needs an actor-local mutable Mesh. Import that unique
        // resource with shadow policy established before deserialization.
        // This is safe for both first use and an already-cached unshadowed
        // source: no Mesh::clone copy can bypass the CPU shadow via a native
        // delegate, and no shared resource is unloaded or mutated.
        mesh = ImportShadowedFlexbodyMesh(
            mesh_name, mesh_unique_name, resource_group_name);

        FlexBodyInitialVertexStreams initial_vertex_streams;
        if (!CaptureShadowedFlexbodyVertexStreams(
                mesh, initial_vertex_streams))
        {
            LOG("FlexFactory: flexbody has an unsafe or invalid vertex "
                "stream after shadowed import: " + mesh_name);
            OGRE_EXCEPT(
                Ogre::Exception::ERR_INVALID_STATE,
                "Flexbody position, normal, and present UV streams require "
                "valid CPU shadows",
                "FlexFactory::CreateFlexBody");
        }
        std::vector<FlexMeshTopologySection> cpu_topology;
        const OgreFlexbodyTopologySource topology_source(mesh);
        const bool captured_cpu_topology =
            CaptureFlexMeshCpuTopology(topology_source, cpu_topology);
        if (!captured_cpu_topology ||
            !InstallShadowedFlexbodyIndexBuffers(mesh, cpu_topology))
        {
            // Keep the legacy visual usable, but fail modern capture closed.
            // In particular, never read an unshadowed private buffer.
            cpu_topology.clear();
            LOG("FlexFactory: imported flexbody has no safe CPU index "
                "topology: " + mesh_name);
        }

        const std::string flexbody_name =
            m_rig_spawner->ComposeName("Flexbody", flexbody_id);
        entity = App::GetGfxScene()->GetSceneManager()->createEntity(
                flexbody_name, mesh_unique_name, resource_group_name);
        m_rig_spawner->SetupNewEntity(
            entity, Ogre::ColourValue(0.5, 0.5, 1));

        FLEX_DEBUG_LOG(__FUNCTION__);
        FlexBodyCacheData* from_cache = nullptr;
        if (m_is_flexbody_cache_loaded)
        {
            FLEX_DEBUG_LOG(__FUNCTION__ " >> Get entry from cache ");
            from_cache = m_flexbody_cache.GetLoadedItem(
                m_flexbody_cache_next_index);
            if (from_cache != nullptr)
            {
                ++m_flexbody_cache_next_index;
            }
            else
            {
                // A short/corrupt cache must never index past its loaded
                // records or shift later flexbodies onto the wrong entry.
                m_is_flexbody_cache_loaded = false;
                m_is_flexbody_cache_safe_to_save = false;
                LOG("FlexFactory: flexbody cache exhausted; rebuilding "
                    "remaining flexbodies from source meshes");
            }
        }

        Ogre::Quaternion rot = Ogre::Quaternion(
            Ogre::Degree(rotation.z), Ogre::Vector3::UNIT_Z);
        rot = rot * Ogre::Quaternion(
            Ogre::Degree(rotation.y), Ogre::Vector3::UNIT_Y);
        rot = rot * Ogre::Quaternion(
            Ogre::Degree(rotation.x), Ogre::Vector3::UNIT_X);

        std::unique_ptr<FlexBody> new_flexbody(new FlexBody(
            from_cache,
            m_rig_spawner->GetActor()->GetGfxActor(),
            entity,
            ref_node,
            x_node,
            y_node,
            offset,
            rot,
            node_indices,
            forvert_data,
            std::move(initial_vertex_streams),
            std::move(cpu_topology)));
        entity_owned_by_flexbody = true;

        // Retain ownership across every operation that can still throw. A
        // failed metadata allocation or cache-vector growth destroys the
        // FlexBody (and its Entity/unique Mesh) instead of leaking it or
        // publishing a dangling cache pointer.
        new_flexbody->m_id = flexbody_id;
        new_flexbody->m_orig_mesh_name = mesh_name;
        if (m_is_flexbody_cache_enabled)
        {
            m_flexbody_cache.AddItemToSave(new_flexbody.get());
        }
        return new_flexbody.release();
    }
    catch (...)
    {
        // The v1 cache is positional and has no mesh identity. Any failed
        // creation makes its remaining cursor ambiguous, so fail over to
        // source construction instead of applying an entry to the next mesh.
        // Never persist that partial positional sequence for the next launch.
        m_is_flexbody_cache_loaded = false;
        m_is_flexbody_cache_safe_to_save = false;
        // Before a complete FlexBody exists, the factory still owns the
        // Entity. Destroy it before removing the unique Mesh registration.
        if (entity != nullptr && !entity_owned_by_flexbody)
        {
            try
            {
                App::GetGfxScene()->GetSceneManager()->destroyEntity(entity);
            }
            catch (...)
            {
                // Best effort only; preserve the construction exception.
            }
        }
        RemoveFlexbodyMeshResourceNoexcept(mesh);
        throw;
    }
}

FlexMeshWheel* FlexFactory::CreateFlexMeshWheel(
    unsigned int wheel_index,
    int axis_node_1_index,
    int axis_node_2_index,
    int nstart,
    int nrays,
    float rim_radius,
    bool rim_reverse,
    std::string const & rim_mesh_name,
    std::string const & rim_mesh_rg,
    std::string const & tire_material_name,
    std::string const & tire_material_rg)
{
    const ActorPtr& actor = m_rig_spawner->GetActor();

    // Load+instantiate static mesh for rim (may be located in addonpart ZIP-bundle!)
    const std::string rim_entity_name = m_rig_spawner->ComposeName("rim @ *wheel*", wheel_index);
    Ogre::Entity* rim_prop_entity = App::GetGfxScene()->GetSceneManager()->createEntity(rim_entity_name, rim_mesh_name, rim_mesh_rg);
    m_rig_spawner->SetupNewEntity(rim_prop_entity, Ogre::ColourValue(0, 0.5, 0.8));

    // Create dynamic mesh for tire (always located in the actor resource group)
    const std::string tire_mesh_name = m_rig_spawner->ComposeName("tire @ *wheel*", wheel_index);
    FlexMeshWheel* flex_mesh_wheel = new FlexMeshWheel(
        rim_prop_entity,
        m_rig_spawner->m_wheels_parent_scenenode->createChildSceneNode(m_rig_spawner->ComposeName("*wheel*", wheel_index)), // Friend access
        m_rig_spawner->GetActor()->GetGfxActor(), axis_node_1_index, axis_node_2_index, nstart, nrays,
        tire_mesh_name, actor->GetGfxActor()->GetResourceGroup(),
        tire_material_name, tire_material_rg, rim_radius, rim_reverse);

    // Instantiate the dynamic tire mesh (always located in the actor resource group)
    const std::string tire_instance_name = m_rig_spawner->ComposeName("tire entity @ *wheel*", wheel_index);
    Ogre::Entity *tire_entity = App::GetGfxScene()->GetSceneManager()->createEntity(
        tire_instance_name, tire_mesh_name, actor->GetGfxActor()->GetResourceGroup());
    m_rig_spawner->SetupNewEntity(tire_entity, Ogre::ColourValue(0, 0.5, 0.8));
    flex_mesh_wheel->m_tire_entity = tire_entity; // Friend access.

    return flex_mesh_wheel;
}

void FlexBodyFileIO::WriteToFile(void* source, size_t length)
{
    size_t num_written = fwrite(source, length, 1, m_file);
    if (num_written != 1)
    {
        FLEX_DEBUG_LOG(__FUNCTION__ " >> EXCEPTION!! ");
        throw RESULT_CODE_FWRITE_OUTPUT_INCOMPLETE;
    }
}

void FlexBodyFileIO::ReadFromFile(void* dest, size_t length)
{
    size_t num_written = fread(dest, length, 1, m_file);
    if (num_written != 1)
    {
        FLEX_DEBUG_LOG(__FUNCTION__ " >> EXCEPTION!! ");
        throw RESULT_CODE_FREAD_OUTPUT_INCOMPLETE;
    }
}

void FlexBodyFileIO::WriteSignature()
{
    FLEX_DEBUG_LOG(__FUNCTION__);
    WriteToFile((void*)SIGNATURE, (strlen(SIGNATURE) + 1) * sizeof(char));
}

void FlexBodyFileIO::ReadAndCheckSignature()
{
    FLEX_DEBUG_LOG(__FUNCTION__);
    char signature[25];
    this->ReadFromFile((void*)&signature, (strlen(SIGNATURE) + 1) * sizeof(char));
    if (strcmp(SIGNATURE, signature) != 0)
    {
        throw RESULT_CODE_ERR_SIGNATURE_MISMATCH;
    }
}

void FlexBodyFileIO::WriteMetadata()
{
    FLEX_DEBUG_LOG(__FUNCTION__);
    FlexBodyFileMetadata meta;
    meta.file_format_version = FILE_FORMAT_VERSION;    
    meta.num_flexbodies      = static_cast<int>(m_items_to_save.size());

    this->WriteToFile((void*)&meta, sizeof(FlexBodyFileMetadata));
}

void FlexBodyFileIO::ReadMetadata(FlexBodyFileMetadata* meta)
{
    FLEX_DEBUG_LOG(__FUNCTION__);
    ROR_ASSERT(meta != nullptr);
    this->ReadFromFile((void*)meta, sizeof(FlexBodyFileMetadata));
}

void FlexBodyFileIO::WriteFlexbodyHeader(FlexBody* flexbody)
{
    FLEX_DEBUG_LOG(__FUNCTION__);
    FlexBodyRecordHeader header;
    header.vertex_count            = static_cast<int>(flexbody->m_vertex_count);
    header.node_center             = flexbody->m_node_center            ;
    header.node_x                  = flexbody->m_node_x                 ;
    header.node_y                  = flexbody->m_node_y                 ;
    header.center_offset           = flexbody->m_center_offset          ;
    header.camera_mode             = flexbody->m_camera_mode            ;
    header.shared_buf_num_verts    = flexbody->m_shared_buf_num_verts   ;
    header.num_submesh_vbufs       = flexbody->m_num_submesh_vbufs      ;

    if (flexbody->m_uses_shared_vertex_data) BITMASK_SET_1(header.flags, FlexBodyRecordHeader::USES_SHARED_VERTEX_DATA);
    if (flexbody->m_has_texture            ) BITMASK_SET_1(header.flags, FlexBodyRecordHeader::HAS_TEXTURE);
    if (flexbody->m_has_texture_blend      ) BITMASK_SET_1(header.flags, FlexBodyRecordHeader::HAS_TEXTURE_BLEND);

    this->WriteToFile((void*)&header, sizeof(FlexBodyRecordHeader));
}

void FlexBodyFileIO::ReadFlexbodyHeader(FlexBodyCacheData* data)
{
    FLEX_DEBUG_LOG(__FUNCTION__);
    this->ReadFromFile((void*)&data->header, sizeof(FlexBodyRecordHeader));
}


void FlexBodyFileIO::WriteFlexbodyLocatorList(FlexBody* flexbody)
{
    FLEX_DEBUG_LOG(__FUNCTION__);
    this->WriteToFile((void*)flexbody->m_locators, sizeof(Locator_t) * flexbody->m_vertex_count);
}

void FlexBodyFileIO::ReadFlexbodyLocatorList(FlexBodyCacheData* data)
{
    FLEX_DEBUG_LOG(__FUNCTION__);
    // Alloc. Use <new> - experiment
    data->locators = new Locator_t[data->header.vertex_count];
    // Read
    this->ReadFromFile((void*)data->locators, sizeof(Locator_t) * data->header.vertex_count);
}

void FlexBodyFileIO::WriteFlexbodyNormalsBuffer(FlexBody* flexbody)
{
    FLEX_DEBUG_LOG(__FUNCTION__);
    this->WriteToFile((void*)flexbody->m_src_normals, sizeof(Ogre::Vector3) * flexbody->m_vertex_count);
}

void FlexBodyFileIO::ReadFlexbodyNormalsBuffer(FlexBodyCacheData* data)
{
    FLEX_DEBUG_LOG(__FUNCTION__);
    const int vertex_count = data->header.vertex_count;
    // Alloc. Use malloc() because that's how flexbodies were implemented.
    data->src_normals=(Ogre::Vector3*)malloc(sizeof(Ogre::Vector3) * vertex_count);
    // Read
    this->ReadFromFile((void*)data->src_normals, sizeof(Ogre::Vector3) * vertex_count);
}

void FlexBodyFileIO::WriteFlexbodyPositionsBuffer(FlexBody* flexbody)
{
    FLEX_DEBUG_LOG(__FUNCTION__);
    this->WriteToFile((void*)flexbody->m_dst_pos, sizeof(Ogre::Vector3) * flexbody->m_vertex_count);
}

void FlexBodyFileIO::ReadFlexbodyPositionsBuffer(FlexBodyCacheData* data)
{
    FLEX_DEBUG_LOG(__FUNCTION__);
    const int vertex_count = data->header.vertex_count;
    // Alloc. Use malloc() because that's how flexbodies were implemented.
    data->dst_pos=(Ogre::Vector3*)malloc(sizeof(Ogre::Vector3) * vertex_count);
    // Read
    this->ReadFromFile((void*)data->dst_pos, sizeof(Ogre::Vector3) * vertex_count);
}

void FlexBodyFileIO::WriteFlexbodyColorsBuffer(FlexBody* flexbody)
{
    FLEX_DEBUG_LOG(__FUNCTION__);
    if (flexbody->m_has_texture_blend)
    {
        this->WriteToFile((void*)flexbody->m_src_colors, sizeof(Ogre::ARGB) * flexbody->m_vertex_count);
    }
}

void FlexBodyFileIO::ReadFlexbodyColorsBuffer(FlexBodyCacheData* data)
{
    FLEX_DEBUG_LOG(__FUNCTION__);
    if (BITMASK_IS_0(data->header.flags, FlexBodyRecordHeader::HAS_TEXTURE_BLEND))
    {
        return;
    }
    const int vertex_count = data->header.vertex_count;
    // Alloc. Use malloc() because that's how flexbodies were implemented.
    data->src_colors=(Ogre::ARGB*)malloc(sizeof(Ogre::ARGB) * vertex_count);
    // Read
    this->ReadFromFile((void*)data->src_colors, sizeof(Ogre::ARGB) * vertex_count);
}

void FlexBodyFileIO::OpenFile(const char* fopen_mode)
{
    FLEX_DEBUG_LOG(__FUNCTION__);
    if (m_cache_entry_number == -1)
    {
        throw RESULT_CODE_ERR_CACHE_NUMBER_UNDEFINED;
    }
    char path[500];
    sprintf(path, "%s%cflexbodies_mod_%00d.dat", App::sys_cache_dir->getStr().c_str(), RoR::PATH_SLASH, m_cache_entry_number);
    m_file = fopen(path, fopen_mode);
    if (m_file == nullptr)
    {
        throw RESULT_CODE_ERR_FOPEN_FAILED;
    }
}

FlexBodyFileIO::ResultCode FlexBodyFileIO::SaveFile()
{
    FLEX_DEBUG_LOG(__FUNCTION__);
    if (m_items_to_save.size() == 0)
    {
        FLEX_DEBUG_LOG(__FUNCTION__ " >> No flexbodies to save >> EXIT");
        return RESULT_CODE_OK;
    }
    try
    {
        this->OpenFile("wb");

        this->WriteSignature();
        this->WriteMetadata();

        auto itor = m_items_to_save.begin();
        auto end  = m_items_to_save.end();
        for (; itor != end; ++itor)
        {
            FlexBody* flexbody = *itor;
            this->WriteFlexbodyHeader(flexbody);

            this->WriteFlexbodyLocatorList    (flexbody);
            this->WriteFlexbodyPositionsBuffer(flexbody);
            this->WriteFlexbodyNormalsBuffer  (flexbody);
            this->WriteFlexbodyColorsBuffer   (flexbody);
        }
        this->CloseFile();
        FLEX_DEBUG_LOG(__FUNCTION__ " >> OK ");
        return RESULT_CODE_OK;
    }
    catch (ResultCode result)
    {
        this->CloseFile();
        FLEX_DEBUG_LOG(__FUNCTION__ " >> EXCEPTION!! ");
        return result;
    }
}

FlexBodyFileIO::ResultCode FlexBodyFileIO::LoadFile()
{
    FLEX_DEBUG_LOG(__FUNCTION__);
    try 
    {
        this->OpenFile("rb");
        this->ReadAndCheckSignature();

        FlexBodyFileMetadata meta;
        this->ReadMetadata(&meta);
        m_fileformat_version = meta.file_format_version;
        if (m_fileformat_version != FILE_FORMAT_VERSION)
        {
            throw RESULT_CODE_ERR_VERSION_MISMATCH;
        }
        m_loaded_items.resize(meta.num_flexbodies);

        for (unsigned int i = 0; i < meta.num_flexbodies; ++i)
        {
            FlexBodyCacheData* data = & m_loaded_items[i];
            this->ReadFlexbodyHeader(data);
            if (BITMASK_IS_0(data->header.flags, FlexBodyRecordHeader::IS_FAULTY))
            {
                this->ReadFlexbodyLocatorList    (data);
                this->ReadFlexbodyPositionsBuffer(data);
                this->ReadFlexbodyNormalsBuffer  (data);
                this->ReadFlexbodyColorsBuffer   (data);
            }
        }

        this->CloseFile();
        FLEX_DEBUG_LOG(__FUNCTION__ " >> OK ");
        return RESULT_CODE_OK;
    }
    catch (ResultCode ret)
    {
        this->CloseFile();
        FLEX_DEBUG_LOG(__FUNCTION__ " >> EXCEPTION!! ");
        return ret;
    }
}

FlexBodyFileIO::FlexBodyFileIO():
    m_file(nullptr),
    m_fileformat_version(0),
    m_cache_entry_number(-1) // flexbody cache disabled (shouldn't be based on the cache entry number ...) ~ ulteq 01/19
    {}

void FlexFactory::CheckAndLoadFlexbodyCache()
{
    FLEX_DEBUG_LOG(__FUNCTION__);
    if (m_is_flexbody_cache_enabled)
    {
        m_is_flexbody_cache_loaded = 
            (m_flexbody_cache.LoadFile() == FlexBodyFileIO::RESULT_CODE_OK);
    }
}

void FlexFactory::SaveFlexbodiesToCache()
{
    FLEX_DEBUG_LOG(__FUNCTION__);
    if (m_is_flexbody_cache_enabled && !m_is_flexbody_cache_loaded &&
        m_is_flexbody_cache_safe_to_save)
    {
        FLEX_DEBUG_LOG(__FUNCTION__ " >> Saving flexbodies");
        m_flexbody_cache.SaveFile();
    }
}
