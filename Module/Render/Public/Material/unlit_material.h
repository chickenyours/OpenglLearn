#pragma once

#include "Render/Public/Material/material.h"
#include "Render/Public/RHIResourceType/Pipeline/pipeline.h"

namespace Render::Material {

inline std::shared_ptr<const MaterialTemplate> MakeUnlitTemplate(Domain domain) {
    if (domain != Domain::Surface && domain != Domain::Sprite) return {};
    MaterialTemplateDesc desc;
    desc.name = domain == Domain::Sprite ? "UnlitSprite" : "UnlitSurface";
    desc.domain = domain;
    desc.parameters = {
        {"baseColor", ParameterType::Vec4, glm::vec4(1.0f), "Base color", "Surface",
         std::nullopt, std::nullopt, true},
        {"uvTransform", ParameterType::Vec4, glm::vec4(1.0f, 1.0f, 0.0f, 0.0f), "UV scale and offset", "Surface",
         std::nullopt, std::nullopt, true},
        {"alphaCutoff", ParameterType::Float, 0.0f, "Alpha cutoff", "Surface", 0.0f, 1.0f, false}
    };
    desc.textures = {{"baseColorTexture", 0, true}};
    return MaterialTemplate::Create(std::move(desc));
}

inline std::string UnlitVertexShader(Domain domain) {
    if (domain != Domain::Surface && domain != Domain::Sprite) return {};
    std::string source = "#version 450 core\n\nlayout(location = 0) in ";
    source += domain == Domain::Sprite ? "vec2 aPosition;\n" : "vec3 aPosition;\n";
    source += R"GLSL(layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;

layout(std140, binding = 0) uniform ViewData {
    mat4 uViewProjection;
    vec4 uCameraPosition;
};
layout(std140, binding = 1) uniform ObjectData {
    mat4 uModel;
};
)GLSL";
    source += MakeUnlitTemplate(domain)->GenerateGLSLUniformBlock();
    source += R"GLSL(
out vec2 vTexCoord;
out vec4 vColor;

void main() {
    vTexCoord = aTexCoord * uvTransform.xy + uvTransform.zw;
    vColor = aColor;
    gl_Position = uViewProjection * uModel * )GLSL";
    source += domain == Domain::Sprite ? "vec4(aPosition, 0.0, 1.0);\n" : "vec4(aPosition, 1.0);\n";
    source += "}\n";
    return source;
}

inline std::string UnlitFragmentShader() {
    std::string source = R"GLSL(#version 450 core

layout(binding = 0) uniform sampler2D baseColorTexture;
)GLSL";
    // Both domains deliberately share the same uniform layout and fragment shader.
    source += MakeUnlitTemplate(Domain::Surface)->GenerateGLSLUniformBlock();
    source += R"GLSL(
in vec2 vTexCoord;
in vec4 vColor;
layout(location = 0) out vec4 outColor;

void main() {
    vec4 color = texture(baseColorTexture, vTexCoord) * vColor * baseColor;
    if (color.a < alphaCutoff) discard;
    outColor = color;
}
)GLSL";
    return source;
}

inline VertexLayout UnlitVertexLayout(Domain domain) {
    if (domain != Domain::Surface && domain != Domain::Sprite) return {};
    return {{domain == Domain::Sprite ? VertexFieldType::Vec2 : VertexFieldType::Vec3,
             VertexFieldType::Vec2, VertexFieldType::Vec4}};
}

inline PipelineSpec UnlitPipelineSpec(RenderResourceHandle<ShaderProgramSpec> program, Domain domain) {
    PipelineSpec spec;
    spec.shaderProgram = program;
    spec.expectVertexLayout = UnlitVertexLayout(domain);
    if (domain == Domain::Sprite) {
        spec.depthTest = false;
        spec.depthWrite = false;
        spec.cullMode = CullMode::None;
        spec.blendEnable = true;
        spec.blendMode = BlendMode::Alpha;
    }
    return spec;
}

} // namespace Render::Material
