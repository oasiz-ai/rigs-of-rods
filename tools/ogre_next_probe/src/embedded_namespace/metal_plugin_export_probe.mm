#include "OgreBuildSettings.h"

// The production combined runtime is static. Compile the exact upstream
// dynamic entry-point source once without OGRE_STATIC_LIB so symbol auditing
// also proves that a later dynamic plugin build receives the private prefix.
#undef OGRE_STATIC_LIB
#include "OgreMetalEngineDll.mm"
