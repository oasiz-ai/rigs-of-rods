#include "OgreNextUvAffinePbs.h"

#include "Compositor/OgreCompositorManager2.h"
#include "Compositor/OgreCompositorWorkspace.h"
#include "OgreAbiUtils.h"
#include "OgreArchiveManager.h"
#include "OgreCamera.h"
#include "OgreColourValue.h"
#include "OgreException.h"
#include "OgreHlmsManager.h"
#include "OgreHlmsPbsDatablock.h"
#include "OgreImage2.h"
#include "OgreLight.h"
#include "OgreManualObject2.h"
#include "OgreMetalPlugin.h"
#include "OgreRenderSystem.h"
#include "OgreRenderSystemCapabilities.h"
#include "OgreRoot.h"
#include "OgreSceneManager.h"
#include "OgreSceneNode.h"
#include "OgreTextureBox.h"
#include "OgreTextureGpu.h"
#include "OgreTextureGpuManager.h"
#include "OgreWindow.h"

#include <algorithm>
#include <array>
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

namespace {
constexpr Ogre::uint32 kWidth = 256u;
constexpr Ogre::uint32 kHeight = 144u;
constexpr std::size_t kWarmupFrames = 6u;

struct Arguments {
  std::string image_path;
};

struct Rgb final {
  double r = 0.0;
  double g = 0.0;
  double b = 0.0;
};

struct Metrics final {
  std::uint64_t fnv1a64 = UINT64_C(14695981039346656037);
  std::size_t distinct_pixels = 0u;
  std::size_t non_background_pixels = 0u;
  std::vector<unsigned char> rgb;
  std::array<Rgb, 4u> samples{};
};

Arguments ParseArguments(int argc, char **argv) {
  Arguments result;
  for (int index = 1; index < argc; ++index) {
    const std::string option = argv[index];
    if (option == "--output" && index + 1 < argc) {
      result.image_path = argv[++index];
    } else {
      throw std::runtime_error(
          "usage: ror_ogre_next_nicemetal_visible_probe --output FRAME.ppm");
    }
  }
  if (result.image_path.empty()) {
    throw std::runtime_error("--output is required for GPU readback evidence");
  }
  return result;
}

unsigned char Quantize(float value) {
  return static_cast<unsigned char>(std::lround(
      std::max(0.0f, std::min(1.0f, value)) * 255.0f));
}

void HashByte(std::uint64_t &hash, unsigned char value) {
  hash ^= value;
  hash *= UINT64_C(1099511628211);
}

Ogre::TextureGpu *CreateSolidTexture(Ogre::TextureGpuManager &manager,
                                     const Ogre::String &name,
                                     const std::array<unsigned char, 4u> &rgba) {
  auto *image = new Ogre::Image2();
  Ogre::TextureGpu *texture = nullptr;
  try {
    image->createEmptyImage(2u, 2u, 1u, Ogre::TextureTypes::Type2D,
                            Ogre::PFG_RGBA8_UNORM, 1u);
    const Ogre::TextureBox pixels = image->getData(0u);
    for (Ogre::uint32 y = 0u; y < 2u; ++y) {
      for (Ogre::uint32 x = 0u; x < 2u; ++x) {
        auto *destination = static_cast<unsigned char *>(pixels.at(x, y, 0u));
        std::copy(rgba.begin(), rgba.end(), destination);
      }
    }
    texture = manager.createTexture(name, Ogre::GpuPageOutStrategy::Discard,
                                    0u, Ogre::TextureTypes::Type2D);
    texture->setResolution(2u, 2u);
    texture->setNumMipmaps(1u);
    texture->setPixelFormat(Ogre::PFG_RGBA8_UNORM);
    texture->scheduleTransitionTo(Ogre::GpuResidency::Resident, image, true);
    image = nullptr;
    return texture;
  } catch (...) {
    delete image;
    if (texture != nullptr) {
      manager.destroyTexture(texture);
    }
    throw;
  }
}

RoR::Render::OgreNextUvAffinePbs *RegisterPbs(Ogre::Root &root) {
  Ogre::String data_path;
  Ogre::StringVector library_paths;
  Ogre::HlmsPbs::getDefaultPaths(data_path, library_paths);
  const Ogre::String media_root = Ogre::String(ROR_NICEMETAL_OGRE_MEDIA_ROOT) + "/";
  Ogre::ArchiveManager &archives = Ogre::ArchiveManager::getSingleton();
  Ogre::Archive *data = archives.load(media_root + data_path, "FileSystem", true);
  Ogre::ArchiveVec libraries;
  for (const Ogre::String &path : library_paths) {
    libraries.push_back(archives.load(media_root + path, "FileSystem", true));
  }
  libraries.push_back(archives.load(
      Ogre::String(ROR_NICEMETAL_CUSTOM_MEDIA_ROOT) +
          "/Hlms/RoR/NiceMetal",
      "FileSystem", true));
  auto *pbs = OGRE_NEW RoR::Render::OgreNextUvAffinePbs(data, &libraries);
  root.getHlmsManager()->registerHlms(pbs);
  return pbs;
}

Ogre::HlmsPbsDatablock *CreateMaterial(
    RoR::Render::OgreNextUvAffinePbs &pbs, Ogre::TextureGpu *base,
    Ogre::TextureGpu *damaged, Ogre::TextureGpu *specular) {
  auto *datablock = static_cast<Ogre::HlmsPbsDatablock *>(pbs.createDatablock(
      "RoRNiceMetalProof_Visible", "RoRNiceMetalProof_Visible",
      Ogre::HlmsMacroblock(), Ogre::HlmsBlendblock(), Ogre::HlmsParamVec()));
  if (!RoR::Render::OgreNextUvAffinePbs::SelectsNiceMetalFlexShader(datablock)) {
    throw std::runtime_error("NiceMetal proof datablock did not select its HLMS property");
  }
  datablock->setWorkflow(Ogre::HlmsPbsDatablock::SpecularWorkflow);
  datablock->setDiffuse(Ogre::Vector3::UNIT_SCALE);
  datablock->setSpecular(Ogre::Vector3::UNIT_SCALE);
  datablock->setFresnel(Ogre::Vector3(0.04f), false);
  datablock->setRoughness(0.78f);
  Ogre::HlmsSamplerblock sampler;
  sampler.mMinFilter = Ogre::FO_LINEAR;
  sampler.mMagFilter = Ogre::FO_LINEAR;
  sampler.mMipFilter = Ogre::FO_NONE;
  sampler.setAddressingMode(Ogre::TAM_CLAMP);
  const auto bind = [&](Ogre::PbsTextureTypes slot, Ogre::TextureGpu *texture) {
    datablock->setTexture(static_cast<Ogre::uint8>(slot), texture, &sampler);
    datablock->setTextureUvSource(slot, 0u);
    if (datablock->getTexture(static_cast<Ogre::uint8>(slot)) != texture ||
        datablock->getTextureUvSource(slot) != 0u) {
      throw std::runtime_error("NiceMetal proof PBS texture binding did not round-trip");
    }
  };
  bind(Ogre::PBSM_DIFFUSE, base);
  bind(Ogre::PBSM_DETAIL0, damaged);
  bind(Ogre::PBSM_SPECULAR, specular);
  return datablock;
}

void AddQuad(Ogre::ManualObject &object, float center_x,
             const Ogre::ColourValue &flex) {
  const Ogre::uint32 base = static_cast<Ogre::uint32>(object.getCurrentVertexCount());
  const std::array<std::array<float, 2u>, 4u> points{{
      {{center_x - 0.30f, -0.55f}}, {{center_x + 0.30f, -0.55f}},
      {{center_x + 0.30f, 0.55f}}, {{center_x - 0.30f, 0.55f}}}};
  const std::array<std::array<float, 2u>, 4u> uvs{{
      {{0.0f, 0.0f}}, {{1.0f, 0.0f}}, {{1.0f, 1.0f}}, {{0.0f, 1.0f}}}};
  for (std::size_t index = 0u; index < points.size(); ++index) {
    object.position(points[index][0], points[index][1], 0.0f);
    object.normal(0.0f, 0.0f, 1.0f);
    object.tangent(1.0f, 0.0f, 0.0f);
    object.textureCoord(uvs[index][0], uvs[index][1]);
    object.colour(flex);
  }
  object.triangle(base, base + 1u, base + 2u);
  object.triangle(base, base + 2u, base + 3u);
}

Ogre::ManualObject *CreatePanels(Ogre::SceneManager &scene_manager,
                                 Ogre::HlmsPbsDatablock &datablock) {
  Ogre::ManualObject *object = scene_manager.createManualObject();
  object->begin(*datablock.getNameStr(), Ogre::OT_TRIANGLE_LIST);
  AddQuad(*object, -1.20f, Ogre::ColourValue(0.0f, 0.0f, 0.0f, 0.0f));
  AddQuad(*object, -0.40f, Ogre::ColourValue(1.0f, 1.0f, 0.0f, 0.0f));
  AddQuad(*object, 0.40f, Ogre::ColourValue(0.0f, 0.0f, 0.0f, 1.0f));
  AddQuad(*object, 1.20f, Ogre::ColourValue(0.0f, 0.0f, 1.0f, 0.0f));
  object->end();
  object->setCastShadows(false);
  Ogre::SceneNode *node = scene_manager.getRootSceneNode(Ogre::SCENE_DYNAMIC)
                              ->createChildSceneNode(Ogre::SCENE_DYNAMIC);
  node->attachObject(object);
  return object;
}

Rgb SamplePatch(const Ogre::Image2 &image, Ogre::uint32 center_x,
                Ogre::uint32 center_y) {
  Rgb sample;
  std::size_t count = 0u;
  for (Ogre::uint32 y = center_y - 4u; y <= center_y + 4u; ++y) {
    for (Ogre::uint32 x = center_x - 4u; x <= center_x + 4u; ++x) {
      const Ogre::ColourValue colour = image.getColourAt(x, y, 0u);
      sample.r += colour.r;
      sample.g += colour.g;
      sample.b += colour.b;
      ++count;
    }
  }
  sample.r /= static_cast<double>(count);
  sample.g /= static_cast<double>(count);
  sample.b /= static_cast<double>(count);
  return sample;
}

Metrics InspectFrame(Ogre::Image2 &image) {
  if (image.getWidth() != kWidth || image.getHeight() != kHeight) {
    throw std::runtime_error("GPU readback dimensions differ from the target");
  }
  Metrics metrics;
  metrics.rgb.reserve(static_cast<std::size_t>(kWidth) * kHeight * 3u);
  std::vector<std::uint32_t> colours;
  colours.reserve(static_cast<std::size_t>(kWidth) * kHeight);
  for (Ogre::uint32 y = 0u; y < kHeight; ++y) {
    for (Ogre::uint32 x = 0u; x < kWidth; ++x) {
      const Ogre::ColourValue colour = image.getColourAt(x, y, 0u);
      if (!std::isfinite(colour.r) || !std::isfinite(colour.g) ||
          !std::isfinite(colour.b)) {
        throw std::runtime_error("GPU readback contains a non-finite pixel");
      }
      const unsigned char red = Quantize(colour.r);
      const unsigned char green = Quantize(colour.g);
      const unsigned char blue = Quantize(colour.b);
      metrics.rgb.insert(metrics.rgb.end(), {red, green, blue});
      HashByte(metrics.fnv1a64, red);
      HashByte(metrics.fnv1a64, green);
      HashByte(metrics.fnv1a64, blue);
      colours.push_back((static_cast<std::uint32_t>(red) << 16u) |
                        (static_cast<std::uint32_t>(green) << 8u) | blue);
    }
  }
  std::sort(colours.begin(), colours.end());
  metrics.distinct_pixels = static_cast<std::size_t>(
      std::distance(colours.begin(), std::unique(colours.begin(), colours.end())));
  std::size_t largest_run = 0u;
  for (auto start = colours.cbegin(); start != colours.cend();) {
    const auto end = std::upper_bound(start, colours.cend(), *start);
    largest_run = std::max(
        largest_run, static_cast<std::size_t>(std::distance(start, end)));
    start = end;
  }
  metrics.non_background_pixels = colours.size() - largest_run;
  metrics.samples = {SamplePatch(image, 32u, 72u),
                     SamplePatch(image, 96u, 72u),
                     SamplePatch(image, 160u, 72u),
                     SamplePatch(image, 224u, 72u)};

  const Rgb &base = metrics.samples[0u];
  const Rgb &rg_mutation = metrics.samples[1u];
  const Rgb &damage = metrics.samples[2u];
  const Rgb &wear = metrics.samples[3u];
  const double rg_delta = std::max({std::abs(base.r - rg_mutation.r),
                                    std::abs(base.g - rg_mutation.g),
                                    std::abs(base.b - rg_mutation.b)});
  if (metrics.distinct_pixels < 8u || metrics.non_background_pixels < 3000u ||
      !(base.r > base.g * 1.5) || !(damage.g > damage.r * 1.5) ||
      !(wear.r < base.r * 0.82 && wear.r > base.r * 0.45) || rg_delta > 0.015) {
    std::ostringstream detail;
    detail << "GPU readback failed the NiceMetal metamorphic gate: distinct="
           << metrics.distinct_pixels << " foreground="
           << metrics.non_background_pixels << " base=" << base.r << ','
           << base.g << ',' << base.b << " rg=" << rg_mutation.r << ','
           << rg_mutation.g << ',' << rg_mutation.b << " damage="
           << damage.r << ',' << damage.g << ',' << damage.b << " wear="
           << wear.r << ',' << wear.g << ',' << wear.b
           << " rg_delta=" << rg_delta;
    throw std::runtime_error(detail.str());
  }
  return metrics;
}

void WritePpm(const std::string &path, const Metrics &metrics) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    throw std::runtime_error("could not open readback output: " + path);
  }
  output << "P6\n" << kWidth << ' ' << kHeight << "\n255\n";
  output.write(reinterpret_cast<const char *>(metrics.rgb.data()),
               static_cast<std::streamsize>(metrics.rgb.size()));
  if (!output) {
    throw std::runtime_error("could not write readback output: " + path);
  }
}

void WriteRgb(std::ostream &output, const Rgb &value) {
  output << "{\"r\":" << value.r << ",\"g\":" << value.g
         << ",\"b\":" << value.b << '}';
}

std::string Render(const Arguments &arguments) {
  const Ogre::AbiCookie abi_cookie = Ogre::generateAbiCookie();
  Ogre::MetalPlugin plugin;
  Ogre::Root root(&abi_cookie, "", "", "", "RoR NiceMetal Visible Probe");
  root.installPlugin(&plugin, nullptr);
  Ogre::RenderSystem *renderer = root.getRenderSystemByName("Metal Rendering Subsystem");
  if (renderer == nullptr) {
    throw std::runtime_error("Metal renderer did not register");
  }
  root.setRenderSystem(renderer);
  root.initialise(false);
  Ogre::NameValuePairList window_parameters;
  window_parameters["hidden"] = "true";
  window_parameters["gamma"] = "true";
  Ogre::Window *window = root.createRenderWindow(
      "RoR NiceMetal Visible Probe", 64u, 64u, false, &window_parameters);
  if (window == nullptr || root.getCompositorManager2() == nullptr) {
    throw std::runtime_error("hidden native window did not initialize Compositor2");
  }
  const Ogre::RenderSystemCapabilities *capabilities = renderer->getCapabilities();
  if (capabilities == nullptr || capabilities->getDeviceName().empty()) {
    throw std::runtime_error("Metal renderer did not identify its device");
  }

  auto *pbs = RegisterPbs(root);
  Ogre::TextureGpuManager *textures = renderer->getTextureGpuManager();
  Ogre::TextureGpu *base = CreateSolidTexture(
      *textures, "RoRNiceMetalProofBase", {{220u, 28u, 20u, 255u}});
  Ogre::TextureGpu *damaged = CreateSolidTexture(
      *textures, "RoRNiceMetalProofDamaged", {{20u, 210u, 32u, 255u}});
  Ogre::TextureGpu *specular = CreateSolidTexture(
      *textures, "RoRNiceMetalProofSpecular", {{28u, 28u, 28u, 255u}});
  Ogre::HlmsPbsDatablock *datablock =
      CreateMaterial(*pbs, base, damaged, specular);

  Ogre::SceneManager *scene =
      root.createSceneManager(Ogre::ST_GENERIC, 1u, "RoRNiceMetalProofScene");
  CreatePanels(*scene, *datablock);
  Ogre::Camera *camera = scene->createCamera("RoRNiceMetalProofCamera");
  camera->setProjectionType(Ogre::PT_ORTHOGRAPHIC);
  camera->setOrthoWindow(4.8f, 1.8f);
  camera->setPosition(0.0f, 0.0f, 3.0f);
  camera->lookAt(Ogre::Vector3::ZERO);
  camera->setNearClipDistance(0.1f);
  camera->setFarClipDistance(20.0f);
  camera->setAspectRatio(static_cast<float>(kWidth) / kHeight);

  Ogre::Light *light = scene->createLight();
  Ogre::SceneNode *light_node = scene->getRootSceneNode()->createChildSceneNode();
  light_node->attachObject(light);
  light->setType(Ogre::Light::LT_DIRECTIONAL);
  light->setDirection(Ogre::Vector3(0.0f, 0.0f, -1.0f));
  light->setPowerScale(Ogre::Math::PI * 1.35f);
  scene->setAmbientLight(Ogre::ColourValue(0.08f, 0.08f, 0.08f),
                         Ogre::ColourValue(0.02f, 0.02f, 0.02f),
                         Ogre::Vector3::UNIT_Y);

  Ogre::TextureGpu *target = textures->createTexture(
      "RoRNiceMetalProofTarget", Ogre::GpuPageOutStrategy::Discard,
      Ogre::TextureFlags::RenderToTexture, Ogre::TextureTypes::Type2D);
  target->setResolution(kWidth, kHeight);
  target->setPixelFormat(Ogre::PFG_RGBA8_UNORM);
  target->scheduleTransitionTo(Ogre::GpuResidency::Resident);
  Ogre::CompositorManager2 *compositor = root.getCompositorManager2();
  const Ogre::String workspace_name = "RoRNiceMetalProofWorkspace";
  compositor->createBasicWorkspaceDef(
      workspace_name, Ogre::ColourValue(0.006f, 0.008f, 0.012f, 1.0f),
      Ogre::IdString());
  Ogre::CompositorWorkspace *workspace =
      compositor->addWorkspace(scene, target, camera, workspace_name, true);
  for (std::size_t frame = 0u; frame < kWarmupFrames; ++frame) {
    if (!root.renderOneFrame()) {
      throw std::runtime_error("Ogre-Next ended the proof frame loop early");
    }
  }

  Ogre::Image2 readback;
  readback.convertFromTexture(target, 0u, 0u);
  const Metrics metrics = InspectFrame(readback);
  WritePpm(arguments.image_path, metrics);

  std::ostringstream report;
  report << std::setprecision(9)
         << "{\"schema_version\":\"ror.ogre-next.nice-metal-visible-runtime@1\","
         << "\"status\":\"pass\",\"renderer\":\"ogre-next\","
         << "\"renderer_name\":\"Metal Rendering Subsystem\","
         << "\"device_name\":\"" << capabilities->getDeviceName() << "\","
         << "\"gpu_readback\":true,"
         << "\"readback_api\":\"Ogre::Image2::convertFromTexture\","
         << "\"compositor2\":true,"
         << "\"hlms_property\":\"ror_nice_metal_flex_v1\","
         << "\"datablock_prefix\":\"RoRNiceMetalProof_\","
         << "\"texture_bindings\":{\"base\":\"PBSM_DIFFUSE\","
         << "\"damaged\":\"PBSM_DETAIL0\",\"specular\":\"PBSM_SPECULAR\"},"
         << "\"vertex_colour_semantics\":{\"red\":\"ignored\","
         << "\"green\":\"ignored\",\"blue\":\"wear_wetness\","
         << "\"alpha\":\"damage\"},\"metamorphic\":{"
         << "\"red_green_ignored\":true,\"alpha_selects_damaged\":true,"
         << "\"blue_dims_base_by_one_third\":true,\"samples\":[";
  for (std::size_t index = 0u; index < metrics.samples.size(); ++index) {
    if (index != 0u) report << ',';
    WriteRgb(report, metrics.samples[index]);
  }
  report << "]},\"frame\":{\"width\":" << kWidth
         << ",\"height\":" << kHeight
         << ",\"warmup_frames\":" << kWarmupFrames
         << ",\"pixel_format\":\""
         << Ogre::PixelFormatGpuUtils::toString(target->getPixelFormat())
         << "\",\"distinct_rgb8_values\":" << metrics.distinct_pixels
         << ",\"non_background_pixels\":" << metrics.non_background_pixels
         << ",\"rgb8_fnv1a64\":\"" << std::hex << std::setfill('0')
         << std::setw(16) << metrics.fnv1a64 << std::dec << "\"},"
         << "\"scope\":{\"legacy_renderer_used\":false,"
         << "\"product_capture_integrated\":false,\"playable\":false}}\n";

  compositor->removeWorkspace(workspace);
  textures->destroyTexture(target);
  root.destroySceneManager(scene);
  return report.str();
}
} // namespace

int main(int argc, char **argv) {
  try {
    std::cout << Render(ParseArguments(argc, argv));
    return 0;
  } catch (const Ogre::Exception &error) {
    std::cerr << "Ogre-Next NiceMetal visible probe failed: "
              << error.getFullDescription() << '\n';
  } catch (const std::exception &error) {
    std::cerr << "Ogre-Next NiceMetal visible probe failed: " << error.what()
              << '\n';
  }
  return 1;
}
