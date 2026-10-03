#ifdef NDEBUG
#undef NDEBUG
#endif
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

#include "Render/Private/Material/rhi_material_resources.h"

namespace Render {
struct RHIDeviceTestAccess {
    static void Install(RHIDevice& device, IBackend* backend) {
        device.backend_ = ObjectPtr<IBackend>(backend);
        device.isRun_.store(true);
    }
    static void Pump(RHIDevice& device) {
        device.commandSystem_.thread_SwitchQueues();
        device.thread_ProcessCurrentQueue();
    }
    static bool Pending(RHIDevice& device) {
        return !device.commandSystem_.thread_empty() || device.commandSystem_.hasPendingCommands();
    }
};
}

namespace {
using namespace Render;

class RecordingBackend final : public OpenglBackend {
public:
    RecordingBackend() : OpenglBackend({}) {}
    int targetsCreated = 0;
    CreateRenderTargetDesc createdDesc;
    RHICommand::SetRenderTarget selected;
    CreateTextureCommand uploaded;
    std::vector<char> deletionOrder;
    void BeginFrame(const RHICommand::BeginFrame&) override {}
    void EndFrame(const RHICommand::EndFrame&) override {}
    void SetRenderTarget(const RHICommand::SetRenderTarget& command) override { selected = command; }
    RenderResourceHandle<RenderTargetSpec> CreateRenderTarget(const CreateRenderTargetCommand& command) override {
        ++targetsCreated;
        createdDesc = command.desc;
        return {7, 4};
    }
    RenderResourceHandle<RHITextureSpec> CreateTexture(const CreateTextureCommand& command) override {
        uploaded = command;
        return command.spec.width ? RenderResourceHandle<RHITextureSpec>{5, 1} : RenderResourceHandle<RHITextureSpec>{};
    }
    void DeleteRenderTarget(const DeleteRenderTargetCommand&) override { deletionOrder.push_back('F'); }
    void DeleteTexture(const DeleteTextureCommand&) override { deletionOrder.push_back('T'); }
};

void CheckDescriptors() {
    static_assert(RHICommand::FrameCommandsSet::id_of<RHICommand::BeginFrame>().value == 6);
    static_assert(RHICommand::FrameCommandsSet::id_of<RHICommand::DrawInstance>().value == 14);
    static_assert(RHICommand::FrameCommandsSet::id_of<RHICommand::SetRenderTarget>().value == 15);
    static_assert(std::is_trivially_copyable_v<RHICommand::SetRenderTarget>);
    assert(CreateRHITextureSpec{}.mipmaps);
    assert(GetTextureFormatByteSize(RHITextureFormat::RGBA16F) == 16);
    assert(GetTextureFormatByteSize(RHITextureFormat::Depth32F) == 4);
    RHITextureSpec color, depth;
    color.width = depth.width = 64;
    color.height = depth.height = 32;
    color.mipmaps = depth.mipmaps = false;
    color.textureDataStoreType = RHITextureFormat::RGBA16F;
    depth.textureDataStoreType = RHITextureFormat::Depth32F;
    assert(AreRenderTargetAttachmentsCompatible(&color, nullptr));
    assert(AreRenderTargetAttachmentsCompatible(nullptr, &depth));
    assert(AreRenderTargetAttachmentsCompatible(&color, &depth));
    assert(!AreRenderTargetAttachmentsCompatible(nullptr, nullptr));
    assert(!AreRenderTargetAttachmentsCompatible(&depth, &color));
    depth.width = 63;
    assert(!AreRenderTargetAttachmentsCompatible(&color, &depth));
    depth.width = 64;
    color.mipmaps = true;
    assert(!AreRenderTargetAttachmentsCompatible(&color, &depth));
    color.mipmaps = false;
    color.height = 0;
    assert(!AreRenderTargetAttachmentsCompatible(&color, nullptr));
}

void CheckQueuesAndOwnership() {
    RHIDevice device;
    auto* backend = new RecordingBackend;
    RHIDeviceTestAccess::Install(device, backend);
    bool ready = false;
    RenderResourceHandle<RenderTargetSpec> target;
    device.async_CreateRenderTarget({{1, 2}, {3, 4}}, [&](auto handle) { ready = true; target = handle; });
    assert(!ready && backend->targetsCreated == 0);
    RHIDeviceTestAccess::Pump(device);
    device.returnSystem.DrainCallbacks();
    assert(ready && target.id == 7 && target.version == 4);
    assert(backend->createdDesc.color.id == 1 && backend->createdDesc.depth.id == 3);

    auto frame = device.BeginFrame({});
    RHICommand::SetRenderTarget command;
    command.target = target;
    command.width = 64;
    command.height = 32;
    command.clearFlags = RHICommand::ClearColor | RHICommand::ClearDepth;
    command.clearColor = glm::vec4(4, 2, 1, 1); // HDR clear values are not clamped by RHI.
    assert(frame.SetRenderTarget(command));
    command.width = 0;
    assert(!frame.SetRenderTarget(command));
    assert(frame.End(false));
    assert(device.async_SubmitFrameCommands(frame.GetCommandBuffer()));
    RHIDeviceTestAccess::Pump(device);
    assert(backend->selected.target == target && backend->selected.width == 64 && backend->selected.height == 32);
    assert(backend->selected.clearColor.x == 4.0f);

    std::array<float, 4> pixels{4, 2, 1, 1};
    CreateRHITextureSpec texture;
    texture.width = texture.height = 1;
    texture.textureDataStoreType = texture.textureUseType = RHITextureFormat::RGBA16F;
    texture.mipmaps = false;
    texture.data = pixels.data();
    device.async_CreateTexture(texture);
    pixels.fill(0);
    RHIDeviceTestAccess::Pump(device);
    assert(backend->uploaded.data.size() == sizeof(pixels));
    float copied = 0;
    std::memcpy(&copied, backend->uploaded.data.data(), sizeof(copied));
    assert(copied == 4.0f && !backend->uploaded.spec.mipmaps);
    texture.data = nullptr;
    device.async_CreateTexture(texture);
    RHIDeviceTestAccess::Pump(device);
    assert(backend->uploaded.data.empty() && backend->uploaded.spec.width == 1);
    texture.width = texture.height = std::numeric_limits<std::uint32_t>::max();
    texture.data = pixels.data();
    device.async_CreateTexture(texture);
    RHIDeviceTestAccess::Pump(device);
    assert(backend->uploaded.spec.width == 0 && backend->uploaded.data.empty());

    Material::OwnedMaterialResources owned;
    for (std::uint32_t i = 0; i < 12; ++i) {
        owned.renderTargets.push_back({i, 1});
        owned.textures.push_back({i, 1});
    }
    Material::RetainMaterialResources(device, std::move(owned)).reset();
    while (RHIDeviceTestAccess::Pending(device)) RHIDeviceTestAccess::Pump(device);
    assert(backend->deletionOrder.size() == 24);
    for (std::size_t i = 0; i < 24; ++i) assert(backend->deletionOrder[i] == (i < 12 ? 'F' : 'T'));
}

GLuint drawFramebuffer = 0, readFramebuffer = 0, nextFramebuffer = 20;
GLuint textureBinding = 0;
GLenum drawBuffer = 0, readBuffer = 0, framebufferStatus = GL_FRAMEBUFFER_COMPLETE;
int drawCalls = 0, unbinds = 0;
bool scissorDisabled = false, srgbDisabled = false;
GLbitfield clearMask = 0;
void APIENTRY FakeGetInteger(GLenum name, GLint* value) {
    if (name == GL_MAX_VIEWPORT_DIMS) { value[0] = value[1] = 8192; }
    else if (name == GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS) *value = 32;
    else if (name == GL_DRAW_FRAMEBUFFER_BINDING) *value = static_cast<GLint>(drawFramebuffer);
    else if (name == GL_READ_FRAMEBUFFER_BINDING) *value = static_cast<GLint>(readFramebuffer);
    else *value = 0;
}
void APIENTRY FakeBindFramebuffer(GLenum target, GLuint value) {
    if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER) drawFramebuffer = value;
    if (target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER) readFramebuffer = value;
}
void APIENTRY FakeGenFramebuffers(GLsizei count, GLuint* output) { while (count--) *output++ = nextFramebuffer++; }
void APIENTRY FakeDeleteFramebuffers(GLsizei, const GLuint*) {}
void APIENTRY FakeAttach(GLenum, GLenum, GLenum, GLuint, GLint) {}
GLenum APIENTRY FakeFramebufferStatus(GLenum) { return framebufferStatus; }
void APIENTRY FakeDrawBuffer(GLenum value) { drawBuffer = value; }
void APIENTRY FakeReadBuffer(GLenum value) { readBuffer = value; }
void APIENTRY FakeViewport(GLint, GLint, GLsizei, GLsizei) {}
void APIENTRY FakeDepthRange(GLdouble, GLdouble) {}
void APIENTRY FakeDisable(GLenum value) {
    if (value == GL_SCISSOR_TEST) scissorDisabled = true;
    if (value == GL_FRAMEBUFFER_SRGB) srgbDisabled = true;
}
void APIENTRY FakeColorMask(GLboolean, GLboolean, GLboolean, GLboolean) {}
void APIENTRY FakeDepthMask(GLboolean value) { assert(value == GL_TRUE); }
void APIENTRY FakeClearColor(GLfloat, GLfloat, GLfloat, GLfloat) {}
void APIENTRY FakeClearDepth(GLdouble) {}
void APIENTRY FakeClear(GLbitfield value) { clearMask = value; }
void APIENTRY FakeActiveTexture(GLenum) {}
void APIENTRY FakeBindTexture(GLenum, GLuint value) { textureBinding = value; if (!value) ++unbinds; }
void APIENTRY FakeDrawArrays(GLenum, GLint, GLsizei) { ++drawCalls; }

void CheckBackendTargetState() {
    glad_glGetIntegerv = &FakeGetInteger;
    glad_glBindFramebuffer = &FakeBindFramebuffer;
    glad_glGenFramebuffers = &FakeGenFramebuffers;
    glad_glDeleteFramebuffers = &FakeDeleteFramebuffers;
    glad_glFramebufferTexture2D = &FakeAttach;
    glad_glCheckFramebufferStatus = &FakeFramebufferStatus;
    glad_glDrawBuffer = &FakeDrawBuffer;
    glad_glReadBuffer = &FakeReadBuffer;
    glad_glViewport = &FakeViewport;
    glad_glDepthRange = &FakeDepthRange;
    glad_glDisable = &FakeDisable;
    glad_glColorMask = &FakeColorMask;
    glad_glDepthMask = &FakeDepthMask;
    glad_glClearColor = &FakeClearColor;
    glad_glClearDepth = &FakeClearDepth;
    glad_glClear = &FakeClear;
    glad_glActiveTexture = &FakeActiveTexture;
    glad_glBindTexture = &FakeBindTexture;
    glad_glDrawArrays = &FakeDrawArrays;
    RHIResourcePool resources;
    OpenglBackend backend({&resources});
    RHITextureSpec depth;
    depth.width = depth.height = 64;
    depth.textureDataStoreType = depth.textureUseType = RHITextureFormat::Depth32F;
    depth.mipmaps = false;
    depth.rhi_id = 55;
    const auto texture = resources.TextureTable.Add(depth);
    const auto target = backend.CreateRenderTarget({{{}, texture}, {}});
    assert(target.IsValid() && drawBuffer == GL_NONE && readBuffer == GL_NONE);
    assert(drawFramebuffer == 0 && readFramebuffer == 0); // Creation restores bindings.
    backend.BindTexture({texture, 4});
    assert(textureBinding == 55);
    RHICommand::SetRenderTarget command;
    command.target = target;
    command.width = command.height = 64;
    command.clearFlags = RHICommand::ClearDepth;
    backend.SetRenderTarget(command);
    assert(drawFramebuffer == 20 && readFramebuffer == 20);
    assert(scissorDisabled && srgbDisabled && clearMask == GL_DEPTH_BUFFER_BIT);
    assert(textureBinding == 0 && unbinds == 1); // Do not leave an output attached as an input sampler.
    command.target.version += 1;
    backend.SetRenderTarget(command);
    backend.DrawRect({});
    assert(drawFramebuffer == 0 && drawCalls == 0); // Stale target cannot draw into old/default framebuffer.
    command.target = {};
    backend.SetRenderTarget(command);
    backend.DrawRect({});
    assert(drawCalls == 1);
    framebufferStatus = GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
    assert(!backend.CreateRenderTarget({{{}, texture}, {}}).IsValid());
    framebufferStatus = GL_FRAMEBUFFER_COMPLETE;
    backend.DeleteRenderTarget({target, {}});
    assert(!resources.renderTargetTable.Get(target));
    assert(resources.TextureTable.Get(texture)); // FBO deletion never owns/deletes its attachments.
}

} // namespace

int main() {
    CheckDescriptors();
    CheckQueuesAndOwnership();
    CheckBackendTargetState();
    std::cout << "RHI render target tests passed\n";
}
