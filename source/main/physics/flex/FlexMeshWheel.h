/*
    This source file is part of Rigs of Rods
    Copyright 2005-2012 Pierre-Michel Ricordel
    Copyright 2007-2012 Thomas Fischer
    Copyright 2013-2020 Petr Ohlidal

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

#pragma once

#include "ForwardDeclarations.h"
#include "FlexMesh.h"

#include <Ogre.h>
#include <string>
#include <vector>

namespace RoR {

/// @addtogroup Gfx
/// @{

/// @addtogroup Flex
/// @{

/// Consists of static mesh, representing the rim, and dynamic mesh, representing the tire.
class FlexMeshWheel: public Flexable
{
    friend class RoR::FlexFactory;

public:

    ~FlexMeshWheel();

    Ogre::Entity* GetTireEntity() { return m_tire_entity; }

    /// The rim half of this wheel: a rigid authored mesh, not a deformable.
    /// It has no CPU staging owner - its motion lives entirely in
    /// GetRimSceneNode(), which flexitPrepare() re-poses every frame.
    Ogre::Entity* GetRimEntity() { return m_rim_entity; }

    /// The node flexitPrepare() positions and orients. The rim Entity's world
    /// transform must be read from here; the tire Entity hangs off a separate,
    /// never-moved node because the tire carries its motion in its vertices.
    Ogre::SceneNode* GetRimSceneNode() { return m_rim_scene_node; }

    Ogre::Vector3 updateVertices();
    /// Copies only the completed graphics staging arrays. Call only after
    /// GfxActor::FinishWheelUpdates(); this never exposes NodeSB/solver data.
    bool copyJoinedCpuStaging(std::vector<Ogre::Vector3>& positions,
                              std::vector<Ogre::Vector3>& normals,
                              std::vector<Ogre::Vector2>& texcoords0) const;
    const std::vector<FlexMeshTopologySection>& getCpuTopologySections() const
    {
        return m_cpu_topology_sections;
    }

    // Flexable
    bool flexitPrepare();
    void flexitCompute();
    Ogre::Vector3 flexitFinal();

    void setVisible(bool visible);

private:

    FlexMeshWheel( // Use FlexFactory
        Ogre::Entity* rim_prop_entity,
        Ogre::SceneNode* rim_scene_node,
        RoR::GfxActor* gfx_actor,
        int axis_node_1_index,
        int axis_node_2_index,
        int nstart,
        int nrays,
        std::string const& tire_mesh_name,
        std::string const& tire_mesh_rg,
        std::string const& tire_material_name,
        std::string const& tire_material_rg,
        float rimradius,
        bool rimreverse
    );

    struct FlexMeshWheelVertex
    {
        Ogre::Vector3 position;
        Ogre::Vector3 normal;
        Ogre::Vector2 texcoord;
    };

    // Wheel
    size_t           m_num_rays;
    float            m_rim_radius;
    RoR::GfxActor*   m_gfx_actor;
    int              m_axis_node0_idx;
    int              m_axis_node1_idx;
    int              m_start_node_idx; //!< First node (lowest index) belonging to this wheel.

    // Meshes
    Ogre::Vector3    m_flexit_center;
    Ogre::MeshPtr    m_mesh;
    Ogre::SubMesh*   m_submesh;
    bool             m_is_rim_reverse;
    Ogre::Entity*    m_rim_entity;
    Ogre::Entity*    m_tire_entity; // Assigned by friend FlexFactory
    Ogre::SceneNode* m_rim_scene_node;

    // Vertices
    float            m_norm_y;
    size_t           m_vertex_count;
    std::vector<FlexMeshWheelVertex> m_vertices;
    Ogre::VertexDeclaration* m_vertex_format;
    Ogre::HardwareVertexBufferSharedPtr m_hw_vbuf;

    // Indices
    size_t           m_index_count;
    std::vector<unsigned short>  m_indices;
    std::vector<FlexMeshTopologySection> m_cpu_topology_sections;
};

/// @} // addtogroup Flex
/// @} // addtogroup Gfx

} // namespace RoR
