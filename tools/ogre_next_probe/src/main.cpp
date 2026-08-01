#include "ror_ogre_next_probe_config.h"

#include "Compositor/OgreCompositorManager2.h"
#include "OgreAbiUtils.h"
#include "OgreException.h"
#include "OgreHlmsPbs.h"
#include "OgreRenderSystem.h"
#include "OgreRoot.h"

#if defined( ROR_OGRE_NEXT_PROBE_METAL )
#    include "OgreMetalPlugin.h"
using ProbeRendererPlugin = Ogre::MetalPlugin;
#elif defined( ROR_OGRE_NEXT_PROBE_D3D11 )
#    include "OgreD3D11Plugin.h"
using ProbeRendererPlugin = Ogre::D3D11Plugin;
#elif defined( ROR_OGRE_NEXT_PROBE_VULKAN )
#    include "OgreVulkanPlugin.h"
using ProbeRendererPlugin = Ogre::VulkanPlugin;
#else
#    error "No reviewed OGRE-Next renderer policy selected"
#endif

#include <cstddef>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace
{
    std::string jsonEscape( const std::string &value )
    {
        std::ostringstream escaped;
        for( const unsigned char character : value )
        {
            switch( character )
            {
            case '\\': escaped << "\\\\"; break;
            case '"': escaped << "\\\""; break;
            case '\b': escaped << "\\b"; break;
            case '\f': escaped << "\\f"; break;
            case '\n': escaped << "\\n"; break;
            case '\r': escaped << "\\r"; break;
            case '\t': escaped << "\\t"; break;
            default:
                if( character < 0x20u )
                {
                    const char hex[] = "0123456789abcdef";
                    escaped << "\\u00" << hex[( character >> 4u ) & 0x0fu]
                            << hex[character & 0x0fu];
                }
                else
                {
                    escaped << static_cast<char>( character );
                }
                break;
            }
        }
        return escaped.str();
    }

    std::string makeReport()
    {
        static_assert( sizeof( void * ) == 8u,
                       "The reviewed OGRE-Next probe policies require a 64-bit ABI" );
        static_assert( OGRE_VERSION_MAJOR == 3 && OGRE_VERSION_MINOR == 0,
                       "The probe must be reviewed before moving OGRE-Next versions" );
        static_assert( OGRE_DEBUG_LEVEL_DEBUG == 3 && OGRE_DEBUG_LEVEL_RELEASE == 0,
                       "The reviewed OGRE-Next debug levels changed" );
        static_assert( OGRE_DEBUG_MODE == 0 || OGRE_DEBUG_MODE == 3,
                       "The active OGRE-Next debug mode is outside the reviewed ABI" );
        static_assert( OGRE_DOUBLE_PRECISION == 0 && OGRE_MEMORY_ALLOCATOR == 0,
                       "The reviewed OGRE-Next scalar or allocator ABI changed" );
        static_assert( OGRE_CONTAINERS_USE_CUSTOM_MEMORY_ALLOCATOR == 0 &&
                           OGRE_STRING_USE_CUSTOM_MEMORY_ALLOCATOR == 0,
                       "The reviewed OGRE-Next container ABI changed" );
        static_assert( OGRE_THREAD_SUPPORT == 0 && OGRE_THREAD_PROVIDER == 0,
                       "The reviewed OGRE-Next threading ABI changed" );
        static_assert( OGRE_HASH_BITS == 32 && sizeof( Ogre::IdString ) == 4u,
                       "The reviewed OGRE-Next IdString ABI changed" );
        static_assert( OGRE_FLEXIBILITY_LEVEL == 0 && OGRE_ASSERT_MODE == 0,
                       "The reviewed OGRE-Next compile-time contract changed" );
        static_assert( OGRE_USE_SIMD == 1 && OGRE_SIMD_ALIGNMENT == 16,
                       "The reviewed OGRE-Next SIMD ABI changed" );
        static_assert( OGRE_RESTRICT_ALIASING == 1,
                       "The reviewed OGRE-Next aliasing contract changed" );

        ProbeRendererPlugin rendererPlugin;
        Ogre::AbiCookie      abiCookie = Ogre::generateAbiCookie();
        std::string          rendererName;
        std::string          firstReportedDevice;
        std::string          pbsDataPath;
        std::size_t          rendererCount = 0u;
        std::size_t          rendererOptionCount = 0u;
        std::size_t          reportedDeviceCount = 0u;
        std::size_t          pbsLibraryPathCount = 0u;
        bool                 rendererRegistered = false;
        bool                 pbsShaderPathMatches = false;
        bool                 compositorManagerDeferred = false;

        {
            Ogre::Root root( &abiCookie, "", "", "", "RoR OGRE-Next Probe" );
            root.installPlugin( &rendererPlugin, nullptr );

            const Ogre::RenderSystemList &renderers = root.getAvailableRenderers();
            rendererCount = renderers.size();
            Ogre::RenderSystem *renderer =
                root.getRenderSystemByName( ROR_OGRE_NEXT_RENDERER_NAME );
            if( !renderer )
                throw std::runtime_error( "pinned renderer did not register with Ogre::Root" );

            rendererRegistered = true;
            rendererName = renderer->getName();
            const Ogre::ConfigOptionMap &configOptions = renderer->getConfigOptions();
            rendererOptionCount = configOptions.size();
            const Ogre::ConfigOptionMap::const_iterator deviceOption =
                configOptions.find( ROR_OGRE_NEXT_DEVICE_OPTION_NAME );
            if( deviceOption == configOptions.end() )
                throw std::runtime_error( "renderer device option did not match policy" );
            for( const Ogre::String &device : deviceOption->second.possibleValues )
            {
                if( device != "(default)" )
                {
                    if( firstReportedDevice.empty() )
                        firstReportedDevice = device;
                    ++reportedDeviceCount;
                }
            }
            if( reportedDeviceCount == 0u )
                throw std::runtime_error( "renderer did not report a hardware device" );
            root.setRenderSystem( renderer );

            Ogre::String       dataPath;
            Ogre::StringVector libraryPaths;
            Ogre::HlmsPbs::getDefaultPaths( dataPath, libraryPaths );
            pbsDataPath = dataPath;
            pbsLibraryPathCount = libraryPaths.size();
            pbsShaderPathMatches =
                dataPath == Ogre::String( "Hlms/Pbs/" ROR_OGRE_NEXT_SHADER_SYNTAX );
            if( !pbsShaderPathMatches || libraryPaths.empty() )
                throw std::runtime_error( "HLMS PBS selected an unexpected shader contract" );

            // Compositor2 is created only after OGRE has a real render window.
            // This probe deliberately avoids claiming scene or presentation support.
            compositorManagerDeferred = root.getCompositorManager2() == nullptr;
            if( !compositorManagerDeferred )
                throw std::runtime_error( "Compositor2 initialized outside the reviewed window path" );
        }

        std::ostringstream report;
        report << "{\n"
               << "  \"schema_version\": 1,\n"
               << "  \"status\": \"pass\",\n"
               << "  \"provenance\": {\n"
               << "    \"repository\": \"" << ROR_OGRE_NEXT_REPOSITORY << "\",\n"
               << "    \"branch\": \"" << ROR_OGRE_NEXT_BRANCH << "\",\n"
               << "    \"commit\": \"" << ROR_OGRE_NEXT_COMMIT << "\",\n"
               << "    \"archive_sha256\": \"" << ROR_OGRE_NEXT_ARCHIVE_SHA256 << "\",\n"
               << "    \"license_spdx\": \"" << ROR_OGRE_NEXT_LICENSE_SPDX << "\",\n"
               << "    \"license_sha256\": \"" << ROR_OGRE_NEXT_LICENSE_SHA256 << "\",\n"
               << "    \"rapidjson_tag\": \"" << ROR_OGRE_NEXT_RAPIDJSON_TAG << "\",\n"
               << "    \"rapidjson_archive_sha256\": \""
               << ROR_OGRE_NEXT_RAPIDJSON_SHA256 << "\",\n"
               << "    \"rapidjson_source_archive_license_spdx\": \""
               << ROR_OGRE_NEXT_RAPIDJSON_LICENSE_SPDX << "\",\n"
               << "    \"rapidjson_compiled_headers_license_spdx\": \""
               << ROR_OGRE_NEXT_RAPIDJSON_COMPILED_HEADERS_SPDX << "\",\n"
               << "    \"rapidjson_license_sha256\": \""
               << ROR_OGRE_NEXT_RAPIDJSON_LICENSE_SHA256 << "\"\n"
               << "  },\n"
               << "  \"build\": {\n"
               << "    \"ogre_version\": \"" << OGRE_VERSION_MAJOR << '.' << OGRE_VERSION_MINOR
               << '.' << OGRE_VERSION_PATCH << "\",\n"
               << "    \"platform_policy\": \"" << ROR_OGRE_NEXT_PLATFORM_POLICY << "\",\n"
               << "    \"system\": \"" << ROR_OGRE_NEXT_SYSTEM_NAME << "\",\n"
               << "    \"processor\": \"" << ROR_OGRE_NEXT_SYSTEM_PROCESSOR << "\",\n"
               << "    \"compiler\": \"" << ROR_OGRE_NEXT_COMPILER_ID << ' '
               << ROR_OGRE_NEXT_COMPILER_VERSION << "\",\n"
               << "    \"cxx_standard\": 17,\n"
               << "    \"pointer_bits\": " << sizeof( void * ) * 8u << ",\n"
               << "    \"static_link\": true,\n"
               << "    \"abi_cookie\": \"" << std::hex << std::setfill( '0' )
               << std::setw( 16 ) << abiCookie.val[0] << std::setw( 16 ) << abiCookie.val[1]
               << std::dec << "\",\n"
               << "    \"debug_mode\": " << OGRE_DEBUG_MODE << ",\n"
               << "    \"double_precision\": "
               << ( OGRE_DOUBLE_PRECISION ? "true" : "false" ) << ",\n"
               << "    \"memory_allocator\": " << OGRE_MEMORY_ALLOCATOR << ",\n"
               << "    \"container_custom_allocator\": "
               << ( OGRE_CONTAINERS_USE_CUSTOM_MEMORY_ALLOCATOR ? "true" : "false" ) << ",\n"
               << "    \"string_custom_allocator\": "
               << ( OGRE_STRING_USE_CUSTOM_MEMORY_ALLOCATOR ? "true" : "false" ) << ",\n"
               << "    \"thread_support\": " << OGRE_THREAD_SUPPORT << ",\n"
               << "    \"thread_provider\": " << OGRE_THREAD_PROVIDER << ",\n"
               << "    \"id_string_bits\": " << OGRE_HASH_BITS << ",\n"
               << "    \"id_string_size\": " << sizeof( Ogre::IdString ) << ",\n"
               << "    \"flexibility_level\": " << OGRE_FLEXIBILITY_LEVEL << ",\n"
               << "    \"simd_alignment\": " << OGRE_SIMD_ALIGNMENT << ",\n"
               << "    \"use_simd\": " << OGRE_USE_SIMD << ",\n"
               << "    \"restrict_aliasing\": " << OGRE_RESTRICT_ALIASING << ",\n"
               << "    \"assert_mode\": " << OGRE_ASSERT_MODE << "\n"
               << "  },\n"
               << "  \"capabilities\": {\n"
               << "    \"renderer\": {\n"
               << "      \"target\": \"" << ROR_OGRE_NEXT_RENDERER_TARGET << "\",\n"
               << "      \"name\": \"" << jsonEscape( rendererName ) << "\",\n"
               << "      \"registered\": " << ( rendererRegistered ? "true" : "false" ) << ",\n"
               << "      \"registered_renderer_count\": " << rendererCount << ",\n"
               << "      \"configuration_option_count\": " << rendererOptionCount << ",\n"
               << "      \"device_option_name\": \"" << ROR_OGRE_NEXT_DEVICE_OPTION_NAME
               << "\",\n"
               << "      \"reported_device_count\": " << reportedDeviceCount << ",\n"
               << "      \"first_reported_device\": \""
               << jsonEscape( firstReportedDevice ) << "\"\n"
               << "    },\n"
               << "    \"hlms_pbs\": {\n"
               << "      \"compiled_and_linked\": true,\n"
               << "      \"shader_data_path\": \"" << jsonEscape( pbsDataPath ) << "\",\n"
               << "      \"shader_path_matches_policy\": "
               << ( pbsShaderPathMatches ? "true" : "false" ) << ",\n"
               << "      \"library_path_count\": " << pbsLibraryPathCount << "\n"
               << "    },\n"
               << "    \"compositor2\": {\n"
               << "      \"compiled_and_linked\": true,\n"
               << "      \"runtime_initialization\": \"deferred_until_real_window\",\n"
               << "      \"deferred_contract_observed\": "
               << ( compositorManagerDeferred ? "true" : "false" ) << "\n"
               << "    },\n"
               << "    \"native_ray_tracing\": \"not_evaluated\"\n"
               << "  }\n"
               << "}\n";
        return report.str();
    }
}  // namespace

int main( int argc, char **argv )
{
    try
    {
        std::string outputPath;
        if( argc == 3 && std::string( argv[1] ) == "--output" )
            outputPath = argv[2];
        else if( argc != 1 )
            throw std::runtime_error( "usage: ror_ogre_next_probe [--output REPORT.json]" );

        const std::string report = makeReport();
        if( !outputPath.empty() )
        {
            std::ofstream output( outputPath, std::ios::binary | std::ios::trunc );
            if( !output )
                throw std::runtime_error( "could not open report output path" );
            output << report;
            if( !output )
                throw std::runtime_error( "could not write report output path" );
        }
        std::cout << report;
        return 0;
    }
    catch( const Ogre::Exception &error )
    {
        std::cerr << "OGRE-Next probe failed: " << error.getFullDescription() << '\n';
    }
    catch( const std::exception &error )
    {
        std::cerr << "OGRE-Next probe failed: " << error.what() << '\n';
    }
    return 1;
}
