#include <OgreArchive.h>
#include <OgreArchiveFactory.h>
#include <OgreArchiveManager.h>
#include <OgreDataStream.h>
#include <OgreException.h>
#include <OgreRoot.h>
#include <OgreRenderSystem.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include <atomic>
#include <cstdint>
#include <ctime>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace
{
class FailingArchive final : public Ogre::Archive
{
public:
    FailingArchive(const Ogre::String& name, const Ogre::String& type):
        Ogre::Archive(name, type)
    {
    }

    bool isCaseSensitive() const override { return true; }
    void load() override
    {
        OGRE_EXCEPT(
            Ogre::Exception::ERR_INVALID_STATE,
            "intentional archive-load rollback probe",
            "FailingArchive::load");
    }
    void unload() override {}
    Ogre::DataStreamPtr open(
        const Ogre::String&, bool = true) const override
    {
        return Ogre::DataStreamPtr();
    }
    Ogre::StringVectorPtr list(bool = true, bool = false) const override
    {
        return Ogre::StringVectorPtr(OGRE_NEW Ogre::StringVector());
    }
    Ogre::FileInfoListPtr listFileInfo(
        bool = true, bool = false) const override
    {
        return Ogre::FileInfoListPtr(OGRE_NEW Ogre::FileInfoList());
    }
    Ogre::StringVectorPtr find(
        const Ogre::String&, bool = true, bool = false) const override
    {
        return Ogre::StringVectorPtr(OGRE_NEW Ogre::StringVector());
    }
    bool exists(const Ogre::String&) const override { return false; }
    std::time_t getModifiedTime(const Ogre::String&) const override
    {
        return 0;
    }
    Ogre::FileInfoListPtr findFileInfo(
        const Ogre::String&, bool = true, bool = false) const override
    {
        return Ogre::FileInfoListPtr(OGRE_NEW Ogre::FileInfoList());
    }
};

class FailingArchiveFactory final : public Ogre::ArchiveFactory
{
public:
    const Ogre::String& getType() const override
    {
        static const Ogre::String TYPE = "RorArchiveLoadRollbackProbe";
        return TYPE;
    }
    Ogre::Archive* createInstance(
        const Ogre::String& name, bool) override
    {
        ++create_count;
        return OGRE_NEW FailingArchive(name, getType());
    }
    void destroyInstance(Ogre::Archive* archive) override
    {
        ++destroy_count;
        OGRE_DELETE archive;
    }

    std::size_t create_count = 0U;
    std::size_t destroy_count = 0U;
};

bool VerifyArchiveManagerLoadRollback()
{
    static FailingArchiveFactory factory;
    static bool registered = false;
    if (!registered)
    {
        Ogre::ArchiveManager::getSingleton().addArchiveFactory(&factory);
        registered = true;
    }

    const Ogre::String archive_name =
        "ror-archive-manager-load-rollback-probe";
    const std::size_t initial_creates = factory.create_count;
    const std::size_t initial_destroys = factory.destroy_count;
    for (std::size_t attempt = 0U; attempt < 2U; ++attempt)
    {
        bool threw = false;
        try
        {
            (void)Ogre::ArchiveManager::getSingleton().load(
                archive_name, factory.getType(), true);
        }
        catch (const Ogre::Exception&)
        {
            threw = true;
        }
        if (!threw)
        {
            std::cerr << "failing archive unexpectedly loaded\n";
            return false;
        }

        Ogre::ArchiveManager::ArchiveMapIterator archives =
            Ogre::ArchiveManager::getSingleton().getArchiveIterator();
        while (archives.hasMoreElements())
        {
            if (archives.peekNextKey() == archive_name)
            {
                std::cerr
                    << "failed archive remained published after throw\n";
                return false;
            }
            archives.getNext();
        }
    }
    if (factory.create_count != initial_creates + 2U ||
        factory.destroy_count != initial_destroys + 2U)
    {
        std::cerr
            << "ArchiveManager did not return failed instances to factory\n";
        return false;
    }
    return true;
}

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

bool VerifyConcurrentZipReads(const std::string& zip_path)
{
    constexpr std::size_t thread_count = 8;
    constexpr std::size_t iterations_per_thread = 500;
    Ogre::Archive* archive = nullptr;

    try
    {
        archive = Ogre::ArchiveManager::getSingleton().load(
            zip_path,
            "Zip",
            true);
        std::atomic<std::size_t> ready_threads{0};
        std::atomic<bool> start{false};
        std::atomic<bool> failed{false};
        std::vector<std::thread> threads;
        threads.reserve(thread_count);

        for (std::size_t thread_index = 0;
             thread_index < thread_count;
             ++thread_index)
        {
            threads.emplace_back(
                [archive, thread_index, &ready_threads, &start, &failed]()
                {
                    ready_threads.fetch_add(1, std::memory_order_release);
                    while (!start.load(std::memory_order_acquire))
                    {
                        std::this_thread::yield();
                    }

                    try
                    {
                        for (std::size_t iteration = 0;
                             iteration < iterations_per_thread;
                             ++iteration)
                        {
                            const bool select_alpha =
                                ((thread_index + iteration) % 2) == 0;
#if OGRE_RESOURCEMANAGER_STRICT
                            const Ogre::String filename = select_alpha
                                ? "nested/alpha.txt"
                                : "other/beta.txt";
#else
                            // The basename deliberately misses the first
                            // zip_entry_open(), forcing open() to call the
                            // separately locked findFileInfo() helper.
                            const Ogre::String filename = select_alpha
                                ? "alpha.txt"
                                : "beta.txt";
#endif
                            const Ogre::String expected = select_alpha
                                ? "alpha-payload\n"
                                : "beta-payload\n";
                            const Ogre::DataStreamPtr stream =
                                archive->open(filename);
                            if (stream == nullptr ||
                                stream->getAsString() != expected ||
                                !archive->exists(filename) ||
                                archive->list(true, false)->size() != 2 ||
                                archive->listFileInfo(true, false)->size() != 2 ||
                                archive->find(
                                    "*.txt",
                                    true,
                                    false)->size() != 2 ||
                                archive->findFileInfo(
                                    "*.txt",
                                    true,
                                    false)->size() != 2)
                            {
                                failed.store(
                                    true,
                                    std::memory_order_relaxed);
                                return;
                            }
                        }
                    }
                    catch (const std::exception&)
                    {
                        failed.store(true, std::memory_order_relaxed);
                    }
                });
        }

        while (ready_threads.load(std::memory_order_acquire) != thread_count)
        {
            std::this_thread::yield();
        }
        start.store(true, std::memory_order_release);
        for (std::thread& thread : threads)
        {
            thread.join();
        }

        Ogre::ArchiveManager::getSingleton().unload(archive);
        archive = nullptr;
        if (failed.load(std::memory_order_relaxed))
        {
            std::cerr << "concurrent ZIP archive probe failed\n";
            return false;
        }
    }
    catch (const std::exception& error)
    {
        if (archive != nullptr)
        {
            Ogre::ArchiveManager::getSingleton().unload(archive);
        }
        std::cerr << "ZIP archive probe failed: " << error.what() << '\n';
        return false;
    }

    return true;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 5)
    {
        std::cerr << "usage: ogre_recipe_probe PLUGINS_CFG RENDERER_TOKEN "
                     "RELOCATED_PREFIX ZIP_ARCHIVE\n";
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
                if (!VerifyConcurrentZipReads(argv[4]))
                {
                    return 1;
                }
                if (!VerifyArchiveManagerLoadRollback())
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
