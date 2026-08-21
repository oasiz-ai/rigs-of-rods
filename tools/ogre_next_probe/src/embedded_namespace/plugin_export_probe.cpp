#include "OgreBuildSettings.h"

// RoR-Combined links each renderer statically. Compile the exact upstream
// dynamic entry-point source once without OGRE_STATIC_LIB so the namespace
// audit also proves a later shared-plugin build exports only the private
// RoROgreNext entry points on the selected platform.
#undef OGRE_STATIC_LIB

#if defined(ROR_OGRE_NEXT_PLUGIN_EXPORT_D3D11)
#include "OgreD3D11EngineDll.cpp"
#elif defined(ROR_OGRE_NEXT_PLUGIN_EXPORT_VULKAN)
#include "OgreVulkanEngineDll.cpp"
#else
#error "Exactly one non-Apple Ogre-Next plugin export probe is required"
#endif
