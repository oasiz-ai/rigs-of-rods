/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "OgreNextDemoPrivatePolicy.h"

#include "gfx/render/RenderPayloadDigest.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <utility>

namespace RoR::Gfx::Detail {
namespace {

constexpr std::uint64_t kFnv1a64OffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnv1a64Prime = 1099511628211ULL;

constexpr std::array<OgreNextDemoCuratedCityWorldMaterial,
                     kOgreNextDemoCuratedCityWorldAsiaPolicyEntryCount>
    kCuratedCityWorldAsiaMaterials{{
        {"Material_#58/asiafacade",
         "8edac31773f79238db4f836fa33642db0ef6cde614e4a8cc1dd4f5d1416ec7eb",
         1330U, 1763U,
         "317b5e0c7462c1e0f9d4321487e177a1ca4eb487a7c77b54e2c4e6355034157c",
         "asiafacade.dds", "asiafacade_spec.dds", "767chrome.jpg",
         OgreNextDemoCuratedCityWorldWorkflow::SPECULAR, 0U, 1U, 2U,
         0.48F, {1.0F, 1.0F, 1.0F}, 1.50F,
         OgreNextDemoCuratedCityWorldAlphaPolicy::FORCE_OPAQUE, true, true,
         OgreNextDemoCuratedCityWorldSamplerPolicy::
             REVIEWED_CONFIGURED_ANISOTROPIC4_V1,
         OgreNextDemoCuratedCityWorldEnvironmentPolicy::
             SPHERICAL_AUTHORITY_BOUND_PENDING_NOT_PRESENTED},
        {"Material_#58/asiawindow",
         "180e624782c8b4aefb966ef04ecf5d610ab753004fa40d4e87ff5ac4ee6a2137",
         162U, 595U,
         "fc370571849e3dee88ae7e119b5a8ea1979f0fed147c7f97b457022a849f7769",
         "asiawindow.dds", "asiawindow_spec.dds", "767chrome.jpg",
         OgreNextDemoCuratedCityWorldWorkflow::SPECULAR, 0U, 1U, 2U,
         0.12F, {1.0F, 1.0F, 1.0F}, 1.52F,
         OgreNextDemoCuratedCityWorldAlphaPolicy::FORCE_OPAQUE, true, true,
         OgreNextDemoCuratedCityWorldSamplerPolicy::
             REVIEWED_CONFIGURED_ANISOTROPIC4_V1,
         OgreNextDemoCuratedCityWorldEnvironmentPolicy::
             SPHERICAL_AUTHORITY_BOUND_PENDING_NOT_PRESENTED},
        {"Material_#58/asiamarble",
         "8e5b1d6fafd227d4a7968abc8c1dd689d09cf951ce44e513803160c7abc8928f",
         903U, 1328U,
         "fc25121c77de533f3e8da7d9c64915f3e228adfe23062af570d81689392bacad",
         "marble.dds", "marble_spec.dds", "767chrome.jpg",
         OgreNextDemoCuratedCityWorldWorkflow::SPECULAR, 0U, 1U, 2U,
         0.27F, {1.0F, 1.0F, 1.0F}, 1.50F,
         OgreNextDemoCuratedCityWorldAlphaPolicy::FORCE_OPAQUE, true, true,
         OgreNextDemoCuratedCityWorldSamplerPolicy::
             REVIEWED_CONFIGURED_ANISOTROPIC4_V1,
         OgreNextDemoCuratedCityWorldEnvironmentPolicy::
             SPHERICAL_AUTHORITY_BOUND_PENDING_NOT_PRESENTED},
    }};

std::string DigestHex(const Render::RenderPayloadDigest &digest) {
  constexpr char kHex[] = "0123456789abcdef";
  std::string result;
  result.resize(digest.size() * 2U);
  for (std::size_t index = 0U; index < digest.size(); ++index) {
    result[index * 2U] = kHex[digest[index] >> 4U];
    result[index * 2U + 1U] = kHex[digest[index] & 0x0fU];
  }
  return result;
}

bool IsLowercaseSha256(std::string_view value) noexcept {
  return value.size() == 64U &&
         std::all_of(value.begin(), value.end(), [](char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

Render::ValidationResult
Failure(Render::ValidationCode code, const char *field, const char *detail,
        std::size_t index = Render::ValidationResult::kNoElement) {
  return Render::ValidationResult::Failure(code, field, detail, index);
}

std::uint32_t CompleteMipCount(std::uint32_t width,
                               std::uint32_t height) noexcept {
  std::uint32_t count = 1U;
  while (width > 1U || height > 1U) {
    width = (std::max)(1U, width / 2U);
    height = (std::max)(1U, height / 2U);
    ++count;
  }
  return count;
}

// Frozen Q0.32 decode points for the 256 sRGB byte codes. This table is part
// of modern source-normalization policy v2 and removes libm/compiler drift
// from generated mip bytes.
constexpr std::array<std::uint32_t, 256U> kSrgbLinearQ32 = {
    0U,          1303638U,    2607277U,    3910915U,    5214554U,
    6518192U,    7821831U,    9125469U,    10429108U,   11732746U,
    13036385U,   14373262U,   15790479U,   17286028U,   18861100U,
    20516859U,   22254445U,   24074974U,   25979540U,   27969217U,
    30045058U,   32208097U,   34459351U,   36799819U,   39230483U,
    41752310U,   44366252U,   47073245U,   49874213U,   52770066U,
    55761699U,   58849999U,   62035836U,   65320072U,   68703557U,
    72187129U,   75771618U,   79457840U,   83246606U,   87138715U,
    91134956U,   95236110U,   99442951U,   103756241U,  108176738U,
    112705190U,  117342336U,  122088910U,  126945637U,  131913236U,
    136992419U,  142183890U,  147488347U,  152906483U,  158438983U,
    164086527U,  169849787U,  175729433U,  181726125U,  187840520U,
    194073270U,  200425020U,  206896410U,  213488075U,  220200647U,
    227034750U,  233991006U,  241070029U,  248272432U,  255598822U,
    263049800U,  270625965U,  278327911U,  286156227U,  294111500U,
    302194310U,  310405235U,  318744849U,  327213722U,  335812420U,
    344541505U,  353401536U,  362393069U,  371516655U,  380772844U,
    390162179U,  399685202U,  409342452U,  419134464U,  429061771U,
    439124900U,  449324378U,  459660727U,  470134468U,  480746117U,
    491496188U,  502385192U,  513413639U,  524582032U,  535890876U,
    547340670U,  558931911U,  570665096U,  582540715U,  594559259U,
    606721215U,  619027069U,  631477301U,  644072393U,  656812821U,
    669699062U,  682731588U,  695910869U,  709237375U,  722711571U,
    736333921U,  750104887U,  764024929U,  778094504U,  792314068U,
    806684074U,  821204974U,  835877217U,  850701250U,  865677518U,
    880806466U,  896088534U,  911524162U,  927113788U,  942857848U,
    958756776U,  974811004U,  991020962U,  1007387079U, 1023909783U,
    1040589497U, 1057426646U, 1074421651U, 1091574933U, 1108886909U,
    1126357997U, 1143988611U, 1161779166U, 1179730072U, 1197841740U,
    1216114580U, 1234548997U, 1253145399U, 1271904188U, 1290825768U,
    1309910539U, 1329158902U, 1348571255U, 1368147994U, 1387889515U,
    1407796211U, 1427868476U, 1448106700U, 1468511273U, 1489082583U,
    1509821018U, 1530726963U, 1551800803U, 1573042920U, 1594453696U,
    1616033513U, 1637782748U, 1659701780U, 1681790986U, 1704050740U,
    1726481418U, 1749083391U, 1771857033U, 1794802712U, 1817920800U,
    1841211663U, 1864675668U, 1888313183U, 1912124570U, 1936110194U,
    1960270418U, 1984605601U, 2009116105U, 2033802289U, 2058664510U,
    2083703126U, 2108918491U, 2134310962U, 2159880890U, 2185628630U,
    2211554532U, 2237658948U, 2263942226U, 2290404714U, 2317046762U,
    2343868714U, 2370870916U, 2398053713U, 2425417448U, 2452962464U,
    2480689102U, 2508597703U, 2536688606U, 2564962150U, 2593418672U,
    2622058510U, 2650882000U, 2679889476U, 2709081272U, 2738457721U,
    2768019156U, 2797765908U, 2827698308U, 2857816685U, 2888121367U,
    2918612683U, 2949290959U, 2980156522U, 3011209696U, 3042450807U,
    3073880178U, 3105498131U, 3137304989U, 3169301072U, 3201486702U,
    3233862196U, 3266427875U, 3299184055U, 3332131054U, 3365269189U,
    3398598774U, 3432120125U, 3465833555U, 3499739378U, 3533837906U,
    3568129450U, 3602614323U, 3637292832U, 3672165289U, 3707232002U,
    3742493279U, 3777949427U, 3813600752U, 3849447560U, 3885490157U,
    3921728847U, 3958163932U, 3994795717U, 4031624504U, 4068650594U,
    4105874287U, 4143295885U, 4180915686U, 4218733989U, 4256751093U,
    4294967295U,
};

// D((code + 0.5) / 255) in the same Q0.32 domain. These are the exact
// round-to-nearest encoded-code decision boundaries for this frozen table;
// equality selects the higher code.
constexpr std::array<std::uint32_t, 255U> kSrgbEncodeBoundaryQ32 = {
    651819U,     1955458U,    3259096U,    4562735U,    5866373U,
    7170012U,    8473650U,    9777289U,    11080927U,   12384565U,
    13693648U,   15072154U,   16528387U,   18063550U,   19678822U,
    21375353U,   23154272U,   25016685U,   26963673U,   28996302U,
    31115614U,   33322635U,   35618372U,   38003816U,   40479942U,
    43047708U,   45708059U,   48461925U,   51310223U,   54253854U,
    57293711U,   60430671U,   63665601U,   66999356U,   70432780U,
    73966707U,   77601961U,   81339355U,   85179693U,   89123770U,
    93172370U,   97326272U,   101586242U,  105953042U,  110427423U,
    115010130U,  119701899U,  124503459U,  129415533U,  134438835U,
    139574074U,  144821952U,  150183162U,  155658395U,  161248332U,
    166953650U,  172775020U,  178713107U,  184768569U,  190942060U,
    197234230U,  203645720U,  210177168U,  216829209U,  223602468U,
    230497570U,  237515133U,  244655770U,  251920091U,  259308700U,
    266822197U,  274461179U,  282226236U,  290117958U,  298136927U,
    306283722U,  314558920U,  322963093U,  331496807U,  340160629U,
    348955117U,  357880830U,  366938321U,  376128140U,  385450834U,
    394906945U,  404497015U,  414221580U,  424081173U,  434076324U,
    444207562U,  454475411U,  464880391U,  475423022U,  486103818U,
    496923292U,  507881954U,  518980311U,  530218866U,  541598123U,
    553118578U,  564780730U,  576585070U,  588532091U,  600622280U,
    612856125U,  625234107U,  637756710U,  650424410U,  663237686U,
    676197010U,  689302855U,  702555690U,  715955982U,  729504197U,
    743200798U,  757046245U,  771040996U,  785185509U,  799480238U,
    813925634U,  828522150U,  843270232U,  858170327U,  873222879U,
    888428332U,  903787126U,  919299698U,  934966487U,  950787927U,
    966764450U,  982896490U,  999184474U,  1015628831U, 1032229987U,
    1048988366U, 1065904391U, 1082978482U, 1100211058U, 1117602538U,
    1135153338U, 1152863870U, 1170734549U, 1188765785U, 1206957988U,
    1225311566U, 1243826925U, 1262504470U, 1281344604U, 1300347730U,
    1319514247U, 1338844555U, 1358339051U, 1377998132U, 1397822191U,
    1417811623U, 1437966818U, 1458288168U, 1478776061U, 1499430886U,
    1520253028U, 1541242872U, 1562400803U, 1583727202U, 1605222451U,
    1626886929U, 1648721016U, 1670725088U, 1692899521U, 1715244690U,
    1737760969U, 1760448730U, 1783308345U, 1806340182U, 1829544611U,
    1852922000U, 1876472714U, 1900197120U, 1924095580U, 1948168458U,
    1972416117U, 1996838916U, 2021437215U, 2046211373U, 2071161746U,
    2096288693U, 2121592566U, 2147073722U, 2172732512U, 2198569289U,
    2224584404U, 2250778207U, 2277151047U, 2303703272U, 2330435228U,
    2357347262U, 2384439719U, 2411712942U, 2439167275U, 2466803059U,
    2494620636U, 2522620345U, 2550802526U, 2579167517U, 2607715656U,
    2636447278U, 2665362719U, 2694462313U, 2723746394U, 2753215295U,
    2782869347U, 2812708882U, 2842734229U, 2872945717U, 2903343675U,
    2933928430U, 2964700309U, 2995659637U, 3026806739U, 3058141940U,
    3089665562U, 3121377927U, 3153279357U, 3185370174U, 3217650696U,
    3250121242U, 3282782132U, 3315633683U, 3348676210U, 3381910031U,
    3415335459U, 3448952811U, 3482762398U, 3516764534U, 3550959532U,
    3585347701U, 3619929353U, 3654704798U, 3689674345U, 3724838301U,
    3760196975U, 3795750673U, 3831499702U, 3867444366U, 3903584971U,
    3939921821U, 3976455219U, 4013185467U, 4050112867U, 4087237721U,
    4124560329U, 4162080991U, 4199800006U, 4237717672U, 4275834288U,
};

std::uint8_t EncodeSrgbQ32(std::uint32_t linear) noexcept {
  std::uint8_t best = 0U;
  for (std::size_t index = 0U; index < kSrgbEncodeBoundaryQ32.size(); ++index) {
    if (linear >= kSrgbEncodeBoundaryQ32[index]) {
      best = static_cast<std::uint8_t>(index + 1U);
    } else {
      break;
    }
  }
  return best;
}

std::uint8_t EncodeSrgbQ32Average(std::uint64_t linear_sum) noexcept {
  std::uint8_t best = 0U;
  for (std::size_t index = 0U; index < kSrgbEncodeBoundaryQ32.size(); ++index) {
    if (linear_sum >=
        static_cast<std::uint64_t>(kSrgbEncodeBoundaryQ32[index]) * 4U) {
      best = static_cast<std::uint8_t>(index + 1U);
    } else {
      break;
    }
  }
  return best;
}

std::size_t SaturatingAdd(std::size_t lhs, std::size_t rhs) noexcept {
  const std::size_t maximum = (std::numeric_limits<std::size_t>::max)();
  return lhs > maximum - rhs ? maximum : lhs + rhs;
}

bool HasZeroGpuReadbacks(
    const OgreNextDemoTextureSourceCounters &counters) noexcept {
  return counters.gpu_readbacks == 0U &&
         counters.authenticated_gpu_readbacks == 0U &&
         counters.unauthenticated_gpu_readbacks == 0U;
}

bool HasConsistentMaterialDenominators(
    const OgreNextDemoTextureSourceCounters &counters) noexcept {
  std::size_t exclusions = 0U;
  for (std::size_t count : counters.exclusions_by_reason) {
    exclusions = SaturatingAdd(exclusions, count);
  }
  const std::size_t blend_partition = SaturatingAdd(
      counters.active_replace_material_projections,
      SaturatingAdd(counters.active_straight_source_over_material_projections,
                    counters
                        .active_legacy_straight_alpha_material_projections));
  const std::size_t alpha_test_partition = SaturatingAdd(
      counters.active_alpha_test_disabled_material_projections,
      SaturatingAdd(counters.active_alpha_test_greater_material_projections,
                    counters
                        .active_alpha_test_greater_equal_material_projections));
  const std::size_t workflow_partition = SaturatingAdd(
      counters.active_metallic_roughness_workflow_projections,
      counters.active_specular_workflow_projections);
  const std::size_t normalization_partition = SaturatingAdd(
      counters.active_opaque_texture_normalizations,
      SaturatingAdd(counters.active_straight_alpha_texture_normalizations,
                    counters.active_linear_specular_texture_normalizations));
  const std::size_t decode_normalization_partition = SaturatingAdd(
      counters.opaque_source_normalizations,
      SaturatingAdd(counters.straight_alpha_source_normalizations,
                    counters.linear_specular_source_normalizations));
  return exclusions == counters.source_exclusions &&
         exclusions == counters.matte_excluded_sections &&
         counters.candidate_sections ==
             SaturatingAdd(counters.projected_sections, exclusions) &&
         counters.distinct_eligible_texture_keys ==
             SaturatingAdd(counters.distinct_projected_texture_keys,
                           counters.distinct_matte_only_texture_keys) &&
         counters.projections == blend_partition &&
         counters.projections == alpha_test_partition &&
         counters.projections == workflow_partition &&
         counters.active_anisotropic_sampler_projections <=
             counters.projections &&
         counters.active_normalized_texture_observations ==
             normalization_partition &&
         counters.modern_source_normalizations ==
             decode_normalization_partition &&
         counters.normalized_output_mip_levels ==
             SaturatingAdd(counters.authored_mip_prefix_levels,
                           counters.generated_mip_tail_levels) &&
         counters.normalized_specular_output_mip_levels ==
             SaturatingAdd(counters.authored_specular_mip_prefix_levels,
                           counters.generated_specular_mip_tail_levels);
}

} // namespace

const OgreNextDemoCuratedCityWorldMaterial *
FindOgreNextDemoCuratedCityWorldMaterial(
    std::string_view exact_material_name) noexcept {
  const auto found = std::find_if(
      kCuratedCityWorldAsiaMaterials.begin(),
      kCuratedCityWorldAsiaMaterials.end(),
      [exact_material_name](const auto &entry) {
        return entry.exact_material_name == exact_material_name;
      });
  return found == kCuratedCityWorldAsiaMaterials.end() ? nullptr : &*found;
}

const OgreNextDemoCuratedCityWorldMaterial *
OgreNextDemoCuratedCityWorldMaterialAt(std::size_t index) noexcept {
  return index < kCuratedCityWorldAsiaMaterials.size()
             ? &kCuratedCityWorldAsiaMaterials[index]
             : nullptr;
}

Render::ValidationResult AuthenticateOgreNextDemoCuratedCityWorldMaterial(
    const OgreNextDemoCuratedCityWorldMaterial &policy,
    const OgreNextDemoCuratedCityWorldSourceObservation &observation) {
  try {
    const OgreNextDemoCuratedCityWorldMaterial *const reviewed =
        FindOgreNextDemoCuratedCityWorldMaterial(policy.exact_material_name);
    const bool matches_reviewed_row =
        reviewed != nullptr &&
        reviewed->review_identity_sha256 == policy.review_identity_sha256 &&
        reviewed->source_byte_start == policy.source_byte_start &&
        reviewed->source_byte_end_exclusive ==
            policy.source_byte_end_exclusive &&
        reviewed->source_span_sha256 == policy.source_span_sha256 &&
        reviewed->base_color_texture_name == policy.base_color_texture_name &&
        reviewed->linear_specular_texture_name ==
            policy.linear_specular_texture_name &&
        reviewed->spherical_environment_texture_name ==
            policy.spherical_environment_texture_name &&
        reviewed->workflow == policy.workflow &&
        reviewed->base_color_texture_unit == policy.base_color_texture_unit &&
        reviewed->linear_specular_texture_unit ==
            policy.linear_specular_texture_unit &&
        reviewed->spherical_environment_texture_unit ==
            policy.spherical_environment_texture_unit &&
        reviewed->roughness_factor == policy.roughness_factor &&
        reviewed->specular_factor == policy.specular_factor &&
        reviewed->index_of_refraction == policy.index_of_refraction &&
        reviewed->alpha_policy == policy.alpha_policy &&
        reviewed->depth_write == policy.depth_write &&
        reviewed->clockwise_cull == policy.clockwise_cull &&
        reviewed->sampler_policy == policy.sampler_policy &&
        reviewed->environment_policy == policy.environment_policy;
    if (!matches_reviewed_row) {
      return Failure(Render::ValidationCode::REVISION_MISMATCH,
                     "ogre_next_demo.curated_cityworld.reviewed_row",
                     "candidate policy is not the exact source-controlled reviewed row");
    }
    if (policy.exact_material_name.empty() ||
        policy.review_identity_sha256.empty() ||
        policy.source_span_sha256.empty() ||
        policy.base_color_texture_name.empty() ||
        policy.linear_specular_texture_name.empty() ||
        policy.spherical_environment_texture_name.empty() ||
        policy.workflow != OgreNextDemoCuratedCityWorldWorkflow::SPECULAR ||
        policy.base_color_texture_unit != 0U ||
        policy.linear_specular_texture_unit != 1U ||
        policy.spherical_environment_texture_unit != 2U ||
        !std::isfinite(policy.roughness_factor) ||
        policy.roughness_factor < 1.0e-4F ||
        policy.roughness_factor > 1.0F ||
        !std::all_of(policy.specular_factor.begin(),
                     policy.specular_factor.end(), [](float value) {
                       return std::isfinite(value) && value >= 0.0F &&
                              value <= 1.0F;
                     }) ||
        !std::isfinite(policy.index_of_refraction) ||
        policy.index_of_refraction < 1.0F ||
        policy.index_of_refraction > 3.0F ||
        policy.alpha_policy !=
            OgreNextDemoCuratedCityWorldAlphaPolicy::FORCE_OPAQUE ||
        !policy.depth_write || !policy.clockwise_cull ||
        policy.sampler_policy !=
            OgreNextDemoCuratedCityWorldSamplerPolicy::
                REVIEWED_CONFIGURED_ANISOTROPIC4_V1 ||
        policy.environment_policy !=
            OgreNextDemoCuratedCityWorldEnvironmentPolicy::
                SPHERICAL_AUTHORITY_BOUND_PENDING_NOT_PRESENTED ||
        policy.source_byte_start >= policy.source_byte_end_exclusive ||
        !IsLowercaseSha256(policy.review_identity_sha256) ||
        !IsLowercaseSha256(policy.source_span_sha256)) {
      return Failure(Render::ValidationCode::INVALID_ENUM,
                     "ogre_next_demo.curated_cityworld.policy",
                     "reviewed CityWorld policy row is incomplete");
    }
    if (observation.archive_sha256 !=
            kOgreNextDemoCuratedCityWorldArchiveSha256 ||
        observation.exact_script_member !=
            kOgreNextDemoCuratedCityWorldScriptMember ||
        observation.source_script_sha256 !=
            kOgreNextDemoCuratedCityWorldScriptSha256 ||
        !IsLowercaseSha256(observation.archive_sha256) ||
        !IsLowercaseSha256(observation.source_script_sha256)) {
      return Failure(Render::ValidationCode::REVISION_MISMATCH,
                     "ogre_next_demo.curated_cityworld.source_identity",
                     "authenticated package or script identity is not the reviewed CityWorld source");
    }
    if (observation.source_script_bytes == nullptr ||
        observation.source_script_size == 0U ||
        policy.source_byte_end_exclusive > observation.source_script_size) {
      return Failure(Render::ValidationCode::SIZE_MISMATCH,
                     "ogre_next_demo.curated_cityworld.source_span",
                     "reviewed material source span is outside the authenticated script");
    }
    const std::string script_sha256 = DigestHex(
        Render::ComputeRenderPayloadDigest(observation.source_script_bytes,
                                           observation.source_script_size));
    if (script_sha256 != observation.source_script_sha256) {
      return Failure(Render::ValidationCode::REVISION_MISMATCH,
                     "ogre_next_demo.curated_cityworld.script_sha256",
                     "authenticated script bytes do not match their reviewed digest");
    }
    const std::uint8_t *const span =
        observation.source_script_bytes + policy.source_byte_start;
    const std::size_t span_size =
        policy.source_byte_end_exclusive - policy.source_byte_start;
    const std::string span_sha256 = DigestHex(
        Render::ComputeRenderPayloadDigest(span, span_size));
    if (span_sha256 != policy.source_span_sha256) {
      return Failure(Render::ValidationCode::REVISION_MISMATCH,
                     "ogre_next_demo.curated_cityworld.source_span_sha256",
                     "authenticated material declaration span changed");
    }

    // Mirrors tools/classify_cityworld_material_families.py::_material_id.
    // This independent runtime derivation binds the reviewed identity to the
    // authenticated member, complete script digest, exact name, byte range,
    // and span digest instead of treating the name as an allowlist.
    std::string canonical;
    const auto append_field = [&canonical](std::string_view value) {
      canonical.append(value.data(), value.size());
      canonical.push_back('\0');
    };
    append_field(observation.exact_script_member);
    append_field(observation.source_script_sha256);
    append_field(policy.exact_material_name);
    append_field(std::to_string(policy.source_byte_start));
    append_field(std::to_string(policy.source_byte_end_exclusive));
    canonical.append(policy.source_span_sha256.data(),
                     policy.source_span_sha256.size());
    const std::string review_identity = DigestHex(
        Render::ComputeRenderPayloadDigest(
            reinterpret_cast<const std::uint8_t *>(canonical.data()),
            canonical.size()));
    if (review_identity != policy.review_identity_sha256) {
      return Failure(Render::ValidationCode::REVISION_MISMATCH,
                     "ogre_next_demo.curated_cityworld.review_identity_sha256",
                     "derived material declaration identity is not the reviewed table entry");
    }
    return Render::ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(Render::ValidationCode::EMPTY_PAYLOAD,
                   "ogre_next_demo.curated_cityworld.allocation",
                   "allocation failed while authenticating the reviewed material declaration");
  } catch (...) {
    return Failure(Render::ValidationCode::UNSUPPORTED_FEATURE,
                   "ogre_next_demo.curated_cityworld.exception",
                   "unexpected failure while authenticating the reviewed material declaration");
  }
}

Render::ValidationResult
AuthenticateOgreNextDemoCuratedCityWorldScriptRepair(
    const OgreNextDemoCuratedCityWorldScriptRepairObservation &observation) {
  if (observation.original_sha256 !=
          kOgreNextDemoCuratedCityWorldScriptSha256 ||
      !IsLowercaseSha256(observation.original_sha256) ||
      !IsLowercaseSha256(observation.effective_sha256)) {
    return Failure(
        Render::ValidationCode::REVISION_MISMATCH,
        "ogre_next_demo.curated_cityworld.script_repair.identity",
        "authenticated script is not the reviewed CityWorld source script");
  }
  if (observation.repair_plan_version !=
      kOgreNextDemoCuratedCityWorldRepairPlanVersion) {
    return Failure(
        Render::ValidationCode::REVISION_MISMATCH,
        "ogre_next_demo.curated_cityworld.script_repair.plan_version",
        "authenticated script carries an unreviewed repair-plan version");
  }
  // The receipt's own repair-plan digest must equal the digest the caller
  // recomputed from the source-controlled plan table. That is what makes a
  // repaired script reviewed rather than merely modified: an unreviewed or
  // tampered plan produces a different canonical digest and is refused, in
  // both the repaired and the unrepaired arm.
  if (!IsLowercaseSha256(observation.repair_plan_sha256) ||
      !IsLowercaseSha256(observation.reviewed_repair_plan_sha256) ||
      observation.repair_plan_sha256 !=
          observation.reviewed_repair_plan_sha256) {
    return Failure(
        Render::ValidationCode::REVISION_MISMATCH,
        "ogre_next_demo.curated_cityworld.script_repair.plan_sha256",
        "authenticated repair plan is not the reviewed source-controlled plan");
  }
  if (!observation.repair_applied) {
    if (observation.applied_edit_count != 0U ||
        observation.effective_sha256 != observation.original_sha256 ||
        !observation.effective_bytes_equal_original) {
      return Failure(
          Render::ValidationCode::REVISION_MISMATCH,
          "ogre_next_demo.curated_cityworld.script_repair.unrepaired",
          "script reports no repair while its effective bytes differ");
    }
    return Render::ValidationResult::Success();
  }
  if (observation.applied_edit_count !=
          kOgreNextDemoCuratedCityWorldAppliedEditCount ||
      observation.effective_sha256 == observation.original_sha256 ||
      observation.effective_bytes_equal_original) {
    return Failure(
        Render::ValidationCode::REVISION_MISMATCH,
        "ogre_next_demo.curated_cityworld.script_repair.applied",
        "applied repair is not the reviewed edit count or changed nothing");
  }
  return Render::ValidationResult::Success();
}

bool IsOgreNextDemoAuthenticatedTextureSourceMode(
    OgreNextDemoTextureSourceMode mode) noexcept {
  return mode == OgreNextDemoTextureSourceMode::
                     AUTHENTICATED_ARCHIVE_SOURCE_BYTES ||
         mode == OgreNextDemoTextureSourceMode::
                     AUTHENTICATED_GENERATED_SOURCE_BYTES;
}

Render::ValidationResult RecordOgreNextDemoTextureSourceDecode(
    OgreNextDemoTextureSourceMode mode,
    OgreNextDemoTextureSourceCounters &counters) {
  if (!HasZeroGpuReadbacks(counters)) {
    return Failure(Render::ValidationCode::SEQUENCE_MISMATCH,
                   "ogre_next_demo.material.source_accounting.gpu_readbacks",
                   "source-byte accounting cannot follow a GPU readback");
  }
  OgreNextDemoTextureSourceCounters candidate = counters;
  switch (mode) {
  case OgreNextDemoTextureSourceMode::AUTHENTICATED_ARCHIVE_SOURCE_BYTES:
    candidate.authenticated_archive_source_decodes =
        SaturatingAdd(candidate.authenticated_archive_source_decodes, 1U);
    candidate.authenticated_source_decodes =
        SaturatingAdd(candidate.authenticated_source_decodes, 1U);
    break;
  case OgreNextDemoTextureSourceMode::AUTHENTICATED_GENERATED_SOURCE_BYTES:
    candidate.authenticated_generated_source_decodes =
        SaturatingAdd(candidate.authenticated_generated_source_decodes, 1U);
    candidate.authenticated_source_decodes =
        SaturatingAdd(candidate.authenticated_source_decodes, 1U);
    break;
  case OgreNextDemoTextureSourceMode::ORDINARY_OBSERVED_SOURCE_BYTES:
    candidate.ordinary_observed_source_decodes =
        SaturatingAdd(candidate.ordinary_observed_source_decodes, 1U);
    break;
  default:
    return Failure(Render::ValidationCode::INVALID_ENUM,
                   "ogre_next_demo.material.source_accounting.mode",
                   "texture source mode is invalid");
  }
  counters = std::move(candidate);
  return Render::ValidationResult::Success();
}

Render::ValidationResult RecordOgreNextDemoTextureSourceCacheHit(
    OgreNextDemoTextureSourceCounters &counters) {
  if (!HasZeroGpuReadbacks(counters)) {
    return Failure(Render::ValidationCode::SEQUENCE_MISMATCH,
                   "ogre_next_demo.material.source_accounting.gpu_readbacks",
                   "source cache accounting cannot follow a GPU readback");
  }
  counters.source_cache_hits = SaturatingAdd(counters.source_cache_hits, 1U);
  return Render::ValidationResult::Success();
}

Render::ValidationResult RecordOgreNextDemoTextureProjectionExclusion(
    OgreNextDemoTextureProjectionExclusion exclusion,
    OgreNextDemoTextureSourceCounters &counters) {
  const std::size_t index = static_cast<std::size_t>(exclusion);
  if (exclusion == OgreNextDemoTextureProjectionExclusion::NONE ||
      index >= kOgreNextDemoTextureProjectionExclusionCount) {
    return Failure(Render::ValidationCode::INVALID_ENUM,
                   "ogre_next_demo.material.source_accounting.exclusion",
                   "texture projection exclusion is invalid or empty");
  }
  if (!HasZeroGpuReadbacks(counters)) {
    return Failure(Render::ValidationCode::SEQUENCE_MISMATCH,
                   "ogre_next_demo.material.source_accounting.gpu_readbacks",
                   "source exclusion accounting cannot follow a GPU readback");
  }
  OgreNextDemoTextureSourceCounters candidate = counters;
  candidate.source_exclusions = SaturatingAdd(candidate.source_exclusions, 1U);
  candidate.candidate_sections =
      SaturatingAdd(candidate.candidate_sections, 1U);
  candidate.matte_excluded_sections =
      SaturatingAdd(candidate.matte_excluded_sections, 1U);
  candidate.exclusions_by_reason[index] =
      SaturatingAdd(candidate.exclusions_by_reason[index], 1U);
  if (exclusion ==
          OgreNextDemoTextureProjectionExclusion::SOURCE_DECODE_REJECTED ||
      exclusion == OgreNextDemoTextureProjectionExclusion::
                       UNSUPPORTED_SOURCE_CONTAINER) {
    candidate.source_decode_rejections =
        SaturatingAdd(candidate.source_decode_rejections, 1U);
  }
  counters = std::move(candidate);
  return Render::ValidationResult::Success();
}

std::string_view OgreNextDemoTextureProjectionExclusionName(
    OgreNextDemoTextureProjectionExclusion exclusion) noexcept {
  constexpr std::array<std::string_view,
                       kOgreNextDemoTextureProjectionExclusionCount>
      names = {"none",
               "source_unavailable",
               "manual_or_procedural",
               "render_target",
               "cube_texture",
               "volume_texture",
               "non_2d",
               "non_unit_depth",
               "non_unit_face_count",
               "dimension_out_of_range",
               "ordinary_selected_source_unavailable",
               "unsupported_source_container",
               "unsupported_source_semantic",
               "source_decode_rejected",
               "missing_authored_uv0",
               "material_structure_unsupported",
               "material_state_unsupported",
               "texture_unit_structure_unsupported",
               "texture_unit_semantic_unsupported",
               "sampler_state_unsupported",
               "alexis_approximation_unsafe",
               "texture_alpha_combine_unsupported",
               "alpha_state_unsupported",
               "managed_material_authority_unavailable",
               "managed_material_semantic_unsupported",
               "ambiguous_bc1_alpha_semantic",
               "material_multi_pass_unsupported",
               "material_authored_program_unsupported",
               "material_texture_unit_layer_unsupported",
               "material_blended_overlay_pass_unsupported",
               "material_additive_overlay_pass_unsupported",
               "material_alpha_tested_overlay_pass_unsupported"};
  const std::size_t index = static_cast<std::size_t>(exclusion);
  return index < names.size() ? names[index] : std::string_view{"invalid"};
}

Render::ValidationResult AccumulateOgreNextDemoTextureSourceCounters(
    const OgreNextDemoTextureSourceCounters &increment,
    OgreNextDemoTextureSourceCounters &total) {
  if (!HasZeroGpuReadbacks(increment) || !HasZeroGpuReadbacks(total)) {
    return Failure(
        Render::ValidationCode::SEQUENCE_MISMATCH,
        "ogre_next_demo.material.source_accounting.gpu_readbacks",
        "material texture capture observed a forbidden GPU readback");
  }
  if (!HasConsistentMaterialDenominators(increment) ||
      !HasConsistentMaterialDenominators(total)) {
    return Failure(
        Render::ValidationCode::SEQUENCE_MISMATCH,
        "ogre_next_demo.material.source_accounting.denominator",
        "candidate sections must equal projected plus every named matte");
  }
  OgreNextDemoTextureSourceCounters candidate = total;
  candidate.authenticated_archive_source_decodes =
      SaturatingAdd(candidate.authenticated_archive_source_decodes,
                    increment.authenticated_archive_source_decodes);
  candidate.authenticated_generated_source_decodes =
      SaturatingAdd(candidate.authenticated_generated_source_decodes,
                    increment.authenticated_generated_source_decodes);
  candidate.ordinary_observed_source_decodes =
      SaturatingAdd(candidate.ordinary_observed_source_decodes,
                    increment.ordinary_observed_source_decodes);
  candidate.source_cache_hits =
      SaturatingAdd(candidate.source_cache_hits, increment.source_cache_hits);
  candidate.source_decode_rejections = SaturatingAdd(
      candidate.source_decode_rejections, increment.source_decode_rejections);
  candidate.source_exclusions =
      SaturatingAdd(candidate.source_exclusions, increment.source_exclusions);
  for (std::size_t index = 0U; index < candidate.exclusions_by_reason.size();
       ++index) {
    candidate.exclusions_by_reason[index] =
        SaturatingAdd(candidate.exclusions_by_reason[index],
                      increment.exclusions_by_reason[index]);
  }
  candidate.authenticated_source_decodes =
      SaturatingAdd(candidate.authenticated_source_decodes,
                    increment.authenticated_source_decodes);
  candidate.projections =
      SaturatingAdd(candidate.projections, increment.projections);
  candidate.new_frozen_material_decisions =
      SaturatingAdd(candidate.new_frozen_material_decisions,
                    increment.new_frozen_material_decisions);
  candidate.candidate_sections =
      SaturatingAdd(candidate.candidate_sections, increment.candidate_sections);
  candidate.projected_sections =
      SaturatingAdd(candidate.projected_sections, increment.projected_sections);
  candidate.matte_excluded_sections = SaturatingAdd(
      candidate.matte_excluded_sections, increment.matte_excluded_sections);
  candidate.distinct_eligible_texture_keys =
      SaturatingAdd(candidate.distinct_eligible_texture_keys,
                    increment.distinct_eligible_texture_keys);
  candidate.distinct_projected_texture_keys =
      SaturatingAdd(candidate.distinct_projected_texture_keys,
                    increment.distinct_projected_texture_keys);
  candidate.distinct_matte_only_texture_keys =
      SaturatingAdd(candidate.distinct_matte_only_texture_keys,
                    increment.distinct_matte_only_texture_keys);
  candidate.modern_source_normalizations =
      SaturatingAdd(candidate.modern_source_normalizations,
                    increment.modern_source_normalizations);
  candidate.authored_mip_prefix_levels =
      SaturatingAdd(candidate.authored_mip_prefix_levels,
                    increment.authored_mip_prefix_levels);
  candidate.generated_mip_tail_levels = SaturatingAdd(
      candidate.generated_mip_tail_levels, increment.generated_mip_tail_levels);
  candidate.normalized_output_mip_levels =
      SaturatingAdd(candidate.normalized_output_mip_levels,
                    increment.normalized_output_mip_levels);
  candidate.legacy_native_additional_mip_levels =
      SaturatingAdd(candidate.legacy_native_additional_mip_levels,
                    increment.legacy_native_additional_mip_levels);
  candidate.legacy_texture_unit_gamma_nonunit_observations =
      SaturatingAdd(candidate.legacy_texture_unit_gamma_nonunit_observations,
                    increment.legacy_texture_unit_gamma_nonunit_observations);
  candidate.legacy_texture_gamma_nonunit_observations =
      SaturatingAdd(candidate.legacy_texture_gamma_nonunit_observations,
                    increment.legacy_texture_gamma_nonunit_observations);
  candidate.legacy_texture_unit_hardware_gamma_off_observations = SaturatingAdd(
      candidate.legacy_texture_unit_hardware_gamma_off_observations,
      increment.legacy_texture_unit_hardware_gamma_off_observations);
  candidate.legacy_hardware_gamma_off_observations =
      SaturatingAdd(candidate.legacy_hardware_gamma_off_observations,
                    increment.legacy_hardware_gamma_off_observations);
  candidate.legacy_automipmap_observations =
      SaturatingAdd(candidate.legacy_automipmap_observations,
                    increment.legacy_automipmap_observations);
  candidate.active_texture_state_observations =
      SaturatingAdd(candidate.active_texture_state_observations,
                    increment.active_texture_state_observations);
  candidate.active_authored_mip_prefix_levels =
      SaturatingAdd(candidate.active_authored_mip_prefix_levels,
                    increment.active_authored_mip_prefix_levels);
  candidate.active_generated_mip_tail_levels =
      SaturatingAdd(candidate.active_generated_mip_tail_levels,
                    increment.active_generated_mip_tail_levels);
  candidate.active_normalized_output_mip_levels =
      SaturatingAdd(candidate.active_normalized_output_mip_levels,
                    increment.active_normalized_output_mip_levels);
  candidate.active_legacy_native_additional_mip_levels =
      SaturatingAdd(candidate.active_legacy_native_additional_mip_levels,
                    increment.active_legacy_native_additional_mip_levels);
  candidate.active_legacy_texture_unit_gamma_nonunit_observations =
      SaturatingAdd(
          candidate.active_legacy_texture_unit_gamma_nonunit_observations,
          increment.active_legacy_texture_unit_gamma_nonunit_observations);
  candidate.active_legacy_texture_gamma_nonunit_observations =
      SaturatingAdd(candidate.active_legacy_texture_gamma_nonunit_observations,
                    increment.active_legacy_texture_gamma_nonunit_observations);
  candidate.active_legacy_texture_unit_hardware_gamma_off_observations =
      SaturatingAdd(
          candidate.active_legacy_texture_unit_hardware_gamma_off_observations,
          increment.active_legacy_texture_unit_hardware_gamma_off_observations);
  candidate.active_legacy_hardware_gamma_off_observations =
      SaturatingAdd(candidate.active_legacy_hardware_gamma_off_observations,
                    increment.active_legacy_hardware_gamma_off_observations);
  candidate.active_legacy_automipmap_observations =
      SaturatingAdd(candidate.active_legacy_automipmap_observations,
                    increment.active_legacy_automipmap_observations);
  candidate.lossy_material_normalizations =
      SaturatingAdd(candidate.lossy_material_normalizations,
                    increment.lossy_material_normalizations);
  candidate.opaque_source_normalizations =
      SaturatingAdd(candidate.opaque_source_normalizations,
                    increment.opaque_source_normalizations);
  candidate.straight_alpha_source_normalizations =
      SaturatingAdd(candidate.straight_alpha_source_normalizations,
                    increment.straight_alpha_source_normalizations);
  candidate.alpha_test_material_projections =
      SaturatingAdd(candidate.alpha_test_material_projections,
                    increment.alpha_test_material_projections);
  candidate.straight_source_over_material_projections = SaturatingAdd(
      candidate.straight_source_over_material_projections,
      increment.straight_source_over_material_projections);
  candidate.legacy_straight_alpha_material_projections = SaturatingAdd(
      candidate.legacy_straight_alpha_material_projections,
      increment.legacy_straight_alpha_material_projections);
  candidate.specular_workflow_projections =
      SaturatingAdd(candidate.specular_workflow_projections,
                    increment.specular_workflow_projections);
  candidate.layered_legacy_material_projections =
      SaturatingAdd(candidate.layered_legacy_material_projections,
                    increment.layered_legacy_material_projections);
  candidate.unpresented_legacy_layer_units =
      SaturatingAdd(candidate.unpresented_legacy_layer_units,
                    increment.unpresented_legacy_layer_units);
  candidate.additive_overlay_legacy_material_projections =
      SaturatingAdd(candidate.additive_overlay_legacy_material_projections,
                    increment.additive_overlay_legacy_material_projections);
  candidate.unpresented_legacy_additive_overlay_passes =
      SaturatingAdd(candidate.unpresented_legacy_additive_overlay_passes,
                    increment.unpresented_legacy_additive_overlay_passes);
  candidate.authored_specular_source_decodes =
      SaturatingAdd(candidate.authored_specular_source_decodes,
                    increment.authored_specular_source_decodes);
  candidate.linear_specular_source_normalizations = SaturatingAdd(
      candidate.linear_specular_source_normalizations,
      increment.linear_specular_source_normalizations);
  candidate.authored_specular_mip_prefix_levels = SaturatingAdd(
      candidate.authored_specular_mip_prefix_levels,
      increment.authored_specular_mip_prefix_levels);
  candidate.generated_specular_mip_tail_levels = SaturatingAdd(
      candidate.generated_specular_mip_tail_levels,
      increment.generated_specular_mip_tail_levels);
  candidate.normalized_specular_output_mip_levels = SaturatingAdd(
      candidate.normalized_specular_output_mip_levels,
      increment.normalized_specular_output_mip_levels);
  candidate.anisotropic_sampler_projections =
      SaturatingAdd(candidate.anisotropic_sampler_projections,
                    increment.anisotropic_sampler_projections);
  candidate.active_replace_material_projections = SaturatingAdd(
      candidate.active_replace_material_projections,
      increment.active_replace_material_projections);
  candidate.active_straight_source_over_material_projections = SaturatingAdd(
      candidate.active_straight_source_over_material_projections,
      increment.active_straight_source_over_material_projections);
  candidate.active_legacy_straight_alpha_material_projections = SaturatingAdd(
      candidate.active_legacy_straight_alpha_material_projections,
      increment.active_legacy_straight_alpha_material_projections);
  candidate.active_alpha_test_disabled_material_projections = SaturatingAdd(
      candidate.active_alpha_test_disabled_material_projections,
      increment.active_alpha_test_disabled_material_projections);
  candidate.active_alpha_test_greater_material_projections = SaturatingAdd(
      candidate.active_alpha_test_greater_material_projections,
      increment.active_alpha_test_greater_material_projections);
  candidate.active_alpha_test_greater_equal_material_projections =
      SaturatingAdd(
          candidate.active_alpha_test_greater_equal_material_projections,
          increment.active_alpha_test_greater_equal_material_projections);
  candidate.active_metallic_roughness_workflow_projections = SaturatingAdd(
      candidate.active_metallic_roughness_workflow_projections,
      increment.active_metallic_roughness_workflow_projections);
  candidate.active_specular_workflow_projections = SaturatingAdd(
      candidate.active_specular_workflow_projections,
      increment.active_specular_workflow_projections);
  candidate.active_anisotropic_sampler_projections = SaturatingAdd(
      candidate.active_anisotropic_sampler_projections,
      increment.active_anisotropic_sampler_projections);
  candidate.active_normalized_texture_observations = SaturatingAdd(
      candidate.active_normalized_texture_observations,
      increment.active_normalized_texture_observations);
  candidate.active_opaque_texture_normalizations = SaturatingAdd(
      candidate.active_opaque_texture_normalizations,
      increment.active_opaque_texture_normalizations);
  candidate.active_straight_alpha_texture_normalizations = SaturatingAdd(
      candidate.active_straight_alpha_texture_normalizations,
      increment.active_straight_alpha_texture_normalizations);
  candidate.active_linear_specular_texture_normalizations = SaturatingAdd(
      candidate.active_linear_specular_texture_normalizations,
      increment.active_linear_specular_texture_normalizations);
  if (!HasConsistentMaterialDenominators(candidate)) {
    return Failure(Render::ValidationCode::SEQUENCE_MISMATCH,
                   "ogre_next_demo.material.source_accounting.denominator",
                   "accumulated candidate denominator became inconsistent");
  }
  total = std::move(candidate);
  return Render::ValidationResult::Success();
}

Render::ValidationResult ClassifyOgreNextDemoTextureProjectionEligibility(
    const OgreNextDemoTextureEligibilityObservation &observation,
    OgreNextDemoTextureProjectionExclusion &output) {
  OgreNextDemoTextureProjectionExclusion candidate =
      OgreNextDemoTextureProjectionExclusion::NONE;
  if (!observation.source_available) {
    candidate = OgreNextDemoTextureProjectionExclusion::SOURCE_UNAVAILABLE;
  } else if (observation.render_target) {
    candidate = OgreNextDemoTextureProjectionExclusion::RENDER_TARGET;
  } else if (observation.cube_texture) {
    candidate = OgreNextDemoTextureProjectionExclusion::CUBE_TEXTURE;
  } else if (observation.volume_texture) {
    candidate = OgreNextDemoTextureProjectionExclusion::VOLUME_TEXTURE;
  } else if (!observation.texture_2d) {
    candidate = OgreNextDemoTextureProjectionExclusion::NON_2D;
  } else if (observation.manually_loaded) {
    candidate = OgreNextDemoTextureProjectionExclusion::MANUAL_OR_PROCEDURAL;
  } else if (!observation.unit_depth) {
    candidate = OgreNextDemoTextureProjectionExclusion::NON_UNIT_DEPTH;
  } else if (!observation.unit_face_count) {
    candidate = OgreNextDemoTextureProjectionExclusion::NON_UNIT_FACE_COUNT;
  } else if (!observation.dimensions_in_range) {
    candidate = OgreNextDemoTextureProjectionExclusion::DIMENSION_OUT_OF_RANGE;
  }
  output = candidate;
  return Render::ValidationResult::Success();
}

bool MatchOgreNextDemoExactSamplerObservation(
    const OgreNextDemoExactSamplerObservation &left,
    const OgreNextDemoExactSamplerObservation &right) noexcept {
  return left.minification_filter == right.minification_filter &&
         left.magnification_filter == right.magnification_filter &&
         left.mip_filter == right.mip_filter &&
         left.address_u == right.address_u &&
         left.address_v == right.address_v &&
         left.address_w == right.address_w &&
         left.mip_lod_bias == right.mip_lod_bias &&
         left.maximum_anisotropy == right.maximum_anisotropy &&
         left.compare_enabled == right.compare_enabled &&
         left.compare_function_token == right.compare_function_token &&
         left.border_color == right.border_color;
}

Render::ValidationResult ValidateOgreNextDemoExactTextureObservation(
    const OgreNextDemoExactTextureObservation &observation) {
  if (!std::isfinite(observation.texture_unit_gamma) ||
      !std::isfinite(observation.texture_gamma)) {
    return Failure(Render::ValidationCode::NON_FINITE_VALUE,
                   "ogre_next_demo.material.texture.native_gamma",
                   "native TUS/texture gamma must be finite provenance");
  }
  if (observation.additional_mip_count ==
          (std::numeric_limits<std::uint32_t>::max)() ||
      observation.actual_mip_count != observation.additional_mip_count + 1U) {
    return Failure(Render::ValidationCode::SIZE_MISMATCH,
                   "ogre_next_demo.material.texture.native_mip_count",
                   "actual mip count must include the base plus all additional "
                   "native levels");
  }
  if (observation.source_width == 0U || observation.source_height == 0U ||
      observation.source_depth == 0U || observation.output_width == 0U ||
      observation.output_height == 0U || observation.output_depth == 0U ||
      observation.face_count == 0U) {
    return Failure(Render::ValidationCode::EMPTY_PAYLOAD,
                   "ogre_next_demo.material.texture.native_dimensions",
                   "native source/output dimensions and faces must be nonzero");
  }
  return Render::ValidationResult::Success();
}

bool MatchOgreNextDemoExactTextureObservation(
    const OgreNextDemoExactTextureObservation &left,
    const OgreNextDemoExactTextureObservation &right) noexcept {
  const auto same_float_bits = [](float lhs, float rhs) noexcept {
    std::uint32_t lhs_bits = 0U;
    std::uint32_t rhs_bits = 0U;
    static_assert(sizeof(lhs_bits) == sizeof(lhs));
    std::memcpy(&lhs_bits, &lhs, sizeof(lhs_bits));
    std::memcpy(&rhs_bits, &rhs, sizeof(rhs_bits));
    return lhs_bits == rhs_bits;
  };
  return same_float_bits(left.texture_unit_gamma, right.texture_unit_gamma) &&
         same_float_bits(left.texture_gamma, right.texture_gamma) &&
         left.texture_unit_hardware_gamma ==
             right.texture_unit_hardware_gamma &&
         left.texture_hardware_gamma == right.texture_hardware_gamma &&
         left.additional_mip_count == right.additional_mip_count &&
         left.actual_mip_count == right.actual_mip_count &&
         left.mipmaps_hardware_generated == right.mipmaps_hardware_generated &&
         left.usage_token == right.usage_token &&
         left.source_width == right.source_width &&
         left.source_height == right.source_height &&
         left.source_depth == right.source_depth &&
         left.source_format_token == right.source_format_token &&
         left.output_width == right.output_width &&
         left.output_height == right.output_height &&
         left.output_depth == right.output_depth &&
         left.output_format_token == right.output_format_token &&
         left.face_count == right.face_count &&
         left.texture_type_token == right.texture_type_token;
}

Render::ValidationResult BuildOgreNextDemoSamplerDescriptor(
    const OgreNextDemoExactSamplerObservation &observation,
    std::size_t mip_count, std::string_view debug_token,
    Render::SamplerResourceDescriptor &output) {
  if (mip_count == 0U) {
    return Failure(Render::ValidationCode::EMPTY_PAYLOAD,
                   "ogre_next_demo.material.sampler",
                   "projected sampler requires a complete texture");
  }
  const auto map_filter = [](OgreNextDemoObservedSamplerFilter source,
                             Render::SamplerFilter &destination) noexcept {
    switch (source) {
    case OgreNextDemoObservedSamplerFilter::POINT:
      destination = Render::SamplerFilter::NEAREST;
      return true;
    case OgreNextDemoObservedSamplerFilter::LINEAR:
      destination = Render::SamplerFilter::LINEAR;
      return true;
    case OgreNextDemoObservedSamplerFilter::ANISOTROPIC:
      destination = Render::SamplerFilter::LINEAR;
      return true;
    default:
      return false;
    }
  };
  const auto map_address =
      [](OgreNextDemoObservedSamplerAddressMode source,
         Render::SamplerAddressMode &destination) noexcept {
        switch (source) {
        case OgreNextDemoObservedSamplerAddressMode::WRAP:
          destination = Render::SamplerAddressMode::REPEAT;
          return true;
        case OgreNextDemoObservedSamplerAddressMode::MIRROR:
          destination = Render::SamplerAddressMode::MIRRORED_REPEAT;
          return true;
        case OgreNextDemoObservedSamplerAddressMode::CLAMP:
          destination = Render::SamplerAddressMode::CLAMP_TO_EDGE;
          return true;
        default:
          return false;
        }
      };

  Render::SamplerResourceDescriptor candidate;
  candidate.debug_name = "OgreNextDemoPbrSampler/" + std::string(debug_token);
  const bool min_anisotropic = observation.minification_filter ==
                               OgreNextDemoObservedSamplerFilter::ANISOTROPIC;
  const bool mag_anisotropic = observation.magnification_filter ==
                               OgreNextDemoObservedSamplerFilter::ANISOTROPIC;
  const bool mip_anisotropic = observation.mip_filter ==
                               OgreNextDemoObservedSamplerFilter::ANISOTROPIC;
  const bool any_anisotropic =
      min_anisotropic || mag_anisotropic || mip_anisotropic;
  const bool canonical_anisotropic =
      min_anisotropic && mag_anisotropic &&
      observation.mip_filter == OgreNextDemoObservedSamplerFilter::LINEAR;
  if (any_anisotropic && !canonical_anisotropic) {
    return Failure(
        Render::ValidationCode::UNSUPPORTED_FEATURE,
        "ogre_next_demo.material.sampler.anisotropic_filters",
        "the pinned TFO_ANISOTROPIC tuple is min/mag ANISOTROPIC with mip LINEAR");
  }
  if (!map_filter(observation.minification_filter,
                  candidate.minification_filter) ||
      !map_filter(observation.magnification_filter,
                  candidate.magnification_filter) ||
      !map_filter(observation.mip_filter, candidate.mip_filter)) {
    return Failure(Render::ValidationCode::UNSUPPORTED_FEATURE,
                   "ogre_next_demo.material.sampler.filter",
                   "TUS0 sampler requires POINT, LINEAR, or the pinned min/mag ANISOTROPIC plus mip LINEAR tuple");
  }
  if (!map_address(observation.address_u, candidate.address_u) ||
      !map_address(observation.address_v, candidate.address_v) ||
      !map_address(observation.address_w, candidate.address_w)) {
    return Failure(Render::ValidationCode::UNSUPPORTED_FEATURE,
                   "ogre_next_demo.material.sampler.address",
                   "TUS0 sampler uses border or unknown addressing");
  }
  if (observation.mip_lod_bias != 0.0F) {
    return Failure(Render::ValidationCode::UNSUPPORTED_FEATURE,
                   "ogre_next_demo.material.sampler.mip_lod_bias",
                   "TUS0 sampler requires zero mip LOD bias");
  }
  if ((!canonical_anisotropic && observation.maximum_anisotropy != 1U) ||
      (canonical_anisotropic &&
       (observation.maximum_anisotropy <= 1U ||
        observation.maximum_anisotropy > 16U))) {
    return Failure(Render::ValidationCode::UNSUPPORTED_FEATURE,
                   "ogre_next_demo.material.sampler.anisotropy",
                   "TUS0 anisotropy must be 1 when disabled or in (1, 16] when enabled");
  }
  if (observation.compare_enabled) {
    return Failure(Render::ValidationCode::UNSUPPORTED_FEATURE,
                   "ogre_next_demo.material.sampler.compare",
                   "TUS0 sampler comparison must be disabled");
  }
  candidate.mip_lod_bias = 0.0F;
  candidate.minimum_lod = 0.0F;
  candidate.maximum_lod = static_cast<float>(mip_count - 1U);
  candidate.anisotropy_enabled = canonical_anisotropic;
  candidate.maximum_anisotropy =
      static_cast<float>(observation.maximum_anisotropy);
  candidate.compare_enabled = false;
  candidate.compare_operation = Render::SamplerCompareOperation::ALWAYS;
  candidate.border_color = {
      observation.border_color[0U], observation.border_color[1U],
      observation.border_color[2U], observation.border_color[3U]};
  Render::ValidationResult validation =
      Render::ValidateSamplerResourceDescriptor(candidate);
  if (!validation) {
    validation.field = "ogre_next_demo.material.sampler." + validation.field;
    return validation;
  }
  output = std::move(candidate);
  return Render::ValidationResult::Success();
}

Render::ValidationResult
BuildOgreNextDemoCachedProjectionPublicationTransaction(
    const std::vector<OgreNextDemoCachedProjectionPublicationInput>
        &projections,
    const std::vector<OgreNextDemoCachedTexturePublicationInput> &textures,
    const std::vector<OgreNextDemoCachedSamplerPublicationInput> &samplers,
    const std::vector<std::string> &used_projection_keys,
    IOgreNextDemoTexturePublicationBatchValidator &validator,
    OgreNextDemoCachedProjectionPublicationTransaction &output) {
  std::map<std::string, const OgreNextDemoCachedProjectionPublicationInput *,
           std::less<>>
      catalog_by_key;
  std::map<std::uint64_t, std::string> projection_keys_by_material_id;
  for (std::size_t index = 0U; index < projections.size(); ++index) {
    const OgreNextDemoCachedProjectionPublicationInput &input =
        projections[index];
    if (input.projection_key.empty() || input.texture_key.empty() ||
        input.sampler_key.empty() || input.material_source_id == 0U) {
      return Failure(Render::ValidationCode::MISSING_REFERENCE,
                     "ogre_next_demo.material.publication.projection",
                     "cached projection publication identity is empty or zero",
                     index);
    }
    if (!catalog_by_key.emplace(input.projection_key, &input).second) {
      return Failure(Render::ValidationCode::DUPLICATE_IDENTIFIER,
                     "ogre_next_demo.material.publication.projection_key",
                     "cached projection key is duplicated", index);
    }
    const auto material_identity = projection_keys_by_material_id.emplace(
        input.material_source_id, input.projection_key);
    if (!material_identity.second &&
        material_identity.first->second != input.projection_key) {
      return Failure(Render::ValidationCode::DUPLICATE_IDENTIFIER,
                     "ogre_next_demo.material.publication.material_id",
                     "distinct cached projections share one material source ID",
                     index);
    }
  }

  std::map<std::string, const OgreNextDemoCachedTexturePublicationInput *,
           std::less<>>
      textures_by_key;
  std::map<std::uint64_t, std::string> texture_keys_by_id;
  for (std::size_t index = 0U; index < textures.size(); ++index) {
    const OgreNextDemoCachedTexturePublicationInput &input = textures[index];
    if (input.texture_key.empty() || input.texture_source_id == 0U) {
      return Failure(Render::ValidationCode::MISSING_REFERENCE,
                     "ogre_next_demo.material.publication.texture",
                     "cached texture publication identity is empty or zero",
                     index);
    }
    if (!IsOgreNextDemoAuthenticatedTextureSourceMode(input.source_mode) &&
        input.source_mode !=
            OgreNextDemoTextureSourceMode::ORDINARY_OBSERVED_SOURCE_BYTES) {
      return Failure(Render::ValidationCode::INVALID_ENUM,
                     "ogre_next_demo.material.publication.texture_mode",
                     "cached texture source mode is invalid", index);
    }
    if (!textures_by_key.emplace(input.texture_key, &input).second ||
        !texture_keys_by_id.emplace(input.texture_source_id, input.texture_key)
             .second) {
      return Failure(Render::ValidationCode::DUPLICATE_IDENTIFIER,
                     "ogre_next_demo.material.publication.texture",
                     "cached texture key or source ID is duplicated", index);
    }
  }

  std::map<std::string, const OgreNextDemoCachedSamplerPublicationInput *,
           std::less<>>
      samplers_by_key;
  std::map<std::uint64_t, std::string> sampler_keys_by_id;
  for (std::size_t index = 0U; index < samplers.size(); ++index) {
    const OgreNextDemoCachedSamplerPublicationInput &input = samplers[index];
    if (input.sampler_key.empty() || input.sampler_source_id == 0U) {
      return Failure(Render::ValidationCode::MISSING_REFERENCE,
                     "ogre_next_demo.material.publication.sampler",
                     "cached sampler publication identity is empty or zero",
                     index);
    }
    if (!samplers_by_key.emplace(input.sampler_key, &input).second ||
        !sampler_keys_by_id.emplace(input.sampler_source_id, input.sampler_key)
             .second) {
      return Failure(Render::ValidationCode::DUPLICATE_IDENTIFIER,
                     "ogre_next_demo.material.publication.sampler",
                     "cached sampler key or source ID is duplicated", index);
    }
  }

  for (std::size_t index = 0U; index < projections.size(); ++index) {
    const OgreNextDemoCachedProjectionPublicationInput &input =
        projections[index];
    if (textures_by_key.find(input.texture_key) == textures_by_key.end() ||
        samplers_by_key.find(input.sampler_key) == samplers_by_key.end()) {
      return Failure(Render::ValidationCode::MISSING_REFERENCE,
                     "ogre_next_demo.material.publication.dependency",
                     "cached projection texture or sampler owner is absent",
                     index);
    }
  }

  std::map<std::string, bool, std::less<>> used_keys;
  for (std::size_t index = 0U; index < used_projection_keys.size(); ++index) {
    const std::string &key = used_projection_keys[index];
    if (key.empty() || catalog_by_key.find(key) == catalog_by_key.end()) {
      return Failure(
          Render::ValidationCode::MISSING_REFERENCE,
          "ogre_next_demo.material.publication.used_projection",
          "frame-reachable projection is absent from the frozen cache", index);
    }
    if (!used_keys.emplace(key, true).second) {
      return Failure(Render::ValidationCode::DUPLICATE_IDENTIFIER,
                     "ogre_next_demo.material.publication.used_projection",
                     "frame-reachable projection key is duplicated", index);
    }
  }

  OgreNextDemoCachedProjectionPublicationTransaction candidate;
  candidate.owner_catalog.reserve(projections.size());
  candidate.frame_root_material_source_ids.reserve(used_keys.size());
  std::map<std::string, bool, std::less<>> observed_authenticated_textures;
  std::map<std::string, bool, std::less<>> observed_ordinary_textures;
  for (const OgreNextDemoCachedProjectionPublicationInput &input :
       projections) {
    const bool frame_reachable =
        used_keys.find(input.projection_key) != used_keys.end();
    const OgreNextDemoCachedTexturePublicationInput &texture =
        *textures_by_key.find(input.texture_key)->second;
    const OgreNextDemoCachedSamplerPublicationInput &sampler =
        *samplers_by_key.find(input.sampler_key)->second;
    OgreNextDemoCachedProjectionPublicationOwner owner;
    owner.projection_key = input.projection_key;
    owner.material_source_id = input.material_source_id;
    owner.texture_source_id = texture.texture_source_id;
    owner.sampler_source_id = sampler.sampler_source_id;
    owner.frame_reachable = frame_reachable;
    candidate.owner_catalog.push_back(std::move(owner));
    if (!frame_reachable) {
      continue;
    }
    candidate.frame_root_material_source_ids.push_back(
        input.material_source_id);
    if (IsOgreNextDemoAuthenticatedTextureSourceMode(texture.source_mode)) {
      if (observed_authenticated_textures.emplace(input.texture_key, true)
              .second) {
        candidate.authenticated_texture_keys.push_back(input.texture_key);
      }
    } else if (observed_ordinary_textures.emplace(input.texture_key, true)
                   .second) {
      candidate.ordinary_texture_keys.push_back(input.texture_key);
    }
  }
  if (!candidate.authenticated_texture_keys.empty()) {
    Render::ValidationResult validation =
        validator.ValidateReachableAuthenticatedTextureBatch(
            candidate.authenticated_texture_keys);
    if (!validation) {
      validation.field =
          "ogre_next_demo.material.publication." + validation.field;
      return validation;
    }
  }
  if (!candidate.ordinary_texture_keys.empty()) {
    Render::ValidationResult validation =
        validator.ValidateReachableOrdinaryTextureBatch(
            candidate.ordinary_texture_keys);
    if (!validation) {
      validation.field =
          "ogre_next_demo.material.publication." + validation.field;
      return validation;
    }
  }
  output = std::move(candidate);
  return Render::ValidationResult::Success();
}

Render::ValidationResult SelectOgreNextDemoTextureSourceMode(
    bool authenticated_source_required, bool authenticated_resolution_attempted,
    const Render::ValidationResult &authenticated_resolution_result,
    OgreNextDemoTextureSourceMode authenticated_resolution_mode,
    bool ordinary_resolution_attempted,
    const Render::ValidationResult &ordinary_resolution_result,
    OgreNextDemoTextureSourceSelection &output) {
  if (authenticated_source_required) {
    if (ordinary_resolution_attempted || !ordinary_resolution_result) {
      return Failure(
          Render::ValidationCode::SEQUENCE_MISMATCH,
          "ogre_next_demo.material.authenticated.ordinary_resolution",
          "authenticated-required texture probed ordinary source authority");
    }
    if (!authenticated_resolution_attempted) {
      return Failure(
          Render::ValidationCode::MISSING_REFERENCE,
          "ogre_next_demo.material.authenticated.resolution",
          "authenticated-required texture has no source resolution attempt");
    }
    if (!authenticated_resolution_result) {
      return authenticated_resolution_result;
    }
    if (!IsOgreNextDemoAuthenticatedTextureSourceMode(
            authenticated_resolution_mode)) {
      return Failure(Render::ValidationCode::INVALID_ENUM,
                     "ogre_next_demo.material.authenticated.source_kind",
                     "authenticated resolution has no exact archive/generated "
                     "source kind");
    }
    OgreNextDemoTextureSourceSelection candidate;
    candidate.selected = true;
    candidate.mode = authenticated_resolution_mode;
    candidate.exclusion = OgreNextDemoTextureProjectionExclusion::NONE;
    output = candidate;
    return Render::ValidationResult::Success();
  }
  if (authenticated_resolution_attempted) {
    return Failure(
        Render::ValidationCode::SEQUENCE_MISMATCH,
        "ogre_next_demo.material.unauthenticated.resolution",
        "ordinary texture must not probe authenticated source authority");
  }
  if (!authenticated_resolution_result) {
    return Failure(
        Render::ValidationCode::SEQUENCE_MISMATCH,
        "ogre_next_demo.material.unauthenticated.resolution_result",
        "ordinary texture received a stale source-resolution failure");
  }
  OgreNextDemoTextureSourceSelection candidate;
  candidate.mode =
      OgreNextDemoTextureSourceMode::ORDINARY_OBSERVED_SOURCE_BYTES;
  if (!ordinary_resolution_attempted) {
    candidate.selected = false;
    candidate.exclusion = OgreNextDemoTextureProjectionExclusion::
        ORDINARY_SELECTED_SOURCE_UNAVAILABLE;
    output = candidate;
    return Render::ValidationResult::Success();
  }
  if (!ordinary_resolution_result) {
    const bool honestly_absent =
        ordinary_resolution_result.code ==
            Render::ValidationCode::MISSING_REFERENCE &&
        (ordinary_resolution_result.field ==
             "selected_texture_registry.resource_lookup" ||
         ordinary_resolution_result.field ==
             "selected_texture_resolution.group_generation" ||
         ordinary_resolution_result.field ==
             "selected_texture_resolution.package_marker");
    if (!honestly_absent) {
      return ordinary_resolution_result;
    }
    candidate.selected = false;
    candidate.exclusion = OgreNextDemoTextureProjectionExclusion::
        ORDINARY_SELECTED_SOURCE_UNAVAILABLE;
    output = candidate;
    return Render::ValidationResult::Success();
  }
  candidate.selected = true;
  candidate.exclusion = OgreNextDemoTextureProjectionExclusion::NONE;
  output = candidate;
  return Render::ValidationResult::Success();
}

Render::ValidationResult ValidateOgreNextDemoCachedTextureSourceAuthority(
    OgreNextDemoTextureSourceMode frozen_mode, bool frame_reachable,
    bool source_classification_matches, bool fresh_resolution_attempted,
    const Render::ValidationResult &fresh_resolution_result,
    bool immutable_receipt_matches) {
  if (!IsOgreNextDemoAuthenticatedTextureSourceMode(frozen_mode) &&
      frozen_mode !=
          OgreNextDemoTextureSourceMode::ORDINARY_OBSERVED_SOURCE_BYTES) {
    return Failure(Render::ValidationCode::INVALID_ENUM,
                   "ogre_next_demo.material.cached.source_mode",
                   "cached texture source mode is invalid");
  }
  if (!frame_reachable) {
    if (fresh_resolution_attempted || !fresh_resolution_result ||
        immutable_receipt_matches) {
      return Failure(
          Render::ValidationCode::SEQUENCE_MISMATCH,
          "ogre_next_demo.material.cached.unreachable",
          "unreachable anti-tombstone owner probed live texture authority");
    }
    return Render::ValidationResult::Success();
  }

  if (!source_classification_matches) {
    return Failure(
        Render::ValidationCode::REVISION_MISMATCH,
        "ogre_next_demo.material.cached.source_classification",
        "cached texture changed selected source authority classification");
  }
  if (!fresh_resolution_attempted) {
    return Failure(Render::ValidationCode::MISSING_REFERENCE,
                   "ogre_next_demo.material.cached.fresh_resolution",
                   "reachable source-byte texture has no fresh resolution");
  }
  if (!fresh_resolution_result) {
    return fresh_resolution_result;
  }
  if (!immutable_receipt_matches) {
    return Failure(
        Render::ValidationCode::REVISION_MISMATCH,
        "ogre_next_demo.material.cached.immutable_receipt",
        "fresh source receipt differs from the frozen immutable state");
  }
  return Render::ValidationResult::Success();
}

Render::ValidationResult ValidateOgreNextDemoSampling(
    const OgreNextDemoSamplingObservation &observation) {
  if (!observation.ordinary_texture) {
    return Failure(Render::ValidationCode::UNSUPPORTED_FEATURE,
                   "ogre_next_demo.terrain.sampling.ordinary",
                   "TUS0 must be a named, single-frame, loaded 2D texture "
                   "without UAV access");
  }
  if (!observation.uv0_identity) {
    return Failure(
        Render::ValidationCode::UNSUPPORTED_FEATURE,
        "ogre_next_demo.terrain.sampling.uv",
        "TUS0 must use UV0 with no generation, effects, or transform");
  }
  if (!observation.sampler_identity) {
    return Failure(Render::ValidationCode::UNSUPPORTED_FEATURE,
                   "ogre_next_demo.terrain.sampling.sampler",
                   "TUS0 must use clamp U/V/W, linear min/mag, nearest mip, "
                   "and no comparison");
  }
  if (!observation.gamma_disabled) {
    return Failure(Render::ValidationCode::UNSUPPORTED_FEATURE,
                   "ogre_next_demo.terrain.sampling.gamma",
                   "display-domain filtering requires native hardware gamma "
                   "decode to remain disabled");
  }
  if (!observation.fog_disabled) {
    return Failure(Render::ValidationCode::UNSUPPORTED_FEATURE,
                   "ogre_next_demo.terrain.sampling.fog",
                   "the disposable opaque terrain lowering cannot preserve "
                   "OGRE scene fog");
  }
  return Render::ValidationResult::Success();
}

Render::ValidationResult
RevalidateOgreNextDemoSampling(const OgreNextDemoSamplingObservation &before,
                               const OgreNextDemoSamplingObservation &after) {
  Render::ValidationResult validation = ValidateOgreNextDemoSampling(before);
  if (!validation) {
    return validation;
  }
  validation = ValidateOgreNextDemoSampling(after);
  if (!validation) {
    return validation;
  }
  if (before.exact_native_state.empty() || after.exact_native_state.empty() ||
      before.exact_native_state != after.exact_native_state) {
    return Failure(Render::ValidationCode::REVISION_MISMATCH,
                   "ogre_next_demo.terrain.readback.revalidation",
                   "terrain, TUS0, sampler, texture, or mip state mutated "
                   "during readback");
  }
  return Render::ValidationResult::Success();
}

Render::ValidationResult
CompleteOgreNextDemoOpaqueMipChain(Render::TextureResourceDescriptor &texture) {
  if (texture.type != Render::TextureResourceType::TEXTURE_2D ||
      texture.format != Render::TextureResourceFormat::RGBA8_UNORM ||
      texture.color_space != Render::TextureColorSpace::SRGB ||
      texture.array_layers != 1U || texture.width == 0U ||
      texture.height == 0U || texture.mip_levels.size() != 1U) {
    return Failure(
        Render::ValidationCode::SIZE_MISMATCH,
        "ogre_next_demo.terrain.texture.full_mip_chain",
        "opaque lowering requires exactly one fresh SRGB RGBA8 2D base level");
  }

  const Render::TextureMipLevelDescriptor &base = texture.mip_levels.front();
  const std::uint64_t row_bytes =
      static_cast<std::uint64_t>(texture.width) * 4U;
  if (texture.height != 0U &&
      row_bytes >
          (std::numeric_limits<std::uint64_t>::max)() / texture.height) {
    return Failure(Render::ValidationCode::SIZE_MISMATCH,
                   "ogre_next_demo.terrain.texture.mip_layout",
                   "RGBA8 base-level byte count overflows", 0U);
  }
  const std::uint64_t layer_bytes = row_bytes * texture.height;
  if (layer_bytes > static_cast<std::uint64_t>(
                        (std::numeric_limits<std::size_t>::max)()) ||
      base.width != texture.width || base.height != texture.height ||
      base.row_pitch_bytes != row_bytes ||
      base.layer_pitch_bytes != layer_bytes ||
      base.bytes.size() != static_cast<std::size_t>(layer_bytes)) {
    return Failure(Render::ValidationCode::SIZE_MISMATCH,
                   "ogre_next_demo.terrain.texture.mip_layout",
                   "opaque lowering requires an exact tight RGBA8 base layout",
                   0U);
  }

  // Validation above completes before the first write, so malformed input is
  // transactionally unchanged. Only the fourth byte of a native base texel is
  // touched; its RGB triplet remains byte-identical to the fresh readback.
  for (std::size_t alpha = 3U; alpha < texture.mip_levels.front().bytes.size();
       alpha += 4U) {
    texture.mip_levels.front().bytes[alpha] = 255U;
  }

  while (texture.mip_levels.size() <
         CompleteMipCount(texture.width, texture.height)) {
    const Render::TextureMipLevelDescriptor &source = texture.mip_levels.back();
    Render::TextureMipLevelDescriptor destination;
    destination.width = (std::max)(1U, source.width / 2U);
    destination.height = (std::max)(1U, source.height / 2U);
    destination.row_pitch_bytes =
        static_cast<std::uint64_t>(destination.width) * 4U;
    destination.layer_pitch_bytes =
        destination.row_pitch_bytes * destination.height;
    if (destination.layer_pitch_bytes >
        static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)())) {
      return Failure(Render::ValidationCode::SIZE_MISMATCH,
                     "ogre_next_demo.terrain.texture.generated_mip",
                     "generated mip allocation exceeds host address space");
    }
    destination.bytes.resize(
        static_cast<std::size_t>(destination.layer_pitch_bytes));

    for (std::uint32_t y = 0U; y < destination.height; ++y) {
      const std::uint32_t source_y0 = y * 2U;
      const std::uint32_t source_y1 =
          (std::min)(source_y0 + 1U, source.height - 1U);
      for (std::uint32_t x = 0U; x < destination.width; ++x) {
        const std::uint32_t source_x0 = x * 2U;
        const std::uint32_t source_x1 =
            (std::min)(source_x0 + 1U, source.width - 1U);
        const std::size_t offsets[4U] = {
            static_cast<std::size_t>(source_y0) * source.row_pitch_bytes +
                static_cast<std::size_t>(source_x0) * 4U,
            static_cast<std::size_t>(source_y0) * source.row_pitch_bytes +
                static_cast<std::size_t>(source_x1) * 4U,
            static_cast<std::size_t>(source_y1) * source.row_pitch_bytes +
                static_cast<std::size_t>(source_x0) * 4U,
            static_cast<std::size_t>(source_y1) * source.row_pitch_bytes +
                static_cast<std::size_t>(source_x1) * 4U,
        };
        const std::size_t output =
            static_cast<std::size_t>(y) * destination.row_pitch_bytes +
            static_cast<std::size_t>(x) * 4U;
        for (std::size_t channel = 0U; channel < 3U; ++channel) {
          const std::uint32_t sum =
              static_cast<std::uint32_t>(source.bytes[offsets[0U] + channel]) +
              static_cast<std::uint32_t>(source.bytes[offsets[1U] + channel]) +
              static_cast<std::uint32_t>(source.bytes[offsets[2U] + channel]) +
              static_cast<std::uint32_t>(source.bytes[offsets[3U] + channel]);
          // Round to nearest integer with a deterministic half-up rule. This
          // operates on encoded bytes because the material contract filters in
          // display space and decodes only after sampling.
          destination.bytes[output + channel] =
              static_cast<std::uint8_t>((sum + 2U) / 4U);
        }
        destination.bytes[output + 3U] = 255U;
      }
    }
    texture.mip_levels.push_back(std::move(destination));
  }
  return Render::ValidationResult::Success();
}

Render::ValidationResult ResolveOgreNextDemoBc1AlphaMode(
    bool legacy_dxt1, OgreNextDemoTextureAlphaPolicy alpha_policy,
    bool authoritative_one_bit_alpha,
    Render::Ogre14SourceTextureBc1AlphaMode &output) noexcept {
  if (alpha_policy != OgreNextDemoTextureAlphaPolicy::FORCE_OPAQUE &&
      alpha_policy != OgreNextDemoTextureAlphaPolicy::PRESERVE_STRAIGHT) {
    return Failure(Render::ValidationCode::INVALID_ENUM,
                   "ogre_next_demo.material.bc1_alpha.alpha_policy",
                   "texture alpha normalization policy is invalid");
  }
  Render::Ogre14SourceTextureBc1AlphaMode candidate =
      Render::Ogre14SourceTextureBc1AlphaMode::NOT_APPLICABLE;
  if (legacy_dxt1) {
    if (alpha_policy == OgreNextDemoTextureAlphaPolicy::PRESERVE_STRAIGHT &&
        !authoritative_one_bit_alpha) {
      return Failure(
          Render::ValidationCode::MISSING_REFERENCE,
          "ogre_next_demo.material.bc1_alpha.authority",
          "legacy DXT1 needs explicit opaque-versus-one-bit alpha authority");
    }
    candidate = alpha_policy == OgreNextDemoTextureAlphaPolicy::FORCE_OPAQUE
                    ? Render::Ogre14SourceTextureBc1AlphaMode::OPAQUE_COLOR
                    : Render::Ogre14SourceTextureBc1AlphaMode::ONE_BIT_ALPHA;
  }
  output = candidate;
  return Render::ValidationResult::Success();
}

Render::ValidationResult CompleteOgreNextDemoSrgbPbrMipChain(
    Render::TextureResourceDescriptor &texture,
    OgreNextDemoTextureAlphaPolicy alpha_policy,
    OgreNextDemoTextureNormalizationObservation *observation) {
  if (texture.type != Render::TextureResourceType::TEXTURE_2D ||
      texture.format != Render::TextureResourceFormat::RGBA8_UNORM ||
      texture.color_space != Render::TextureColorSpace::SRGB ||
      texture.array_layers != 1U || texture.width == 0U ||
      texture.height == 0U || texture.mip_levels.empty() ||
      texture.mip_levels.size() >
          CompleteMipCount(texture.width, texture.height)) {
    return Failure(Render::ValidationCode::SIZE_MISMATCH,
                   "ogre_next_demo.material.texture.full_mip_chain",
                   "sRGB PBR lowering requires a canonical nonempty authored "
                   "SRGB RGBA8 2D mip prefix");
  }
  if (alpha_policy != OgreNextDemoTextureAlphaPolicy::FORCE_OPAQUE &&
      alpha_policy != OgreNextDemoTextureAlphaPolicy::PRESERVE_STRAIGHT) {
    return Failure(Render::ValidationCode::INVALID_ENUM,
                   "ogre_next_demo.material.texture.alpha_policy",
                   "sRGB PBR alpha normalization policy is invalid");
  }

  std::uint32_t expected_width = texture.width;
  std::uint32_t expected_height = texture.height;
  for (std::size_t level = 0U; level < texture.mip_levels.size(); ++level) {
    const Render::TextureMipLevelDescriptor &mip = texture.mip_levels[level];
    const std::uint64_t row_bytes =
        static_cast<std::uint64_t>(expected_width) * 4U;
    if (expected_height != 0U &&
        row_bytes >
            (std::numeric_limits<std::uint64_t>::max)() / expected_height) {
      return Failure(Render::ValidationCode::SIZE_MISMATCH,
                     "ogre_next_demo.material.texture.mip_layout",
                     "RGBA8 authored mip byte count overflows", level);
    }
    const std::uint64_t layer_bytes = row_bytes * expected_height;
    if (layer_bytes > static_cast<std::uint64_t>(
                          (std::numeric_limits<std::size_t>::max)()) ||
        mip.width != expected_width || mip.height != expected_height ||
        mip.row_pitch_bytes != row_bytes ||
        mip.layer_pitch_bytes != layer_bytes ||
        mip.bytes.size() != static_cast<std::size_t>(layer_bytes)) {
      return Failure(
          Render::ValidationCode::SIZE_MISMATCH,
          "ogre_next_demo.material.texture.mip_layout",
          "sRGB PBR lowering requires an exact tight authored mip prefix",
          level);
    }
    expected_width = (std::max)(1U, expected_width / 2U);
    expected_height = (std::max)(1U, expected_height / 2U);
  }

  const std::size_t authored_mip_count = texture.mip_levels.size();
  // Work on a complete candidate so every validation failure leaves every
  // authored source level byte-for-byte unchanged.
  Render::TextureResourceDescriptor candidate = texture;
  if (alpha_policy == OgreNextDemoTextureAlphaPolicy::FORCE_OPAQUE) {
    for (Render::TextureMipLevelDescriptor &mip : candidate.mip_levels) {
      for (std::size_t alpha = 3U; alpha < mip.bytes.size(); alpha += 4U) {
        mip.bytes[alpha] = 255U;
      }
    }
  }

  while (candidate.mip_levels.size() <
         CompleteMipCount(candidate.width, candidate.height)) {
    const Render::TextureMipLevelDescriptor &source =
        candidate.mip_levels.back();
    Render::TextureMipLevelDescriptor destination;
    destination.width = (std::max)(1U, source.width / 2U);
    destination.height = (std::max)(1U, source.height / 2U);
    destination.row_pitch_bytes =
        static_cast<std::uint64_t>(destination.width) * 4U;
    destination.layer_pitch_bytes =
        destination.row_pitch_bytes * destination.height;
    if (destination.layer_pitch_bytes >
        static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)())) {
      return Failure(Render::ValidationCode::SIZE_MISMATCH,
                     "ogre_next_demo.material.texture.generated_mip",
                     "generated mip allocation exceeds host address space");
    }
    destination.bytes.resize(
        static_cast<std::size_t>(destination.layer_pitch_bytes));

    for (std::uint32_t y = 0U; y < destination.height; ++y) {
      const std::uint32_t source_y0 = y * 2U;
      const std::uint32_t source_y1 =
          (std::min)(source_y0 + 1U, source.height - 1U);
      for (std::uint32_t x = 0U; x < destination.width; ++x) {
        const std::uint32_t source_x0 = x * 2U;
        const std::uint32_t source_x1 =
            (std::min)(source_x0 + 1U, source.width - 1U);
        const std::size_t offsets[4U] = {
            static_cast<std::size_t>(source_y0) * source.row_pitch_bytes +
                static_cast<std::size_t>(source_x0) * 4U,
            static_cast<std::size_t>(source_y0) * source.row_pitch_bytes +
                static_cast<std::size_t>(source_x1) * 4U,
            static_cast<std::size_t>(source_y1) * source.row_pitch_bytes +
                static_cast<std::size_t>(source_x0) * 4U,
            static_cast<std::size_t>(source_y1) * source.row_pitch_bytes +
                static_cast<std::size_t>(source_x1) * 4U,
        };
        const std::size_t output =
            static_cast<std::size_t>(y) * destination.row_pitch_bytes +
            static_cast<std::size_t>(x) * 4U;
        std::uint32_t alpha_sum = 0U;
        for (const std::size_t offset : offsets) {
          alpha_sum += source.bytes[offset + 3U];
        }
        for (std::size_t channel = 0U; channel < 3U; ++channel) {
          if (alpha_policy ==
              OgreNextDemoTextureAlphaPolicy::FORCE_OPAQUE) {
            const std::uint64_t linear_sum =
                static_cast<std::uint64_t>(
                    kSrgbLinearQ32[source.bytes[offsets[0U] + channel]]) +
                static_cast<std::uint64_t>(
                    kSrgbLinearQ32[source.bytes[offsets[1U] + channel]]) +
                static_cast<std::uint64_t>(
                    kSrgbLinearQ32[source.bytes[offsets[2U] + channel]]) +
                static_cast<std::uint64_t>(
                    kSrgbLinearQ32[source.bytes[offsets[3U] + channel]]);
            destination.bytes[output + channel] =
                EncodeSrgbQ32Average(linear_sum);
          } else if (alpha_sum == 0U) {
            // Transparent colour is unobservable. Canonical black prevents
            // arbitrary hidden RGB from being amplified by unpremultiply.
            destination.bytes[output + channel] = 0U;
          } else {
            std::uint64_t premultiplied_linear_sum = 0U;
            for (const std::size_t offset : offsets) {
              premultiplied_linear_sum +=
                  static_cast<std::uint64_t>(
                      kSrgbLinearQ32[source.bytes[offset + channel]]) *
                  source.bytes[offset + 3U];
            }
            const std::uint64_t straight_linear =
                (premultiplied_linear_sum + alpha_sum / 2U) / alpha_sum;
            destination.bytes[output + channel] = EncodeSrgbQ32(
                static_cast<std::uint32_t>((std::min)(
                    straight_linear,
                    static_cast<std::uint64_t>(
                        (std::numeric_limits<std::uint32_t>::max)()))));
          }
        }
        destination.bytes[output + 3U] =
            alpha_policy == OgreNextDemoTextureAlphaPolicy::FORCE_OPAQUE
                ? 255U
                : static_cast<std::uint8_t>((alpha_sum + 2U) / 4U);
      }
    }
    candidate.mip_levels.push_back(std::move(destination));
  }

  Render::ValidationResult validation =
      Render::ValidateTextureResourceDescriptor(candidate);
  if (!validation) {
    validation.field = "ogre_next_demo.material.texture." + validation.field;
    return validation;
  }
  OgreNextDemoTextureNormalizationObservation candidate_observation;
  candidate_observation.policy =
      alpha_policy == OgreNextDemoTextureAlphaPolicy::FORCE_OPAQUE
          ? OgreNextDemoTextureNormalizationObservation::Policy::
                SRGB_OPAQUE_V2
          : OgreNextDemoTextureNormalizationObservation::Policy::
                SRGB_STRAIGHT_ALPHA_V1;
  candidate_observation.policy_version =
      alpha_policy == OgreNextDemoTextureAlphaPolicy::FORCE_OPAQUE
          ? kOgreNextDemoModernSourceNormalizationPolicyVersion
          : kOgreNextDemoStraightAlphaNormalizationPolicyVersion;
  candidate_observation.authored_mip_prefix_levels = authored_mip_count;
  candidate_observation.generated_mip_tail_levels =
      candidate.mip_levels.size() - authored_mip_count;
  texture = std::move(candidate);
  if (observation != nullptr) {
    *observation = candidate_observation;
  }
  return Render::ValidationResult::Success();
}

Render::ValidationResult BuildOgreNextDemoSrgbPbrTextureFromDecodedSource(
    Render::Ogre14DecodedSourceTexture decoded,
    std::uint32_t expected_native_width, std::uint32_t expected_native_height,
    std::string_view debug_name, OgreNextDemoTextureAlphaPolicy alpha_policy,
    Render::TextureResourceDescriptor &output,
    OgreNextDemoTextureNormalizationObservation *observation) {
  if (alpha_policy != OgreNextDemoTextureAlphaPolicy::FORCE_OPAQUE &&
      alpha_policy != OgreNextDemoTextureAlphaPolicy::PRESERVE_STRAIGHT) {
    return Failure(Render::ValidationCode::INVALID_ENUM,
                   "ogre_next_demo.material.authenticated.alpha_policy",
                   "decoded sRGB alpha normalization policy is invalid");
  }
  if (decoded.version != Render::kOgre14DecodedSourceTextureVersion ||
      decoded.width == 0U || decoded.height == 0U ||
      decoded.width != expected_native_width ||
      decoded.height != expected_native_height || debug_name.empty() ||
      decoded.color_semantic !=
          Render::Ogre14SourceTextureColorSemantic::SRGB_COLOR ||
      decoded.mip_levels.empty() ||
      decoded.mip_levels.size() >
          CompleteMipCount(decoded.width, decoded.height)) {
    return Failure(Render::ValidationCode::REVISION_MISMATCH,
                   "ogre_next_demo.material.authenticated.decoded_identity",
                   "decoded source schema, dimensions, semantic, or mip count "
                   "disagrees with the loaded texture");
  }
  const Render::Ogre14SourceTextureBc1AlphaMode expected_bc1_alpha =
      alpha_policy == OgreNextDemoTextureAlphaPolicy::PRESERVE_STRAIGHT
          ? Render::Ogre14SourceTextureBc1AlphaMode::ONE_BIT_ALPHA
          : Render::Ogre14SourceTextureBc1AlphaMode::OPAQUE_COLOR;
  if ((decoded.source_format == Render::Ogre14SourceTextureFormat::BC1_UNORM &&
       decoded.bc1_alpha_mode != expected_bc1_alpha) ||
      (decoded.source_format != Render::Ogre14SourceTextureFormat::BC1_UNORM &&
       decoded.bc1_alpha_mode !=
           Render::Ogre14SourceTextureBc1AlphaMode::NOT_APPLICABLE)) {
    return Failure(Render::ValidationCode::INVALID_ENUM,
                   "ogre_next_demo.material.authenticated.bc1_alpha_mode",
                   "product projection requires the frozen alpha-policy BC1 "
                   "interpretation only for BC1 sources");
  }

  std::uint32_t mip_width = decoded.width;
  std::uint32_t mip_height = decoded.height;
  for (std::size_t level = 0U; level < decoded.mip_levels.size(); ++level) {
    const Render::Ogre14DecodedSourceTextureMip &mip =
        decoded.mip_levels[level];
    const std::uint64_t row_bytes = static_cast<std::uint64_t>(mip_width) * 4U;
    const std::uint64_t slice_bytes = row_bytes * mip_height;
    if (mip.version != Render::kOgre14DecodedSourceTextureMipVersion ||
        mip.width != mip_width || mip.height != mip_height ||
        mip.row_pitch_bytes != row_bytes ||
        mip.slice_pitch_bytes != slice_bytes ||
        slice_bytes > static_cast<std::uint64_t>(
                          (std::numeric_limits<std::size_t>::max)()) ||
        mip.rgba8_unorm.size() != static_cast<std::size_t>(slice_bytes)) {
      return Failure(
          Render::ValidationCode::SIZE_MISMATCH,
          "ogre_next_demo.material.authenticated.decoded_mip",
          "decoded source mip prefix is not canonical tight RGBA8 geometry",
          level);
    }
    mip_width = (std::max)(1U, mip_width / 2U);
    mip_height = (std::max)(1U, mip_height / 2U);
  }

  Render::TextureResourceDescriptor candidate;
  candidate.debug_name.assign(debug_name.data(), debug_name.size());
  candidate.type = Render::TextureResourceType::TEXTURE_2D;
  candidate.format = Render::TextureResourceFormat::RGBA8_UNORM;
  candidate.color_space = Render::TextureColorSpace::SRGB;
  candidate.width = decoded.width;
  candidate.height = decoded.height;
  candidate.array_layers = 1U;
  candidate.mip_levels.reserve(decoded.mip_levels.size());
  for (Render::Ogre14DecodedSourceTextureMip &decoded_mip :
       decoded.mip_levels) {
    Render::TextureMipLevelDescriptor mip;
    mip.width = decoded_mip.width;
    mip.height = decoded_mip.height;
    mip.row_pitch_bytes = decoded_mip.row_pitch_bytes;
    mip.layer_pitch_bytes = decoded_mip.slice_pitch_bytes;
    mip.bytes = std::move(decoded_mip.rgba8_unorm);
    candidate.mip_levels.push_back(std::move(mip));
  }

  OgreNextDemoTextureNormalizationObservation candidate_observation;
  Render::ValidationResult validation =
      CompleteOgreNextDemoSrgbPbrMipChain(candidate, alpha_policy,
                                          &candidate_observation);
  if (!validation) {
    return validation;
  }
  validation = Render::ValidateTextureResourceDescriptor(candidate);
  if (!validation) {
    validation.field =
        "ogre_next_demo.material.authenticated.texture." + validation.field;
    return validation;
  }
  output = std::move(candidate);
  if (observation != nullptr) {
    *observation = candidate_observation;
  }
  return Render::ValidationResult::Success();
}

Render::ValidationResult
BuildOgreNextDemoLinearSpecularTextureFromDecodedSource(
    Render::Ogre14DecodedSourceTexture decoded,
    std::uint32_t expected_native_width, std::uint32_t expected_native_height,
    std::string_view debug_name, Render::TextureResourceDescriptor &output,
    OgreNextDemoTextureNormalizationObservation *observation) {
  if (decoded.version != Render::kOgre14DecodedSourceTextureVersion ||
      decoded.width == 0U || decoded.height == 0U ||
      decoded.width != expected_native_width ||
      decoded.height != expected_native_height || debug_name.empty() ||
      decoded.color_semantic !=
          Render::Ogre14SourceTextureColorSemantic::LINEAR_DATA ||
      decoded.mip_levels.empty() ||
      decoded.mip_levels.size() >
          CompleteMipCount(decoded.width, decoded.height)) {
    return Failure(
        Render::ValidationCode::REVISION_MISMATCH,
        "ogre_next_demo.material.specular.decoded_identity",
        "decoded specular schema, dimensions, linear semantic, or mip count disagrees with the loaded source");
  }
  if ((decoded.source_format == Render::Ogre14SourceTextureFormat::BC1_UNORM &&
       decoded.bc1_alpha_mode !=
           Render::Ogre14SourceTextureBc1AlphaMode::OPAQUE_COLOR) ||
      (decoded.source_format != Render::Ogre14SourceTextureFormat::BC1_UNORM &&
       decoded.bc1_alpha_mode !=
           Render::Ogre14SourceTextureBc1AlphaMode::NOT_APPLICABLE)) {
    return Failure(Render::ValidationCode::INVALID_ENUM,
                   "ogre_next_demo.material.specular.bc1_alpha_mode",
                   "linear specular projection requires opaque BC1 interpretation only for BC1 sources");
  }

  Render::TextureResourceDescriptor candidate;
  candidate.debug_name.assign(debug_name.data(), debug_name.size());
  candidate.type = Render::TextureResourceType::TEXTURE_2D;
  candidate.format = Render::TextureResourceFormat::RGBA8_UNORM;
  candidate.color_space = Render::TextureColorSpace::LINEAR;
  candidate.width = decoded.width;
  candidate.height = decoded.height;
  candidate.array_layers = 1U;
  candidate.mip_levels.reserve(
      CompleteMipCount(candidate.width, candidate.height));

  std::uint32_t mip_width = decoded.width;
  std::uint32_t mip_height = decoded.height;
  for (std::size_t level = 0U; level < decoded.mip_levels.size(); ++level) {
    Render::Ogre14DecodedSourceTextureMip &decoded_mip =
        decoded.mip_levels[level];
    const std::uint64_t row_bytes =
        static_cast<std::uint64_t>(mip_width) * 4U;
    const std::uint64_t slice_bytes = row_bytes * mip_height;
    if (decoded_mip.version !=
            Render::kOgre14DecodedSourceTextureMipVersion ||
        decoded_mip.width != mip_width ||
        decoded_mip.height != mip_height ||
        decoded_mip.row_pitch_bytes != row_bytes ||
        decoded_mip.slice_pitch_bytes != slice_bytes ||
        slice_bytes > static_cast<std::uint64_t>(
                          (std::numeric_limits<std::size_t>::max)()) ||
        decoded_mip.rgba8_unorm.size() !=
            static_cast<std::size_t>(slice_bytes)) {
      return Failure(Render::ValidationCode::SIZE_MISMATCH,
                     "ogre_next_demo.material.specular.decoded_mip",
                     "decoded linear specular mip prefix is not canonical tight RGBA8 geometry",
                     level);
    }
    Render::TextureMipLevelDescriptor mip;
    mip.width = mip_width;
    mip.height = mip_height;
    mip.row_pitch_bytes = row_bytes;
    mip.layer_pitch_bytes = slice_bytes;
    mip.bytes = std::move(decoded_mip.rgba8_unorm);
    for (std::size_t alpha = 3U; alpha < mip.bytes.size(); alpha += 4U) {
      mip.bytes[alpha] = 255U;
    }
    candidate.mip_levels.push_back(std::move(mip));
    mip_width = (std::max)(1U, mip_width / 2U);
    mip_height = (std::max)(1U, mip_height / 2U);
  }
  const std::size_t authored_mip_count = candidate.mip_levels.size();

  while (candidate.mip_levels.size() <
         CompleteMipCount(candidate.width, candidate.height)) {
    const Render::TextureMipLevelDescriptor &source =
        candidate.mip_levels.back();
    Render::TextureMipLevelDescriptor destination;
    destination.width = (std::max)(1U, source.width / 2U);
    destination.height = (std::max)(1U, source.height / 2U);
    destination.row_pitch_bytes =
        static_cast<std::uint64_t>(destination.width) * 4U;
    destination.layer_pitch_bytes =
        destination.row_pitch_bytes * destination.height;
    if (destination.layer_pitch_bytes > static_cast<std::uint64_t>(
                                            (std::numeric_limits<std::size_t>::max)())) {
      return Failure(Render::ValidationCode::SIZE_MISMATCH,
                     "ogre_next_demo.material.specular.generated_mip",
                     "generated linear specular mip exceeds host address space");
    }
    destination.bytes.resize(
        static_cast<std::size_t>(destination.layer_pitch_bytes));
    for (std::uint32_t y = 0U; y < destination.height; ++y) {
      const std::uint32_t source_y0 = y * 2U;
      const std::uint32_t source_y1 =
          (std::min)(source_y0 + 1U, source.height - 1U);
      for (std::uint32_t x = 0U; x < destination.width; ++x) {
        const std::uint32_t source_x0 = x * 2U;
        const std::uint32_t source_x1 =
            (std::min)(source_x0 + 1U, source.width - 1U);
        const std::size_t offsets[4U] = {
            static_cast<std::size_t>(source_y0) * source.row_pitch_bytes +
                static_cast<std::size_t>(source_x0) * 4U,
            static_cast<std::size_t>(source_y0) * source.row_pitch_bytes +
                static_cast<std::size_t>(source_x1) * 4U,
            static_cast<std::size_t>(source_y1) * source.row_pitch_bytes +
                static_cast<std::size_t>(source_x0) * 4U,
            static_cast<std::size_t>(source_y1) * source.row_pitch_bytes +
                static_cast<std::size_t>(source_x1) * 4U,
        };
        const std::size_t output_offset =
            static_cast<std::size_t>(y) * destination.row_pitch_bytes +
            static_cast<std::size_t>(x) * 4U;
        for (std::size_t channel = 0U; channel < 3U; ++channel) {
          const std::uint32_t sum =
              static_cast<std::uint32_t>(source.bytes[offsets[0U] + channel]) +
              static_cast<std::uint32_t>(source.bytes[offsets[1U] + channel]) +
              static_cast<std::uint32_t>(source.bytes[offsets[2U] + channel]) +
              static_cast<std::uint32_t>(source.bytes[offsets[3U] + channel]);
          destination.bytes[output_offset + channel] =
              static_cast<std::uint8_t>((sum + 2U) / 4U);
        }
        destination.bytes[output_offset + 3U] = 255U;
      }
    }
    candidate.mip_levels.push_back(std::move(destination));
  }

  Render::ValidationResult validation =
      Render::ValidateTextureResourceDescriptor(candidate);
  if (!validation) {
    validation.field =
        "ogre_next_demo.material.specular.texture." + validation.field;
    return validation;
  }
  OgreNextDemoTextureNormalizationObservation candidate_observation;
  candidate_observation.policy =
      OgreNextDemoTextureNormalizationObservation::Policy::
          LINEAR_SPECULAR_V1;
  candidate_observation.policy_version =
      kOgreNextDemoLinearSpecularNormalizationPolicyVersion;
  candidate_observation.authored_mip_prefix_levels = authored_mip_count;
  candidate_observation.generated_mip_tail_levels =
      candidate.mip_levels.size() - authored_mip_count;
  output = std::move(candidate);
  if (observation != nullptr) {
    *observation = candidate_observation;
  }
  return Render::ValidationResult::Success();
}

Render::ValidationResult DeriveOgreNextDemoSourceId(std::string_view domain,
                                                    std::string_view exact_key,
                                                    std::uint64_t &source_id) {
  if (domain.empty() || exact_key.empty()) {
    return Failure(Render::ValidationCode::INVALID_IDENTIFIER,
                   "ogre_next_demo.source_id",
                   "source ID domain and exact identity must not be empty");
  }
  std::uint64_t hash = kFnv1a64OffsetBasis;
  const auto append = [&hash](std::string_view bytes) {
    for (const char byte : bytes) {
      hash ^= static_cast<std::uint8_t>(static_cast<unsigned char>(byte));
      hash *= kFnv1a64Prime;
    }
  };
  append(domain);
  const char separator = '\0';
  append(std::string_view(&separator, 1U));
  append(exact_key);
  if (hash == 0U) {
    return Failure(Render::ValidationCode::INVALID_IDENTIFIER,
                   "ogre_next_demo.source_id",
                   "domain-separated identity hashed to reserved zero");
  }
  source_id = hash;
  return Render::ValidationResult::Success();
}

Render::ValidationResult
BuildOgreNextDemoMatteTangents(std::size_t vertex_count,
                               std::vector<Render::Float3> &normals,
                               std::vector<Render::Float4> &tangents) {
  if (vertex_count == 0U) {
    return Failure(Render::ValidationCode::EMPTY_PAYLOAD,
                   "ogre_next_demo.matte_mesh.normals",
                   "demo normal sanitization requires at least one vertex");
  }
  if (!normals.empty() && normals.size() != vertex_count) {
    return Failure(Render::ValidationCode::SIZE_MISMATCH,
                   "ogre_next_demo.matte_mesh.normals",
                   "demo normal stream must be absent or complete");
  }

  constexpr Render::Float3 kFallbackNormal{0.0F, 1.0F, 0.0F};
  std::vector<Render::Float3> candidate_normals = normals;
  if (candidate_normals.empty()) {
    candidate_normals.assign(vertex_count, kFallbackNormal);
  }
  std::vector<Render::Float4> candidate_tangents;
  candidate_tangents.reserve(vertex_count);
  for (std::size_t index = 0U; index < vertex_count; ++index) {
    Render::Float3 &normal = candidate_normals[index];
    const float normal_length_squared =
        normal.x * normal.x + normal.y * normal.y + normal.z * normal.z;
    if (std::isfinite(normal.x) && std::isfinite(normal.y) &&
        std::isfinite(normal.z) && std::isfinite(normal_length_squared) &&
        normal_length_squared > 0.0F) {
      const float inverse_length = 1.0F / std::sqrt(normal_length_squared);
      normal = {normal.x * inverse_length, normal.y * inverse_length,
                normal.z * inverse_length};
      const float sanitized_length_squared =
          normal.x * normal.x + normal.y * normal.y + normal.z * normal.z;
      if (!std::isfinite(sanitized_length_squared) ||
          std::fabs(sanitized_length_squared - 1.0F) > 1.0e-3F) {
        normal = kFallbackNormal;
      }
    } else {
      normal = kFallbackNormal;
    }
    // Cross the normal with the least-parallel fixed axis. The tangent has no
    // material-space consumer in the matte path; it only provides the exact,
    // deterministic RT4 vertex layout while staying orthogonal as a FlexBody
    // normal deforms from frame to frame.
    const Render::Float3 axis = std::fabs(normal.z) < 0.875F
                                    ? Render::Float3{0.0F, 0.0F, 1.0F}
                                    : Render::Float3{0.0F, 1.0F, 0.0F};
    const Render::Float3 crossed{axis.y * normal.z - axis.z * normal.y,
                                 axis.z * normal.x - axis.x * normal.z,
                                 axis.x * normal.y - axis.y * normal.x};
    const float length_squared =
        crossed.x * crossed.x + crossed.y * crossed.y + crossed.z * crossed.z;
    if (!std::isfinite(length_squared) || length_squared <= 0.0F) {
      return Failure(Render::ValidationCode::VALUE_OUT_OF_RANGE,
                     "ogre_next_demo.matte_mesh.tangents",
                     "an authored normal cannot produce a finite matte tangent",
                     index);
    }
    const float inverse_length = 1.0F / std::sqrt(length_squared);
    candidate_tangents.push_back({crossed.x * inverse_length,
                                  crossed.y * inverse_length,
                                  crossed.z * inverse_length, 1.0F});
  }
  normals = std::move(candidate_normals);
  tangents = std::move(candidate_tangents);
  return Render::ValidationResult::Success();
}

Render::ValidationResult
NormalizeOgreNextDemoMatteMesh(Render::MeshResourceDescriptor &mesh) {
  Render::MeshResourceDescriptor candidate = mesh;
  if (candidate.texture_coordinates_0.empty()) {
    candidate.texture_coordinates_0.assign(candidate.positions.size(), {});
  }
  candidate.texture_coordinates_1.clear();
  candidate.colors.clear();
  candidate.velocities.clear();
  Render::ValidationResult validation = BuildOgreNextDemoMatteTangents(
      candidate.positions.size(), candidate.normals, candidate.tangents);
  if (!validation) {
    return validation;
  }

  validation = Render::ValidateMeshResourceDescriptor(candidate);
  if (!validation) {
    validation.field = "ogre_next_demo.matte_mesh." + validation.field;
    return validation;
  }
  mesh = std::move(candidate);
  return Render::ValidationResult::Success();
}

Render::ValidationResult BuildOgreNextDemoStaticCaptureRadius(
    float left, float right, float top, float bottom, float near_plane,
    float far_plane, float target_aspect, float &radius_meters) {
  if (!std::isfinite(left) || !std::isfinite(right) || !std::isfinite(top) ||
      !std::isfinite(bottom) || !std::isfinite(near_plane) ||
      !std::isfinite(far_plane) || !std::isfinite(target_aspect) ||
      !(left < right) || !(bottom < top) || !(near_plane > 0.0F) ||
      !(far_plane > near_plane) || !(target_aspect > 0.0F)) {
    return Failure(Render::ValidationCode::VALUE_OUT_OF_RANGE,
                   "ogre_next_demo.static_capture.frustum",
                   "static capture requires finite ordered perspective "
                   "extents, clip distances, and target aspect");
  }

  const double horizontal_span =
      static_cast<double>(right) - static_cast<double>(left);
  const double vertical_span =
      static_cast<double>(top) - static_cast<double>(bottom);
  const double horizontal_offset =
      std::fabs((static_cast<double>(right) + static_cast<double>(left)) /
                horizontal_span);
  const double vertical_offset = std::fabs(
      (static_cast<double>(top) + static_cast<double>(bottom)) / vertical_span);
  const double half_vertical_slope =
      vertical_span / (2.0 * static_cast<double>(near_plane));
  const double half_horizontal_slope =
      half_vertical_slope * static_cast<double>(target_aspect);
  const double maximum_horizontal_slope =
      half_horizontal_slope * (1.0 + horizontal_offset);
  const double maximum_vertical_slope =
      half_vertical_slope * (1.0 + vertical_offset);
  const double candidate =
      static_cast<double>(far_plane) *
      std::sqrt(1.0 + maximum_horizontal_slope * maximum_horizontal_slope +
                maximum_vertical_slope * maximum_vertical_slope);
  if (!std::isfinite(candidate) || !(candidate > 0.0) ||
      candidate > static_cast<double>((std::numeric_limits<float>::max)())) {
    return Failure(
        Render::ValidationCode::VALUE_OUT_OF_RANGE,
        "ogre_next_demo.static_capture.radius",
        "normalized far-frustum enclosing radius is not representable");
  }
  const float conservative = std::nextafter(
      static_cast<float>(candidate), (std::numeric_limits<float>::infinity)());
  if (!std::isfinite(conservative) || !(conservative > 0.0F)) {
    return Failure(Render::ValidationCode::VALUE_OUT_OF_RANGE,
                   "ogre_next_demo.static_capture.radius",
                   "normalized far-frustum enclosing radius overflowed "
                   "conservative rounding");
  }
  radius_meters = conservative;
  return Render::ValidationResult::Success();
}

Render::ValidationResult ClassifyOgreNextDemoStaticBounds(
    const Render::Bounds3 &world_bounds, const Render::Float3 &camera_position,
    float radius_meters, bool &within_capture_radius) {
  const auto finite = [](float value) { return std::isfinite(value); };
  if (!finite(world_bounds.minimum.x) || !finite(world_bounds.minimum.y) ||
      !finite(world_bounds.minimum.z) || !finite(world_bounds.maximum.x) ||
      !finite(world_bounds.maximum.y) || !finite(world_bounds.maximum.z) ||
      !finite(camera_position.x) || !finite(camera_position.y) ||
      !finite(camera_position.z) || !finite(radius_meters) ||
      world_bounds.minimum.x > world_bounds.maximum.x ||
      world_bounds.minimum.y > world_bounds.maximum.y ||
      world_bounds.minimum.z > world_bounds.maximum.z ||
      !(radius_meters > 0.0F)) {
    return Failure(Render::ValidationCode::VALUE_OUT_OF_RANGE,
                   "ogre_next_demo.static_capture.bounds",
                   "static capture requires a finite ordered world AABB, "
                   "camera, and positive radius");
  }

  const auto separation = [](double value, double minimum, double maximum) {
    if (value < minimum) {
      return minimum - value;
    }
    if (value > maximum) {
      return value - maximum;
    }
    return 0.0;
  };
  const double dx = separation(camera_position.x, world_bounds.minimum.x,
                               world_bounds.maximum.x);
  const double dy = separation(camera_position.y, world_bounds.minimum.y,
                               world_bounds.maximum.y);
  const double dz = separation(camera_position.z, world_bounds.minimum.z,
                               world_bounds.maximum.z);
  const double radius = radius_meters;
  within_capture_radius = dx * dx + dy * dy + dz * dz <= radius * radius;
  return Render::ValidationResult::Success();
}

Render::ValidationResult
OgreNextDemoIdentityRegistry::Register(std::string exact_key,
                                       std::uint64_t source_id) {
  if (exact_key.empty() || source_id == 0U) {
    return Failure(
        Render::ValidationCode::INVALID_IDENTIFIER, "ogre_next_demo.source_id",
        "registered source ID and exact identity must be nonzero and nonempty");
  }
  const auto id_match = keys_by_id_.find(source_id);
  if (id_match != keys_by_id_.end() && id_match->second != exact_key) {
    return Failure(Render::ValidationCode::DUPLICATE_IDENTIFIER,
                   "ogre_next_demo.source_id",
                   "distinct domain-separated identities collided");
  }
  const auto key_match = ids_by_key_.find(exact_key);
  if (key_match != ids_by_key_.end() && key_match->second != source_id) {
    return Failure(Render::ValidationCode::REVISION_MISMATCH,
                   "ogre_next_demo.source_id",
                   "one exact identity changed its source ID");
  }
  keys_by_id_[source_id] = exact_key;
  ids_by_key_[std::move(exact_key)] = source_id;
  return Render::ValidationResult::Success();
}

bool OgreNextDemoIdentityRegistry::Contains(std::string_view exact_key,
                                            std::uint64_t source_id) const {
  const auto match = keys_by_id_.find(source_id);
  return match != keys_by_id_.end() && match->second == exact_key;
}

std::size_t OgreNextDemoIdentityRegistry::size() const noexcept {
  return keys_by_id_.size();
}

bool OgreNextDemoRequiresMatte(std::size_t texture_unit_count,
                               bool has_authored_program) noexcept {
  return texture_unit_count != 0U || has_authored_program;
}

namespace {

constexpr float kOgreNextDemoMatteTintSteps =
    static_cast<float>(kOgreNextDemoMatteTintLevels - 1U);

std::uint32_t QuantizeOgreNextDemoMatteTintChannel(float value) noexcept {
  const float scaled = value * kOgreNextDemoMatteTintSteps;
  float level = std::floor(scaled + 0.5F);
  if (level < 0.0F) {
    level = 0.0F;
  }
  if (level > kOgreNextDemoMatteTintSteps) {
    level = kOgreNextDemoMatteTintSteps;
  }
  return static_cast<std::uint32_t>(level);
}

} // namespace

OgreNextDemoMatteTint
OgreNextDemoResolveMatteTint(float authored_red, float authored_green,
                             float authored_blue) noexcept {
  const OgreNextDemoMatteTint neutral;
  if (!std::isfinite(authored_red) || !std::isfinite(authored_green) ||
      !std::isfinite(authored_blue)) {
    return neutral;
  }
  // Fail closed rather than clamp: a legacy script that authors a channel
  // outside unit range is not describing a base-colour modulator this matte
  // can represent, so it keeps the reviewed neutral stand-in.
  if (authored_red < 0.0F || authored_red > 1.0F || authored_green < 0.0F ||
      authored_green > 1.0F || authored_blue < 0.0F || authored_blue > 1.0F) {
    return neutral;
  }
  const std::uint32_t red_level =
      QuantizeOgreNextDemoMatteTintChannel(authored_red);
  const std::uint32_t green_level =
      QuantizeOgreNextDemoMatteTintChannel(authored_green);
  const std::uint32_t blue_level =
      QuantizeOgreNextDemoMatteTintChannel(authored_blue);
  const std::uint32_t white_level = kOgreNextDemoMatteTintLevels - 1U;
  // The OGRE pass default diffuse is opaque white, which the overwhelming
  // majority of legacy scripts never override. White carries no tint
  // information, so it keeps the untinted matte identity byte-for-byte.
  if (red_level == white_level && green_level == white_level &&
      blue_level == white_level) {
    return neutral;
  }
  OgreNextDemoMatteTint tint;
  tint.red = static_cast<float>(red_level) / kOgreNextDemoMatteTintSteps;
  tint.green = static_cast<float>(green_level) / kOgreNextDemoMatteTintSteps;
  tint.blue = static_cast<float>(blue_level) / kOgreNextDemoMatteTintSteps;
  tint.token =
      (red_level * kOgreNextDemoMatteTintLevels + green_level) *
          kOgreNextDemoMatteTintLevels +
      blue_level;
  tint.tinted = true;
  return tint;
}

bool OgreNextDemoDropsDynamicBlendColors(
    bool has_dynamic_texture_blend) noexcept {
  return has_dynamic_texture_blend;
}

bool OgreNextDemoOmitsInvisibleCab(std::string_view exact_material_name,
                                   float diffuse_alpha,
                                   bool depth_write_enabled) noexcept {
  return exact_material_name == "invisible" && diffuse_alpha == 0.0F &&
         !depth_write_enabled;
}

bool OgreNextDemoOmitsNonUniformSpeedBump(
    std::string_view exact_mesh_name,
    const Render::Float3 &derived_scale) noexcept {
  return exact_mesh_name == "topeQr.mesh" && derived_scale.x == 1.0F &&
         derived_scale.y == 0.5F && derived_scale.z == 0.5F;
}

namespace {

constexpr std::string_view kAlexisBundleGroup =
    "{bundle USER:/mods/AlexisSaber.zip}";

/// True when `exact_material_name` is exactly `base` followed by the actor
/// spawner's instance suffix for AlexisSaber.truck, with an all-digit body.
bool MatchesAlexisManagedBase(std::string_view exact_material_name,
                              std::string_view base) noexcept {
  constexpr std::string_view kSuffixPrefix =
      " (AlexisSaber.truck [Instance ID ";
  constexpr std::string_view kSuffixEnd = "])";
  if (exact_material_name.size() <=
          base.size() + kSuffixPrefix.size() + kSuffixEnd.size() ||
      exact_material_name.substr(0U, base.size()) != base ||
      exact_material_name.substr(base.size(), kSuffixPrefix.size()) !=
          kSuffixPrefix ||
      exact_material_name.substr(exact_material_name.size() -
                                 kSuffixEnd.size()) != kSuffixEnd) {
    return false;
  }
  const std::string_view instance = exact_material_name.substr(
      base.size() + kSuffixPrefix.size(),
      exact_material_name.size() - base.size() - kSuffixPrefix.size() -
          kSuffixEnd.size());
  return !instance.empty() &&
         std::all_of(instance.begin(), instance.end(),
                     [](char value) { return value >= '0' && value <= '9'; });
}

} // namespace

bool OgreNextDemoAllowsAlexisTUS0Approximation(
    std::string_view exact_resource_group,
    std::string_view exact_material_name) noexcept {
  if (exact_resource_group != kAlexisBundleGroup) {
    return false;
  }
  constexpr std::array<std::string_view, 5U> kOpaqueManagedNames{
      {"SaberChassis", "SaberChassisM", "SaberWheels", "SaberGrilles",
       "SaberBody"}};
  for (const std::string_view base : kOpaqueManagedNames) {
    if (MatchesAlexisManagedBase(exact_material_name, base)) {
      return true;
    }
  }
  return false;
}

bool OgreNextDemoResolveAlexisAuthoredRoughness(
    std::string_view exact_resource_group,
    std::string_view exact_material_name, float &out_roughness) noexcept {
  // Only the body paint. The chassis, wheels and grilles are not clearcoated
  // surfaces and keep the shininess derivation they ship with today, so this
  // exception cannot silently restyle the rest of the vehicle.
  if (exact_resource_group != kAlexisBundleGroup ||
      !MatchesAlexisManagedBase(exact_material_name, "SaberBody")) {
    return false;
  }
  out_roughness = kOgreNextDemoAlexisBodyPaintRoughness;
  return true;
}

} // namespace RoR::Gfx::Detail
