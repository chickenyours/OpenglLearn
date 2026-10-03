#ifdef NDEBUG
#undef NDEBUG
#endif
#include <array>
#include <cassert>
#include <iostream>
#include <vector>

#include "Render/Private/rhi_device.h"

namespace Render {
struct RHIDeviceTestAccess {
    static void Install(RHIDevice& device, IBackend* backend) {
        device.backend_ = ObjectPtr<IBackend>(backend);
        device.isRun_.store(true);
    }
    static void Pump(RHIDevice& device) {
        device.commandSystem_.thread_SwitchQueues();
        device.thread_ProcessCurrentQueue();
        device.returnSystem.DrainCallbacks();
    }
};
}

namespace {
using namespace Render;

class RecordingBackend final : public OpenglBackend {
public:
    RecordingBackend() : OpenglBackend({}) {}
    CreateTextureCommand created;
    RHICommand::ResolveRenderTarget resolved;
    unsigned resolves = 0;
    void BeginFrame(const RHICommand::BeginFrame&) override {}
    void EndFrame(const RHICommand::EndFrame&) override {}
    void ResolveRenderTarget(const RHICommand::ResolveRenderTarget& command) override {
        resolved = command; ++resolves;
    }
    RenderResourceHandle<RHITextureSpec> CreateTexture(const CreateTextureCommand& command) override {
        created = command;
        return command.spec.width ? RenderResourceHandle<RHITextureSpec>{3, 2} : RenderResourceHandle<RHITextureSpec>{};
    }
};

void DescriptorsAndDispatch() {
    static_assert(RHICommand::FrameCommandsSet::id_of<RHICommand::SetRenderTarget>().value == 15);
    static_assert(RHICommand::FrameCommandsSet::id_of<RHICommand::ResolveRenderTarget>().value == 16);
    static_assert(std::is_trivially_copyable_v<RHICommand::ResolveRenderTarget>);
    assert(CreateRHITextureSpec{}.samples == 1 && RHITextureSpec{}.samples == 1);
    assert(RenderTargetSpec{}.samples == 1);
    RHITextureSpec color, depth;
    color.width = depth.width = color.height = depth.height = 64;
    color.mipmaps = depth.mipmaps = false;
    color.samples = depth.samples = 4;
    depth.textureDataStoreType = RHITextureFormat::Depth32F;
    assert(AreRenderTargetAttachmentsCompatible(&color, &depth));
    depth.samples = 1;
    assert(!AreRenderTargetAttachmentsCompatible(&color, &depth));
    color.samples = 3;
    assert(!AreRenderTargetAttachmentsCompatible(&color, nullptr));

    RHIDevice device;
    auto* backend = new RecordingBackend;
    RHIDeviceTestAccess::Install(device, backend);
    CreateRHITextureSpec spec;
    spec.width = spec.height = 2; spec.samples = 4; spec.mipmaps = false;
    bool completed = false;
    device.async_CreateTexture(spec, [&](auto handle) { completed = handle.IsValid(); });
    RHIDeviceTestAccess::Pump(device);
    assert(completed && backend->created.spec.samples == 4 && backend->created.data.empty());
    const std::array<unsigned char, 16> pixels{};
    spec.data = pixels.data();
    device.async_CreateTexture(spec, [&](auto handle) { completed = handle.IsValid(); });
    RHIDeviceTestAccess::Pump(device);
    assert(!completed && backend->created.data.empty()); // Invalid upload is never copied.
    spec.data = nullptr; spec.mipmaps = true;
    device.async_CreateTexture(spec, [&](auto handle) { completed = handle.IsValid(); });
    RHIDeviceTestAccess::Pump(device);
    assert(!completed);
    spec.mipmaps = false; spec.samples = 0;
    device.async_CreateTexture(spec, [&](auto handle) { completed = handle.IsValid(); });
    RHIDeviceTestAccess::Pump(device);
    assert(!completed);

    auto frame = device.BeginFrame({});
    assert(!frame.ResolveRenderTarget({}));
    assert(!frame.ResolveRenderTarget({{1,1}, {1,1}}));
    assert(!frame.ResolveRenderTarget({{1,1}, {2,1}, false, false}));
    assert(frame.ResolveRenderTarget({{1,1}, {2,1}, true, true}));
    assert(frame.End(false) && device.async_SubmitFrameCommands(frame.GetCommandBuffer()));
    RHIDeviceTestAccess::Pump(device);
    assert(backend->resolves == 1 && backend->resolved.source.id == 1 && backend->resolved.destination.id == 2);
    assert(backend->resolved.color && backend->resolved.depth);
}

GLuint texture2D = 91, textureMS = 92, nextTexture = 100, nextFramebuffer = 200;
GLuint readFramebuffer = 31, drawFramebuffer = 32;
GLint actualSamples = 4, maxSamples = 8, maxColorSamples = 8, maxDepthSamples = 4;
GLenum error = GL_NO_ERROR, framebufferStatus = GL_FRAMEBUFFER_COMPLETE;
bool scissor = true, srgb = true, multisampling = false;
int allocations = 0, deletedTextures = 0, blits = 0, viewportCalls = 0;
GLbitfield blitMask = 0;
std::vector<GLenum> attachmentTargets;

void APIENTRY GetInteger(GLenum name, GLint* value) {
    switch (name) {
    case GL_MAX_TEXTURE_SIZE: *value = 4096; break;
    case GL_MAX_SAMPLES: *value = maxSamples; break;
    case GL_MAX_COLOR_TEXTURE_SAMPLES: *value = maxColorSamples; break;
    case GL_MAX_DEPTH_TEXTURE_SAMPLES: *value = maxDepthSamples; break;
    case GL_TEXTURE_BINDING_2D: *value = texture2D; break;
    case GL_TEXTURE_BINDING_2D_MULTISAMPLE: *value = textureMS; break;
    case GL_DRAW_FRAMEBUFFER_BINDING: *value = drawFramebuffer; break;
    case GL_READ_FRAMEBUFFER_BINDING: *value = readFramebuffer; break;
    case GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS: *value = 32; break;
    case GL_MAX_VIEWPORT_DIMS: value[0] = value[1] = 4096; break;
    default: assert(false);
    }
}
void APIENTRY GenTextures(GLsizei count, GLuint* values) { while (count--) *values++ = nextTexture++; }
void APIENTRY DeleteTextures(GLsizei count, const GLuint*) { deletedTextures += count; }
void APIENTRY BindTexture(GLenum target, GLuint value) {
    if (target == GL_TEXTURE_2D) texture2D = value;
    else { assert(target == GL_TEXTURE_2D_MULTISAMPLE); textureMS = value; }
}
void APIENTRY ImageMS(GLenum target, GLsizei samples, GLenum format, GLsizei width, GLsizei height, GLboolean fixed) {
    assert(target == GL_TEXTURE_2D_MULTISAMPLE && samples > 1 && width == 64 && height == 32 && fixed == GL_TRUE);
    assert(format == GL_RGBA16F || format == GL_DEPTH_COMPONENT32F);
    ++allocations;
}
void APIENTRY GetTextureLevel(GLenum target, GLint level, GLenum name, GLint* value) {
    assert(target == GL_TEXTURE_2D_MULTISAMPLE && level == 0 && name == GL_TEXTURE_SAMPLES);
    *value = actualSamples;
}
GLenum APIENTRY GetError() { const auto result = error; error = GL_NO_ERROR; return result; }
void APIENTRY ActiveTexture(GLenum) {}
void APIENTRY GenFramebuffers(GLsizei count, GLuint* values) { while (count--) *values++ = nextFramebuffer++; }
void APIENTRY DeleteFramebuffers(GLsizei, const GLuint*) {}
void APIENTRY BindFramebuffer(GLenum target, GLuint value) {
    if (target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER) readFramebuffer = value;
    if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER) drawFramebuffer = value;
}
void APIENTRY Attach(GLenum target, GLenum, GLenum textureTarget, GLuint, GLint level) {
    assert(target == GL_FRAMEBUFFER && level == 0);
    attachmentTargets.push_back(textureTarget);
}
void APIENTRY DrawBuffer(GLenum) {}
void APIENTRY ReadBuffer(GLenum) {}
GLenum APIENTRY FramebufferStatus(GLenum) { return framebufferStatus; }
GLboolean APIENTRY IsEnabled(GLenum flag) {
    if (flag == GL_SCISSOR_TEST) return scissor;
    assert(flag == GL_FRAMEBUFFER_SRGB); return srgb;
}
void APIENTRY Enable(GLenum flag) {
    if (flag == GL_SCISSOR_TEST) scissor = true;
    else if (flag == GL_FRAMEBUFFER_SRGB) srgb = true;
    else { assert(flag == GL_MULTISAMPLE); multisampling = true; }
}
void APIENTRY Disable(GLenum flag) {
    if (flag == GL_SCISSOR_TEST) scissor = false;
    else { assert(flag == GL_FRAMEBUFFER_SRGB); srgb = false; }
}
void APIENTRY Blit(GLint sx0, GLint sy0, GLint sx1, GLint sy1,
    GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield mask, GLenum filter) {
    assert(sx0 == 0 && sy0 == 0 && dx0 == 0 && dy0 == 0);
    assert(sx1 == 64 && sy1 == 32 && dx1 == 64 && dy1 == 32 && filter == GL_NEAREST);
    assert(!scissor && !srgb && readFramebuffer != drawFramebuffer);
    blitMask = mask; ++blits;
}
void APIENTRY Viewport(GLint, GLint, GLsizei, GLsizei) { ++viewportCalls; }
void APIENTRY DepthRange(GLdouble, GLdouble) {}
void APIENTRY ColorMask(GLboolean, GLboolean, GLboolean, GLboolean) {}
void APIENTRY DepthMask(GLboolean) {}

void InstallGL() {
    glad_glGetIntegerv = &GetInteger;
    glad_glGenTextures = &GenTextures; glad_glDeleteTextures = &DeleteTextures;
    glad_glBindTexture = &BindTexture; glad_glActiveTexture = &ActiveTexture;
    glad_glTexImage2DMultisample = &ImageMS; glad_glGetTexLevelParameteriv = &GetTextureLevel;
    glad_glGetError = &GetError;
    glad_glGenFramebuffers = &GenFramebuffers; glad_glDeleteFramebuffers = &DeleteFramebuffers;
    glad_glBindFramebuffer = &BindFramebuffer; glad_glFramebufferTexture2D = &Attach;
    glad_glCheckFramebufferStatus = &FramebufferStatus;
    glad_glDrawBuffer = &DrawBuffer; glad_glReadBuffer = &ReadBuffer;
    glad_glIsEnabled = &IsEnabled; glad_glEnable = &Enable; glad_glDisable = &Disable;
    glad_glBlitFramebuffer = &Blit; glad_glViewport = &Viewport;
    glad_glDepthRange = &DepthRange; glad_glColorMask = &ColorMask; glad_glDepthMask = &DepthMask;
}

void MultisampleAllocation() {
    RHIResourcePool pool;
    OpenglBackend backend({&pool});
    CreateTextureCommand create;
    create.spec.width = 64; create.spec.height = 32;
    create.spec.textureDataStoreType = create.spec.textureUseType = RHITextureFormat::RGBA16F;
    create.spec.samples = 4; create.spec.mipmaps = false;
    const auto handle = backend.CreateTexture(create);
    assert(handle.IsValid() && allocations == 1 && textureMS == 92 && texture2D == 91);
    assert(pool.TextureTable.Get(handle)->samples == 4);
    backend.BindTexture({handle, 0});
    assert(texture2D == 0); // sampler2D must not accidentally sample stale data.
    UpdateTextureCommand update;
    update.handle = handle;
    update.desc = {0,0,1,1,RHITextureFormat::RGBA16F,nullptr,16}; update.data.resize(16);
    assert(!backend.UpdateTexture(update));

    create.spec.samples = 3;
    assert(!backend.CreateTexture(create).IsValid() && allocations == 1);
    create.spec.samples = 4; create.spec.mipmaps = true;
    assert(!backend.CreateTexture(create).IsValid() && allocations == 1);
    create.spec.mipmaps = false; create.data.resize(64 * 32 * 16);
    assert(!backend.CreateTexture(create).IsValid() && allocations == 1);
    create.data.clear(); create.spec.data = &create;
    assert(!backend.CreateTexture(create).IsValid() && allocations == 1);
    create.spec.data = nullptr; create.spec.samples = 8; maxColorSamples = 4;
    assert(!backend.CreateTexture(create).IsValid() && allocations == 1);
    maxColorSamples = 8; maxSamples = 4;
    assert(!backend.CreateTexture(create).IsValid() && allocations == 1);
    maxSamples = 8;
    create.spec.textureDataStoreType = create.spec.textureUseType = RHITextureFormat::Depth32F;
    assert(!backend.CreateTexture(create).IsValid() && allocations == 1); // Depth limit is independent.
    create.spec.samples = 4;
    assert(backend.CreateTexture(create).IsValid() && allocations == 2);
    create.spec.samples = 2; // Driver rounds to four; metadata must never lie.
    assert(!backend.CreateTexture(create).IsValid() && allocations == 3 && deletedTextures == 1);
    create.spec.samples = 4; error = GL_OUT_OF_MEMORY;
    assert(!backend.CreateTexture(create).IsValid() && deletedTextures == 2 && textureMS == 92);
}

void ResolveValidationAndState() {
    RHIResourcePool pool;
    OpenglBackend backend({&pool});
    const auto texture = [&](RHITextureFormat format, unsigned samples) {
        RHITextureSpec spec;
        spec.width = 64; spec.height = 32; spec.rhi_id = nextTexture++;
        spec.textureDataStoreType = spec.textureUseType = format;
        spec.mipmaps = false; spec.samples = samples;
        return pool.TextureTable.Add(spec);
    };
    const auto colorMS = texture(RHITextureFormat::RGBA16F, 4), depthMS = texture(RHITextureFormat::Depth32F, 4);
    const auto color = texture(RHITextureFormat::RGBA16F, 1), depth = texture(RHITextureFormat::Depth32F, 1);
    assert(!backend.CreateRenderTarget({{colorMS, depth}, {}}).IsValid());
    const auto source = backend.CreateRenderTarget({{colorMS, depthMS}, {}});
    const auto destination = backend.CreateRenderTarget({{color, depth}, {}});
    assert(source.IsValid() && destination.IsValid());
    assert(pool.renderTargetTable.Get(source)->samples == 4);
    assert(attachmentTargets.size() == 4 && attachmentTargets[0] == GL_TEXTURE_2D_MULTISAMPLE &&
        attachmentTargets[1] == GL_TEXTURE_2D_MULTISAMPLE && attachmentTargets[2] == GL_TEXTURE_2D);
    assert(readFramebuffer == 31 && drawFramebuffer == 32);
    RHICommand::SetRenderTarget selected;
    selected.target = source; selected.width = 64; selected.height = 32;
    backend.SetRenderTarget(selected);
    assert(multisampling && viewportCalls == 1);
    const auto active = drawFramebuffer;
    scissor = srgb = true;
    readFramebuffer = 73; // Distinct read binding must also survive resolve.
    RHICommand::ResolveRenderTarget resolve{source, destination, true, true};
    backend.ResolveRenderTarget(resolve);
    assert(blits == 1 && blitMask == (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
    assert(readFramebuffer == 73 && drawFramebuffer == active && scissor && srgb && viewportCalls == 1);
    resolve.color = false;
    backend.ResolveRenderTarget(resolve);
    assert(blits == 2 && blitMask == GL_DEPTH_BUFFER_BIT);
    resolve.color = true; resolve.depth = false;
    scissor = srgb = false;
    backend.ResolveRenderTarget(resolve);
    assert(blits == 3 && blitMask == GL_COLOR_BUFFER_BIT && !scissor && !srgb);
    resolve.color = false;
    backend.ResolveRenderTarget(resolve);
    resolve.color = true; resolve.source = destination;
    backend.ResolveRenderTarget(resolve);
    resolve.source = source; resolve.destination = source;
    backend.ResolveRenderTarget(resolve);
    resolve.destination = {}; backend.ResolveRenderTarget(resolve);
    resolve.destination = destination;
    pool.renderTargetTable.Get(destination)->width = 63;
    backend.ResolveRenderTarget(resolve);
    pool.renderTargetTable.Get(destination)->width = 64;
    pool.TextureTable.Get(color)->textureDataStoreType = RHITextureFormat::RGBA8;
    backend.ResolveRenderTarget(resolve);
    pool.TextureTable.Get(color)->textureDataStoreType = RHITextureFormat::RGBA16F;
    framebufferStatus = GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
    backend.ResolveRenderTarget(resolve);
    framebufferStatus = GL_FRAMEBUFFER_COMPLETE;
    assert(blits == 3 && readFramebuffer == 73 && drawFramebuffer == active && viewportCalls == 1);
    pool.TextureTable.Remove(color);
    backend.ResolveRenderTarget(resolve);
    assert(blits == 3); // Retired attachments cannot be copied even if FBO metadata survives.
}
}

int main() {
    DescriptorsAndDispatch();
    InstallGL();
    MultisampleAllocation();
    ResolveValidationAndState();
    std::cout << "MSAA allocation, resolve, validation and preserved GL state tests passed\n";
}
