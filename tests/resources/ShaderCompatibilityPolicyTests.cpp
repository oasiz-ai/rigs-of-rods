#include "ShaderCompatibilityPolicy.h"

#include <cstdlib>
#include <iostream>

namespace
{

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

void TestUnboundAndSupportedProgramsArePreserved()
{
    CHECK(!RoR::NeedsGeneratedShaderFallback({false, false, false, false}));
    CHECK(!RoR::NeedsGeneratedShaderFallback({true, true, true, false}));
}

void TestMissingOrUnsupportedProgramsUseGeneratedShaders()
{
    CHECK(RoR::NeedsGeneratedShaderFallback({true, false, false, false}));
    CHECK(RoR::NeedsGeneratedShaderFallback({true, true, false, false}));
    CHECK(RoR::NeedsGeneratedShaderFallback({true, true, true, true}));
}

void TestCompileErrorsDoNotAffectUnboundStages()
{
    CHECK(!RoR::NeedsGeneratedShaderFallback({false, true, true, true}));
}

void TestIncompleteGraphicsPipelinesUseGeneratedShaders()
{
    CHECK(RoR::NeedsGeneratedShaderFallbackForIncompletePipeline(
        true, {true, true, false, false, false, false}));
    CHECK(RoR::NeedsGeneratedShaderFallbackForIncompletePipeline(
        true, {true, false, true, false, false, false}));
    CHECK(RoR::NeedsGeneratedShaderFallbackForIncompletePipeline(
        true, {true, false, false, false, false, false}));
    CHECK(RoR::NeedsGeneratedShaderFallbackForIncompletePipeline(
        true, {true, false, false, true, false, false}));
    CHECK(RoR::NeedsGeneratedShaderFallbackForIncompletePipeline(
        true, {true, true, false, true, false, false}));

    CHECK(!RoR::NeedsGeneratedShaderFallbackForIncompletePipeline(
        true, {false, false, false, false, false, false}));
    CHECK(!RoR::NeedsGeneratedShaderFallbackForIncompletePipeline(
        true, {true, true, true, false, false, false}));
    CHECK(!RoR::NeedsGeneratedShaderFallbackForIncompletePipeline(
        true, {true, false, true, true, true, false}));
    CHECK(!RoR::NeedsGeneratedShaderFallbackForIncompletePipeline(
        true, {true, false, false, false, false, true}));
    CHECK(!RoR::NeedsGeneratedShaderFallbackForIncompletePipeline(
        false, {true, true, false, false, false, false}));
}

void TestCompatibleTechniqueOnlySuppressesItsOwnScheme()
{
    const std::vector<RoR::ShaderTechniqueCompatibility> techniques = {
        {"Default", false},
        {"ShadowCaster", true},
        {"Default", true}};

    CHECK(RoR::HasCompatibleShaderTechniqueForScheme(
        techniques, "Default"));
    CHECK(RoR::HasCompatibleShaderTechniqueForScheme(
        techniques, "ShadowCaster"));
    CHECK(!RoR::HasCompatibleShaderTechniqueForScheme(
        techniques, "DepthOnly"));

    CHECK(!RoR::ShouldRepairIncompatibleShaderPass(true, true));
    CHECK(!RoR::ShouldRepairIncompatibleShaderPass(false, false));
    CHECK(RoR::ShouldRepairIncompatibleShaderPass(false, true));
}

void TestAlternateSchemeCannotHideBrokenDefaultSource()
{
    const std::vector<RoR::ShaderTechniqueCompatibility> techniques = {
        {"Default", false},
        {"ShadowCaster", true},
        {"HydraxDepth", true}};

    const bool default_has_compatible_technique =
        RoR::HasCompatibleShaderTechniqueForScheme(
            techniques, "Default");
    CHECK(!default_has_compatible_technique);
    CHECK(RoR::ShouldRepairIncompatibleShaderPass(
        default_has_compatible_technique, true));
}

void TestScriptOwnershipFollowsOrderedArchiveOccurrences()
{
    const std::vector<RoR::ScriptArchiveState> locations = {
        {0, true},
        {2, true},
        {1, false},
        {1, true}};

    CHECK(RoR::IsPackageOwnedScriptOccurrence(locations, 0));
    CHECK(RoR::IsPackageOwnedScriptOccurrence(locations, 1));
    CHECK(!RoR::IsPackageOwnedScriptOccurrence(locations, 2));
    CHECK(RoR::IsPackageOwnedScriptOccurrence(locations, 3));
    CHECK(!RoR::IsPackageOwnedScriptOccurrence(locations, 4));
}

} // namespace

int main()
{
    TestUnboundAndSupportedProgramsArePreserved();
    TestMissingOrUnsupportedProgramsUseGeneratedShaders();
    TestCompileErrorsDoNotAffectUnboundStages();
    TestIncompleteGraphicsPipelinesUseGeneratedShaders();
    TestCompatibleTechniqueOnlySuppressesItsOwnScheme();
    TestAlternateSchemeCannotHideBrokenDefaultSource();
    TestScriptOwnershipFollowsOrderedArchiveOccurrences();
    return EXIT_SUCCESS;
}
