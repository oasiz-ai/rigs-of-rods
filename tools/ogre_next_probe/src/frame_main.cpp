#include "ror_ogre_next_frame_probe_config.h"

#include "Compositor/OgreCompositorManager2.h"
#include "Compositor/OgreCompositorWorkspace.h"
#include "OgreAbiUtils.h"
#include "OgreArchiveManager.h"
#include "OgreCamera.h"
#include "OgreColourValue.h"
#include "OgreException.h"
#include "OgreHlmsManager.h"
#include "OgreHlmsPbs.h"
#include "OgreHlmsPbsDatablock.h"
#include "OgreImage2.h"
#include "OgreLight.h"
#include "OgreManualObject2.h"
#include "OgreRenderSystem.h"
#include "OgreRoot.h"
#include "OgreSceneManager.h"
#include "OgreSceneNode.h"
#include "OgreTextureBox.h"
#include "OgreTextureGpu.h"
#include "OgreTextureGpuManager.h"
#include "OgreWindow.h"

#if defined( ROR_OGRE_NEXT_PROBE_METAL )
#    include "OgreMetalPlugin.h"
using FrameRendererPlugin = Ogre::MetalPlugin;
#elif defined( ROR_OGRE_NEXT_PROBE_D3D11 )
#    include "OgreD3D11Plugin.h"
using FrameRendererPlugin = Ogre::D3D11Plugin;
#elif defined( ROR_OGRE_NEXT_PROBE_VULKAN )
#    include "OgreVulkanPlugin.h"
using FrameRendererPlugin = Ogre::VulkanPlugin;
#else
#    error "No reviewed OGRE-Next frame renderer policy selected"
#endif

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    constexpr Ogre::uint32 kWidth = 192u;
    constexpr Ogre::uint32 kHeight = 128u;
    constexpr std::size_t  kWarmupFrames = 4u;

    struct Arguments
    {
        std::string imagePath;
        std::string reportPath;
    };

    struct FrameMetrics
    {
        std::size_t distinctPixels = 0u;
        std::size_t nonBackgroundPixels = 0u;
        std::uint64_t fnv1a64 = UINT64_C( 14695981039346656037 );
        float minimumLuminance = std::numeric_limits<float>::infinity();
        float maximumLuminance = -std::numeric_limits<float>::infinity();
        std::vector<unsigned char> rgb;
    };

    Arguments parseArguments( int argc, char **argv )
    {
        Arguments arguments;
        for( int index = 1; index < argc; ++index )
        {
            const std::string option = argv[index];
            if( option == "--output" && index + 1 < argc )
                arguments.imagePath = argv[++index];
            else if( option == "--report" && index + 1 < argc )
                arguments.reportPath = argv[++index];
            else
                throw std::runtime_error(
                    "usage: ror_ogre_next_frame_probe [--output FRAME.ppm] "
                    "[--report REPORT.json]" );
        }
        return arguments;
    }

    unsigned char quantize( float value )
    {
        const float clamped = std::max( 0.0f, std::min( 1.0f, value ) );
        return static_cast<unsigned char>( std::lround( clamped * 255.0f ) );
    }

    void hashByte( std::uint64_t &hash, unsigned char value )
    {
        hash ^= value;
        hash *= UINT64_C( 1099511628211 );
    }

    void writeText( const std::string &path, const std::string &content )
    {
        if( path.empty() )
            return;
        std::ofstream output( path, std::ios::binary | std::ios::trunc );
        if( !output )
            throw std::runtime_error( "could not open output: " + path );
        output.write( content.data(), static_cast<std::streamsize>( content.size() ) );
        if( !output )
            throw std::runtime_error( "could not write output: " + path );
    }

    void writePpm( const std::string &path, const FrameMetrics &metrics )
    {
        if( path.empty() )
            return;
        std::ostringstream header;
        header << "P6\n" << kWidth << ' ' << kHeight << "\n255\n";
        std::ofstream output( path, std::ios::binary | std::ios::trunc );
        if( !output )
            throw std::runtime_error( "could not open frame output: " + path );
        output << header.str();
        output.write( reinterpret_cast<const char *>( metrics.rgb.data() ),
                      static_cast<std::streamsize>( metrics.rgb.size() ) );
        if( !output )
            throw std::runtime_error( "could not write frame output: " + path );
    }

    Ogre::HlmsPbs *registerPbs( Ogre::Root &root )
    {
        Ogre::String       dataPath;
        Ogre::StringVector libraryPaths;
        Ogre::HlmsPbs::getDefaultPaths( dataPath, libraryPaths );

        const Ogre::String mediaRoot = Ogre::String( ROR_OGRE_NEXT_FRAME_MEDIA_ROOT ) + "/";
        Ogre::ArchiveManager &archiveManager = Ogre::ArchiveManager::getSingleton();
        Ogre::Archive *dataArchive =
            archiveManager.load( mediaRoot + dataPath, "FileSystem", true );
        Ogre::ArchiveVec libraries;
        for( const Ogre::String &libraryPath : libraryPaths )
            libraries.push_back(
                archiveManager.load( mediaRoot + libraryPath, "FileSystem", true ) );

        Ogre::HlmsPbs *pbs = OGRE_NEW Ogre::HlmsPbs( dataArchive, &libraries );
        root.getHlmsManager()->registerHlms( pbs );
        return pbs;
    }

    Ogre::HlmsPbsDatablock *createMaterial( Ogre::HlmsPbs &pbs )
    {
        Ogre::HlmsPbsDatablock *datablock =
            static_cast<Ogre::HlmsPbsDatablock *>( pbs.createDatablock(
                "RoRFrameProbePbs", "RoRFrameProbePbs", Ogre::HlmsMacroblock(),
                Ogre::HlmsBlendblock(), Ogre::HlmsParamVec() ) );
        datablock->setDiffuse( Ogre::Vector3( 0.08f, 0.48f, 0.92f ) );
        datablock->setRoughness( 0.18f );
        datablock->setFresnel( Ogre::Vector3( 0.06f ), false );
        return datablock;
    }

    Ogre::ManualObject *createTriangle( Ogre::SceneManager &sceneManager,
                                        Ogre::HlmsPbsDatablock &datablock )
    {
        Ogre::ManualObject *triangle = sceneManager.createManualObject();
        triangle->begin( *datablock.getNameStr(), Ogre::OT_TRIANGLE_LIST );

        triangle->position( -1.15f, -0.85f, 0.0f );
        triangle->normal( 0.0f, 0.0f, 1.0f );
        triangle->tangent( 1.0f, 0.0f, 0.0f );
        triangle->textureCoord( 0.0f, 0.0f );

        triangle->position( 1.15f, -0.85f, 0.0f );
        triangle->normal( 0.0f, 0.0f, 1.0f );
        triangle->tangent( 1.0f, 0.0f, 0.0f );
        triangle->textureCoord( 1.0f, 0.0f );

        triangle->position( 0.0f, 0.95f, 0.0f );
        triangle->normal( 0.0f, 0.0f, 1.0f );
        triangle->tangent( 1.0f, 0.0f, 0.0f );
        triangle->textureCoord( 0.5f, 1.0f );
        triangle->triangle( 0u, 1u, 2u );
        triangle->end();

        Ogre::SceneNode *node = sceneManager.getRootSceneNode( Ogre::SCENE_DYNAMIC )
                                    ->createChildSceneNode( Ogre::SCENE_DYNAMIC );
        node->attachObject( triangle );
        return triangle;
    }

    FrameMetrics inspectFrame( Ogre::Image2 &image )
    {
        if( image.getWidth() != kWidth || image.getHeight() != kHeight )
            throw std::runtime_error( "GPU readback dimensions do not match the render target" );

        FrameMetrics metrics;
        metrics.rgb.reserve( static_cast<std::size_t>( kWidth ) * kHeight * 3u );
        std::vector<std::uint32_t> colours;
        colours.reserve( static_cast<std::size_t>( kWidth ) * kHeight );

        for( Ogre::uint32 y = 0u; y < kHeight; ++y )
        {
            for( Ogre::uint32 x = 0u; x < kWidth; ++x )
            {
                const Ogre::ColourValue colour = image.getColourAt( x, y, 0u );
                if( !std::isfinite( colour.r ) || !std::isfinite( colour.g ) ||
                    !std::isfinite( colour.b ) || !std::isfinite( colour.a ) )
                {
                    throw std::runtime_error( "GPU frame readback contains a non-finite pixel" );
                }
                const unsigned char red = quantize( colour.r );
                const unsigned char green = quantize( colour.g );
                const unsigned char blue = quantize( colour.b );
                metrics.rgb.push_back( red );
                metrics.rgb.push_back( green );
                metrics.rgb.push_back( blue );
                hashByte( metrics.fnv1a64, red );
                hashByte( metrics.fnv1a64, green );
                hashByte( metrics.fnv1a64, blue );
                colours.push_back( ( static_cast<std::uint32_t>( red ) << 16u ) |
                                   ( static_cast<std::uint32_t>( green ) << 8u ) |
                                   static_cast<std::uint32_t>( blue ) );
                const float luminance =
                    0.2126f * colour.r + 0.7152f * colour.g + 0.0722f * colour.b;
                metrics.minimumLuminance = std::min( metrics.minimumLuminance, luminance );
                metrics.maximumLuminance = std::max( metrics.maximumLuminance, luminance );
            }
        }

        std::sort( colours.begin(), colours.end() );
        metrics.distinctPixels = static_cast<std::size_t>(
            std::distance( colours.begin(), std::unique( colours.begin(), colours.end() ) ) );
        std::size_t largestColourRun = 0u;
        for( auto runStart = colours.cbegin(); runStart != colours.cend(); )
        {
            const auto runEnd = std::upper_bound( runStart, colours.cend(), *runStart );
            largestColourRun = std::max<std::size_t>(
                largestColourRun,
                static_cast<std::size_t>( std::distance( runStart, runEnd ) ) );
            runStart = runEnd;
        }
        metrics.nonBackgroundPixels = colours.size() - largestColourRun;

        if( metrics.distinctPixels < 8u || metrics.nonBackgroundPixels < 512u ||
            metrics.maximumLuminance - metrics.minimumLuminance < 0.05f )
        {
            throw std::runtime_error(
                "readback does not prove that HLMS PBS rendered scene geometry" );
        }
        return metrics;
    }

    std::string makeReport( const FrameMetrics &metrics, const Ogre::TextureGpu &target )
    {
        std::ostringstream report;
        report << "{\n"
               << "  \"schema_version\": 1,\n"
               << "  \"status\": \"pass\",\n"
               << "  \"platform_policy\": \"" << ROR_OGRE_NEXT_FRAME_PLATFORM_POLICY
               << "\",\n"
               << "  \"renderer\": \"" << ROR_OGRE_NEXT_FRAME_RENDERER_NAME << "\",\n"
               << "  \"frame\": {\n"
               << "    \"width\": " << kWidth << ",\n"
               << "    \"height\": " << kHeight << ",\n"
               << "    \"warmup_frames\": " << kWarmupFrames << ",\n"
               << "    \"pixel_format\": \""
               << Ogre::PixelFormatGpuUtils::toString( target.getPixelFormat() ) << "\",\n"
               << "    \"ui_included\": false,\n"
               << "    \"hlms_pbs_geometry\": true,\n"
               << "    \"compositor2\": true,\n"
               << "    \"gpu_readback\": true,\n"
               << "    \"distinct_rgb8_values\": " << metrics.distinctPixels << ",\n"
               << "    \"non_background_pixels\": " << metrics.nonBackgroundPixels << ",\n"
               << "    \"minimum_luminance\": " << std::setprecision( 9 )
               << metrics.minimumLuminance << ",\n"
               << "    \"maximum_luminance\": " << metrics.maximumLuminance << ",\n"
               << "    \"rgb8_fnv1a64\": \"" << std::hex << std::setfill( '0' )
               << std::setw( 16 ) << metrics.fnv1a64 << std::dec << "\"\n"
               << "  },\n"
               << "  \"native_ray_tracing\": \"not_evaluated\"\n"
               << "}\n";
        return report.str();
    }

    std::string renderFrame( const Arguments &arguments )
    {
        const Ogre::AbiCookie abiCookie = Ogre::generateAbiCookie();
        FrameRendererPlugin   rendererPlugin;
        Ogre::Root root( &abiCookie, "", "", "", "RoR OGRE-Next Frame Probe" );
        root.installPlugin( &rendererPlugin, nullptr );

        Ogre::RenderSystem *renderer =
            root.getRenderSystemByName( ROR_OGRE_NEXT_FRAME_RENDERER_NAME );
        if( !renderer )
            throw std::runtime_error( "reviewed renderer did not register" );
        root.setRenderSystem( renderer );
        const Ogre::ConfigOptionMap options = renderer->getConfigOptions();
        if( options.find( "sRGB Gamma Conversion" ) != options.end() )
            renderer->setConfigOption( "sRGB Gamma Conversion", "Yes" );
        root.initialise( false );

        Ogre::NameValuePairList windowParameters;
        windowParameters["hidden"] = "true";
        windowParameters["gamma"] = "true";
        windowParameters["FSAA"] = "1";
        Ogre::Window *window = root.createRenderWindow(
            "RoR OGRE-Next Frame Probe", 64u, 64u, false, &windowParameters );
        if( !window || !root.getCompositorManager2() )
            throw std::runtime_error( "native window did not initialize Compositor2" );

        Ogre::HlmsPbs *pbs = registerPbs( root );
        Ogre::HlmsPbsDatablock *datablock = createMaterial( *pbs );
        Ogre::SceneManager *sceneManager =
            root.createSceneManager( Ogre::ST_GENERIC, 1u, "RoRFrameProbeScene" );
        createTriangle( *sceneManager, *datablock );

        Ogre::Camera *camera = sceneManager->createCamera( "RoRFrameProbeCamera" );
        camera->setPosition( 0.0f, 0.0f, 3.0f );
        camera->lookAt( Ogre::Vector3::ZERO );
        camera->setNearClipDistance( 0.1f );
        camera->setFarClipDistance( 20.0f );
        camera->setAspectRatio( static_cast<float>( kWidth ) / static_cast<float>( kHeight ) );

        Ogre::Light *light = sceneManager->createLight();
        Ogre::SceneNode *lightNode =
            sceneManager->getRootSceneNode()->createChildSceneNode();
        lightNode->attachObject( light );
        light->setType( Ogre::Light::LT_DIRECTIONAL );
        light->setDirection( Ogre::Vector3( 0.2f, -0.3f, -1.0f ).normalisedCopy() );
        light->setPowerScale( Ogre::Math::PI * 1.5f );
        sceneManager->setAmbientLight( Ogre::ColourValue( 0.03f, 0.04f, 0.06f ),
                                       Ogre::ColourValue( 0.01f, 0.01f, 0.015f ),
                                       Ogre::Vector3::UNIT_Y );

        Ogre::TextureGpuManager *textureManager = renderer->getTextureGpuManager();
        Ogre::TextureGpu *target = textureManager->createTexture(
            "RoRFrameProbeTarget", Ogre::GpuPageOutStrategy::Discard,
            Ogre::TextureFlags::RenderToTexture, Ogre::TextureTypes::Type2D );
        target->setResolution( kWidth, kHeight );
        target->setPixelFormat( Ogre::PFG_RGBA8_UNORM );
        target->scheduleTransitionTo( Ogre::GpuResidency::Resident );

        Ogre::CompositorManager2 *compositorManager = root.getCompositorManager2();
        const Ogre::String workspaceName = "RoRFrameProbeWorkspace";
        compositorManager->createBasicWorkspaceDef(
            workspaceName, Ogre::ColourValue( 0.008f, 0.012f, 0.02f, 1.0f ), Ogre::IdString() );
        Ogre::CompositorWorkspace *workspace = compositorManager->addWorkspace(
            sceneManager, target, camera, workspaceName, true );

        for( std::size_t frame = 0u; frame < kWarmupFrames; ++frame )
        {
            if( !root.renderOneFrame() )
                throw std::runtime_error( "OGRE-Next ended the frame loop early" );
        }

        Ogre::Image2 image;
        image.convertFromTexture( target, 0u, 0u );
        FrameMetrics metrics = inspectFrame( image );
        writePpm( arguments.imagePath, metrics );
        const std::string report = makeReport( metrics, *target );
        writeText( arguments.reportPath, report );

        compositorManager->removeWorkspace( workspace );
        textureManager->destroyTexture( target );
        root.destroySceneManager( sceneManager );
        return report;
    }
}  // namespace

int main( int argc, char **argv )
{
    try
    {
        const Arguments arguments = parseArguments( argc, argv );
        std::cout << renderFrame( arguments );
        return 0;
    }
    catch( const Ogre::Exception &error )
    {
        std::cerr << "OGRE-Next frame probe failed: " << error.getFullDescription() << '\n';
    }
    catch( const std::exception &error )
    {
        std::cerr << "OGRE-Next frame probe failed: " << error.what() << '\n';
    }
    return 1;
}
