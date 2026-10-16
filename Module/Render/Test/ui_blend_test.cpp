#ifdef NDEBUG
#undef NDEBUG
#endif
#include "Render/Private/Backend/Opengl/backend.h"
#include <cassert>
#include <iostream>

namespace {
using namespace Render;
bool enabled = false;
GLenum sourceColor = 0, destinationColor = 0, sourceAlpha = 0, destinationAlpha = 0;
GLenum colorEquation = GL_FUNC_REVERSE_SUBTRACT, alphaEquation = GL_FUNC_REVERSE_SUBTRACT;
unsigned blendChanges = 0;

void APIENTRY UseProgram(GLuint program) { assert(program == 37); }
void APIENTRY PolygonMode(GLenum, GLenum) {}
void APIENTRY FrontFace(GLenum) {}
void APIENTRY Enable(GLenum capability) {
    assert(capability == GL_BLEND);
    enabled = true;
}
void APIENTRY Disable(GLenum capability) {
    assert(capability == GL_CULL_FACE || capability == GL_DEPTH_TEST || capability == GL_BLEND);
    if (capability == GL_BLEND) enabled = false;
}
void APIENTRY DepthMask(GLboolean) {}
void APIENTRY DepthFunc(GLenum) {}
void APIENTRY BlendEquation(GLenum color, GLenum alpha) {
    colorEquation = color;
    alphaEquation = alpha;
}
void APIENTRY BlendSeparate(GLenum sc, GLenum dc, GLenum sa, GLenum da) {
    sourceColor = sc;
    destinationColor = dc;
    sourceAlpha = sa;
    destinationAlpha = da;
    ++blendChanges;
}
void APIENTRY Blend(GLenum source, GLenum destination) {
    BlendSeparate(source, destination, source, destination);
}

void PipelineBlendState() {
    // Exercise real RHI pipeline selection without requiring a GPU/context.
    glad_glUseProgram = UseProgram;
    glad_glPolygonMode = PolygonMode;
    glad_glFrontFace = FrontFace;
    glad_glEnable = Enable;
    glad_glDisable = Disable;
    glad_glDepthMask = DepthMask;
    glad_glDepthFunc = DepthFunc;
    glad_glBlendEquationSeparate = BlendEquation;
    glad_glBlendFuncSeparate = BlendSeparate;
    glad_glBlendFunc = Blend;

    RHIResourcePool resources;
    OpenglBackend backend({&resources});
    PipelineSpec pipeline;
    pipeline.shaderProgram = resources.shaderProgramTable.Add({ShaderProgramType::Graphics, 37});
    pipeline.cullMode = CullMode::None;
    pipeline.depthTest = pipeline.depthWrite = false;
    pipeline.blendEnable = true;
    const auto select = [&](BlendMode mode, bool enable = true) {
        pipeline.blendMode = mode;
        pipeline.blendEnable = enable;
        const auto handle = resources.PipelineTable.Add(pipeline);
        backend.SetPipeline({handle});
        return handle;
    };

    const auto ui = select(BlendMode::AlphaComposite);
    assert(enabled && sourceColor == GL_SRC_ALPHA && destinationColor == GL_ONE_MINUS_SRC_ALPHA);
    assert(sourceAlpha == GL_ONE && destinationAlpha == GL_ONE_MINUS_SRC_ALPHA);
    assert(colorEquation == GL_FUNC_ADD && alphaEquation == GL_FUNC_ADD);
    backend.SetPipeline({ui});
    assert(blendChanges == 1); // Rebinding an immutable pipeline preserves cached state.

    // Returning from UI must restore both factors; existing material Alpha stays unchanged.
    select(BlendMode::Alpha);
    assert(enabled && sourceColor == GL_SRC_ALPHA && destinationColor == GL_ONE_MINUS_SRC_ALPHA);
    assert(sourceAlpha == GL_SRC_ALPHA && destinationAlpha == GL_ONE_MINUS_SRC_ALPHA);
    select(BlendMode::Additive);
    assert(enabled && sourceColor == GL_SRC_ALPHA && destinationColor == GL_ONE);
    assert(sourceAlpha == GL_SRC_ALPHA && destinationAlpha == GL_ONE);
    select(BlendMode::AlphaComposite, false);
    assert(!enabled);
    select(BlendMode::Opaque);
    assert(!enabled && blendChanges == 3);
}
}

int main() {
    PipelineBlendState();
    std::cout << "RHI UI alpha compositing and legacy blend state tests passed\n";
}
