#pragma once

#include "engine/ECS/data_type.h"

#include "engine/Resource/Material/material.h"
#include "engine/Model/mesh.h"
#include "engine/Graphic/data_type.h"
#include "engine/Resource/Shader/shader_program.h"
#include "engine/Resource/RenderPipe/UniformBindings.h"


struct BaseParticleDrawItem {
    Resource::Material* material = nullptr;
    // Render::Mesh* mesh;                  // billboard / quad / mesh particle
    Graphic::SSBO* particleBuffer = nullptr;   // position, size, color...
};

struct ParticleComputeTaskParams{
    float simulationTime;
    float deltaTime;
};

struct ParticleComputeTask{
    Graphic::SSBO* particleBuffer = nullptr;
    Resource::ShaderProgram* computeShaderProgram = nullptr;
    ParticleComputeTaskParams params;
};