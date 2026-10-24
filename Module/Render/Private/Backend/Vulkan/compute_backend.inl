// Included inside the Vulkan backend's private namespace; native handles never
// appear in the public compute facade.
Buffer VulkanBackend::MakeComputeReadbackBuffer(size_t bytes, bool& coherent) {
  Buffer buffer;
  buffer.bytes = std::max<size_t>(bytes, 16);
  try {
    VkBufferCreateInfo create{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    create.size = buffer.bytes;
    create.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    Check(vkCreateBuffer(device_, &create, nullptr, &buffer.handle), "create compute readback buffer");
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device_, buffer.handle, &requirements);
    VkPhysicalDeviceMemoryProperties memory;
    vkGetPhysicalDeviceMemoryProperties(physical_, &memory);
    uint32_t selected = UINT32_MAX;
    int best = -1;
    for (uint32_t type = 0; type < memory.memoryTypeCount; ++type) {
      const auto flags = memory.memoryTypes[type].propertyFlags;
      if (!(requirements.memoryTypeBits & (1u << type)) || !(flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) continue;
      const int score = ((flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) ? 2 : 0) +
          ((flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) ? 1 : 0);
      if (score > best) { best = score; selected = type; }
    }
    if (selected == UINT32_MAX) throw std::runtime_error("Vulkan compute: host-visible readback memory unavailable");
    coherent = (memory.memoryTypes[selected].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
    VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate.allocationSize = requirements.size; allocate.memoryTypeIndex = selected;
    Check(vkAllocateMemory(device_, &allocate, nullptr, &buffer.memory), "allocate cached compute readback memory");
    Check(vkBindBufferMemory(device_, buffer.handle, buffer.memory, 0), "bind compute readback memory");
    Check(vkMapMemory(device_, buffer.memory, 0, VK_WHOLE_SIZE, 0, &buffer.mapped), "map cached compute readback memory");
    return buffer;
  } catch (...) { Destroy(buffer); throw; }
}
VulkanBackend::ComputeBuffer& VulkanBackend::GetComputeBuffer(Compute::BufferHandle h) {
  if (h.device != computeOwner_ || !computeBuffers_.contains(h.id))
    throw std::invalid_argument("Vulkan compute: stale or foreign buffer handle");
  return computeBuffers_.at(h.id);
}
VulkanBackend::ComputePipeline& VulkanBackend::GetComputePipeline(Compute::PipelineHandle h) {
  if (h.device != computeOwner_ || !computePipelines_.contains(h.id))
    throw std::invalid_argument("Vulkan compute: stale or foreign pipeline handle");
  return computePipelines_.at(h.id);
}
void VulkanBackend::DestroyComputePipeline(ComputePipeline& p) {
  if (p.pipeline) vkDestroyPipeline(device_, p.pipeline, nullptr);
  if (p.layout) vkDestroyPipelineLayout(device_, p.layout, nullptr);
  if (p.descriptors) vkDestroyDescriptorSetLayout(device_, p.descriptors, nullptr);
  for(auto layout:p.frameLayouts) if(layout) vkDestroyDescriptorSetLayout(device_,layout,nullptr);
  if (p.shader) vkDestroyShaderModule(device_, p.shader, nullptr);
  p = {};
}
Compute::BufferHandle VulkanBackend::ComputeCreateBuffer(size_t bytes, const void* initial, const std::string& label) {
  if (!bytes || bytes > properties_.limits.maxStorageBufferRange)
    throw std::invalid_argument("Vulkan compute: storage buffer size exceeds device range or is zero");
  ComputeBuffer b;
  b.bytes = bytes;
  b.label = label;
  b.allocation = MakeBuffer((bytes + 3) & ~size_t(3), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
  // Zero initialization makes optional rest-state fields deterministic.
  std::memset(b.allocation.mapped, 0, b.allocation.bytes);
  if (initial) std::memcpy(b.allocation.mapped, initial, bytes);
  const auto id = nextComputeId_++;
  try { computeBuffers_.emplace(id, std::move(b)); }
  catch (...) { Destroy(b.allocation); throw; }
  return {id, computeOwner_};
}
void VulkanBackend::ComputeUpload(Compute::BufferHandle h, const void* data, size_t bytes, size_t offset) {
  auto& b = GetComputeBuffer(h);
  if (offset > b.bytes || bytes > b.bytes - offset || (bytes && !data))
    throw std::invalid_argument("Vulkan compute: upload range is outside its storage buffer");
  if (recording_) throw std::logic_error("Vulkan compute upload must be outside a graphics frame");
  if (bytes) std::memcpy(static_cast<std::byte*>(b.allocation.mapped) + offset, data, bytes);
}
Compute::PipelineHandle VulkanBackend::ComputeCreatePipeline(const std::string& source,
    uint32_t bindings, uint32_t pushBytes, const std::string& label) {
  if (source.empty() || !bindings || bindings > properties_.limits.maxPerStageDescriptorStorageBuffers ||
      bindings > properties_.limits.maxDescriptorSetStorageBuffers ||
      pushBytes > properties_.limits.maxPushConstantsSize || pushBytes % 4)
    throw std::invalid_argument("Vulkan compute: invalid shader/binding count/push constant size");
  shaderc::Compiler compiler;
  shaderc::CompileOptions options;
  options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
  options.SetOptimizationLevel(shaderc_optimization_level_performance);
  const std::string shaderName = label.empty() ? "compute.glsl" : label;
  auto result = compiler.CompileGlslToSpv(source, shaderc_compute_shader, shaderName.c_str(), options);
  if (result.GetCompilationStatus() != shaderc_compilation_status_success)
    throw std::runtime_error("Vulkan compute shader " + shaderName + ": " + result.GetErrorMessage());
  std::vector<uint32_t> code(result.cbegin(), result.cend());
  for (size_t at = 5; at < code.size();) {
    const uint32_t words = code[at] >> 16, op = code[at] & 0xffff;
    if (!words || words > code.size() - at) throw std::runtime_error("Invalid compute SPIR-V");
    // OpExecutionMode LocalSize: enforce the actual GPU's workgroup limits.
    if (op == 16 && words >= 6 && code[at + 2] == 17) {
      uint64_t count = 1;
      for (size_t axis = 0; axis < 3; ++axis) {
        const auto size = code[at + 3 + axis];
        if (!size || size > properties_.limits.maxComputeWorkGroupSize[axis])
          throw std::invalid_argument("Vulkan compute: shader local size exceeds device limit");
        count *= size;
      }
      if (count > properties_.limits.maxComputeWorkGroupInvocations)
        throw std::invalid_argument("Vulkan compute: shader local invocation count exceeds device limit");
    }
    at += words;
  }
  ComputePipeline p;
  p.bindings = bindings; p.pushBytes = pushBytes; p.label = label;
  try {
    VkShaderModuleCreateInfo module{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    module.codeSize = code.size() * sizeof(uint32_t); module.pCode = code.data();
    Check(vkCreateShaderModule(device_, &module, nullptr, &p.shader), "create compute shader");
    std::vector<VkDescriptorSetLayoutBinding> entries(bindings);
    for (uint32_t i = 0; i < bindings; ++i)
      entries[i] = {i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo descriptors{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    descriptors.bindingCount = bindings; descriptors.pBindings = entries.data();
    Check(vkCreateDescriptorSetLayout(device_, &descriptors, nullptr, &p.descriptors), "create compute descriptor layout");
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, pushBytes};
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount = 1; layout.pSetLayouts = &p.descriptors;
    if (pushBytes) { layout.pushConstantRangeCount = 1; layout.pPushConstantRanges = &push; }
    Check(vkCreatePipelineLayout(device_, &layout, nullptr, &p.layout), "create compute pipeline layout");
    VkComputePipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipeline.layout = p.layout;
    pipeline.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    pipeline.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeline.stage.module = p.shader; pipeline.stage.pName = "main";
    Check(vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipeline, nullptr, &p.pipeline), "create compute pipeline");
    const auto id = nextComputeId_++;
    computePipelines_.emplace(id, std::move(p));
    return {id, computeOwner_};
  } catch (...) { DestroyComputePipeline(p); throw; }
}
void VulkanBackend::ComputeSubmit(const Compute::Batch& batch) {
  if (batch.dispatches.empty()) return;
  if (recording_) throw std::logic_error("Vulkan compute submit must be outside a graphics frame");
  if (batch.dispatches.size() > 4096) throw std::invalid_argument("Vulkan compute: batch exceeds 4096 kernels");
  uint32_t descriptorCount = 0;
  // Validate every kernel before changing command-buffer or descriptor state.
  for (const auto& d : batch.dispatches) {
    auto& p = GetComputePipeline(d.pipeline);
    if (p.frame || !d.groupsX || !d.groupsY || !d.groupsZ ||
        d.groupsX > properties_.limits.maxComputeWorkGroupCount[0] ||
        d.groupsY > properties_.limits.maxComputeWorkGroupCount[1] ||
        d.groupsZ > properties_.limits.maxComputeWorkGroupCount[2] ||
        d.pushConstants.size() != p.pushBytes || d.bindings.size() != p.bindings)
      throw std::invalid_argument("Vulkan compute: invalid dispatch size, descriptor count or push constants");
    std::vector<bool> used(p.bindings, false);
    for (const auto& binding : d.bindings) {
      GetComputeBuffer(binding.buffer);
      if (binding.binding >= p.bindings || used[binding.binding])
        throw std::invalid_argument("Vulkan compute: duplicate or out-of-range storage binding");
      used[binding.binding] = true;
    }
    descriptorCount += p.bindings;
  }
  const auto begin = std::chrono::steady_clock::now();
  VkDescriptorPool pool{};
  try {
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptorCount};
    VkDescriptorPoolCreateInfo create{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    create.maxSets = uint32_t(batch.dispatches.size());
    create.poolSizeCount = 1; create.pPoolSizes = &size;
    Check(vkCreateDescriptorPool(device_, &create, nullptr, &pool), "create compute descriptor pool");
    // Prepare immutable descriptors and barriers before beginning commands.
    struct Prepared { VkDescriptorSet set{}; ComputePipeline* pipeline{}; std::vector<VkBufferMemoryBarrier> barriers; };
    std::vector<Prepared> prepared;
    prepared.reserve(batch.dispatches.size());
    for (const auto& d : batch.dispatches) {
      auto& p = GetComputePipeline(d.pipeline);
      Prepared entry; entry.pipeline = &p;
      VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
      allocate.descriptorPool = pool; allocate.descriptorSetCount = 1; allocate.pSetLayouts = &p.descriptors;
      Check(vkAllocateDescriptorSets(device_, &allocate, &entry.set), "allocate compute descriptors");
      std::vector<VkDescriptorBufferInfo> infos(p.bindings);
      std::vector<VkWriteDescriptorSet> writes(p.bindings);
      std::unordered_set<uint64_t> written;
      for (size_t i = 0; i < d.bindings.size(); ++i) {
        const auto& binding = d.bindings[i]; auto& b = GetComputeBuffer(binding.buffer);
        infos[i] = {b.allocation.handle, 0, b.bytes};
        writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[i].dstSet = entry.set; writes[i].dstBinding = binding.binding;
        writes[i].descriptorCount = 1; writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &infos[i];
        if (binding.access != Compute::Access::ReadOnly && written.insert(binding.buffer.id).second) {
          VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
          barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
          barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
          barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
          barrier.buffer = b.allocation.handle; barrier.offset = 0; barrier.size = VK_WHOLE_SIZE;
          entry.barriers.push_back(barrier);
        }
      }
      vkUpdateDescriptorSets(device_, uint32_t(writes.size()), writes.data(), 0, nullptr);
      prepared.push_back(std::move(entry));
    }
    StartCommands();
    VkMemoryBarrier acquire{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    acquire.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    acquire.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(command_, VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &acquire, 0, nullptr, 0, nullptr);
    for (size_t i = 0; i < batch.dispatches.size(); ++i) {
      const auto& d = batch.dispatches[i]; auto& ready = prepared[i]; auto& p = *ready.pipeline;
      vkCmdBindPipeline(command_, VK_PIPELINE_BIND_POINT_COMPUTE, p.pipeline);
      vkCmdBindDescriptorSets(command_, VK_PIPELINE_BIND_POINT_COMPUTE, p.layout, 0, 1, &ready.set, 0, nullptr);
      if (p.pushBytes) vkCmdPushConstants(command_, p.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, p.pushBytes, d.pushConstants.data());
      vkCmdDispatch(command_, d.groupsX, d.groupsY, d.groupsZ);
      // The execution dependency is also needed for read -> write hazards,
      // including a kernel whose bindings were all declared read-only.
      vkCmdPipelineBarrier(command_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr,
          uint32_t(ready.barriers.size()), ready.barriers.data(), 0, nullptr);
    }
    VkMemoryBarrier release{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    release.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    release.dstAccessMask = VK_ACCESS_HOST_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(command_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 1, &release, 0, nullptr, 0, nullptr);
    Submit();
    vkDestroyDescriptorPool(device_, pool, nullptr);
    ++computeStats_.submittedBatches;
    computeStats_.dispatchedKernels += batch.dispatches.size();
    computeStats_.lastSubmitMilliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - begin).count();
  } catch (...) {
    if (recording_) {
      vkDeviceWaitIdle(device_);
      vkResetCommandPool(device_, commandPool_, 0);
      recording_ = false;
    }
    if (pool) vkDestroyDescriptorPool(device_, pool, nullptr);
    throw;
  }
}
std::vector<std::byte> VulkanBackend::ComputeReadback(Compute::BufferHandle h, size_t bytes, size_t offset) {
  auto& b = GetComputeBuffer(h);
  if (recording_) throw std::logic_error("Vulkan compute readback must be outside a graphics frame");
  if (offset > b.bytes) throw std::invalid_argument("Vulkan compute: readback offset out of range");
  if (!bytes) bytes = b.bytes - offset;
  if (bytes > b.bytes - offset) throw std::invalid_argument("Vulkan compute: readback size out of range");
  std::vector<std::byte> result(bytes);
  if (!bytes) return result;
  // CPU reads from a coherent device-local BAR allocation can be uncached and
  // tens of milliseconds for a hair vertex buffer. Transfer into reusable
  // HOST_CACHED staging memory before touching the pixels/vertices on the CPU.
  if (!b.readback.handle) b.readback = MakeComputeReadbackBuffer(b.allocation.bytes, b.readbackCoherent);
  StartCommands();
  VkBufferMemoryBarrier acquire{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
  acquire.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT;
  acquire.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  acquire.srcQueueFamilyIndex = acquire.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  acquire.buffer = b.allocation.handle; acquire.size = VK_WHOLE_SIZE;
  vkCmdPipelineBarrier(command_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &acquire, 0, nullptr);
  const size_t transferOffset = offset & ~size_t(3);
  const size_t transferBytes = ((offset + bytes + 3) & ~size_t(3)) - transferOffset;
  VkBufferCopy copy{transferOffset, 0, transferBytes};
  vkCmdCopyBuffer(command_, b.allocation.handle, b.readback.handle, 1, &copy);
  VkBufferMemoryBarrier release{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
  release.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; release.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  release.srcQueueFamilyIndex = release.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  release.buffer = b.readback.handle; release.size = VK_WHOLE_SIZE;
  vkCmdPipelineBarrier(command_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
      0, 0, nullptr, 1, &release, 0, nullptr);
  Submit();
  if (!b.readbackCoherent) {
    VkMappedMemoryRange invalidate{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
    invalidate.memory = b.readback.memory; invalidate.offset = 0; invalidate.size = VK_WHOLE_SIZE;
    Check(vkInvalidateMappedMemoryRanges(device_, 1, &invalidate), "invalidate compute readback cache");
  }
  std::memcpy(result.data(), static_cast<std::byte*>(b.readback.mapped) + (offset - transferOffset), bytes);
  return result;
}
void VulkanBackend::ComputeCopyToVertexBuffer(Compute::BufferHandle h,
    RenderResourceHandle<VertexBufferSpec> handle, size_t bytes, size_t srcOffset, size_t dstOffset) {
  auto& source = GetComputeBuffer(h);
  auto* mesh = rhiContext_.resourcePool->vertexBufferTable.Get(handle);
  if (!mesh || !meshes_.contains(mesh->rhi_id)) throw std::invalid_argument("Vulkan compute: invalid target mesh");
  auto& target = meshes_.at(mesh->rhi_id).vertices;
  if (!bytes || srcOffset > source.bytes || bytes > source.bytes - srcOffset ||
      dstOffset > mesh->vertexByteSize || bytes > mesh->vertexByteSize - dstOffset ||
      srcOffset % 4 || dstOffset % 4 || bytes % 4)
    throw std::invalid_argument("Vulkan compute: mesh copy requires in-range, four-byte-aligned regions");
  if (recording_) throw std::logic_error("Vulkan compute mesh copy must be outside a graphics frame");
  StartCommands();
  VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
  barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.buffer = source.allocation.handle; barrier.size = VK_WHOLE_SIZE;
  vkCmdPipelineBarrier(command_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &barrier, 0, nullptr);
  VkBufferCopy copy{srcOffset, dstOffset, bytes};
  vkCmdCopyBuffer(command_, source.allocation.handle, target.handle, 1, &copy);
  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
  barrier.buffer = target.handle;
  vkCmdPipelineBarrier(command_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
      0, 0, nullptr, 1, &barrier, 0, nullptr);
  Submit();
}
void VulkanBackend::ComputeDestroy(Compute::BufferHandle h) {
  if (!h) return;
  auto& b = GetComputeBuffer(h);
  if (recording_) throw std::logic_error("Vulkan compute destruction must be outside a graphics frame");
  Destroy(b.readback); Destroy(b.allocation); computeBuffers_.erase(h.id);
}
void VulkanBackend::ComputeDestroy(Compute::PipelineHandle h) {
  if (!h) return;
  auto& p = GetComputePipeline(h);
  if (recording_) throw std::logic_error("Vulkan compute destruction must be outside a graphics frame");
  DestroyComputePipeline(p); computePipelines_.erase(h.id);
}
Compute::Statistics VulkanBackend::ComputeDiagnostics() const {
  auto stats = computeStats_;
  stats.available = device_ != VK_NULL_HANDLE;
  stats.frameAvailable = stats.available && !context_.headlessCompute;
  stats.deviceName = properties_.deviceName;
  stats.liveBuffers = computeBuffers_.size(); stats.livePipelines = computePipelines_.size();
  stats.validationErrors = validationErrors_;
  stats.maxStorageBufferBytes = properties_.limits.maxStorageBufferRange;
  stats.maxStorageBindings = std::min(properties_.limits.maxPerStageDescriptorStorageBuffers,
      properties_.limits.maxDescriptorSetStorageBuffers);
  for (const auto& [id, b] : computeBuffers_) stats.allocatedBytes += b.allocation.bytes + b.readback.bytes;
  return stats;
}
