#include <OgreRoot.h>
#include <OgreRenderSystem.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

namespace
{
bool IsOgreImage(const std::string& path)
{
    const std::size_t slash = path.find_last_of("/\\");
    const std::string filename =
        slash == std::string::npos ? path : path.substr(slash + 1);
    return filename.find("libOgre") == 0 ||
        filename.find("RenderSystem_") == 0 ||
        filename.find("Plugin_") == 0 ||
        filename.find("Codec_") == 0;
}

bool IsUnderPrefix(const std::string& path, std::string prefix)
{
    if (prefix.empty())
    {
        return false;
    }
    if (prefix.back() != '/')
    {
        prefix.push_back('/');
    }
    return path.size() > prefix.size() &&
        path.compare(0, prefix.size(), prefix) == 0;
}

#if defined(__APPLE__)
bool CanonicalPath(
    const std::string& input,
    std::string& canonical_path)
{
    std::error_code error;
    const std::filesystem::path resolved =
        std::filesystem::canonical(input, error);
    if (error || resolved.empty())
    {
        std::cerr << "cannot canonicalize existing path " << input
                  << ": " << error.message() << '\n';
        return false;
    }
    canonical_path = resolved.generic_string();
    return true;
}
#endif

bool VerifyLoadedOgreImages(const std::string& expected_prefix)
{
#if defined(__APPLE__)
    std::string canonical_prefix;
    if (!CanonicalPath(expected_prefix, canonical_prefix))
    {
        return false;
    }
    bool found_core = false;
    bool found_plugin = false;
    const std::uint32_t image_count = _dyld_image_count();
    for (std::uint32_t index = 0; index < image_count; ++index)
    {
        const char* const image_name = _dyld_get_image_name(index);
        if (image_name == nullptr)
        {
            continue;
        }
        const std::string path(image_name);
        if (!IsOgreImage(path))
        {
            continue;
        }
        std::string canonical_image;
        if (!CanonicalPath(path, canonical_image))
        {
            return false;
        }
        if (!IsUnderPrefix(canonical_image, canonical_prefix))
        {
            std::cerr << "OGRE image escaped relocated package: "
                      << canonical_image << '\n';
            return false;
        }
        const std::size_t slash = canonical_image.find_last_of('/');
        const std::string filename =
            slash == std::string::npos
            ? canonical_image
            : canonical_image.substr(slash + 1);
        found_core = found_core || filename.find("libOgreMain") == 0;
        found_plugin = found_plugin ||
            filename.find("RenderSystem_") == 0;
    }
    if (!found_core || !found_plugin)
    {
        std::cerr << "relocated runtime did not load OGRE core and renderer\n";
        return false;
    }
#else
    (void)expected_prefix;
#endif
    return true;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 4)
    {
        std::cerr << "usage: ogre_recipe_probe PLUGINS_CFG RENDERER_TOKEN "
                     "RELOCATED_PREFIX\n";
        return 2;
    }

    try
    {
        Ogre::Root root(argv[1], "", "");
        const Ogre::RenderSystemList& renderers =
            root.getAvailableRenderers();
        for (Ogre::RenderSystem* renderer : renderers)
        {
            if (renderer != nullptr &&
                renderer->getName().find(argv[2]) != std::string::npos)
            {
                root.setRenderSystem(renderer);
                root.initialise(false);
                if (!VerifyLoadedOgreImages(argv[3]))
                {
                    return 1;
                }
                std::cout << renderer->getName() << '\n';
                return 0;
            }
        }
        std::cerr << "renderer token not registered: " << argv[2] << '\n';
    }
    catch (const std::exception& error)
    {
        std::cerr << "OGRE recipe probe failed: " << error.what() << '\n';
    }
    return 1;
}
