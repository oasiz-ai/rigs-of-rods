#ifndef ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_REMAP
#    error "The OgreNext adapter must compile with the private namespace remap"
#endif

#include "OgreRoot.h"

#include <cstdint>

extern "C" std::uintptr_t ror_embedded_ogre_next_root_address()
{
    return reinterpret_cast<std::uintptr_t>(
        RoROgreNext::Root::getSingletonPtr() );
}
