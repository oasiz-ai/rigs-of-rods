#ifdef ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_REMAP
#    error "The namespace remap must remain inside the OgreNext adapter"
#endif

#include <cstdint>

extern "C" std::uintptr_t ror_embedded_ogre_next_root_address();
extern "C" std::uintptr_t ror_ogre14_root_address();

int main()
{
    const std::uintptr_t next = ror_embedded_ogre_next_root_address();
    const std::uintptr_t legacy = ror_ogre14_root_address();

    // Neither runtime is initialized in this bounded link smoke. Calling both
    // exported singleton accessors proves that both ABI owners resolved and
    // can execute in one process without creating windows or GPU state.
    return next == 0U && legacy == 0U ? 0 : 1;
}
