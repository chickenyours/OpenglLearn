#include "Render/Private/Backend/backend.h"
#include "Render/Public/render_backend_context.h"
#if !defined(RENDER_HAS_VULKAN)
namespace Render {
bool RegisterVulkanBackend() { return false; }
} // namespace Render
#else
#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <regex>
#include <shaderc/shaderc.hpp>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace Render {
namespace {
void Check(VkResult result, const char *operation) {
  if (result != VK_SUCCESS)
    throw std::runtime_error(std::string("Vulkan ") + operation + ": " +
                             std::to_string(result));
}
VkFormat FormatOf(RHITextureFormat f) {
  switch (f) {
  case RHITextureFormat::RGB8:
  case RHITextureFormat::RGBA8:
    return VK_FORMAT_R8G8B8A8_UNORM;
  case RHITextureFormat::RGB32F:
  case RHITextureFormat::RGBA32F:
    return VK_FORMAT_R32G32B32A32_SFLOAT;
  case RHITextureFormat::RGBA16F:
    return VK_FORMAT_R16G16B16A16_SFLOAT;
  case RHITextureFormat::Depth32F:
    return VK_FORMAT_D32_SFLOAT;
  }
  return VK_FORMAT_UNDEFINED;
}
uint32_t PixelBytes(VkFormat f) {
  return f == VK_FORMAT_R32G32B32A32_SFLOAT   ? 16
         : f == VK_FORMAT_R16G16B16A16_SFLOAT ? 8
                                              : 4;
}
uint16_t Half(float f) {
  uint32_t v = std::bit_cast<uint32_t>(f), sign = (v >> 16) & 0x8000;
  int e = int((v >> 23) & 255) - 127 + 15;
  uint32_t m = v & 0x7fffff;
  if (e <= 0) {
    if (e < -10)
      return uint16_t(sign);
    m = (m | 0x800000) >> (1 - e);
    return uint16_t(sign | ((m + 0x1000) >> 13));
  }
  if (e >= 31)
    return uint16_t(sign | 0x7c00 | (m ? 0x200 : 0));
  uint32_t h = sign | (uint32_t(e) << 10) | (m >> 13);
  return uint16_t(
      h + ((m & 0x1fff) > 0x1000 || ((m & 0x1fff) == 0x1000 && (h & 1))));
}
float Float(uint16_t h) {
  uint32_t s = uint32_t(h & 0x8000) << 16, m = h & 1023, e = (h >> 10) & 31, v;
  if (!e) {
    if (!m)
      v = s;
    else {
      int n = -14;
      while (!(m & 1024)) {
        m <<= 1;
        --n;
      }
      v = s | (uint32_t(n + 127) << 23) | ((m & 1023) << 13);
    }
  } else
    v = s | ((e == 31 ? 255 : e + 112) << 23) | (m << 13);
  return std::bit_cast<float>(v);
}
struct Buffer {
  VkBuffer handle{};
  VkDeviceMemory memory{};
  void *mapped{};
  VkDeviceSize bytes{};
};
struct Image {
  VkImage handle{};
  VkDeviceMemory memory{};
  VkImageView view{};
  VkSampler sampler{};
  VkFormat format{};
  uint32_t width{}, height{}, mips{1}, samples{1};
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
  std::vector<std::byte>
      upload; // Immutable geometry uploads are also AS input.
  bool Depth() const { return format == VK_FORMAT_D32_SFLOAT; }
};
struct Mesh {
  Buffer vertices, indices;
};
struct Shader {
  VkShaderModule module{};
  ShaderSourceType type{};
  std::string source;
  std::vector<uint32_t> spirv;
  std::array<uint32_t, 2> bindings{};
  bool acceleration = false;
  uint32_t inputs = 0;
};
struct Program {
  Shader vertex, fragment, geometry;
  bool rayQuery = false;
  int geometrySlot = 0;
  std::array<VkDescriptorSetLayout, 3> layouts{};
  VkPipelineLayout layout{};
  std::array<uint32_t, 2> bindings{};
};
struct Target {
  Image *color{};
  Image *depth{};
  uint32_t width{}, height{}, samples{1};
};
struct Acceleration {
  VkAccelerationStructureKHR blas{}, tlas{};
  Buffer vertices, indices, instances, blasStore, tlasStore, scratch;
};

std::optional<BackendUniformBlock> ReflectBlock(const Shader &shader,
                                                const std::string &name) {
  struct Type {
    uint32_t op = 0;
    std::vector<uint32_t> args;
  };
  std::unordered_map<uint32_t, Type> types;
  std::unordered_map<uint32_t, std::string> names;
  std::map<std::pair<uint32_t, uint32_t>, std::string> memberNames;
  std::map<std::pair<uint32_t, uint32_t>, uint32_t> offsets, matrixStrides;
  std::unordered_map<uint32_t, uint32_t> strides, constants, bindings;
  std::vector<std::pair<uint32_t, uint32_t>> variables;
  auto &code = shader.spirv;
  for (size_t p = 5; p < code.size();) {
    auto n = code[p] >> 16, op = code[p] & 65535;
    auto *a = code.data() + p;
    if (op == 5)
      names[a[1]] = reinterpret_cast<const char *>(a + 2);
    else if (op == 6)
      memberNames[{a[1], a[2]}] = reinterpret_cast<const char *>(a + 3);
    else if (op == 71 && n >= 4) {
      if (a[2] == 6)
        strides[a[1]] = a[3];
      if (a[2] == 33)
        bindings[a[1]] = a[3];
    } else if (op == 72 && n >= 5) {
      if (a[3] == 35)
        offsets[{a[1], a[2]}] = a[4];
      if (a[3] == 7)
        matrixStrides[{a[1], a[2]}] = a[4];
    } else if (op >= 21 && op <= 32)
      types[a[1]] = {op, std::vector<uint32_t>(a + 2, a + n)};
    else if (op == 43 && n >= 4)
      constants[a[2]] = a[3];
    else if (op == 59 && n >= 4 && a[3] == 2)
      variables.push_back({a[1], a[2]});
    p += n;
  }
  std::function<uint32_t(uint32_t)> size = [&](uint32_t id) -> uint32_t {
    auto i = types.find(id);
    if (i == types.end())
      return 0;
    auto &t = i->second;
    auto &a = t.args;
    if (t.op == 21 || t.op == 22)
      return a[0] / 8;
    if (t.op == 23 || t.op == 24)
      return size(a[0]) * a[1];
    if (t.op == 28)
      return strides[id] * constants[a[1]];
    if (t.op == 30) {
      uint32_t end = 0;
      for (uint32_t m = 0; m < a.size(); ++m) {
        auto mt = types.find(a[m]);
        auto ms = size(a[m]);
        if (mt != types.end() && mt->second.op == 24 &&
            matrixStrides.contains({id, m}))
          ms = matrixStrides[{id, m}] * mt->second.args[1];
        end = std::max(end, offsets[{id, m}] + ms);
      }
      return (end + 15) & ~15u;
    }
    return 0;
  };
  for (auto [pointer, variable] : variables) {
    auto i = types.find(pointer);
    if (i == types.end() || i->second.op != 32)
      continue;
    uint32_t type = i->second.args[1];
    if (names[type] != name)
      continue;
    BackendUniformBlock b;
    b.bytes = size(type);
    b.binding = bindings[variable];
    for (uint32_t m = 0; m < types[type].args.size(); ++m)
      b.offsets[memberNames[{type, m}]] = offsets[{type, m}];
    return b;
  }
  return {};
}

class VulkanBackend final : public IBackend {
  VulkanBackendContext context_;
  VkInstance instance_{};
  VkDebugUtilsMessengerEXT debug_{};
  VkSurfaceKHR surface_{};
  VkPhysicalDevice physical_{};
  VkDevice device_{};
  VkPhysicalDeviceProperties properties_{};
  uint32_t queueFamily_{};
  VkQueue queue_{};
  VkCommandPool commandPool_{};
  VkCommandBuffer command_{};
  VkFence fence_{};
  VkSemaphore acquired_{};
  std::vector<VkSemaphore> swapPresented_;
  VkSwapchainKHR swapchain_{};
  VkFormat swapFormat_{};
  VkExtent2D swapExtent_{};
  std::vector<VkImage> swapImages_;
  std::vector<bool> swapInitialized_;
  VkDescriptorPool descriptors_{};
  Buffer uniformRing_;
  size_t uniformCursor_{};
  // Reuse immutable snapshots within a frame. Updates create a new version;
  // earlier draws continue to point at their original ring bytes.
  std::unordered_map<uint32_t, uint64_t> uniformVersions_;
  std::map<std::tuple<uint32_t, uint64_t, size_t, size_t>,
           VkDescriptorBufferInfo> uniformSnapshots_;
  std::map<std::array<uint64_t, 34>, VkDescriptorSet> descriptorSnapshots_;
  VkQueryPool timestamps_{};
  double gpuMs_{};
  uint32_t validationErrors_ = 0;
  Image fallback_, windowColor_, windowDepth_;
  Target current_{};
  bool recording_ = false, rendering_ = false, rayQuery_ = false;
  VkViewport viewport_{};
  VkRect2D scissor_{};
  bool scissorEnabled_ = false;
  uint32_t nextId_ = 1;
  std::unordered_map<uint32_t, Mesh> meshes_;
  std::unordered_map<uint32_t, std::vector<std::byte>> uniforms_;
  std::unordered_map<uint32_t, Shader> shaders_;
  std::unordered_map<uint32_t, Program> programs_;
  std::unordered_map<uint32_t, Image> images_;
  std::map<std::tuple<uint32_t, VkFormat, VkFormat, uint32_t, uint32_t>,
           VkPipeline>
      pipelines_;
  std::unordered_map<uint32_t, Acceleration> acceleration_;
  std::array<RenderResourceHandle<RHITextureSpec>, 16> textureBindings_{};
  std::array<RHICommand::BindUniformBuffer, 16> uniformBindings_{};
  RenderResourceHandle<VertexBufferSpec> mesh_;
  RenderResourceHandle<PipelineSpec> pipeline_;
  PFN_vkCreateAccelerationStructureKHR createAS_{};
  PFN_vkDestroyAccelerationStructureKHR destroyAS_{};
  PFN_vkGetAccelerationStructureBuildSizesKHR asSizes_{};
  PFN_vkCmdBuildAccelerationStructuresKHR buildAS_{};
  PFN_vkGetAccelerationStructureDeviceAddressKHR asAddress_{};
  uint32_t MemoryType(uint32_t bits, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties p;
    vkGetPhysicalDeviceMemoryProperties(physical_, &p);
    for (uint32_t i = 0; i < p.memoryTypeCount; ++i)
      if ((bits & (1u << i)) &&
          (p.memoryTypes[i].propertyFlags & flags) == flags)
        return i;
    throw std::runtime_error("Vulkan: compatible memory type unavailable");
  }
  Buffer MakeBuffer(VkDeviceSize bytes, VkBufferUsageFlags usage,
                    bool host = true) {
    Buffer b{};
    b.bytes = std::max<VkDeviceSize>(bytes, 16);
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = b.bytes;
    info.usage = usage;
    Check(vkCreateBuffer(device_, &info, nullptr, &b.handle), "create buffer");
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device_, b.handle, &req);
    VkMemoryAllocateFlagsInfo address{
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
    address.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = req.size;
    allocation.memoryTypeIndex = MemoryType(
        req.memoryTypeBits, host ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                                 : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
      allocation.pNext = &address;
    Check(vkAllocateMemory(device_, &allocation, nullptr, &b.memory),
          "allocate buffer");
    Check(vkBindBufferMemory(device_, b.handle, b.memory, 0), "bind buffer");
    if (host)
      Check(vkMapMemory(device_, b.memory, 0, b.bytes, 0, &b.mapped),
            "map buffer");
    return b;
  }
  void Destroy(Buffer &b) {
    if (b.mapped)
      vkUnmapMemory(device_, b.memory);
    if (b.handle)
      vkDestroyBuffer(device_, b.handle, nullptr);
    if (b.memory)
      vkFreeMemory(device_, b.memory, nullptr);
    b = {};
  }
  void Destroy(Image &i) {
    if (i.sampler)
      vkDestroySampler(device_, i.sampler, nullptr);
    if (i.view)
      vkDestroyImageView(device_, i.view, nullptr);
    if (i.handle)
      vkDestroyImage(device_, i.handle, nullptr);
    if (i.memory)
      vkFreeMemory(device_, i.memory, nullptr);
    i = {};
  }
  Image MakeImage(uint32_t w, uint32_t h, VkFormat format, uint32_t samples,
                  uint32_t mips = 1) {
    Image i{};
    i.width = w;
    i.height = h;
    i.format = format;
    i.samples = samples;
    i.mips = mips;
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = format;
    ci.extent = {w, h, 1};
    ci.mipLevels = mips;
    ci.arrayLayers = 1;
    ci.samples = VkSampleCountFlagBits(samples);
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
               VK_IMAGE_USAGE_TRANSFER_DST_BIT |
               (i.Depth() ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                          : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    Check(vkCreateImage(device_, &ci, nullptr, &i.handle), "create image");
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device_, i.handle, &req);
    VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ma.allocationSize = req.size;
    ma.memoryTypeIndex =
        MemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    Check(vkAllocateMemory(device_, &ma, nullptr, &i.memory), "allocate image");
    Check(vkBindImageMemory(device_, i.handle, i.memory, 0), "bind image");
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = i.handle;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange = {VkImageAspectFlags(i.Depth()
                                                  ? VK_IMAGE_ASPECT_DEPTH_BIT
                                                  : VK_IMAGE_ASPECT_COLOR_BIT),
                           0, mips, 0, 1};
    Check(vkCreateImageView(device_, &vi, nullptr, &i.view),
          "create image view");
    return i;
  }
  void StartCommands() {
    Check(vkResetCommandPool(device_, commandPool_, 0), "reset command pool");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    Check(vkBeginCommandBuffer(command_, &begin), "begin commands");
    recording_ = true;
  }
  void EndRendering() {
    if (rendering_) {
      vkCmdEndRendering(command_);
      rendering_ = false;
    }
  }
  void Submit(VkSemaphore wait = VK_NULL_HANDLE,
              VkSemaphore signal = VK_NULL_HANDLE) {
    EndRendering();
    Check(vkEndCommandBuffer(command_), "end commands");
    Check(vkResetFences(device_, 1, &fence_), "reset fence");
    VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command_;
    if (wait) {
      submit.waitSemaphoreCount = 1;
      submit.pWaitSemaphores = &wait;
      submit.pWaitDstStageMask = &stage;
    }
    if (signal) {
      submit.signalSemaphoreCount = 1;
      submit.pSignalSemaphores = &signal;
    }
    Check(vkQueueSubmit(queue_, 1, &submit, fence_), "submit");
    Check(vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX),
          "wait submission");
    recording_ = false;
  }
  void Transition(Image &i, VkImageLayout layout) {
    if (i.layout == layout)
      return;
    EndRendering();
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = i.layout;
    b.newLayout = layout;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = i.handle;
    b.subresourceRange = {VkImageAspectFlags(i.Depth()
                                                 ? VK_IMAGE_ASPECT_DEPTH_BIT
                                                 : VK_IMAGE_ASPECT_COLOR_BIT),
                          0, i.mips, 0, 1};
    const auto scope=[](VkImageLayout value) -> std::pair<VkPipelineStageFlags,VkAccessFlags> {
      switch(value) {
      case VK_IMAGE_LAYOUT_UNDEFINED:return {VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,0};
      case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:return {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};
      case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:return {VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
      case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:return {VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,VK_ACCESS_SHADER_READ_BIT};
      case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:return {VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT};
      case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:return {VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT};
      default:return {VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT};
      }
    };
    const auto [sourceStage,sourceAccess]=scope(i.layout);
    const auto [destinationStage,destinationAccess]=scope(layout);
    b.srcAccessMask=sourceAccess;b.dstAccessMask=destinationAccess;
    // Texture transitions synchronize their actual producer/consumer stages;
    // independent raster and transfer work need not drain the entire queue.
    vkCmdPipelineBarrier(command_, sourceStage,
                         destinationStage, 0, 0, nullptr, 0,
                         nullptr, 1, &b);
    i.layout = layout;
  }
  void RenderTarget(uint8_t flags = 0, glm::vec4 color = glm::vec4(0),
                    float depth = 1) {
    if (rendering_)
      return;
    if (current_.color)
      Transition(*current_.color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    if (current_.depth)
      Transition(*current_.depth,
                 VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo c{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO},
        d{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    c.imageView = current_.color ? current_.color->view : VK_NULL_HANDLE;
    c.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    c.loadOp = (flags & RHICommand::ClearColor) ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                                : VK_ATTACHMENT_LOAD_OP_LOAD;
    c.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    std::memcpy(c.clearValue.color.float32, &color, 16);
    d.imageView = current_.depth ? current_.depth->view : VK_NULL_HANDLE;
    d.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    d.loadOp = (flags & RHICommand::ClearDepth) ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                                : VK_ATTACHMENT_LOAD_OP_LOAD;
    d.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    d.clearValue.depthStencil = {depth, 0};
    VkRenderingInfo r{VK_STRUCTURE_TYPE_RENDERING_INFO};
    r.renderArea = {{0, 0}, {current_.width, current_.height}};
    r.layerCount = 1;
    r.colorAttachmentCount = current_.color ? 1 : 0;
    r.pColorAttachments = current_.color ? &c : nullptr;
    r.pDepthAttachment = current_.depth ? &d : nullptr;
    vkCmdBeginRendering(command_, &r);
    rendering_ = true;
  }
  Target LookupTarget(RenderResourceHandle<RenderTargetSpec> h) {
    if (!h.IsValid())
      return {&windowColor_, &windowDepth_, windowColor_.width,
              windowColor_.height, 1};
    auto *t = rhiContext_.resourcePool->renderTargetTable.Get(h);
    if (!t)
      throw std::runtime_error("Vulkan: invalid target handle");
    return {LookupImage(t->color), LookupImage(t->depth), t->width, t->height,
            t->samples};
  }
  Image *LookupImage(RenderResourceHandle<RHITextureSpec> h) {
    auto *t = rhiContext_.resourcePool->TextureTable.Get(h);
    if (!t)
      return nullptr;
    auto it = images_.find(t->rhi_id);
    return it == images_.end() ? nullptr : &it->second;
  }
  void ResizeWindow(uint32_t w, uint32_t h) {
    if (windowColor_.width == w && windowColor_.height == h)
      return;
    Destroy(windowColor_);
    Destroy(windowDepth_);
    windowColor_ = MakeImage(w, h, VK_FORMAT_R8G8B8A8_UNORM, 1);
    windowDepth_ = MakeImage(w, h, VK_FORMAT_D32_SFLOAT, 1);
  }
  bool MakeSwapchain();
  void Present();
  std::vector<std::byte> Convert(const std::vector<std::byte> &data,
                                 RHITextureFormat input, VkFormat output);
  void Upload(Image &, const std::vector<std::byte> &, uint32_t x = 0,
              uint32_t y = 0, uint32_t w = 0, uint32_t h = 0);
  Shader Compile(const CreateShaderSourceCommand &,
                 bool remapVertexDepth = true);
  VkPipeline NativePipeline(const PipelineSpec &, uint32_t meshId);
  Acceleration &BuildAcceleration(uint32_t imageId, const glm::ivec4 &geometry);
  void PrepareDraw();
  void Destroy(Acceleration &a) {
    if (a.tlas)
      destroyAS_(device_, a.tlas, nullptr);
    if (a.blas)
      destroyAS_(device_, a.blas, nullptr);
    Destroy(a.vertices);
    Destroy(a.indices);
    Destroy(a.instances);
    Destroy(a.blasStore);
    Destroy(a.tlasStore);
    Destroy(a.scratch);
    a = {};
  }

protected:
  void Init(void *) override;
  void Shutdown() override;
  RenderResourceHandle<VertexBufferSpec>
  CreateVertexBuffer(const CreateVertexBufferCommand &) override;
  bool UpdateVertexBuffer(const UpdateVertexBufferCommand &) override;
  void DeleteVertexBuffer(const DeleteVertexBufferCommand &) override;
  RenderResourceHandle<UniformBufferSpec>
  CreateUniformBuffer(const CreateUniformBufferCommand &) override;
  void DeleteUniformBuffer(const DeleteUniformBufferCommand &) override;
  RenderResourceHandle<ShaderSourceSpec>
  CreateShaderSource(const CreateShaderSourceCommand &) override;
  RenderResourceHandle<ShaderProgramSpec> CreateGraphicShaderProgram(
      const CreateGraphicShaderProgramCommand &) override;
  void DeleteShaderSource(const DeleteShaderSourceCommand &) override;
  void DeleteShaderProgram(const DeleteShaderProgramCommand &) override;
  RenderResourceHandle<PipelineSpec>
  CreatePipeline(const CreatePipelineCommand &) override;
  void DeletePipeline(const DeletePipelineCommand &) override;
  RenderResourceHandle<RHITextureSpec>
  CreateTexture(const CreateTextureCommand &) override;
  bool UpdateTexture(const UpdateTextureCommand &) override;
  void DeleteTexture(const DeleteTextureCommand &) override;
  RenderResourceHandle<RenderTargetSpec>
  CreateRenderTarget(const CreateRenderTargetCommand &) override;
  void DeleteRenderTarget(const DeleteRenderTargetCommand &c) override {
    rhiContext_.resourcePool->renderTargetTable.Remove(c.handle);
  }
  void SetBackgroundColor(const RHICommand::SetBackgroundColor &c) override {
    RenderTarget(RHICommand::ClearColor, c.color);
  }
  void Flip(const RHICommand::Flip &) override { EndFrame({true}); }
  void SetVertexBuffer(const RHICommand::SetVertexBuffer &c) override {
    mesh_ = c.buffer;
  }
  void SetPipeline(const RHICommand::SetPipeline &c) override {
    pipeline_ = c.pipeline;
  }
  void Draw(const RHICommand::Draw &) override;
  void DrawIndexed(const RHICommand::DrawIndexed &) override;
  void DrawInstance(const RHICommand::DrawInstance &c) override {
    Draw({0, 0, c.instanceCount, 0});
  }
  void DrawRect(const RHICommand::DrawRect &) override { Draw({6}); }
  void BeginFrame(const RHICommand::BeginFrame &) override;
  void EndFrame(const RHICommand::EndFrame &) override;
  void SetRenderTarget(const RHICommand::SetRenderTarget &) override;
  void ResolveRenderTarget(const RHICommand::ResolveRenderTarget &) override;
  void SetViewport(const RHICommand::SetViewport &c) override {
    viewport_ = {float(c.x),      float(c.y), float(c.width),
                 float(c.height), c.minDepth, c.maxDepth};
  }
  void SetScissor(const RHICommand::SetScissor &c) override {
    scissorEnabled_ = c.enabled;
    scissor_ = {{c.x, c.y}, {c.width, c.height}};
  }
  void BindTexture(const RHICommand::BindTexture &c) override {
    if (c.slot < 16)
      textureBindings_[c.slot] = c.texture;
  }
  void BindUniformBuffer(const RHICommand::BindUniformBuffer &c) override {
    if (c.binding < 16)
      uniformBindings_[c.binding] = c;
  }
  void UpdateUniformBuffer(const RHICommand::UpdateUniformBuffer &) override;

public:
  explicit VulkanBackend(const RHIBackContext &c) : IBackend(c) {}
  bool HardwareRayTracingAvailable() const override { return rayQuery_; }
  std::string DeviceName() const override { return properties_.deviceName; }
  double LastGpuMilliseconds() const override { return gpuMs_; }
  void AbortFrame() override {
    ++validationErrors_;
    if (recording_) {
      // Retire the valid prefix: recorded layout transitions and AS
      // builds must reach the GPU before their tracked state is reused.
      Submit();
    }
  }
  uint32_t ValidationErrorCount() const override { return validationErrors_; }
  std::vector<float> ReadTexture(RenderResourceHandle<RHITextureSpec>) override;
  std::vector<uint8_t> ReadWindow() override;
  std::optional<BackendUniformBlock>
  ReflectUniformBlock(RenderResourceHandle<ShaderProgramSpec> h,
                      const std::string &name) override {
    auto *s = rhiContext_.resourcePool->shaderProgramTable.Get(h);
    if (!s)
      return {};
    auto &p = programs_.at(s->rhi_id);
    for (auto *stage : {&p.fragment, &p.vertex, &p.geometry})
      if (auto b = ReflectBlock(*stage, name))
        return b;
    return {};
  }
};

void VulkanBackend::Init(void *data) {
  context_ = *static_cast<VulkanBackendContext *>(data);
  if (!context_.window)
    throw std::runtime_error("Vulkan: missing GLFW window");
  uint32_t extCount = 0;
  auto extensions = glfwGetRequiredInstanceExtensions(&extCount);
  if (!extensions)
    throw std::runtime_error("Vulkan loader unavailable");
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "OpenglLearn RHI";
  app.apiVersion = VK_API_VERSION_1_3;
  const char *layer = "VK_LAYER_KHRONOS_validation";
  std::vector<const char *> instanceExts(extensions, extensions + extCount);
  if (context_.validation)
    instanceExts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  VkDebugUtilsMessengerCreateInfoEXT debug{
      VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
  debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                      VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  debug.pUserData = &validationErrors_;
  debug.pfnUserCallback = [](VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                             VkDebugUtilsMessageTypeFlagsEXT,
                             const VkDebugUtilsMessengerCallbackDataEXT *d,
                             void *user) -> VkBool32 {
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
      ++*static_cast<uint32_t *>(user);
    std::cerr << "Vulkan validation: " << d->pMessage << '\n';
    return VK_FALSE;
  };
  VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ci.pApplicationInfo = &app;
  ci.enabledExtensionCount = uint32_t(instanceExts.size());
  ci.ppEnabledExtensionNames = instanceExts.data();
  if (context_.validation) {
    ci.enabledLayerCount = 1;
    ci.ppEnabledLayerNames = &layer;
    ci.pNext = &debug;
  }
  Check(vkCreateInstance(&ci, nullptr, &instance_), "create instance");
  if (context_.validation) {
    auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
    Check(create(instance_, &debug, nullptr, &debug_),
          "create validation messenger");
  }
  Check(glfwCreateWindowSurface(instance_,
                                static_cast<GLFWwindow *>(context_.window),
                                nullptr, &surface_),
        "create window surface");
  uint32_t count = 0;
  Check(vkEnumeratePhysicalDevices(instance_, &count, nullptr),
        "enumerate devices");
  std::vector<VkPhysicalDevice> devices(count);
  vkEnumeratePhysicalDevices(instance_, &count, devices.data());
  int best = -1;
  for (auto device : devices) {
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(device, &properties);
    if (properties.apiVersion < VK_API_VERSION_1_3)
      continue;
    uint32_t n = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &n, nullptr);
    std::vector<VkQueueFamilyProperties> q(n);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &n, q.data());
    for (uint32_t i = 0; i < n; ++i) {
      VkBool32 present = 0;
      vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &present);
      if (!present || !(q[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) ||
          !q[i].timestampValidBits)
        continue;
      int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU
                      ? 100
                      : 10;
      if (score > best) {
        physical_ = device;
        queueFamily_ = i;
        properties_ = properties;
        best = score;
      }
      break;
    }
  }
  if (!physical_)
    throw std::runtime_error(
        "Vulkan 1.3 graphics/presentation device unavailable");
  uint32_t n = 0;
  vkEnumerateDeviceExtensionProperties(physical_, nullptr, &n, nullptr);
  std::vector<VkExtensionProperties> exts(n);
  vkEnumerateDeviceExtensionProperties(physical_, nullptr, &n, exts.data());
  auto has = [&](const char *name) {
    return std::any_of(exts.begin(), exts.end(), [&](auto &x) {
      return std::strcmp(name, x.extensionName) == 0;
    });
  };
  VkPhysicalDeviceRayQueryFeaturesKHR ray{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
  VkPhysicalDeviceAccelerationStructureFeaturesKHR as{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
  VkPhysicalDeviceVulkan12Features f12{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
  VkPhysicalDeviceVulkan13Features f13{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
  f13.pNext = &f12;
  bool rayExtensions = has(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) &&
                       has(VK_KHR_RAY_QUERY_EXTENSION_NAME) &&
                       has(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
  if (rayExtensions) {
    f12.pNext = &as;
    as.pNext = &ray;
  }
  VkPhysicalDeviceFeatures2 features{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
  features.pNext = &f13;
  vkGetPhysicalDeviceFeatures2(physical_, &features);
  if (!f13.dynamicRendering)
    throw std::runtime_error("Vulkan dynamic rendering unavailable");
  rayQuery_ = context_.hardwareRayTracing && rayExtensions && ray.rayQuery &&
              as.accelerationStructure && f12.bufferDeviceAddress;
  if (context_.requireHardwareRayTracing && !rayQuery_)
    throw std::runtime_error(
        "Selected Vulkan GPU does not support native hardware ray queries");
  VkPhysicalDeviceFeatures enabled{};
  enabled.geometryShader = features.features.geometryShader;
  enabled.fillModeNonSolid = features.features.fillModeNonSolid;
  f13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
  f13.dynamicRendering = VK_TRUE;
  f12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
  f13.pNext = &f12;
  f12.bufferDeviceAddress = rayQuery_;
  as = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
  as.accelerationStructure = rayQuery_;
  ray = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
  ray.rayQuery = rayQuery_;
  if (rayQuery_) {
    f12.pNext = &as;
    as.pNext = &ray;
  }
  std::vector<const char *> requested{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  if (rayQuery_) {
    requested.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
    requested.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
    requested.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
  }
  float priority = 1;
  VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  q.queueFamilyIndex = queueFamily_;
  q.queueCount = 1;
  q.pQueuePriorities = &priority;
  VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  dc.pNext = &f13;
  dc.pEnabledFeatures = &enabled;
  dc.queueCreateInfoCount = 1;
  dc.pQueueCreateInfos = &q;
  dc.enabledExtensionCount = uint32_t(requested.size());
  dc.ppEnabledExtensionNames = requested.data();
  Check(vkCreateDevice(physical_, &dc, nullptr, &device_), "create device");
  vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
  if (rayQuery_) {
#define LOAD_AS(member, name)                                                  \
  member = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device_, #name));  \
  if (!member)                                                                 \
  throw std::runtime_error("Missing Vulkan entry point: " #name)
    LOAD_AS(createAS_, vkCreateAccelerationStructureKHR);
    LOAD_AS(destroyAS_, vkDestroyAccelerationStructureKHR);
    LOAD_AS(asSizes_, vkGetAccelerationStructureBuildSizesKHR);
    LOAD_AS(buildAS_, vkCmdBuildAccelerationStructuresKHR);
    LOAD_AS(asAddress_, vkGetAccelerationStructureDeviceAddressKHR);
#undef LOAD_AS
  }
  VkCommandPoolCreateInfo pc{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pc.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pc.queueFamilyIndex = queueFamily_;
  Check(vkCreateCommandPool(device_, &pc, nullptr, &commandPool_),
        "create command pool");
  VkCommandBufferAllocateInfo ca{
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  ca.commandPool = commandPool_;
  ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ca.commandBufferCount = 1;
  Check(vkAllocateCommandBuffers(device_, &ca, &command_), "allocate commands");
  VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  Check(vkCreateFence(device_, &fc, nullptr, &fence_), "create fence");
  VkSemaphoreCreateInfo sc{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  Check(vkCreateSemaphore(device_, &sc, nullptr, &acquired_),
        "create acquire semaphore");
  std::array<VkDescriptorPoolSize, 3> sizes{
      {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 65536},
       {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 65536},
       {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
        rayQuery_ ? 4096u : 0u}}};
  VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  dp.maxSets = 12288;
  dp.poolSizeCount = rayQuery_ ? 3 : 2;
  dp.pPoolSizes = sizes.data();
  Check(vkCreateDescriptorPool(device_, &dp, nullptr, &descriptors_),
        "create descriptor pool");
  uniformRing_ =
      MakeBuffer(64 * 1024 * 1024, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
  VkQueryPoolCreateInfo qp{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
  qp.queryType = VK_QUERY_TYPE_TIMESTAMP;
  qp.queryCount = 2;
  Check(vkCreateQueryPool(device_, &qp, nullptr, &timestamps_),
        "create timestamp pool");
  fallback_ = MakeImage(1, 1, VK_FORMAT_R8G8B8A8_UNORM, 1);
  VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  si.magFilter = si.minFilter = VK_FILTER_NEAREST;
  si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  si.addressModeU = si.addressModeV = si.addressModeW =
      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  Check(vkCreateSampler(device_, &si, nullptr, &fallback_.sampler),
        "fallback sampler");
  Upload(fallback_, std::vector<std::byte>(4));
  MakeSwapchain();
  std::cout << "Vulkan RHI: " << properties_.deviceName
            << " | native ray query=" << rayQuery_ << " | UBO/sampler limits="
            << properties_.limits.maxPerStageDescriptorUniformBuffers << '/'
            << properties_.limits.maxPerStageDescriptorSamplers << '\n';
}
void VulkanBackend::Shutdown() {
  if (device_) {
    vkDeviceWaitIdle(device_);
    for (auto &[id, a] : acceleration_)
      Destroy(a);
    acceleration_.clear();
    for (auto &[key, p] : pipelines_)
      vkDestroyPipeline(device_, p, nullptr);
    pipelines_.clear();
    for (auto &[id, p] : programs_) {
      if (p.layout)
        vkDestroyPipelineLayout(device_, p.layout, nullptr);
      for (auto l : p.layouts)
        if (l)
          vkDestroyDescriptorSetLayout(device_, l, nullptr);
      for (auto *s : {&p.vertex, &p.fragment, &p.geometry})
        if (s->module)
          vkDestroyShaderModule(device_, s->module, nullptr);
    }
    programs_.clear();
    for (auto &[id, s] : shaders_)
      vkDestroyShaderModule(device_, s.module, nullptr);
    shaders_.clear();
    for (auto &[id, i] : images_)
      Destroy(i);
    images_.clear();
    for (auto &[id, m] : meshes_) {
      Destroy(m.vertices);
      Destroy(m.indices);
    }
    meshes_.clear();
    Destroy(fallback_);
    Destroy(windowColor_);
    Destroy(windowDepth_);
    Destroy(uniformRing_);
    if (timestamps_)
      vkDestroyQueryPool(device_, timestamps_, nullptr);
    if (descriptors_)
      vkDestroyDescriptorPool(device_, descriptors_, nullptr);
    if (swapchain_)
      vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    if (acquired_)
      vkDestroySemaphore(device_, acquired_, nullptr);
    for (auto semaphore : swapPresented_)
      vkDestroySemaphore(device_, semaphore, nullptr);
    swapPresented_.clear();
    if (fence_)
      vkDestroyFence(device_, fence_, nullptr);
    if (commandPool_)
      vkDestroyCommandPool(device_, commandPool_, nullptr);
    vkDestroyDevice(device_, nullptr);
    device_ = VK_NULL_HANDLE;
  }
  if (surface_)
    vkDestroySurfaceKHR(instance_, surface_, nullptr);
  surface_ = VK_NULL_HANDLE;
  if (debug_)
    reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"))(
        instance_, debug_, nullptr);
  debug_ = VK_NULL_HANDLE;
  if (instance_)
    vkDestroyInstance(instance_, nullptr);
  instance_ = VK_NULL_HANDLE;
}
bool VulkanBackend::MakeSwapchain() {
  vkDeviceWaitIdle(device_);
  VkSurfaceCapabilitiesKHR cap;
  Check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &cap),
        "surface capabilities");
  if(!cap.currentExtent.width||!cap.currentExtent.height)return false;
  uint32_t count = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &count, nullptr);
  std::vector<VkSurfaceFormatKHR> formats(count);
  vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &count,
                                       formats.data());
  auto format = formats.front();
  for (auto f : formats)
    if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
        f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      format = f;
      break;
    }
  swapFormat_ = format.format;
  swapExtent_ = cap.currentExtent;
  if (swapExtent_.width == UINT32_MAX) {
    int w = 0, h = 0;
    glfwGetFramebufferSize(static_cast<GLFWwindow *>(context_.window), &w, &h);
    swapExtent_ = {
        std::clamp(uint32_t(std::max(w, 1)), cap.minImageExtent.width,
                   cap.maxImageExtent.width),
        std::clamp(uint32_t(std::max(h, 1)), cap.minImageExtent.height,
                   cap.maxImageExtent.height)};
  }
  if (!(cap.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
    throw std::runtime_error(
        "Vulkan surface does not support presentation blits");
  VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
  if (!context_.vsync) {
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &count,
                                              nullptr);
    std::vector<VkPresentModeKHR> modes(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &count,
                                              modes.data());
    if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_MAILBOX_KHR) !=
        modes.end())
      mode = VK_PRESENT_MODE_MAILBOX_KHR;
    else if (std::find(modes.begin(), modes.end(),
                       VK_PRESENT_MODE_IMMEDIATE_KHR) != modes.end())
      mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
  }
  VkSwapchainCreateInfoKHR si{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  si.surface = surface_;
  si.minImageCount = std::max(3u, cap.minImageCount);
  if (cap.maxImageCount)
    si.minImageCount = std::min(si.minImageCount, cap.maxImageCount);
  si.imageFormat = format.format;
  si.imageColorSpace = format.colorSpace;
  si.imageExtent = swapExtent_;
  si.imageArrayLayers = 1;
  si.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  si.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  si.preTransform = cap.currentTransform;
  si.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  si.presentMode = mode;
  si.clipped = VK_TRUE;
  si.oldSwapchain = swapchain_;
  VkSwapchainKHR next;
  Check(vkCreateSwapchainKHR(device_, &si, nullptr, &next), "create swapchain");
  if (swapchain_)
    vkDestroySwapchainKHR(device_, swapchain_, nullptr);
  swapchain_ = next;
  vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr);
  swapImages_.resize(count);
  vkGetSwapchainImagesKHR(device_, swapchain_, &count, swapImages_.data());
  swapInitialized_.assign(count, false);
  for (auto semaphore : swapPresented_)
    vkDestroySemaphore(device_, semaphore, nullptr);
  swapPresented_.assign(count, VK_NULL_HANDLE);
  VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  for (auto& semaphore : swapPresented_)
    Check(vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &semaphore), "create image presentation semaphore");
  std::cout << "Vulkan presentation: " << swapExtent_.width << 'x' << swapExtent_.height
            << " mode=" << (mode==VK_PRESENT_MODE_MAILBOX_KHR?"MAILBOX":mode==VK_PRESENT_MODE_IMMEDIATE_KHR?"IMMEDIATE":"FIFO")
            << " images=" << count << " vsync=" << context_.vsync << '\n';
  return true;
}
void VulkanBackend::Present() {
  int windowWidth=0,windowHeight=0;
  glfwGetFramebufferSize(static_cast<GLFWwindow*>(context_.window),&windowWidth,&windowHeight);
  if(windowWidth<=0||windowHeight<=0||(!swapchain_&&!MakeSwapchain())){Submit();return;}
  uint32_t index = 0;
  auto result = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
                                      acquired_, VK_NULL_HANDLE, &index);
  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    if(!MakeSwapchain()){Submit();return;}
    result = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, acquired_,
                                   VK_NULL_HANDLE, &index);
  }
  if (result != VK_SUBOPTIMAL_KHR)
    Check(result, "acquire swapchain");
  Transition(windowColor_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex =
      VK_QUEUE_FAMILY_IGNORED;
  barrier.image = swapImages_[index];
  barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  barrier.oldLayout = swapInitialized_[index] ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                                              : VK_IMAGE_LAYOUT_UNDEFINED;
  barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  vkCmdPipelineBarrier(command_, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &barrier);
  // Offscreen images retain GL's bottom-row-first convention. Only the final
  // swapchain blit flips Y; all existing projection/UV/depth code stays intact.
  VkImageBlit blit{};
  blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  blit.dstSubresource = blit.srcSubresource;
  blit.srcOffsets[0] = {0, int(windowColor_.height), 0};
  blit.srcOffsets[1] = {int(windowColor_.width), 0, 1};
  blit.dstOffsets[1] = {int(swapExtent_.width), int(swapExtent_.height), 1};
  vkCmdBlitImage(command_, windowColor_.handle, windowColor_.layout,
                 swapImages_[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                 &blit, VK_FILTER_LINEAR);
  barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask = 0;
  vkCmdPipelineBarrier(command_, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &barrier);
  swapInitialized_[index] = true;
  const auto presented = swapPresented_[index];
  Submit(acquired_, presented);
  VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  present.waitSemaphoreCount = 1;
  present.pWaitSemaphores = &presented;
  present.swapchainCount = 1;
  present.pSwapchains = &swapchain_;
  present.pImageIndices = &index;
  result = vkQueuePresentKHR(queue_, &present);
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    MakeSwapchain();
  else
    Check(result, "present");
  // The submission fence still retires every resource command. Presentation
  // can continue independently; reacquiring this image makes its semaphore
  // reusable (Khronos swapchain semaphore reuse guidance).
}
std::vector<std::byte>
VulkanBackend::Convert(const std::vector<std::byte> &data,
                       RHITextureFormat input, VkFormat output) {
  size_t inBytes = GetTextureFormatByteSize(input);
  if (!inBytes || data.size() % inBytes)
    throw std::runtime_error("Vulkan: invalid texture upload size");
  size_t count = data.size() / inBytes;
  std::vector<std::byte> result(count * PixelBytes(output));
  bool byte =
      input == RHITextureFormat::RGB8 || input == RHITextureFormat::RGBA8;
  int channels =
      input == RHITextureFormat::RGB8 || input == RHITextureFormat::RGB32F ? 3
      : input == RHITextureFormat::Depth32F                                ? 1
                                                                           : 4;
  for (size_t i = 0; i < count; ++i) {
    float v[4]{0, 0, 0, 1};
    for (int c = 0; c < channels; ++c) {
      if (byte)
        v[c] = float(std::to_integer<uint8_t>(data[i * inBytes + c])) / 255;
      else
        std::memcpy(&v[c], data.data() + i * inBytes + c * 4, 4);
    }
    auto *out = result.data() + i * PixelBytes(output);
    if (output == VK_FORMAT_R8G8B8A8_UNORM)
      for (int c = 0; c < 4; ++c)
        out[c] =
            std::byte(uint8_t(std::round(std::clamp(v[c], 0.f, 1.f) * 255)));
    else if (output == VK_FORMAT_R16G16B16A16_SFLOAT)
      for (int c = 0; c < 4; ++c) {
        auto h = Half(v[c]);
        std::memcpy(out + c * 2, &h, 2);
      }
    else
      std::memcpy(out, v, output == VK_FORMAT_D32_SFLOAT ? 4 : 16);
  }
  return result;
}
void VulkanBackend::Upload(Image &image, const std::vector<std::byte> &data,
                           uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
  if (data.empty())
    return;
  if (!w)
    w = image.width;
  if (!h)
    h = image.height;
  if (recording_)
    throw std::runtime_error(
        "Vulkan: resource uploads must precede frame recording");
  Buffer staging = MakeBuffer(data.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
  std::memcpy(staging.mapped, data.data(), data.size());
  StartCommands();
  Transition(image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  VkBufferImageCopy region{};
  region.imageSubresource = {
      VkImageAspectFlags(image.Depth() ? VK_IMAGE_ASPECT_DEPTH_BIT
                                       : VK_IMAGE_ASPECT_COLOR_BIT),
      0, 0, 1};
  region.imageOffset = {int(x), int(y), 0};
  region.imageExtent = {w, h, 1};
  vkCmdCopyBufferToImage(command_, staging.handle, image.handle, image.layout,
                         1, &region);
  if (image.mips > 1) {
    for (uint32_t m = 1; m < image.mips; ++m) {
      VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
      b.image = image.handle;
      b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, m - 1, 1, 0, 1};
      b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      vkCmdPipelineBarrier(command_, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                           nullptr, 1, &b);
      VkImageBlit blit{};
      blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, m - 1, 0, 1};
      blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, m, 0, 1};
      blit.srcOffsets[1] = {int(std::max(1u, image.width >> (m - 1))),
                            int(std::max(1u, image.height >> (m - 1))), 1};
      blit.dstOffsets[1] = {int(std::max(1u, image.width >> m)),
                            int(std::max(1u, image.height >> m)), 1};
      vkCmdBlitImage(command_, image.handle,
                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image.handle,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                     VK_FILTER_LINEAR);
      b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      vkCmdPipelineBarrier(command_, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                           nullptr, 1, &b);
    }
  }
  Transition(image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  Submit();
  Destroy(staging);
}
RenderResourceHandle<RHITextureSpec>
VulkanBackend::CreateTexture(const CreateTextureCommand &c) {
  try {
    if (!c.spec.width || !c.spec.height ||
        c.spec.width > properties_.limits.maxImageDimension2D ||
        c.spec.height > properties_.limits.maxImageDimension2D ||
        FormatOf(c.spec.textureDataStoreType) == VK_FORMAT_UNDEFINED ||
        !IsValidTextureSampleCount(c.spec.samples) ||
        c.spec.samples > 1 && (c.spec.mipmaps || !c.data.empty()))
      return {};
    uint32_t mips = c.spec.mipmaps ? uint32_t(std::floor(std::log2(std::max(
                                         c.spec.width, c.spec.height)))) +
                                         1
                                   : 1;
    auto image =
        MakeImage(c.spec.width, c.spec.height,
                  FormatOf(c.spec.textureDataStoreType), c.spec.samples, mips);
    VkSamplerCreateInfo s{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    s.magFilter = s.minFilter = c.spec.filterMode == RHIFilterMode::Linear
                                    ? VK_FILTER_LINEAR
                                    : VK_FILTER_NEAREST;
    s.mipmapMode = c.spec.mipmapMode == RHIMipmapMode::Linear
                       ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                       : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    auto address = c.spec.addressMode == RHIAddressMode::Repeat
                       ? VK_SAMPLER_ADDRESS_MODE_REPEAT
                   : c.spec.addressMode == RHIAddressMode::MirroredRepeat
                       ? VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT
                   : c.spec.addressMode == RHIAddressMode::ClampToBorder
                       ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER
                       : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    s.addressModeU = s.addressModeV = s.addressModeW = address;
    s.maxLod = float(mips - 1);
    s.borderColor = image.Depth() ? VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE
                                  : VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    Check(vkCreateSampler(device_, &s, nullptr, &image.sampler),
          "create sampler");
    if (!c.data.empty()) {
      auto bytes = Convert(c.data, c.spec.textureUseType, image.format);
      if (bytes.size() !=
          size_t(image.width) * image.height * PixelBytes(image.format)) {
        Destroy(image);
        return {};
      }
      Upload(image, bytes);
      if (image.format == VK_FORMAT_R32G32B32A32_SFLOAT)
        image.upload = std::move(bytes);
    }
    auto id = nextId_++;
    images_.emplace(id, std::move(image));
    RHITextureSpec spec{};
    spec.width = c.spec.width;
    spec.height = c.spec.height;
    spec.textureDataStoreType = c.spec.textureDataStoreType;
    spec.textureUseType = c.spec.textureUseType;
    spec.filterMode = c.spec.filterMode;
    spec.mipmapMode = c.spec.mipmapMode;
    spec.addressMode = c.spec.addressMode;
    spec.mipmaps = c.spec.mipmaps;
    spec.samples = c.spec.samples;
    spec.rhi_id = id;
    return rhiContext_.resourcePool->TextureTable.Add(spec);
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return {};
  }
}
bool VulkanBackend::UpdateTexture(const UpdateTextureCommand &c) {
  auto *image = LookupImage(c.handle);
  if (!image || !c.desc.width || !c.desc.height || c.data.empty() ||
      image->samples != 1 || uint64_t(c.desc.x) + c.desc.width > image->width ||
      uint64_t(c.desc.y) + c.desc.height > image->height)
    return false;
  try {
    auto bytes = Convert(c.data, c.desc.format, image->format);
    if (bytes.size() !=
        size_t(c.desc.width) * c.desc.height * PixelBytes(image->format))
      return false;
    auto *spec = rhiContext_.resourcePool->TextureTable.Get(c.handle);
    auto a = acceleration_.find(spec->rhi_id);
    if (a != acceleration_.end()) {
      Destroy(a->second);
      acceleration_.erase(a);
    }
    Upload(*image, bytes, c.desc.x, c.desc.y, c.desc.width, c.desc.height);
    if (!image->upload.empty())
      for (uint32_t y = 0; y < c.desc.height; ++y)
        std::memcpy(
            image->upload.data() + ((c.desc.y + y) * image->width + c.desc.x) *
                                       PixelBytes(image->format),
            bytes.data() + size_t(y) * c.desc.width * PixelBytes(image->format),
            size_t(c.desc.width) * PixelBytes(image->format));
    return true;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return false;
  }
}
void VulkanBackend::DeleteTexture(const DeleteTextureCommand &c) {
  auto *spec = rhiContext_.resourcePool->TextureTable.Get(c.handle);
  if (!spec)
    return;
  auto a = acceleration_.find(spec->rhi_id);
  if (a != acceleration_.end()) {
    Destroy(a->second);
    acceleration_.erase(a);
  }
  Destroy(images_.at(spec->rhi_id));
  images_.erase(spec->rhi_id);
  rhiContext_.resourcePool->TextureTable.Remove(c.handle);
}
RenderResourceHandle<RenderTargetSpec>
VulkanBackend::CreateRenderTarget(const CreateRenderTargetCommand &c) {
  auto *color = rhiContext_.resourcePool->TextureTable.Get(c.desc.color);
  auto *depth = rhiContext_.resourcePool->TextureTable.Get(c.desc.depth);
  if (c.desc.color.IsValid() && !color || c.desc.depth.IsValid() && !depth ||
      !AreRenderTargetAttachmentsCompatible(color, depth))
    return {};
  auto *dimensions = color ? color : depth;
  return rhiContext_.resourcePool->renderTargetTable.Add(
      RenderTargetSpec{dimensions->width, dimensions->height, nextId_++,
                       c.desc.color, c.desc.depth, dimensions->samples});
}
RenderResourceHandle<VertexBufferSpec>
VulkanBackend::CreateVertexBuffer(const CreateVertexBufferCommand &c) {
  if (c.vertexData.empty() || !c.numVertex)
    return {};
  try {
    Mesh m{};
    m.vertices =
        MakeBuffer(c.vertexData.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    std::memcpy(m.vertices.mapped, c.vertexData.data(), c.vertexData.size());
    if (!c.indexData.empty()) {
      m.indices =
          MakeBuffer(c.indexData.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
      std::memcpy(m.indices.mapped, c.indexData.data(), c.indexData.size());
    }
    auto id = nextId_++;
    meshes_.emplace(id, m);
    VertexBufferSpec s{};
    s.layout = c.vertexLayout;
    s.num = s.vertexCount = c.numVertex;
    s.indexCount = c.numIndex;
    s.indexType = c.indexType;
    s.usage = c.usage;
    s.rhi_id = id;
    s.vertexByteSize = uint32_t(c.vertexData.size());
    s.indexByteSize = uint32_t(c.indexData.size());
    s.isUseElementBuffer = !c.indexData.empty();
    return rhiContext_.resourcePool->vertexBufferTable.Add(s);
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return {};
  }
}
bool VulkanBackend::UpdateVertexBuffer(const UpdateVertexBufferCommand &c) {
  auto *s = rhiContext_.resourcePool->vertexBufferTable.Get(c.handle);
  if (!s || c.vertexData.empty() || !c.numVertex)
    return false;
  auto &m = meshes_.at(s->rhi_id);
  Destroy(m.vertices);
  Destroy(m.indices);
  m.vertices =
      MakeBuffer(c.vertexData.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
  std::memcpy(m.vertices.mapped, c.vertexData.data(), c.vertexData.size());
  if (!c.indexData.empty()) {
    m.indices =
        MakeBuffer(c.indexData.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    std::memcpy(m.indices.mapped, c.indexData.data(), c.indexData.size());
  }
  s->num = s->vertexCount = c.numVertex;
  s->indexCount = c.numIndex;
  s->indexType = c.indexType;
  s->vertexByteSize = uint32_t(c.vertexData.size());
  s->indexByteSize = uint32_t(c.indexData.size());
  s->isUseElementBuffer = !c.indexData.empty();
  return true;
}
void VulkanBackend::DeleteVertexBuffer(const DeleteVertexBufferCommand &c) {
  auto *s = rhiContext_.resourcePool->vertexBufferTable.Get(c.handle);
  if (!s)
    return;
  auto &m = meshes_.at(s->rhi_id);
  Destroy(m.vertices);
  Destroy(m.indices);
  meshes_.erase(s->rhi_id);
  rhiContext_.resourcePool->vertexBufferTable.Remove(c.handle);
}
RenderResourceHandle<UniformBufferSpec>
VulkanBackend::CreateUniformBuffer(const CreateUniformBufferCommand &c) {
  if (!c.byteSize || c.byteSize > properties_.limits.maxUniformBufferRange ||
      c.initialData.size() > c.byteSize)
    return {};
  auto id = nextId_++;
  auto &data = uniforms_[id];
  data.resize(c.byteSize);
  std::copy(c.initialData.begin(), c.initialData.end(), data.begin());
  uniformVersions_[id] = 1;
  return rhiContext_.resourcePool->uniformBufferTable.Add(
      UniformBufferSpec{c.byteSize, id, c.usage});
}
void VulkanBackend::DeleteUniformBuffer(const DeleteUniformBufferCommand &c) {
  auto *s = rhiContext_.resourcePool->uniformBufferTable.Get(c.handle);
  if (!s)
    return;
  uniforms_.erase(s->rhi_id);
  uniformVersions_.erase(s->rhi_id);
  rhiContext_.resourcePool->uniformBufferTable.Remove(c.handle);
}
void VulkanBackend::UpdateUniformBuffer(
    const RHICommand::UpdateUniformBuffer &c) {
  auto *s = rhiContext_.resourcePool->uniformBufferTable.Get(c.buffer);
  if (!s || c.size > c.data.size() || uint64_t(c.offset) + c.size > s->byteSize)
    return;
  auto *destination = uniforms_.at(s->rhi_id).data() + c.offset;
  if (c.size && std::memcmp(destination, c.data.data(), c.size) != 0) {
    std::memcpy(destination, c.data.data(), c.size);
    ++uniformVersions_.at(s->rhi_id);
  }
}
Shader VulkanBackend::Compile(const CreateShaderSourceCommand &c,
                              bool remapVertexDepth) {
  Shader s{};
  s.type = c.type;
  s.source = c.source;
  std::string source = c.source;
  // Uniform buffers and textures share binding numbers in OpenGL. Vulkan
  // separates these namespaces into sets without changing frontend bindings.
  source = std::regex_replace(
      source,
      std::regex(R"(layout\s*\(([^)]*)\)\s*uniform\s+([iu]?sampler\w+))"),
      "layout($1,set=1) uniform $2");
  source = std::regex_replace(
      source, std::regex(R"(layout\s*\(([^)]*std140[^)]*)\)\s*uniform)"),
      "layout($1,set=0) uniform");
  if (rayQuery_ &&
      source.find("RENDER_NATIVE_RAY_QUERY") != std::string::npos) {
    source = std::regex_replace(source, std::regex(R"(#version\s+450)"),
                                "#version 460");
    auto line = source.find('\n');
    source.insert(line + 1, "#extension GL_EXT_ray_query : require\n#define "
                            "RENDER_NATIVE_RAY_QUERY 1\n");
  }
  if (c.type == ShaderSourceType::Vertex) {
    if (remapVertexDepth) {
      source =
          std::regex_replace(source, std::regex(R"(void\s+main\s*\(\s*\))"),
                             "void RhiSourceMain()");
      source += "\nvoid "
                "main(){RhiSourceMain();gl_Position.z=(gl_Position.z+gl_"
                "Position.w)*.5;}\n";
    }
    source =
        std::regex_replace(source, std::regex("gl_VertexID"), "gl_VertexIndex");
    source = std::regex_replace(source, std::regex("gl_InstanceID"),
                                "gl_InstanceIndex");
  }
  if (c.type == ShaderSourceType::Geometry) {
    source = std::regex_replace(source, std::regex(R"(\bEmitVertex\s*\(\s*\))"),
                                "RhiEmitVertex()");
    auto at = source.find("void main");
    if (at == std::string::npos)
      throw std::runtime_error("Geometry shader requires a main entry point");
    source.insert(at, "void RhiEmitVertex(){float "
                      "depth=gl_Position.z;gl_Position.z=(depth+gl_Position.w)*"
                      ".5;EmitVertex();gl_"
                      "Position.z=depth;}\n");
  }
  shaderc::Compiler compiler;
  shaderc::CompileOptions options;
  options.SetTargetEnvironment(shaderc_target_env_vulkan,
                               shaderc_env_version_vulkan_1_2);
  options.SetAutoMapLocations(true);
  options.SetGenerateDebugInfo();
  options.SetOptimizationLevel(shaderc_optimization_level_performance);
  auto kind = c.type == ShaderSourceType::Vertex     ? shaderc_vertex_shader
              : c.type == ShaderSourceType::Fragment ? shaderc_fragment_shader
              : c.type == ShaderSourceType::Geometry ? shaderc_geometry_shader
                                                     : shaderc_compute_shader;
  auto result = compiler.CompileGlslToSpv(source, kind, "rhi.glsl", options);
  if (result.GetCompilationStatus() != shaderc_compilation_status_success)
    throw std::runtime_error("Vulkan GLSL: " + result.GetErrorMessage());
  s.spirv.assign(result.cbegin(), result.cend());
  // SPIR-V descriptor decorations include macro-expanded binding numbers.
  std::unordered_map<uint32_t, uint32_t> sets, bindings, locations;
  std::unordered_set<uint32_t> inputVars;
  for (size_t p = 5; p < s.spirv.size();) {
    uint32_t n = s.spirv[p] >> 16, op = s.spirv[p] & 65535;
    if (!n || p + n > s.spirv.size())
      throw std::runtime_error("Invalid SPIR-V");
    if (op == 71 && n >= 4) {
      if (s.spirv[p + 2] == 33)
        bindings[s.spirv[p + 1]] = s.spirv[p + 3];
      if (s.spirv[p + 2] == 34)
        sets[s.spirv[p + 1]] = s.spirv[p + 3];
      if (s.spirv[p + 2] == 30)
        locations[s.spirv[p + 1]] = s.spirv[p + 3];
    }
    if (op == 59 && n >= 4 && s.spirv[p + 3] == 1)
      inputVars.insert(s.spirv[p + 2]);
    p += n;
  }
  for (auto [id, location] : locations)
    if (inputVars.contains(id) && location < 32)
      s.inputs |= 1u << location;
  for (auto [id, b] : bindings)
    if (sets.contains(id)) {
      if (sets[id] < 2 && b < 16)
        s.bindings[sets[id]] |= 1u << b;
      else if (sets[id] == 2)
        s.acceleration = true;
    }
  VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  info.codeSize = s.spirv.size() * 4;
  info.pCode = s.spirv.data();
  Check(vkCreateShaderModule(device_, &info, nullptr, &s.module),
        "create shader module");
  return s;
}
RenderResourceHandle<ShaderSourceSpec>
VulkanBackend::CreateShaderSource(const CreateShaderSourceCommand &c) {
  try {
    auto s = Compile(c);
    auto id = nextId_++;
    shaders_.emplace(id, std::move(s));
    return rhiContext_.resourcePool->shaderSourceTable.Add(
        ShaderSourceSpec{c.type, id});
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return {};
  }
}
void VulkanBackend::DeleteShaderSource(const DeleteShaderSourceCommand &c) {
  auto *s = rhiContext_.resourcePool->shaderSourceTable.Get(c.handle);
  if (!s)
    return;
  vkDestroyShaderModule(device_, shaders_.at(s->rhi_id).module, nullptr);
  shaders_.erase(s->rhi_id);
  rhiContext_.resourcePool->shaderSourceTable.Remove(c.handle);
}
RenderResourceHandle<ShaderProgramSpec>
VulkanBackend::CreateGraphicShaderProgram(
    const CreateGraphicShaderProgramCommand &c) {
  try {
    Program p{};
    auto clone = [&](RenderResourceHandle<ShaderSourceSpec> h,
                     ShaderSourceType type) {
      auto *spec = rhiContext_.resourcePool->shaderSourceTable.Get(h);
      if (!spec || spec->type != type)
        throw std::runtime_error("Vulkan: invalid shader source");
      Shader s = shaders_.at(spec->rhi_id);
      VkShaderModuleCreateInfo info{
          VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
      info.codeSize = s.spirv.size() * 4;
      info.pCode = s.spirv.data();
      Check(vkCreateShaderModule(device_, &info, nullptr, &s.module),
            "retain shader module");
      return s;
    };
    p.vertex = clone(c.createDesc.vertexShaderSource, ShaderSourceType::Vertex);
    p.fragment =
        clone(c.createDesc.fragmentShaderSource, ShaderSourceType::Fragment);
    if (c.createDesc.geometryShaderSource.IsValid())
      p.geometry =
          clone(c.createDesc.geometryShaderSource, ShaderSourceType::Geometry);
    if (p.geometry.module) {
      auto vertex = Compile(CreateShaderSourceCommand{ShaderSourceType::Vertex,
                                                      p.vertex.source,
                                                      {}},
                            false);
      vkDestroyShaderModule(device_, p.vertex.module, nullptr);
      p.vertex = std::move(vertex);
    }
    p.rayQuery = p.fragment.acceleration;
    std::smatch slot;
    if (std::regex_search(p.fragment.source, slot,
                          std::regex(R"(#define\s+RT_GEOMETRY_SLOT\s+(\d+))")))
      p.geometrySlot = std::stoi(slot[1]);
    for (uint32_t set = 0; set < 2; ++set) {
      std::vector<VkDescriptorSetLayoutBinding> bindings;
      for (uint32_t b = 0; b < 16; ++b) {
        VkShaderStageFlags stages = 0;
        for (auto [s, stage] :
             {std::pair{&p.vertex, VK_SHADER_STAGE_VERTEX_BIT},
              std::pair{&p.fragment, VK_SHADER_STAGE_FRAGMENT_BIT},
              std::pair{&p.geometry, VK_SHADER_STAGE_GEOMETRY_BIT}})
          if (s->bindings[set] & (1u << b))
            stages |= stage;
        if (stages) {
          p.bindings[set] |= 1u << b;
          bindings.push_back({b,
                              set ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                                  : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                              1, stages, nullptr});
        }
      }
      VkDescriptorSetLayoutCreateInfo l{
          VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
      l.bindingCount = uint32_t(bindings.size());
      l.pBindings = bindings.data();
      Check(vkCreateDescriptorSetLayout(device_, &l, nullptr, &p.layouts[set]),
            "create descriptor layout");
    }
    if (p.rayQuery) {
      VkDescriptorSetLayoutBinding binding{
          0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1,
          VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
      VkDescriptorSetLayoutCreateInfo l{
          VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
      l.bindingCount = 1;
      l.pBindings = &binding;
      Check(vkCreateDescriptorSetLayout(device_, &l, nullptr, &p.layouts[2]),
            "create AS layout");
    }
    VkPipelineLayoutCreateInfo l{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    l.setLayoutCount = p.rayQuery ? 3 : 2;
    l.pSetLayouts = p.layouts.data();
    Check(vkCreatePipelineLayout(device_, &l, nullptr, &p.layout),
          "create pipeline layout");
    auto id = nextId_++;
    programs_.emplace(id, std::move(p));
    return rhiContext_.resourcePool->shaderProgramTable.Add(
        ShaderProgramSpec{ShaderProgramType::Graphics, id});
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return {};
  }
}
void VulkanBackend::DeleteShaderProgram(const DeleteShaderProgramCommand &c) {
  auto *s = rhiContext_.resourcePool->shaderProgramTable.Get(c.handle);
  if (!s)
    return;
  auto &p = programs_.at(s->rhi_id);
  vkDestroyPipelineLayout(device_, p.layout, nullptr);
  for (auto l : p.layouts)
    if (l)
      vkDestroyDescriptorSetLayout(device_, l, nullptr);
  for (auto *shader : {&p.vertex, &p.fragment, &p.geometry})
    if (shader->module)
      vkDestroyShaderModule(device_, shader->module, nullptr);
  programs_.erase(s->rhi_id);
  rhiContext_.resourcePool->shaderProgramTable.Remove(c.handle);
}
RenderResourceHandle<PipelineSpec>
VulkanBackend::CreatePipeline(const CreatePipelineCommand &c) {
  if (!rhiContext_.resourcePool->shaderProgramTable.Get(
          c.desc.spec.shaderProgram))
    return {};
  return rhiContext_.resourcePool->PipelineTable.Add(c.desc.spec);
}
void VulkanBackend::DeletePipeline(const DeletePipelineCommand &c) {
  for (auto i = pipelines_.begin(); i != pipelines_.end();)
    if (std::get<0>(i->first) == c.handle.id) {
      vkDestroyPipeline(device_, i->second, nullptr);
      i = pipelines_.erase(i);
    } else
      ++i;
  rhiContext_.resourcePool->PipelineTable.Remove(c.handle);
}
VkPipeline VulkanBackend::NativePipeline(const PipelineSpec &s,
                                         uint32_t meshId) {
  auto key =
      std::tuple{pipeline_.id,
                 current_.color ? current_.color->format : VK_FORMAT_UNDEFINED,
                 current_.depth ? current_.depth->format : VK_FORMAT_UNDEFINED,
                 current_.samples, meshId};
  if (auto i = pipelines_.find(key); i != pipelines_.end())
    return i->second;
  auto *programSpec =
      rhiContext_.resourcePool->shaderProgramTable.Get(s.shaderProgram);
  if (!programSpec)
    throw std::runtime_error("Vulkan: missing graphics program");
  auto &program = programs_.at(programSpec->rhi_id);
  std::vector<VkPipelineShaderStageCreateInfo> stages;
  for (auto [shader, stage] :
       {std::pair{&program.vertex, VK_SHADER_STAGE_VERTEX_BIT},
        std::pair{&program.fragment, VK_SHADER_STAGE_FRAGMENT_BIT},
        std::pair{&program.geometry, VK_SHADER_STAGE_GEOMETRY_BIT}})
    if (shader->module) {
      VkPipelineShaderStageCreateInfo ci{
          VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
      ci.stage = stage;
      ci.module = shader->module;
      ci.pName = "main";
      stages.push_back(ci);
    }
  auto *mesh = rhiContext_.resourcePool->vertexBufferTable.Get(mesh_);
  std::vector<VkVertexInputAttributeDescription> attributes;
  uint32_t stride = 0, location = 0;
  for (auto t : mesh->layout.typeSlots) {
    uint32_t columns = t == VertexFieldType::Mat2   ? 2
                       : t == VertexFieldType::Mat3 ? 3
                       : t == VertexFieldType::Mat4 ? 4
                                                    : 1;
    uint32_t rows =
        t == VertexFieldType::Vec2 || t == VertexFieldType::Mat2   ? 2
        : t == VertexFieldType::Vec3 || t == VertexFieldType::Mat3 ? 3
        : t == VertexFieldType::Vec4 || t == VertexFieldType::Mat4 ? 4
                                                                   : 1;
    VkFormat f = t == VertexFieldType::Byte  ? VK_FORMAT_R8_UINT
                 : t == VertexFieldType::Int ? VK_FORMAT_R32_SINT
                 : rows == 2                 ? VK_FORMAT_R32G32_SFLOAT
                 : rows == 3                 ? VK_FORMAT_R32G32B32_SFLOAT
                 : rows == 4                 ? VK_FORMAT_R32G32B32A32_SFLOAT
                                             : VK_FORMAT_R32_SFLOAT;
    for (uint32_t col = 0; col < columns; ++col) {
      if (program.vertex.inputs & (1u << location))
        attributes.push_back({location, 0, f, stride});
      ++location;
      stride += t == VertexFieldType::Byte ? 1 : rows * 4;
    }
  }
  VkVertexInputBindingDescription binding{0, stride,
                                          VK_VERTEX_INPUT_RATE_VERTEX};
  VkPipelineVertexInputStateCreateInfo vertex{
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vertex.vertexBindingDescriptionCount = 1;
  vertex.pVertexBindingDescriptions = &binding;
  vertex.vertexAttributeDescriptionCount = uint32_t(attributes.size());
  vertex.pVertexAttributeDescriptions = attributes.data();
  VkPipelineInputAssemblyStateCreateInfo assembly{
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  assembly.topology = s.topology == PrimitiveTopology::TriangleStrip
                          ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP
                      : s.topology == PrimitiveTopology::LineList
                          ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST
                      : s.topology == PrimitiveTopology::PointList
                          ? VK_PRIMITIVE_TOPOLOGY_POINT_LIST
                          : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipelineViewportStateCreateInfo vp{
      VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  vp.viewportCount = vp.scissorCount = 1;
  VkPipelineRasterizationStateCreateInfo raster{
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  raster.polygonMode = s.polygonMode == PolygonMode::Line
                           ? VK_POLYGON_MODE_LINE
                           : VK_POLYGON_MODE_FILL;
  raster.cullMode = s.cullMode == CullMode::Back    ? VK_CULL_MODE_BACK_BIT
                    : s.cullMode == CullMode::Front ? VK_CULL_MODE_FRONT_BIT
                                                    : VK_CULL_MODE_NONE;
  // A positive Vulkan viewport gives the same logical bottom-first image
  // coordinates as GL, but reverses the API's front-face convention.
  raster.frontFace = s.frontFaceCounterClockwise
                         ? VK_FRONT_FACE_CLOCKWISE
                         : VK_FRONT_FACE_COUNTER_CLOCKWISE;
  raster.lineWidth = 1;
  VkPipelineMultisampleStateCreateInfo msaa{
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  msaa.rasterizationSamples = VkSampleCountFlagBits(current_.samples);
  constexpr VkCompareOp comparisons[]{VK_COMPARE_OP_NEVER,
                                      VK_COMPARE_OP_LESS,
                                      VK_COMPARE_OP_LESS_OR_EQUAL,
                                      VK_COMPARE_OP_EQUAL,
                                      VK_COMPARE_OP_GREATER_OR_EQUAL,
                                      VK_COMPARE_OP_GREATER,
                                      VK_COMPARE_OP_ALWAYS};
  VkPipelineDepthStencilStateCreateInfo depth{
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  depth.depthTestEnable = s.depthTest;
  depth.depthWriteEnable = s.depthWrite;
  depth.depthCompareOp = comparisons[int(s.depthCompare)];
  VkPipelineColorBlendAttachmentState attachment{};
  attachment.colorWriteMask = 15;
  attachment.blendEnable = s.blendEnable && s.blendMode != BlendMode::Opaque;
  attachment.colorBlendOp = attachment.alphaBlendOp = VK_BLEND_OP_ADD;
  attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  attachment.dstColorBlendFactor = s.blendMode == BlendMode::Additive
                                       ? VK_BLEND_FACTOR_ONE
                                       : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  attachment.dstAlphaBlendFactor = s.blendMode == BlendMode::Additive
                                       ? VK_BLEND_FACTOR_ONE
                                       : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  VkPipelineColorBlendStateCreateInfo blend{
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  blend.attachmentCount = current_.color ? 1 : 0;
  blend.pAttachments = &attachment;
  VkDynamicState dynamics[]{VK_DYNAMIC_STATE_VIEWPORT,
                            VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamic{
      VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dynamic.dynamicStateCount = 2;
  dynamic.pDynamicStates = dynamics;
  VkFormat color =
      current_.color ? current_.color->format : VK_FORMAT_UNDEFINED;
  VkPipelineRenderingCreateInfo rendering{
      VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
  rendering.colorAttachmentCount = current_.color ? 1 : 0;
  rendering.pColorAttachmentFormats = &color;
  rendering.depthAttachmentFormat =
      current_.depth ? current_.depth->format : VK_FORMAT_UNDEFINED;
  VkGraphicsPipelineCreateInfo pipeline{
      VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  pipeline.pNext = &rendering;
  pipeline.stageCount = uint32_t(stages.size());
  pipeline.pStages = stages.data();
  pipeline.pVertexInputState = &vertex;
  pipeline.pInputAssemblyState = &assembly;
  pipeline.pViewportState = &vp;
  pipeline.pRasterizationState = &raster;
  pipeline.pMultisampleState = &msaa;
  pipeline.pDepthStencilState = &depth;
  pipeline.pColorBlendState = &blend;
  pipeline.pDynamicState = &dynamic;
  pipeline.layout = program.layout;
  VkPipeline result;
  Check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline,
                                  nullptr, &result),
        "create graphics pipeline");
  pipelines_.emplace(key, result);
  return result;
}
Acceleration &VulkanBackend::BuildAcceleration(uint32_t imageId,
                                               const glm::ivec4 &geometry) {
  if (auto i = acceleration_.find(imageId); i != acceleration_.end())
    return i->second;
  EndRendering();
  auto &image = images_.at(imageId);
  size_t count = std::max(geometry.w, 0);
  if (geometry.x < 0 ||
      size_t(geometry.x) + count * 5 > image.upload.size() / 16)
    throw std::runtime_error(
        "Vulkan: ray geometry upload missing or out of bounds");
  Acceleration a{};
  a.vertices = MakeBuffer(
      std::max(size_t(16), count * 3 * 12),
      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
  a.indices = MakeBuffer(
      std::max(size_t(16), count * 3 * 4),
      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
  for (size_t t = 0; t < count; ++t)
    for (size_t v = 0; v < 3; ++v) {
      std::memcpy(static_cast<std::byte *>(a.vertices.mapped) +
                      (t * 3 + v) * 12,
                  image.upload.data() + (geometry.x + t * 5 + v) * 16, 12);
      static_cast<uint32_t *>(a.indices.mapped)[t * 3 + v] =
          uint32_t(t * 3 + v);
    }
  auto address = [&](Buffer &b) {
    VkBufferDeviceAddressInfo info{
        VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    info.buffer = b.handle;
    return vkGetBufferDeviceAddress(device_, &info);
  };
  VkAccelerationStructureGeometryKHR tri{
      VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
  tri.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
  tri.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
  auto &triangles = tri.geometry.triangles;
  triangles.sType =
      VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
  triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
  triangles.vertexData.deviceAddress = address(a.vertices);
  triangles.vertexStride = 12;
  triangles.maxVertex = count ? uint32_t(count * 3 - 1) : 0;
  triangles.indexType = VK_INDEX_TYPE_UINT32;
  triangles.indexData.deviceAddress = address(a.indices);
  VkAccelerationStructureBuildGeometryInfoKHR build{
      VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
  build.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
  build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
  build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
  build.geometryCount = 1;
  build.pGeometries = &tri;
  uint32_t primitiveCount = uint32_t(count);
  VkAccelerationStructureBuildSizesInfoKHR sizes{
      VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
  asSizes_(device_, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build,
           &primitiveCount, &sizes);
  a.blasStore =
      MakeBuffer(sizes.accelerationStructureSize,
                 VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                     VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                 false);
  VkAccelerationStructureCreateInfoKHR create{
      VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
  create.buffer = a.blasStore.handle;
  create.size = sizes.accelerationStructureSize;
  create.type = build.type;
  Check(createAS_(device_, &create, nullptr, &a.blas), "create BLAS");
  a.instances = MakeBuffer(
      sizeof(VkAccelerationStructureInstanceKHR),
      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
  VkAccelerationStructureDeviceAddressInfoKHR ai{
      VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
  ai.accelerationStructure = a.blas;
  VkAccelerationStructureInstanceKHR instance{};
  instance.transform.matrix[0][0] = instance.transform.matrix[1][1] =
      instance.transform.matrix[2][2] = 1;
  instance.mask = 255;
  instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
  instance.accelerationStructureReference = asAddress_(device_, &ai);
  std::memcpy(a.instances.mapped, &instance, sizeof(instance));
  VkAccelerationStructureGeometryKHR inst{
      VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
  inst.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
  inst.geometry.instances.sType =
      VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
  inst.geometry.instances.data.deviceAddress = address(a.instances);
  auto top = build;
  top.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
  top.pGeometries = &inst;
  uint32_t one = 1;
  VkAccelerationStructureBuildSizesInfoKHR topSizes{
      VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
  asSizes_(device_, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &top, &one,
           &topSizes);
  a.tlasStore =
      MakeBuffer(topSizes.accelerationStructureSize,
                 VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                     VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                 false);
  create.buffer = a.tlasStore.handle;
  create.size = topSizes.accelerationStructureSize;
  create.type = top.type;
  Check(createAS_(device_, &create, nullptr, &a.tlas), "create TLAS");
  VkPhysicalDeviceAccelerationStructurePropertiesKHR props{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
  VkPhysicalDeviceProperties2 p{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
  p.pNext = &props;
  vkGetPhysicalDeviceProperties2(physical_, &p);
  auto align = props.minAccelerationStructureScratchOffsetAlignment;
  a.scratch = MakeBuffer(
      std::max(sizes.buildScratchSize, topSizes.buildScratchSize) + align,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      false);
  auto scratch = (address(a.scratch) + align - 1) & ~VkDeviceAddress(align - 1);
  build.dstAccelerationStructure = a.blas;
  build.scratchData.deviceAddress = scratch;
  VkAccelerationStructureBuildRangeInfoKHR range{};
  range.primitiveCount = primitiveCount;
  const auto *ranges = &range;
  buildAS_(command_, 1, &build, &ranges);
  VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
  barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR |
                          VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
  vkCmdPipelineBarrier(command_,
                       VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                       VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                       0, 1, &barrier, 0, nullptr, 0, nullptr);
  top.dstAccelerationStructure = a.tlas;
  top.scratchData.deviceAddress = scratch;
  range.primitiveCount = one;
  buildAS_(command_, 1, &top, &ranges);
  barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
  vkCmdPipelineBarrier(command_,
                       VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &barrier, 0,
                       nullptr, 0, nullptr);
  return acceleration_.emplace(imageId, std::move(a)).first->second;
}
void VulkanBackend::PrepareDraw() {
  auto *spec = rhiContext_.resourcePool->PipelineTable.Get(pipeline_);
  auto *mesh = rhiContext_.resourcePool->vertexBufferTable.Get(mesh_);
  if (!spec || !mesh)
    throw std::runtime_error("Vulkan: draw has invalid pipeline/mesh");
  auto *ps =
      rhiContext_.resourcePool->shaderProgramTable.Get(spec->shaderProgram);
  if (!ps)
    throw std::runtime_error("Vulkan: draw has invalid shader program");
  auto &program = programs_.at(ps->rhi_id);
  std::array<VkDescriptorSet, 3> sets{};
  std::array<VkDescriptorBufferInfo, 16> buffers{};
  std::array<VkDescriptorImageInfo, 16> images{};
  std::array<std::array<uint64_t, 34>, 3> keys{};
  for (uint32_t set = 0; set < keys.size(); ++set) {
    keys[set][0] = ps->rhi_id;
    keys[set][1] = set;
  }
  for (uint32_t b = 0; b < 16; ++b)
    if (program.bindings[0] & (1u << b)) {
      auto binding = uniformBindings_[b];
      auto *u =
          rhiContext_.resourcePool->uniformBufferTable.Get(binding.buffer);
      size_t offset = 0,
             size = std::min(65536u, properties_.limits.maxUniformBufferRange);
      if (u) {
        offset = binding.offset;
        if (offset >= u->byteSize)
          throw std::runtime_error("Vulkan: uniform binding out of bounds");
        size = binding.size ? binding.size : u->byteSize - offset;
        if (offset + size > u->byteSize || !size)
          throw std::runtime_error("Vulkan: uniform binding out of bounds");
      }
      const auto snapshotKey = std::tuple{
          u ? u->rhi_id : 0u, u ? uniformVersions_.at(u->rhi_id) : uint64_t(0),
          offset, size};
      auto snapshot = uniformSnapshots_.find(snapshotKey);
      if (snapshot == uniformSnapshots_.end()) {
        auto alignment = std::max<VkDeviceSize>(
            16, properties_.limits.minUniformBufferOffsetAlignment);
        uniformCursor_ = (uniformCursor_ + alignment - 1) & ~(alignment - 1);
        if (uniformCursor_ + size > uniformRing_.bytes)
          throw std::runtime_error(
              "Vulkan: per-frame uniform snapshot budget exceeded");
        if (u)
          std::memcpy(static_cast<std::byte *>(uniformRing_.mapped) +
                          uniformCursor_,
                      uniforms_.at(u->rhi_id).data() + offset, size);
        else
          std::memset(static_cast<std::byte *>(uniformRing_.mapped) +
                          uniformCursor_,
                      0, size);
        snapshot = uniformSnapshots_.emplace(
            snapshotKey,
            VkDescriptorBufferInfo{uniformRing_.handle, uniformCursor_, size})
                       .first;
        uniformCursor_ += size;
      }
      buffers[b] = snapshot->second;
      keys[0][2 + b * 2] = buffers[b].offset;
      keys[0][3 + b * 2] = buffers[b].range;
    }
  for (uint32_t b = 0; b < 16; ++b)
    if (program.bindings[1] & (1u << b)) {
      auto *image = LookupImage(textureBindings_[b]);
      if (!image || image == current_.color || image == current_.depth ||
          image->samples != 1)
        image = &fallback_;
      Transition(*image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      images[b] = {image->sampler, image->view, image->layout};
      if (image != &fallback_)
        keys[1][2 + b] = rhiContext_.resourcePool->TextureTable
                            .Get(textureBindings_[b])->rhi_id;
    }
  VkWriteDescriptorSetAccelerationStructureKHR as{
      VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
  if (program.rayQuery) {
    auto *u = rhiContext_.resourcePool->uniformBufferTable.Get(
        uniformBindings_[15].buffer);
    auto *image = rhiContext_.resourcePool->TextureTable.Get(
        textureBindings_[program.geometrySlot]);
    if (!u || !image || u->byteSize < 96 ||
        uniformBindings_[15].offset > u->byteSize - 96)
      throw std::runtime_error(
          "Vulkan: hardware ray query missing scene bindings");
    glm::ivec4 geometry;
    std::memcpy(
        &geometry,
        uniforms_.at(u->rhi_id).data() + uniformBindings_[15].offset + 80, 16);
    auto &native = BuildAcceleration(image->rhi_id, geometry);
    as.accelerationStructureCount = 1;
    as.pAccelerationStructures = &native.tlas;
    keys[2][2] = image->rhi_id;
  }
  const uint32_t setCount = program.rayQuery ? 3 : 2;
  for (uint32_t set = 0; set < setCount; ++set) {
    if (auto found = descriptorSnapshots_.find(keys[set]);
        found != descriptorSnapshots_.end()) {
      sets[set] = found->second;
      continue;
    }
    VkDescriptorSetAllocateInfo alloc{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool = descriptors_;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &program.layouts[set];
    Check(vkAllocateDescriptorSets(device_, &alloc, &sets[set]),
          "allocate draw descriptor snapshot");
    std::array<VkWriteDescriptorSet, 16> writes{};
    uint32_t writeCount = 0;
    if (set < 2) {
      for (uint32_t b = 0; b < 16; ++b) {
        if (!(program.bindings[set] & (1u << b)))
          continue;
        auto &write = writes[writeCount++];
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = sets[set];
        write.dstBinding = b;
        write.descriptorCount = 1;
        write.descriptorType = set == 0
                                   ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                   : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        if (set == 0)
          write.pBufferInfo = &buffers[b];
        else
          write.pImageInfo = &images[b];
      }
    } else {
      auto &write = writes[writeCount++];
      write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      write.pNext = &as;
      write.dstSet = sets[set];
      write.dstBinding = 0;
      write.descriptorCount = 1;
      write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    }
    if (writeCount)
      vkUpdateDescriptorSets(device_, writeCount, writes.data(), 0, nullptr);
    descriptorSnapshots_.emplace(keys[set], sets[set]);
  }
  auto native = NativePipeline(*spec, mesh->rhi_id);
  RenderTarget();
  vkCmdBindPipeline(command_, VK_PIPELINE_BIND_POINT_GRAPHICS, native);
  vkCmdBindDescriptorSets(command_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          program.layout, 0, setCount,
                          sets.data(), 0, nullptr);
  vkCmdSetViewport(command_, 0, 1, &viewport_);
  VkRect2D scissor = scissorEnabled_
                         ? scissor_
                         : VkRect2D{{0, 0}, {current_.width, current_.height}};
  vkCmdSetScissor(command_, 0, 1, &scissor);
  auto &buffersNative = meshes_.at(mesh->rhi_id);
  VkDeviceSize zero = 0;
  vkCmdBindVertexBuffers(command_, 0, 1, &buffersNative.vertices.handle, &zero);
  if (mesh->isUseElementBuffer)
    vkCmdBindIndexBuffer(command_, buffersNative.indices.handle, 0,
                         mesh->indexType == IndexType::UInt16
                             ? VK_INDEX_TYPE_UINT16
                             : VK_INDEX_TYPE_UINT32);
}
void VulkanBackend::Draw(const RHICommand::Draw &c) {
  auto *mesh = rhiContext_.resourcePool->vertexBufferTable.Get(mesh_);
  if (!mesh || !rhiContext_.resourcePool->PipelineTable.Get(pipeline_) ||
      c.firstVertex >= mesh->vertexCount ||
      c.vertexCount > mesh->vertexCount - c.firstVertex)
    return;
  PrepareDraw();
  vkCmdDraw(command_,
            c.vertexCount ? c.vertexCount : mesh->vertexCount - c.firstVertex,
            c.instanceCount, c.firstVertex, c.firstInstance);
}
void VulkanBackend::DrawIndexed(const RHICommand::DrawIndexed &c) {
  auto *mesh = rhiContext_.resourcePool->vertexBufferTable.Get(mesh_);
  if (!mesh || !rhiContext_.resourcePool->PipelineTable.Get(pipeline_) ||
      !mesh->isUseElementBuffer || c.firstIndex >= mesh->indexCount ||
      c.indexCount > mesh->indexCount - c.firstIndex)
    return;
  PrepareDraw();
  vkCmdDrawIndexed(
      command_, c.indexCount ? c.indexCount : mesh->indexCount - c.firstIndex,
      c.instanceCount, c.firstIndex, c.baseVertex, c.firstInstance);
}
void VulkanBackend::BeginFrame(const RHICommand::BeginFrame &c) {
  ResizeWindow(std::max(c.framebufferWidth, 1u),
               std::max(c.framebufferHeight, 1u));
  Check(vkResetDescriptorPool(device_, descriptors_, 0), "reset descriptors");
  uniformCursor_ = 0;
  uniformSnapshots_.clear();
  descriptorSnapshots_.clear();
  mesh_ = {};
  pipeline_ = {};
  StartCommands();
  vkCmdResetQueryPool(command_, timestamps_, 0, 2);
  vkCmdWriteTimestamp(command_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timestamps_,
                      0);
  SetRenderTarget({{},
                   c.framebufferWidth,
                   c.framebufferHeight,
                   c.clearFlags,
                   c.clearColor,
                   c.clearDepth});
}
void VulkanBackend::EndFrame(const RHICommand::EndFrame &c) {
  if (!recording_)
    return;
  EndRendering();
  vkCmdWriteTimestamp(command_, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                      timestamps_, 1);
  if (c.present)
    Present();
  else
    Submit();
  uint64_t t[2]{};
  Check(vkGetQueryPoolResults(
            device_, timestamps_, 0, 2, sizeof(t), t, sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT),
        "read GPU timestamps");
  gpuMs_ = double(t[1] - t[0]) * properties_.limits.timestampPeriod / 1e6;
}
void VulkanBackend::SetRenderTarget(const RHICommand::SetRenderTarget &c) {
  EndRendering();
  current_ = LookupTarget(c.target);
  mesh_ = {};
  pipeline_ = {};
  for (auto &binding : textureBindings_) {
    auto *image = LookupImage(binding);
    if (image && (image == current_.color || image == current_.depth))
      binding = {};
  }
  viewport_ = {0,
               0,
               float(c.width ? c.width : current_.width),
               float(c.height ? c.height : current_.height),
               0,
               1};
  scissorEnabled_ = false;
  if (c.clearFlags) {
    RenderTarget(c.clearFlags, c.clearColor, c.clearDepth);
    EndRendering();
  }
}
void VulkanBackend::ResolveRenderTarget(
    const RHICommand::ResolveRenderTarget &c) {
  EndRendering();
  auto source = LookupTarget(c.source),
       destination = LookupTarget(c.destination);
  if (!c.source.IsValid() || !c.destination.IsValid() || source.samples <= 1 ||
      destination.samples != 1 || source.width != destination.width ||
      source.height != destination.height)
    throw std::runtime_error("Vulkan: invalid multisample resolve");
  if (c.color && source.color && destination.color) {
    Transition(*source.color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    Transition(*destination.color, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageResolve r{};
    r.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    r.dstSubresource = r.srcSubresource;
    r.extent = {source.width, source.height, 1};
    vkCmdResolveImage(command_, source.color->handle, source.color->layout,
                      destination.color->handle, destination.color->layout, 1,
                      &r);
  }
  if (c.depth && source.depth && destination.depth) {
    Transition(*source.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    Transition(*destination.depth,
               VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo a{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    a.imageView = source.depth->view;
    a.imageLayout = source.depth->layout;
    a.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    a.resolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
    a.resolveImageView = destination.depth->view;
    a.resolveImageLayout = destination.depth->layout;
    VkRenderingInfo r{VK_STRUCTURE_TYPE_RENDERING_INFO};
    r.renderArea = {{0, 0}, {source.width, source.height}};
    r.layerCount = 1;
    r.pDepthAttachment = &a;
    vkCmdBeginRendering(command_, &r);
    vkCmdEndRendering(command_);
  }
}
std::vector<float>
VulkanBackend::ReadTexture(RenderResourceHandle<RHITextureSpec> h) {
  auto *image = LookupImage(h);
  if (!image || image->samples != 1)
    return {};
  size_t pixels = size_t(image->width) * image->height;
  Buffer buffer = MakeBuffer(pixels * PixelBytes(image->format),
                             VK_BUFFER_USAGE_TRANSFER_DST_BIT);
  auto layout = image->layout == VK_IMAGE_LAYOUT_UNDEFINED
                    ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                    : image->layout;
  StartCommands();
  Transition(*image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  VkBufferImageCopy c{};
  c.imageSubresource = {VkImageAspectFlags(image->Depth()
                                               ? VK_IMAGE_ASPECT_DEPTH_BIT
                                               : VK_IMAGE_ASPECT_COLOR_BIT),
                        0, 0, 1};
  c.imageExtent = {image->width, image->height, 1};
  vkCmdCopyImageToBuffer(command_, image->handle, image->layout, buffer.handle,
                         1, &c);
  Transition(*image, layout);
  Submit();
  std::vector<float> result(pixels * (image->Depth() ? 1 : 4));
  for (size_t i = 0; i < result.size(); ++i) {
    if (image->format == VK_FORMAT_R8G8B8A8_UNORM)
      result[i] = static_cast<uint8_t *>(buffer.mapped)[i] / 255.f;
    else if (image->format == VK_FORMAT_R16G16B16A16_SFLOAT)
      result[i] = Float(static_cast<uint16_t *>(buffer.mapped)[i]);
    else
      result[i] = static_cast<float *>(buffer.mapped)[i];
  }
  Destroy(buffer);
  return result;
}
std::vector<uint8_t> VulkanBackend::ReadWindow() {
  if (!windowColor_.width || !windowColor_.height)
    return {};
  size_t bytes = size_t(windowColor_.width) * windowColor_.height * 4;
  Buffer buffer = MakeBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
  StartCommands();
  Transition(windowColor_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  VkBufferImageCopy c{};
  c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  c.imageExtent = {windowColor_.width, windowColor_.height, 1};
  vkCmdCopyImageToBuffer(command_, windowColor_.handle, windowColor_.layout,
                         buffer.handle, 1, &c);
  Submit();
  std::vector<uint8_t> result(bytes);
  std::memcpy(result.data(), buffer.mapped, bytes);
  Destroy(buffer);
  return result;
}
} // namespace
bool RegisterVulkanBackend() {
  VulkanBackendFactory = [](const RHIBackContext &c) -> IBackend * {
    return new VulkanBackend(c);
  };
  return true;
}
} // namespace Render
#endif
