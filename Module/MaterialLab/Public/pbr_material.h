#pragma once

// Compatibility facade: PBR is now a Render material, independent of the lab.
#include "Render/Public/Material/pbr_material.h"

namespace MaterialLab {
using Render::Material::PbrPassBinding;
using Render::Material::PbrPassConstants;
using Render::Material::MakePbrTemplate;
using Render::Material::PbrVertexLayout;
using Render::Material::PbrPipelineSpec;
using Render::Material::PbrVertexShader;
using Render::Material::PbrFragmentShader;
using Render::Material::PbrShadowPipelineSpec;
using Render::Material::PbrShadowVertexShader;
using Render::Material::PbrShadowFragmentShader;
} // namespace MaterialLab