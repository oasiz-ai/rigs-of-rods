/*
    This source file is part of Rigs of Rods
    Copyright 2005-2012 Pierre-Michel Ricordel
    Copyright 2007-2012 Thomas Fischer

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

#include <OgreVector3.h>

#include <cstdint>
#include <vector>

namespace RoR {

/// Immutable CPU-owned topology for one deformable material section. The
/// renderer bridge consumes this copy instead of trying to read a write-only
/// hardware index buffer back from the GPU.
struct FlexMeshTopologySection
{
    enum class IndexFormat : std::uint8_t
    {
        UINT16 = 0U,
        UINT32 = 1U,
    };

    IndexFormat index_format = IndexFormat::UINT32;
    std::uint64_t revision = 1U;
    std::uint32_t vertex_count = 0U;
    std::vector<std::uint32_t> indices;
};

/// @addtogroup Gfx
/// @{

/// @addtogroup Flex
/// @{

// NOTE: class FlexBody no longer uses this interface ~ only_a_ptr, 05/2018
class Flexable
{
public:
    virtual ~Flexable() {}

    virtual bool flexitPrepare() = 0;
    virtual void flexitCompute() = 0;
    virtual Ogre::Vector3 flexitFinal() = 0;

    virtual void setVisible(bool visible) = 0;
};

/// @} // addtogroup Flex
/// @} // addtogroup Gfx

} // namespace RoR
