/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

void Require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "OGRE 14 Metal flex shadow-read contract failed: "
                  << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

std::string ReadSource(const char* path)
{
    std::ifstream input(path, std::ios::binary);
    Require(input.good(), "could not open production source");
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

std::string Slice(
    const std::string& source,
    const char* begin_marker,
    const char* end_marker)
{
    const std::size_t begin = source.find(begin_marker);
    Require(begin != std::string::npos, "production begin marker missing");
    const std::size_t end = source.find(end_marker, begin);
    Require(end != std::string::npos && end > begin,
            "production end marker missing");
    return source.substr(begin, end - begin);
}

} // namespace

int main(int argc, char** argv)
{
    Require(argc == 4,
            "expected FlexFactory, FlexBody, and ActorSpawner source paths");

    const std::string factory = ReadSource(argv[1]);
    Require(factory.find("readData(") == std::string::npos,
            "FlexFactory contains a backend readData path");
    const std::string vertex_capture = Slice(
        factory,
        "bool ValidateShadowedFlexbodyVertexData(",
        "class OgreFlexbodyTopologySource");
    Require(vertex_capture.find("hasShadowBuffer()") != std::string::npos,
            "vertex preflight no longer requires CPU shadows");
    Require(
        vertex_capture.find("HardwareBufferLockGuard") !=
            std::string::npos &&
        vertex_capture.find("HBL_READ_ONLY") != std::string::npos,
        "vertex capture no longer uses exception-safe shadow locks");

    const std::string shadow_read = Slice(
        factory, "    bool ReadShadowedIndices(", "\nprivate:");
    Require(shadow_read.find("hasShadowBuffer()") != std::string::npos,
            "native tap no longer rejects unshadowed buffers");
    Require(shadow_read.find("HardwareBufferLockGuard") != std::string::npos,
            "native tap no longer uses exception-safe shadow locking");
    Require(shadow_read.find("HBL_READ_ONLY") != std::string::npos,
            "native tap no longer requests a read-only shadow lock");
    Require(shadow_read.find("readData(") == std::string::npos,
            "native tap invokes backend readData instead of the CPU shadow");
    const std::string index_install = Slice(
        factory, "bool InstallShadowedFlexbodyIndexBuffers(",
        "\n} // namespace");
    Require(index_install.find("_dirtyState") == std::string::npos,
            "index rebind has a potentially throwing post-commit callback");
    Require(
        index_install.find("InstallFlexMeshCpuTopologyTransaction") !=
            std::string::npos,
        "index buffers no longer use the prepare-all-before-commit gate");
    const std::string isolated_import = Slice(
        factory, "Ogre::MeshPtr ImportShadowedFlexbodyMesh(",
        "\nclass OgreFlexbodyTopologySource");
    const std::size_t modern_policy =
        isolated_import.find("Ogre::HBU_GPU_ONLY");
    const std::size_t legacy_policy = isolated_import.find(
        "Ogre::HardwareBuffer::HBU_STATIC_WRITE_ONLY");
    const std::size_t vertex_policy =
        isolated_import.find(
            "setVertexBufferPolicy(shadowed_static_usage, true)");
    const std::size_t index_policy =
        isolated_import.find(
            "setIndexBufferPolicy(shadowed_static_usage, true)");
    const std::size_t mutable_stream =
        isolated_import.find("Ogre::DataStreamPtr source =");
    const std::size_t serializer_import =
        isolated_import.find("serializer.importMesh(");
    Require(modern_policy != std::string::npos &&
                legacy_policy != std::string::npos &&
                vertex_policy != std::string::npos &&
                index_policy != std::string::npos &&
                mutable_stream != std::string::npos &&
                serializer_import != std::string::npos &&
                modern_policy < vertex_policy &&
                legacy_policy < vertex_policy &&
                vertex_policy < serializer_import &&
                index_policy < serializer_import &&
                mutable_stream < serializer_import &&
                isolated_import.find("const Ogre::DataStreamPtr source") ==
                    std::string::npos,
            "isolated fallback no longer uses portable shadow policies and "
            "a mutable serializer stream before import");
    const std::size_t create_flexbody =
        factory.find("FlexBody* FlexFactory::CreateFlexBody(");
    Require(create_flexbody != std::string::npos,
            "CreateFlexBody source marker missing");
    const std::size_t isolated_fallback = factory.find(
        "ImportShadowedFlexbodyMesh(", create_flexbody);
    const std::size_t capture = factory.find(
        "CaptureShadowedFlexbodyVertexStreams(", create_flexbody);
    const std::size_t entity =
        factory.find("createEntity(", create_flexbody);
    const std::size_t create_flexbody_end = factory.find(
        "\nFlexMeshWheel* FlexFactory::CreateFlexMeshWheel(",
        create_flexbody);
    Require(isolated_fallback != std::string::npos &&
                capture != std::string::npos &&
                entity != std::string::npos &&
                create_flexbody_end != std::string::npos &&
                isolated_fallback < capture && capture < entity,
            "isolated shadow import no longer precedes capture and Entity");
    Require(
        factory.substr(
            create_flexbody,
            create_flexbody_end - create_flexbody).find("->clone(") ==
            std::string::npos,
        "flexbody construction reintroduced a native Mesh::clone copy");
    const std::string create_transaction = factory.substr(
        create_flexbody, create_flexbody_end - create_flexbody);
    const std::size_t owned_flexbody =
        create_transaction.find("std::unique_ptr<FlexBody>");
    const std::size_t assign_id =
        create_transaction.find("new_flexbody->m_id =");
    const std::size_t assign_mesh_name =
        create_transaction.find("new_flexbody->m_orig_mesh_name =");
    const std::size_t cache_publish =
        create_transaction.find("AddItemToSave(new_flexbody.get())");
    const std::size_t ownership_release =
        create_transaction.find("return new_flexbody.release()");
    const std::size_t entity_transfer =
        create_transaction.find("entity_owned_by_flexbody = true");
    const std::size_t entity_rollback =
        create_transaction.find("destroyEntity(entity)");
    const std::size_t cache_invalidation =
        create_transaction.rfind("m_is_flexbody_cache_loaded = false");
    Require(owned_flexbody != std::string::npos &&
                assign_id != std::string::npos &&
                assign_mesh_name != std::string::npos &&
                cache_publish != std::string::npos &&
                ownership_release != std::string::npos &&
                entity_transfer != std::string::npos &&
                entity_rollback != std::string::npos &&
                cache_invalidation != std::string::npos &&
                owned_flexbody < assign_id &&
                owned_flexbody < entity_transfer &&
                entity_transfer < assign_id &&
                assign_id < assign_mesh_name &&
                assign_mesh_name < cache_publish &&
                cache_publish < ownership_release,
            "FlexBody ownership can escape before throwing initialization "
            "and cache publication complete");
    Require(cache_invalidation > ownership_release &&
                cache_invalidation < entity_rollback,
            "failed flexbody construction can misalign the positional cache");
    Require(create_transaction.find(
                "m_is_flexbody_cache_safe_to_save = false") !=
                std::string::npos &&
                factory.find("!m_is_flexbody_cache_loaded &&\n        "
                             "m_is_flexbody_cache_safe_to_save") !=
                    std::string::npos,
            "failed positional cache can be persisted for a later launch");

    const std::string body = ReadSource(argv[2]);
    Require(body.find("readData(") == std::string::npos,
            "FlexBody construction/capture contains backend readData");
    Require(body.find("copyData(") == std::string::npos,
            "FlexBody construction bypasses CPU shadows through copyData");
    const std::string dynamic_rebind = Slice(
        body, "static void MakeFlexBuffersDynamic(", "\n#endif");
    Require(dynamic_rebind.find("hasShadowBuffer()") != std::string::npos &&
                dynamic_rebind.find("HardwareBufferLockGuard") !=
                    std::string::npos &&
                dynamic_rebind.find("HBL_READ_ONLY") != std::string::npos,
            "dynamic flex buffer conversion no longer copies the CPU shadow");
    Require(body.find("defragmentFlexbodyMesh") == std::string::npos,
            "latent non-atomic defrag implementation remains callable");
    Require(body.find("A throwing constructor has no FlexBody destructor") !=
                std::string::npos &&
                body.find("m_scene_entity = nullptr") != std::string::npos,
            "throwing FlexBody construction no longer rolls back local state");
    Require(body.find("FlexBody::~FlexBody() noexcept") !=
                std::string::npos &&
                body.find("void FlexBody::destroyOgreObjects() noexcept") !=
                    std::string::npos &&
                body.find("std::exchange(m_scene_entity, nullptr)") !=
                    std::string::npos,
            "FlexBody rollback can throw or repeat renderer destruction");
    Require(body.find("optimal_vd_owner") != std::string::npos &&
                body.find("destroyVertexDeclaration(declaration)") !=
                    std::string::npos &&
                body.find("optimal_vd_owner.release()") !=
                    std::string::npos,
            "temporary flex vertex declaration has no transfer-aware owner");
    Require(body.find("initial_vertex_streams.positions") !=
                std::string::npos &&
                body.find("initial_vertex_streams.normals") !=
                    std::string::npos &&
                body.find("initial_vertex_streams.texcoords0") !=
                    std::string::npos,
            "FlexBody no longer consumes pre-captured CPU vertex streams");
    const std::string constructor_defrag = Slice(
        body,
        "    // Keep the forset nodes for diagnostics",
        "    // UV0 was captured from CPU shadows");
    Require(
        constructor_defrag.find("flexbody_defrag_enabled") !=
            std::string::npos,
        "defrag compatibility gate disappeared");
    Require(
        constructor_defrag.find("multi-buffer commits are not atomic") !=
            std::string::npos,
        "disabled defrag no longer explains its atomicity boundary");

    const std::string spawner = ReadSource(argv[3]);
    const std::string flexbody_owner = Slice(
        spawner,
        "void ActorSpawner::ProcessFlexbody(",
        "\nvoid ActorSpawner::ProcessMinimass(");
    const std::size_t owner_reserve =
        flexbody_owner.find("m_flexbodies.reserve(");
    const std::size_t factory_create =
        flexbody_owner.find("m_flex_factory.CreateFlexBody(");
    const std::size_t owner_publish =
        flexbody_owner.find("m_flexbodies.emplace_back(flexbody)");
    Require(owner_reserve != std::string::npos &&
                factory_create != std::string::npos &&
                owner_publish != std::string::npos &&
                owner_reserve < factory_create &&
                factory_create < owner_publish,
            "normal flexbody owner slot is not reserved before cache "
            "publication");
    const std::string wheel_owner = Slice(
        spawner,
        "void ActorSpawner::CreateFlexBodyWheelVisuals(",
        "\nunsigned int ActorSpawner::AddWheelBeam(");
    const std::size_t wheel_reserve =
        wheel_owner.find("m_flexbodies.reserve(");
    const std::size_t wheel_create =
        wheel_owner.find("m_flex_factory.CreateFlexBody(");
    const std::size_t wheel_publish =
        wheel_owner.find("m_flexbodies.push_back(flexbody)");
    const std::size_t wheel_skidmarks =
        wheel_owner.find("CreateWheelSkidmarks(wheel_id)");
    Require(wheel_reserve != std::string::npos &&
                wheel_create != std::string::npos &&
                wheel_publish != std::string::npos &&
                wheel_skidmarks != std::string::npos &&
                wheel_reserve < wheel_create &&
                wheel_create < wheel_publish &&
                wheel_publish < wheel_skidmarks,
            "wheel flexbody can escape before final owner publication");

    std::cout << "OGRE 14 Metal flex shadow-read contract verified\n";
    return EXIT_SUCCESS;
}
