#ifdef ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_REMAP
#    error "The namespace remap must remain inside the N1 implementation target"
#endif

#if defined( OGRE_VERSION ) || defined( OGRE_VERSION_MAJOR ) || defined( OGRE_PLATFORM )
#    error "The renderer-neutral N1 session adapter imported an Ogre SDK"
#endif

#include "RendererInProcessSession.h"

#if defined( OGRE_VERSION ) || defined( OGRE_VERSION_MAJOR ) || defined( OGRE_PLATFORM )
#    error "RendererInProcessSession.h imported an Ogre SDK"
#endif

#include "ogrenext/OgreNextN1Frontend.h"

#if defined( OGRE_VERSION ) || defined( OGRE_VERSION_MAJOR ) || defined( OGRE_PLATFORM )
#    error "OgreNextN1Frontend.h imported an Ogre SDK"
#endif

#include <cstdint>
#include <utility>

namespace
{
    class DormantEventPump final : public RoR::IRendererInProcessEventPump
    {
    public:
        RoR::Render::ValidationResult PollEvents(
            RoR::RendererInProcessEventPollPoint,
            RoR::RendererInProcessEventObservation & ) override
        {
            return RoR::Render::ValidationResult::Success();
        }
    };

    class DormantFramePolicy final : public RoR::IRendererInProcessFramePolicy
    {
    public:
        RoR::Render::ValidationResult BeginCapture( std::uint32_t, std::uint32_t ) override
        {
            return RoR::Render::ValidationResult::Success();
        }

        void EndCapture() noexcept override {}

        RoR::Render::ValidationResult NormalizeAndValidate(
            RoR::Render::GraphicsSceneFrameInput &, std::uint32_t,
            std::uint32_t ) override
        {
            return RoR::Render::ValidationResult::Success();
        }
    };
}

extern "C" bool ror_embedded_ogre_next_n1_session_lifecycle() noexcept
{
    try
    {
        RoR::Render::OgreNextN1Configuration configuration;
        RoR::Render::OgreNextN1Frontend frontend( std::move( configuration ) );
        const RoR::Render::FrontendCapabilityReport capabilities =
            frontend.QueryCapabilities();
        DormantEventPump eventPump;
        DormantFramePolicy framePolicy;
        RoR::RendererInProcessSession session( frontend, eventPump, framePolicy );
        return capabilities.frontend_kind ==
                   RoR::Render::RendererFrontendKind::OGRE_NEXT &&
               !session.active() && !session.terminal() &&
               session.registry_id() == 0u;
    }
    catch( ... )
    {
        return false;
    }
}
