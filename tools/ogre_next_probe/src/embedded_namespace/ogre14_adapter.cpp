#ifdef ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_REMAP
#    error "The private OgreNext namespace remap leaked into OGRE14"
#endif

#ifdef Ogre
#    error "The Ogre identifier macro leaked into OGRE14"
#endif

#include "OgreRoot.h"

#include <cstdint>

extern "C" std::uintptr_t ror_ogre14_root_address()
{
    return reinterpret_cast<std::uintptr_t>( Ogre::Root::getSingletonPtr() );
}
