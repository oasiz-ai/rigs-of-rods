#include "BeamNGOgreScriptPolicy.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

#define CHECK(condition)                                                        \
    do                                                                          \
    {                                                                           \
        if (!(condition))                                                       \
        {                                                                       \
            std::cerr << "CHECK failed at " << __FILE__ << ':' << __LINE__     \
                      << ": " #condition << '\n';                               \
            std::exit(EXIT_FAILURE);                                            \
        }                                                                       \
    } while (false)

void TestExecutableOgreScriptSuffixesAreRejected()
{
    const std::array<const char*, 13U> exact_names = {{
        ".material",
        ".MaTeRiAl",
        ".material.json",
        ".MaTeRiAl.JsOn",
        ".program",
        ".compositor",
        ".particle",
        ".overlay",
        ".fontdef",
        ".os",
        ".soundscript",
        ".shader",
        ".cgfx",
    }};
    for (const char* exact_name : exact_names)
    {
        CHECK(RoR::BeamNG::HasUnsafeOgreScriptSuffix(exact_name));
        CHECK(RoR::BeamNG::HasUnsafeOgreScriptSuffix(
            std::string("vehicles/test/hostile") + exact_name));
    }
}

void TestNonScriptSuffixesRemainAllowed()
{
    const std::array<const char*, 12U> allowed_names = {{
        "",
        ".",
        "material",
        ".materialx",
        "vehicle.materials.json",
        ".programs",
        ".compositor.json",
        ".particlex",
        ".ost",
        ".soundscripts",
        ".shader.json",
        ".cgfx2",
    }};
    for (const char* allowed_name : allowed_names)
    {
        CHECK(!RoR::BeamNG::HasUnsafeOgreScriptSuffix(allowed_name));
    }
}

} // namespace

int main()
{
    TestExecutableOgreScriptSuffixesAreRejected();
    TestNonScriptSuffixesRemainAllowed();
    std::cout << "BeamNG Ogre script policy checks passed\n";
    return EXIT_SUCCESS;
}
