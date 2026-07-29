#include <OgreConfigFile.h>
#include <OgreDataStream.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define CHECK(condition)                                                        \
    do                                                                          \
    {                                                                           \
        if (!(condition))                                                       \
        {                                                                       \
            std::cerr << __FILE__ << ":" << __LINE__                           \
                      << ": check failed: " #condition << "\n";                \
            ++failures;                                                         \
        }                                                                       \
    } while (false)

void TestResourceBundleQualifierSurvivesConfigParsing()
{
    std::string fixture =
        "[ResourceBundles]\n"
        "Dependency = CityWorld.zip:CityWorld.terrn2:"
        "ebeac2f0204f25ca1955f29ca1583b2afa4517a3a848feb1db203814acac2ef3\n"
        "Dependency = Shared.zip:Shared.terrn2:"
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n";
    Ogre::DataStreamPtr stream(new Ogre::MemoryDataStream(
        "terrain-resource-bundles.terrn2",
        &fixture[0],
        fixture.size(),
        false,
        true));

    Ogre::ConfigFile config;
    config.load(stream, "\t:=", true);

    const Ogre::ConfigFile::SettingsMultiMap& settings =
        config.getSettings("ResourceBundles");
    CHECK(settings.size() == 2U);
    const std::pair<
        Ogre::ConfigFile::SettingsMultiMap::const_iterator,
        Ogre::ConfigFile::SettingsMultiMap::const_iterator> dependencies =
        settings.equal_range("Dependency");

    std::vector<std::string> values;
    for (Ogre::ConfigFile::SettingsMultiMap::const_iterator iterator =
             dependencies.first;
         iterator != dependencies.second;
         ++iterator)
    {
        values.push_back(iterator->second);
    }
    CHECK(values.size() == 2U);
    if (values.size() == 2U)
    {
        CHECK(values[0] ==
            "CityWorld.zip:CityWorld.terrn2:"
            "ebeac2f0204f25ca1955f29ca1583b2afa4517a3a848feb1db203814acac2ef3");
        CHECK(values[1] ==
            "Shared.zip:Shared.terrn2:"
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    }
    CHECK(settings.count("CityWorld.zip") == 0U);
    CHECK(settings.count("Shared.zip") == 0U);
}

} // namespace

int main()
{
    TestResourceBundleQualifierSurvivesConfigParsing();
    if (failures != 0)
    {
        std::cerr << failures
                  << " terrain dependency config syntax checks failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
