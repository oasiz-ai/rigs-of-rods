/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

// This target receives only the renderer-neutral include directory. It must
// compile without any OGRE, Ogre-Next, platform graphics API, or game include
// path. The macro checks catch accidental transitive renderer SDK inclusion.
#include "RenderContracts.h"

#if defined(OGRE_VERSION) || defined(OGRE_VERSION_MAJOR) ||                    \
    defined(OGRE_PLATFORM) || defined(OGRE_COMPILER) ||                        \
    defined(_OgrePrerequisites_H_) || defined(__OgrePrerequisites_H__) ||      \
    defined(_OgreRoot_H_)
#error "renderer-neutral public headers imported an OGRE SDK header"
#endif

#include <cstdint>
#include <type_traits>

static_assert(__cplusplus >= 201703L,
              "renderer-neutral contracts require portable C++17");
static_assert(std::is_trivially_copyable_v<RoR::Render::Float3>);
static_assert(std::is_trivially_copyable_v<RoR::Render::Double3>);
static_assert(
    std::is_trivially_copyable_v<RoR::Render::ParallaxProbeReferenceInput>);
static_assert(
    std::is_trivially_copyable_v<RoR::Render::ParallaxProbeReferenceResult>);
static_assert(std::is_trivially_copyable_v<
              RoR::Render::ReflectionProbeRuntimeDescriptor>);
static_assert(std::is_trivially_copyable_v<
              RoR::Render::ReflectionProbeUpdateRequest>);
static_assert(std::is_trivially_copyable_v<RoR::Render::ResourceHandle>);
static_assert(std::is_trivially_copyable_v<RoR::Render::RenderAssetId>);
static_assert(std::is_standard_layout_v<RoR::Render::NativeObjectToken>);
static_assert(
    std::is_abstract_v<RoR::Render::IJoinedGraphicsSceneSource>,
    "the joined graphics adapter must remain a narrow source interface");
static_assert(
    std::is_same_v<decltype(RoR::Render::NativeObjectToken{}.value),
                   std::uint64_t>,
    "native token must contain an integer rather than a backend pointer");

int main() {
  const RoR::Render::ResourceHandle handle =
      RoR::Render::ResourceHandle::Create(RoR::Render::ResourceKind::TEXTURE,
                                          1U, 1U, 1U);
  const RoR::Render::RenderAssetId asset =
      RoR::Render::RenderAssetId::FromWords(1U, 2U);
  return handle.valid() && asset.valid() ? 0 : 1;
}
