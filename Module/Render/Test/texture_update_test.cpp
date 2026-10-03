#ifdef NDEBUG
#undef NDEBUG
#endif
#include "Render/Private/rhi_device.h"
#include <array>
#include <cassert>
#include <iostream>

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
    UpdateTextureCommand received;
    bool UpdateTexture(const UpdateTextureCommand& command) override {
        received = command;
        return command.desc.width != 0;
    }
};
void Ownership() {
    RHIDevice device;
    auto* backend = new RecordingBackend;
    RHIDeviceTestAccess::Install(device, backend);
    std::array<unsigned char, 8> pixels{1,2,3,4,5,6,7,8};
    UpdateRHITextureDesc desc{1,2,2,1,RHITextureFormat::RGBA8,pixels.data(),pixels.size()};
    int completions = 0;
    device.async_UpdateTexture({4,2}, desc, [&](bool ok) { assert(ok); ++completions; });
    pixels.fill(0);
    assert(completions == 0);
    RHIDeviceTestAccess::Pump(device);
    assert(completions == 1 && backend->received.handle.id == 4);
    assert(backend->received.desc.data == nullptr && backend->received.data.size() == 8);
    assert(backend->received.data.front() == std::byte{1} && backend->received.data.back() == std::byte{8});
    desc.byteSize = 7;
    device.async_UpdateTexture({4,2}, desc, [&](bool ok) { assert(!ok); ++completions; });
    RHIDeviceTestAccess::Pump(device);
    assert(completions == 2 && backend->received.data.empty());
    desc.width = desc.height = 0xffffffffu;
    device.async_UpdateTexture({4,2}, desc, [&](bool ok) { assert(!ok); ++completions; });
    RHIDeviceTestAccess::Pump(device);
    assert(completions == 3 && backend->received.data.empty());
}

GLint binding = 91, alignment = 8, rowLength = 17, skipRows = 2, skipPixels = 3, unpackBuffer = 82;
int uploads = 0, mipmaps = 0;
GLenum injectedError = GL_NO_ERROR;
void APIENTRY GetInteger(GLenum name, GLint* value) {
    if (name == GL_TEXTURE_BINDING_2D) *value = binding;
    else if (name == GL_UNPACK_ALIGNMENT) *value = alignment;
    else if (name == GL_UNPACK_ROW_LENGTH) *value = rowLength;
    else if (name == GL_UNPACK_SKIP_ROWS) *value = skipRows;
    else if (name == GL_UNPACK_SKIP_PIXELS) *value = skipPixels;
    else if (name == GL_PIXEL_UNPACK_BUFFER_BINDING) *value = unpackBuffer;
    else assert(false);
}
void APIENTRY BindTexture(GLenum target, GLuint value) { assert(target == GL_TEXTURE_2D); binding = value; }
void APIENTRY BindBuffer(GLenum target, GLuint value) { assert(target == GL_PIXEL_UNPACK_BUFFER); unpackBuffer = value; }
void APIENTRY Store(GLenum key, GLint value) {
    if (key == GL_UNPACK_ALIGNMENT) alignment = value;
    else if (key == GL_UNPACK_ROW_LENGTH) rowLength = value;
    else if (key == GL_UNPACK_SKIP_ROWS) skipRows = value;
    else if (key == GL_UNPACK_SKIP_PIXELS) skipPixels = value;
    else assert(false);
}
void APIENTRY Upload(GLenum target, GLint level, GLint x, GLint y, GLsizei width, GLsizei height,
    GLenum format, GLenum type, const void* bytes) {
    assert(target == GL_TEXTURE_2D && level == 0 && x == 1 && y == 2 && width == 2 && height == 1);
    assert(format == GL_RGBA && type == GL_UNSIGNED_BYTE && bytes);
    assert(binding == 7 && alignment == 1 && rowLength == 0 && skipRows == 0 && skipPixels == 0 && unpackBuffer == 0);
    ++uploads;
}
void APIENTRY Generate(GLenum) { ++mipmaps; }
GLenum APIENTRY Error() { return injectedError; }
void BackendValidation() {
    glad_glGetIntegerv = &GetInteger;
    glad_glBindTexture = &BindTexture;
    glad_glBindBuffer = &BindBuffer;
    glad_glPixelStorei = &Store;
    glad_glTexSubImage2D = &Upload;
    glad_glGenerateMipmap = &Generate;
    glad_glGetError = &Error;
    RHIResourcePool pool;
    OpenglBackend backend({&pool});
    RHITextureSpec texture;
    texture.width = texture.height = 4;
    texture.rhi_id = 7;
    texture.mipmaps = false;
    auto handle = pool.TextureTable.Add(texture);
    UpdateTextureCommand update;
    update.handle = handle;
    update.desc = {1,2,2,1,RHITextureFormat::RGBA8,nullptr,8};
    update.data.resize(8);
    assert(backend.UpdateTexture(update) && uploads == 1 && mipmaps == 0);
    assert(binding == 91 && alignment == 8 && rowLength == 17 && skipRows == 2 && skipPixels == 3 && unpackBuffer == 82);
    injectedError = GL_INVALID_OPERATION;
    assert(!backend.UpdateTexture(update) && uploads == 2);
    assert(binding == 91 && alignment == 8 && unpackBuffer == 82);
    injectedError = GL_NO_ERROR;
    update.desc.x = 3;
    assert(!backend.UpdateTexture(update) && uploads == 2);
    update.desc.x = 0xffffffffu;
    assert(!backend.UpdateTexture(update) && uploads == 2);
    update.desc.x = 1;
    update.desc.format = RHITextureFormat::RGB8;
    assert(!backend.UpdateTexture(update) && uploads == 2);
    update.desc.format = RHITextureFormat::RGBA8;
    update.data.resize(7);
    assert(!backend.UpdateTexture(update) && uploads == 2);
    update.data.resize(8);
    pool.TextureTable.Remove(handle);
    assert(!backend.UpdateTexture(update) && uploads == 2);
}
}
int main() {
    Ownership();
    BackendValidation();
    std::cout << "Texture update ownership, bounds, formats and GL state tests passed\n";
}
