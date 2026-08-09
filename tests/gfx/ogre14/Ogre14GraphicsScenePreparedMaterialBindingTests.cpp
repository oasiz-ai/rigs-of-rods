/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "gfx/ogre14/Ogre14GraphicsScenePreparedMaterialBinding.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace RoR::Render::Testing {

class Ogre14LegacyNativeMaterialAuditTestAccess final {
public:
  static ValidationResult
  SealSyntheticCapture(Ogre14LegacyNativeMaterialCapture &capture) {
    Ogre14LegacyMaterialPipelineAudit value;
    ValidationResult validation =
        DeriveOgre14LegacyMaterialPipelineAudit(capture.material, value);
    if (!validation) {
      return validation;
    }
    auto owner = std::make_shared<const Ogre14LegacyMaterialPipelineAudit>(
        std::move(value));
    capture.exact_native_material_audit = owner;
    capture.native_material_audit_receipt =
        Ogre14LegacyNativeMaterialAuditReceipt(std::move(owner));
    return ValidationResult::Success();
  }
};

} // namespace RoR::Render::Testing

namespace {

using namespace RoR::Render;

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

Ogre14LegacyAssetKey Key(std::string group, std::string name) {
  return {std::move(group), std::move(name)};
}

Ogre14LegacyMaterialSemanticRegistry
MakeRegistry(const std::vector<Ogre14LegacyAssetKey> &keys) {
  std::vector<Ogre14LegacyMaterialSemanticDeclaration> declarations;
  for (std::size_t index = 0U; index < keys.size(); ++index) {
    Ogre14LegacyMaterialSemanticDeclaration declaration;
    declaration.material_key = keys[index];
    declaration.source_revision = static_cast<std::uint64_t>(index + 1U);
    declarations.push_back(std::move(declaration));
  }
  Ogre14LegacyMaterialSemanticRegistry registry;
  Require(BuildOgre14LegacyMaterialSemanticRegistry(
              Ogre14LegacyMaterialSemanticRegistryConfiguration{}, declarations,
              registry)
              .ok(),
          "semantic registry fixture did not build");
  return registry;
}

std::unique_ptr<Ogre14LegacyLiveMaterialCoordinator>
MakeCoordinator(const std::vector<Ogre14LegacyAssetKey> &keys) {
  std::unique_ptr<Ogre14LegacyLiveMaterialCoordinator> coordinator;
  const Ogre14LegacyMaterialSemanticRegistry registry = MakeRegistry(keys);
  Require(CreateOgre14LegacyLiveMaterialCoordinator(
              Ogre14LegacyLiveMaterialCoordinatorConfiguration{}, registry,
              coordinator)
                  .ok() &&
              coordinator != nullptr,
          "coordinator fixture did not build");
  return coordinator;
}

Ogre14LegacyMaterialObservation
MakeObservation(Ogre14LegacyLiveMaterialCoordinator &coordinator,
                const Ogre14LegacyAssetKey &key,
                Ogre14LegacyCullMode cull = Ogre14LegacyCullMode::CLOCKWISE) {
  Ogre14LegacyMaterialObservation observation;
  observation.material_key = key;
  Require(
      coordinator.ResolveMaterialSemantics(key, observation.semantic_resolution)
          .ok(),
      "semantic resolution fixture did not resolve");
  observation.native_capture.material.key = key;
  observation.native_capture.material.source_revision = 1U;
  observation.native_capture.material.base_color_semantic =
      observation.semantic_resolution.native_declaration.base_color_semantic;
  observation.native_capture.material.pipeline.cull = cull;
  Require(RoR::Render::Testing::Ogre14LegacyNativeMaterialAuditTestAccess::
              SealSyntheticCapture(observation.native_capture)
                  .ok(),
          "native audit fixture did not seal");
  return observation;
}

Ogre14LegacyPreparedMaterialFrame
Prepare(Ogre14LegacyLiveMaterialCoordinator &coordinator,
        const std::vector<Ogre14LegacyMaterialObservation> &observations,
        std::uint64_t sequence = 1U) {
  Ogre14LegacyPreparedMaterialFrame frame;
  Require(coordinator.PrepareFrame(sequence, observations, frame).ok(),
          "prepared material fixture did not build");
  return frame;
}

Ogre14GraphicsSceneMaterialCaptureInput
Material(const Ogre14LegacyAssetKey &key, std::uint32_t texture_units = 0U,
         Ogre14GraphicsSceneMaterialCull cull =
             Ogre14GraphicsSceneMaterialCull::CLOCKWISE) {
  Ogre14GraphicsSceneMaterialCaptureInput material;
  material.exact_resource_group = key.exact_resource_group;
  material.exact_name = key.exact_name;
  material.texture_unit_count = texture_units;
  material.cull = cull;
  return material;
}

Ogre14GraphicsSceneStaticSectionCaptureInput
StaticSection(const Ogre14LegacyAssetKey &key, bool reverse_winding = false,
              std::uint32_t texture_units = 0U) {
  Ogre14GraphicsSceneStaticSectionCaptureInput section;
  section.material =
      Material(key, texture_units,
               reverse_winding ? Ogre14GraphicsSceneMaterialCull::ANTICLOCKWISE
                               : Ogre14GraphicsSceneMaterialCull::CLOCKWISE);
  section.mesh_identity.reverse_winding = reverse_winding;
  return section;
}

Ogre14GraphicsSceneDynamicSectionCaptureInput
DynamicSection(const Ogre14LegacyAssetKey &key, bool reverse_winding = false,
               std::uint32_t texture_units = 0U) {
  Ogre14GraphicsSceneDynamicSectionCaptureInput section;
  section.material =
      Material(key, texture_units,
               reverse_winding ? Ogre14GraphicsSceneMaterialCull::ANTICLOCKWISE
                               : Ogre14GraphicsSceneMaterialCull::CLOCKWISE);
  section.mesh_reverse_winding = reverse_winding;
  return section;
}

template <typename T>
bool SharesExactOwner(const std::shared_ptr<const T> &lhs,
                      const std::shared_ptr<const T> &rhs) noexcept {
  return lhs != nullptr && rhs != nullptr && lhs.get() == rhs.get() &&
         !lhs.owner_before(rhs) && !rhs.owner_before(lhs);
}

void TestExactStaticDynamicBindingAndLineage() {
  const Ogre14LegacyAssetKey material_key = Key("CityWorld", "building");
  auto coordinator = MakeCoordinator({material_key});
  const Ogre14LegacyMaterialObservation observation =
      MakeObservation(*coordinator, material_key);
  const Ogre14LegacyPreparedMaterialFrame frame =
      Prepare(*coordinator, {observation});
  const std::shared_ptr<const RenderAssetPayload> mesh_payload =
      std::make_shared<const RenderAssetPayload>(MeshResourceDescriptor{});
  const std::shared_ptr<const Ogre14GraphicsSceneJoinedDynamicState>
      dynamic_state =
          std::make_shared<const Ogre14GraphicsSceneJoinedDynamicState>();
  Ogre14GraphicsSceneStaticSectionCaptureInput static_section =
      StaticSection(material_key);
  static_section.mesh_payload = mesh_payload;
  Ogre14GraphicsSceneDynamicSectionCaptureInput dynamic_section =
      DynamicSection(material_key);
  dynamic_section.mesh_payload = mesh_payload;
  dynamic_section.state = dynamic_state;

  Ogre14GraphicsScenePreparedMaterialBinding binding;
  Require(BindOgre14GraphicsScenePreparedMaterials(frame, {static_section},
                                                   {dynamic_section}, binding)
              .ok(),
          "exact static and dynamic sections did not bind");
  Require(binding.initialized() &&
              binding.version() ==
                  kOgre14GraphicsScenePreparedMaterialBindingVersion &&
              binding.prepared_frame() != nullptr &&
              binding.prepared_frame()->SharesImmutableStateWith(frame) &&
              binding.static_sections().size() == 1U &&
              binding.dynamic_sections().size() == 1U &&
              binding.lineage() != nullptr &&
              binding.lineage()->source_sequence == 1U &&
              binding.lineage()->catalog_sequence == 1U,
          "bound frame identity or lineage changed");
  const std::shared_ptr<const Ogre14LegacyMaterialClosure> canonical =
      frame.materials().front().closure;
  Require(
      SharesExactOwner(binding.static_sections().front().resolved_material,
                       canonical) &&
          SharesExactOwner(binding.dynamic_sections().front().resolved_material,
                           canonical) &&
          SharesExactOwner(binding.static_sections().front().mesh_payload,
                           mesh_payload) &&
          SharesExactOwner(binding.dynamic_sections().front().mesh_payload,
                           mesh_payload) &&
          SharesExactOwner(binding.dynamic_sections().front().state,
                           dynamic_state),
      "bound sections did not reuse canonical closure and payload owners");
  Require(coordinator->CommitPreparedFrameAfterAcceptedExposure(
              *binding.prepared_frame()) ==
              Ogre14LegacyPreparedMaterialCommitResult::COMMITTED,
          "binding did not retain the exact committable prepared frame");
}

void TestAnticlockwiseExactBinding() {
  const Ogre14LegacyAssetKey material_key = Key("CityWorld", "anticlockwise");
  auto coordinator = MakeCoordinator({material_key});
  const Ogre14LegacyPreparedMaterialFrame frame = Prepare(
      *coordinator, {MakeObservation(*coordinator, material_key,
                                     Ogre14LegacyCullMode::ANTICLOCKWISE)});
  Ogre14GraphicsScenePreparedMaterialBinding binding;
  Require(BindOgre14GraphicsScenePreparedMaterials(
              frame, {StaticSection(material_key, true)},
              {DynamicSection(material_key, true)}, binding)
                  .ok() &&
              binding.static_sections()
                  .front()
                  .resolved_material->requires_reverse_winding &&
              binding.dynamic_sections()
                  .front()
                  .resolved_material->requires_reverse_winding,
          "anticlockwise exact material did not retain reversed winding");
  coordinator->DiscardPreparedFrame();
}

void TestFallbackAndWindingGates() {
  const Ogre14LegacyAssetKey exact_key = Key("CityWorld", "road");
  const Ogre14LegacyAssetKey fallback_key = Key("CityWorld", "plain");
  auto coordinator = MakeCoordinator({exact_key});
  const Ogre14LegacyPreparedMaterialFrame frame =
      Prepare(*coordinator, {MakeObservation(*coordinator, exact_key)});

  Ogre14GraphicsScenePreparedMaterialBinding mixed;
  Require(BindOgre14GraphicsScenePreparedMaterials(
              frame, {StaticSection(exact_key), StaticSection(fallback_key)},
              {}, mixed)
                  .ok() &&
              mixed.static_sections()[0].resolved_material != nullptr &&
              mixed.static_sections()[1].resolved_material == nullptr,
          "exact and eligible fallback materials did not coexist");

  Ogre14GraphicsScenePreparedMaterialBinding sentinel = mixed;
  const Ogre14GraphicsScenePreparedMaterialBinding expected = sentinel;
  Require(!BindOgre14GraphicsScenePreparedMaterials(
               frame, {StaticSection(fallback_key, false, 1U)}, {}, sentinel)
                  .ok() &&
              sentinel.SharesImmutableStateWith(expected),
          "unprepared textured fallback mutated the prior binding");
  Require(!BindOgre14GraphicsScenePreparedMaterials(
               frame, {StaticSection(exact_key, true)}, {}, sentinel)
                  .ok() &&
              sentinel.SharesImmutableStateWith(expected),
          "static winding mismatch mutated the prior binding");
  Require(!BindOgre14GraphicsScenePreparedMaterials(
               frame, {}, {DynamicSection(exact_key, true)}, sentinel)
                  .ok() &&
              sentinel.SharesImmutableStateWith(expected),
          "dynamic winding mismatch mutated the prior binding");

  Ogre14GraphicsSceneStaticSectionCaptureInput substituted =
      StaticSection(exact_key);
  substituted.resolved_material = frame.materials().front().closure;
  Require(!BindOgre14GraphicsScenePreparedMaterials(frame, {substituted}, {},
                                                    sentinel)
                  .ok() &&
              sentinel.SharesImmutableStateWith(expected),
          "caller-supplied closure substitution mutated the prior binding");

  {
    std::vector<Ogre14GraphicsSceneStaticSectionCaptureInput>
        excessive_sections(kMaximumOgre14GraphicsSceneStaticSections + 1U);
    const ValidationResult excessive = BindOgre14GraphicsScenePreparedMaterials(
        frame, excessive_sections, {}, sentinel);
    Require(!excessive.ok() &&
                excessive.code == ValidationCode::VALUE_OUT_OF_RANGE &&
                excessive.field == "prepared_material_binding.sections" &&
                sentinel.SharesImmutableStateWith(expected),
            "static cap+1 preflight copied input or mutated the prior binding");
  }
  {
    std::vector<Ogre14GraphicsSceneDynamicSectionCaptureInput>
        excessive_sections(kMaximumOgre14GraphicsSceneDynamicSections + 1U);
    const ValidationResult excessive = BindOgre14GraphicsScenePreparedMaterials(
        frame, {}, excessive_sections, sentinel);
    Require(
        !excessive.ok() &&
            excessive.code == ValidationCode::VALUE_OUT_OF_RANGE &&
            excessive.field == "prepared_material_binding.sections" &&
            sentinel.SharesImmutableStateWith(expected),
        "dynamic cap+1 preflight copied input or mutated the prior binding");
  }
  coordinator->DiscardPreparedFrame();
}

class ThrowingInjector final
    : public IOgre14GraphicsScenePreparedMaterialBindingFaultInjector {
public:
  Ogre14GraphicsScenePreparedMaterialBindingFaultPoint point =
      Ogre14GraphicsScenePreparedMaterialBindingFaultPoint::
          AFTER_FIRST_EXACT_BINDING;
  bool bad_alloc = true;

  void AtFaultPoint(
      Ogre14GraphicsScenePreparedMaterialBindingFaultPoint current) override {
    if (current != point) {
      return;
    }
    if (bad_alloc) {
      throw std::bad_alloc();
    }
    throw 17;
  }
};

void TestFaultRollbackAndEmptyFallbackFrame() {
  const Ogre14LegacyAssetKey exact_key = Key("CityWorld", "fixture");
  const Ogre14LegacyAssetKey fallback_key = Key("CityWorld", "factor");
  auto coordinator = MakeCoordinator({exact_key});
  const Ogre14LegacyPreparedMaterialFrame frame =
      Prepare(*coordinator, {MakeObservation(*coordinator, exact_key)});
  Ogre14GraphicsScenePreparedMaterialBinding binding;
  Require(BindOgre14GraphicsScenePreparedMaterials(
              frame, {StaticSection(exact_key)}, {}, binding)
              .ok(),
          "sentinel binding fixture did not build");
  const Ogre14GraphicsScenePreparedMaterialBinding expected = binding;

  ThrowingInjector injector;
  Require(!BindOgre14GraphicsScenePreparedMaterials(
               frame, {StaticSection(exact_key)}, {}, binding, &injector)
                  .ok() &&
              binding.SharesImmutableStateWith(expected),
          "bad_alloc after exact binding changed the sentinel");
  injector.point = Ogre14GraphicsScenePreparedMaterialBindingFaultPoint::
      BEFORE_BINDING_COMMIT;
  injector.bad_alloc = false;
  Require(!BindOgre14GraphicsScenePreparedMaterials(
               frame, {StaticSection(exact_key)}, {}, binding, &injector)
                  .ok() &&
              binding.SharesImmutableStateWith(expected),
          "unexpected precommit exception changed the sentinel");
  coordinator->DiscardPreparedFrame();

  auto empty_coordinator = MakeCoordinator({});
  const Ogre14LegacyPreparedMaterialFrame empty_frame =
      Prepare(*empty_coordinator, {});
  Ogre14GraphicsScenePreparedMaterialBinding fallback_binding;
  Require(BindOgre14GraphicsScenePreparedMaterials(
              empty_frame, {StaticSection(fallback_key)}, {}, fallback_binding)
                  .ok() &&
              fallback_binding.lineage() != nullptr &&
              fallback_binding.lineage()->empty() &&
              fallback_binding.static_sections().front().resolved_material ==
                  nullptr,
          "empty authoritative frame did not preserve factor fallback");
  empty_coordinator->DiscardPreparedFrame();
}

} // namespace

int main() {
  TestExactStaticDynamicBindingAndLineage();
  TestAnticlockwiseExactBinding();
  TestFallbackAndWindingGates();
  TestFaultRollbackAndEmptyFallbackFrame();
  std::cout << "Ogre14 prepared scene material binding tests passed\n";
  return EXIT_SUCCESS;
}
