#ifdef ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_REMAP
#    error "The namespace remap must remain inside the OgreNext adapter"
#endif

#include <cstdint>

extern "C" std::uintptr_t ror_embedded_ogre_next_root_address();
extern "C" std::uintptr_t ror_ogre14_root_address();
extern "C" bool ror_embedded_ogre_next_n1_session_lifecycle() noexcept;
extern "C" bool ror_embedded_ogre_next_presenter_lifecycle() noexcept;

int main()
{
    const std::uintptr_t next = ror_embedded_ogre_next_root_address();
    const std::uintptr_t legacy = ror_ogre14_root_address();
    const bool directSession = ror_embedded_ogre_next_n1_session_lifecycle();
    const bool presenter = ror_embedded_ogre_next_presenter_lifecycle();

    // Neither runtime is initialized in this bounded link smoke. Constructing
    // the production N1 frontend behind the renderer-neutral direct session
    // and the concrete in-process presenter beside the real OGRE14 ABI owner
    // proves the one-process link closure without creating a native window.
    return next == 0U && legacy == 0U && directSession && presenter ? 0 : 1;
}
