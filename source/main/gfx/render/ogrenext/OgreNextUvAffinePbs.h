/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Reserved-name extension of Ogre-Next PBS for exact UV0 affine state.

#pragma once

#include "OgreHlmsPbs.h"

namespace RoR::Render {

inline constexpr char kOgreNextUvAffinePbsDatablockPrefix[] =
    "RoRN1Material_";
inline constexpr char kOgreNextThinSlabPbsDatablockPrefix[] =
    "RoRN1TransmissionMaterial_";
inline constexpr char kOgreNextUvAffinePbsProperty[] =
    "ror_uv0_affine_pbs";
inline constexpr char kOgreNextThinSlabPbsProperty[] =
    "ror_thin_slab_transmission";
inline constexpr char kOgreNextIndirectAlphaPbsProperty[] =
    "ror_indirect_alpha";
inline constexpr char kOgreNextUvAffinePbsMediaPath[] =
    "Hlms/RoR/UvAffinePbs";
inline constexpr char kOgreNextIndirectAlphaPbsMediaPath[] =
    "Hlms/RoR/IndirectAlpha";

/// Keeps upstream PBS behavior intact for every non-RoR datablock. The
/// frontend-owned RT4/V1 prefix selects one custom UV macro piece in both the
/// normal and alpha-test shadow-caster shader hashes.
class OgreNextUvAffinePbs final : public Ogre::HlmsPbs {
public:
  OgreNextUvAffinePbs(Ogre::Archive *data_folder,
                      Ogre::ArchiveVec *library_folders);

  [[nodiscard]] static bool SelectsUv0AffineShader(
      const Ogre::HlmsDatablock *datablock) noexcept;
  [[nodiscard]] static bool SelectsThinSlabTransmissionShader(
      const Ogre::HlmsDatablock *datablock) noexcept;

  /// Stage-3 screen-space shade: when enabled, every opaque RT4/V1 PBS
  /// datablock writes its indirect-luminance fraction into the scene alpha
  /// (Hlms/RoR/IndirectAlpha). Must be decided once before the first shader
  /// is generated; the frontend enables it exactly when the screen-shade
  /// node is part of the workspace, so a disabled shade graph keeps the
  /// stock alpha-1.0 shaders byte-identical.
  static void SetIndirectAlphaExportEnabled(bool enabled) noexcept;
  [[nodiscard]] static bool IndirectAlphaExportEnabled() noexcept;

protected:
  void calculateHashForPreCreate(Ogre::Renderable *renderable,
                                 Ogre::PiecesMap *in_out_pieces) override;
  void calculateHashForPreCaster(
      Ogre::Renderable *renderable, Ogre::PiecesMap *in_out_pieces,
      const Ogre::PiecesMap *normal_pass_pieces) override;
};

} // namespace RoR::Render
