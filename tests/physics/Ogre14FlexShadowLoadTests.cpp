/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include <Ogre.h>
#include <OgreDefaultHardwareBufferManager.h>
#include <OgreMeshSerializer.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

void Require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "OGRE 14 flex shadow-load test failed: " << message
                  << '\n';
        std::exit(EXIT_FAILURE);
    }
}

class PrivateReadTrapBuffer final : public Ogre::HardwareBuffer
{
public:
    PrivateReadTrapBuffer(
        std::size_t byte_count,
        Ogre::HardwareBuffer::Usage usage,
        bool use_shadow)
        : Ogre::HardwareBuffer(usage, false)
        , m_bytes(byte_count)
    {
        mSizeInBytes = byte_count;
        if (use_shadow)
        {
            mShadowBuffer.reset(
                new Ogre::DefaultHardwareBuffer(byte_count));
        }
    }

    static bool trap_private_reads;
    static std::size_t private_read_count;

    void* lockImpl(
        std::size_t offset,
        std::size_t,
        LockOptions options) override
    {
        if (options == HBL_READ_ONLY && trap_private_reads)
        {
            ++private_read_count;
            throw std::runtime_error("private native read trap");
        }
        return m_bytes.data() + offset;
    }

    void unlockImpl() override {}

    void readData(
        std::size_t offset,
        std::size_t length,
        void* destination) override
    {
        if (trap_private_reads)
        {
            ++private_read_count;
            // Match Ogre 14 Metal: virtual readData bypasses the wrapper
            // shadow and reaches the native delegate.
            throw std::runtime_error("Metal-style native readData trap");
        }
        if (mShadowBuffer)
        {
            mShadowBuffer->readData(offset, length, destination);
            return;
        }
        std::memcpy(destination, m_bytes.data() + offset, length);
    }

    void writeData(
        std::size_t offset,
        std::size_t length,
        const void* source,
        bool discard_whole_buffer = false) override
    {
        if (mShadowBuffer)
        {
            mShadowBuffer->writeData(
                offset, length, source, discard_whole_buffer);
        }
        std::memcpy(m_bytes.data() + offset, source, length);
    }

private:
    std::vector<unsigned char> m_bytes;
};

bool PrivateReadTrapBuffer::trap_private_reads = false;
std::size_t PrivateReadTrapBuffer::private_read_count = 0U;

class ShadowAwareHardwareBufferManager final
    : public Ogre::DefaultHardwareBufferManager
{
public:
    Ogre::HardwareVertexBufferSharedPtr createVertexBuffer(
        std::size_t vertex_size,
        std::size_t vertex_count,
        Ogre::HardwareBuffer::Usage usage,
        bool use_shadow_buffer = false) override
    {
        return std::make_shared<Ogre::HardwareVertexBuffer>(
            this, vertex_size, vertex_count,
            new PrivateReadTrapBuffer(
                vertex_size * vertex_count, usage, use_shadow_buffer));
    }

    Ogre::HardwareIndexBufferSharedPtr createIndexBuffer(
        Ogre::HardwareIndexBuffer::IndexType index_type,
        std::size_t index_count,
        Ogre::HardwareBuffer::Usage usage,
        bool use_shadow_buffer = false) override
    {
        return std::make_shared<Ogre::HardwareIndexBuffer>(
            this, index_type, index_count,
            new PrivateReadTrapBuffer(
                Ogre::HardwareIndexBuffer::indexSize(index_type) *
                    index_count,
                usage, use_shadow_buffer));
    }
};

bool AllMeshBuffersAreShadowed(const Ogre::MeshPtr& mesh)
{
    if (mesh.isNull() || mesh->getNumSubMeshes() == 0U)
        return false;
    const auto vertex_data_is_shadowed = [](
        const Ogre::VertexData* vertex_data)
    {
        if (vertex_data == nullptr ||
            vertex_data->vertexBufferBinding == nullptr)
        {
            return false;
        }
        const auto& bindings =
            vertex_data->vertexBufferBinding->getBindings();
        return !bindings.empty() &&
            std::all_of(
                bindings.begin(), bindings.end(),
                [](const auto& binding)
                {
                    return !binding.second.isNull() &&
                        binding.second->hasShadowBuffer();
                });
    };
    if (mesh->sharedVertexData != nullptr &&
        !vertex_data_is_shadowed(mesh->sharedVertexData))
    {
        return false;
    }
    for (std::size_t index = 0U;
         index < mesh->getNumSubMeshes(); ++index)
    {
        const Ogre::SubMesh* const submesh = mesh->getSubMesh(index);
        if (submesh == nullptr || submesh->indexData == nullptr ||
            submesh->indexData->indexBuffer.isNull() ||
            !submesh->indexData->indexBuffer->hasShadowBuffer())
        {
            return false;
        }
        if (!submesh->useSharedVertices &&
            !vertex_data_is_shadowed(submesh->vertexData))
        {
            return false;
        }
    }
    return true;
}

Ogre::MeshPtr ImportIsolatedShadowedMesh(
    const Ogre::String& source_name,
    const Ogre::String& unique_name,
    const Ogre::String& group)
{
    Ogre::MeshManager& manager = Ogre::MeshManager::getSingleton();
    Ogre::MeshPtr mesh = manager.createManual(unique_name, group);
    mesh->setVertexBufferPolicy(Ogre::HBU_GPU_ONLY, true);
    mesh->setIndexBufferPolicy(Ogre::HBU_GPU_ONLY, true);
    Ogre::MeshSerializer serializer;
    serializer.setListener(manager.getListener());
    serializer.importMesh(
        Ogre::ResourceGroupManager::getSingleton().openResource(
            source_name, group),
        mesh.get());
    mesh->load();
    mesh->touch();
    return mesh;
}

void ProveShadowLockAvoidsPrivateDelegate(const Ogre::MeshPtr& mesh)
{
    const Ogre::VertexData* vertex_data = mesh->sharedVertexData;
    if (vertex_data == nullptr)
        vertex_data = mesh->getSubMesh(0U)->vertexData;
    Require(vertex_data != nullptr, "fixture has no vertex data");
    const auto& bindings = vertex_data->vertexBufferBinding->getBindings();
    Require(!bindings.empty(), "fixture has no vertex buffer binding");
    const Ogre::HardwareVertexBufferSharedPtr vertex_buffer =
        bindings.begin()->second;
    const Ogre::HardwareIndexBufferSharedPtr index_buffer =
        mesh->getSubMesh(0U)->indexData->indexBuffer;

    PrivateReadTrapBuffer::private_read_count = 0U;
    PrivateReadTrapBuffer::trap_private_reads = true;
    {
        Ogre::HardwareBufferLockGuard vertex_lock(
            vertex_buffer, 0U, vertex_buffer->getVertexSize(),
            Ogre::HardwareBuffer::HBL_READ_ONLY);
        Require(vertex_lock.pData != nullptr,
                "shadowed vertex lock returned no CPU bytes");
    }
    {
        const std::size_t index_size =
            Ogre::HardwareIndexBuffer::indexSize(index_buffer->getType());
        Ogre::HardwareBufferLockGuard index_lock(
            index_buffer, 0U, index_size,
            Ogre::HardwareBuffer::HBL_READ_ONLY);
        Require(index_lock.pData != nullptr,
                "shadowed index lock returned no CPU bytes");
    }
    PrivateReadTrapBuffer::trap_private_reads = false;
    Require(PrivateReadTrapBuffer::private_read_count == 0U,
            "shadow lock reached the private native delegate");
}

} // namespace

int main(int argc, char** argv)
{
    Require(argc == 2, "expected repository mesh fixture directory");
    Ogre::Root root("", "", "");
    ShadowAwareHardwareBufferManager hardware_buffers;
    Ogre::ResourceGroupManager& resource_groups =
        Ogre::ResourceGroupManager::getSingleton();
    Ogre::MeshManager& meshes = Ogre::MeshManager::getSingleton();
    const Ogre::String group = "FlexShadowLoadFixtures";
    resource_groups.createResourceGroup(group);
    resource_groups.addResourceLocation(argv[1], "FileSystem", group);

    Ogre::MeshPtr first_load = meshes.load(
        "road-street.mesh", group,
        Ogre::HBU_GPU_ONLY, Ogre::HBU_GPU_ONLY, true, true);
    Require(AllMeshBuffersAreShadowed(first_load),
            "explicit first-load policy did not create actual shadows");
    ProveShadowLockAvoidsPrivateDelegate(first_load);

    Ogre::MeshPtr preloaded = meshes.load("sign-stop.mesh", group);
    Require(!AllMeshBuffersAreShadowed(preloaded),
            "default preload unexpectedly created shadows");
    Ogre::MeshPtr cannot_retrofit = meshes.load(
        "sign-stop.mesh", group,
        Ogre::HBU_GPU_ONLY, Ogre::HBU_GPU_ONLY, true, true);
    Require(cannot_retrofit == preloaded &&
                !AllMeshBuffersAreShadowed(cannot_retrofit),
            "createOrRetrieve unexpectedly retrofitted a shared resource");

    Ogre::MeshPtr isolated = ImportIsolatedShadowedMesh(
        "sign-stop.mesh", "sign-stop.flex-shadow.mesh", group);
    Require(AllMeshBuffersAreShadowed(isolated),
            "isolated serializer import did not create actual shadows");
    ProveShadowLockAvoidsPrivateDelegate(isolated);

    meshes.remove(isolated);
    meshes.remove(cannot_retrofit);
    meshes.remove(first_load);
    isolated.reset();
    cannot_retrofit.reset();
    preloaded.reset();
    first_load.reset();
    resource_groups.destroyResourceGroup(group);
    std::cout << "OGRE 14 flex shadow load fixtures verified\n";
    return EXIT_SUCCESS;
}
