#ifdef ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_REMAP
#    error "The namespace remap must remain inside the presenter implementation target"
#endif

#if defined( OGRE_VERSION ) || defined( OGRE_VERSION_MAJOR ) || defined( OGRE_PLATFORM )
#    error "The renderer-neutral presenter adapter imported an Ogre SDK"
#endif

#include "RendererOgreNextInProcessPresenter.h"

#if defined( OGRE_VERSION ) || defined( OGRE_VERSION_MAJOR ) || defined( OGRE_PLATFORM )
#    error "RendererOgreNextInProcessPresenter.h imported an Ogre SDK"
#endif

extern "C" bool ror_embedded_ogre_next_presenter_lifecycle() noexcept
{
    try
    {
        RoR::RendererOgreNextInProcessPresenter presenter;
        return !presenter.prepared() && !presenter.input_attached() &&
               !presenter.quiesced() && presenter.Frontend() == nullptr;
    }
    catch( ... )
    {
        return false;
    }
}
