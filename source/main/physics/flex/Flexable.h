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

#include "FlexMeshTopology.h"

#include <OgreVector2.h>
#include <OgreVector3.h>

#include <cstddef>

namespace RoR {

/// Borrowed, read-only view of one completed deformable graphics-staging
/// allocation. The streams may be interleaved or contiguous; callers consume
/// the view synchronously after the graphics update join and before any owner
/// can mutate the staging again. This never references solver/NodeSB memory.
struct JoinedCpuStagingView
{
    const unsigned char* position_data = nullptr;
    const unsigned char* normal_data = nullptr;
    const unsigned char* texcoord0_data = nullptr;
    std::size_t vertex_count = 0U;
    std::size_t normal_count = 0U;
    std::size_t texcoord0_count = 0U;
    std::size_t position_stride = 0U;
    std::size_t normal_stride = 0U;
    std::size_t texcoord0_stride = 0U;

    const Ogre::Vector3& position(std::size_t index) const noexcept
    {
        return *reinterpret_cast<const Ogre::Vector3*>(
            position_data + index * position_stride);
    }

    const Ogre::Vector3& normal(std::size_t index) const noexcept
    {
        return *reinterpret_cast<const Ogre::Vector3*>(
            normal_data + index * normal_stride);
    }

    const Ogre::Vector2& texcoord0(std::size_t index) const noexcept
    {
        return *reinterpret_cast<const Ogre::Vector2*>(
            texcoord0_data + index * texcoord0_stride);
    }

    bool hasTexcoords0() const noexcept
    {
        return texcoord0_data != nullptr && texcoord0_count != 0U;
    }
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
