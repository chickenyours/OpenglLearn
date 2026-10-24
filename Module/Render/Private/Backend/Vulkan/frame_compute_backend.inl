// Descriptor objects persist only under the same retired-submit contract as
// graphics. Contents continue to use per-command immutable uniform snapshots.
VkDescriptorPool VulkanBackend::MakeFrameComputeDescriptorPool() {
  const std::array<VkDescriptorPoolSize,5> sizes{{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,16384},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,16384},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,8192},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,8192},{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,8192}}};
  VkDescriptorPoolCreateInfo create{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  create.maxSets=8192;create.poolSizeCount=rayQuery_?5:4;create.pPoolSizes=sizes.data();
  VkDescriptorPool pool{};Check(vkCreateDescriptorPool(device_,&create,nullptr,&pool),"create frame compute descriptor pool");return pool;
}
void VulkanBackend::ResetFrameComputeDescriptorCache() {
  if(!submissionRetired_)throw std::runtime_error("Vulkan: compute descriptor reset before submission retirement");
  frameComputeDescriptorSnapshots_.clear();
  for(auto pool:frameComputeOverflowDescriptors_)vkDestroyDescriptorPool(device_,pool,nullptr);
  frameComputeOverflowDescriptors_.clear();
  Check(vkResetDescriptorPool(device_,frameComputeDescriptors_,0),"reset frame compute descriptors");
  activeFrameComputeDescriptors_=frameComputeDescriptors_;frameComputeDescriptorResetRequested_=false;
}
// Frame-native compute uses the graphics command buffer and its completion
// fence. No dispatch below submits, reads back, or waits for the GPU.
Compute::PipelineHandle VulkanBackend::ComputeCreateFramePipeline(const Compute::FramePipelineDesc& d) {
  if(recording_ || context_.headlessCompute || d.source.empty() ||
      (d.uniformMask|d.sampledMask)>65535 || d.storageBuffers>8 || d.storageImages>8 ||
      d.pushConstantBytes>128 || d.pushConstantBytes>properties_.limits.maxPushConstantsSize || d.pushConstantBytes%4 ||
      d.storageBuffers>properties_.limits.maxPerStageDescriptorStorageBuffers ||
      d.storageImages>properties_.limits.maxPerStageDescriptorStorageImages ||
      std::popcount(d.uniformMask)>properties_.limits.maxPerStageDescriptorUniformBuffers ||
      std::popcount(d.sampledMask)>properties_.limits.maxPerStageDescriptorSamplers)
    throw std::invalid_argument("Vulkan frame compute: invalid pipeline description");
  if(d.nativeScene) {
    const auto& scene=*d.nativeScene;
    if(!rayQuery_||scene.geometryTextureSlot>=16||scene.geometryUniformBinding>=16||
        !(d.sampledMask&(1u<<scene.geometryTextureSlot))||!(d.uniformMask&(1u<<scene.geometryUniformBinding))||
        scene.geometryUniformOffset%4||uint64_t(scene.geometryUniformOffset)+sizeof(glm::ivec4)>properties_.limits.maxUniformBufferRange)
      throw std::invalid_argument("Vulkan frame compute: native scene requires ray queries and bound geometry texture/uniform range");
  }
  ComputePipeline p; p.frame=true; p.frameDescription=d; p.pushBytes=d.pushConstantBytes; p.label=d.label;
  std::string source=std::regex_replace(d.source,
      std::regex(R"(layout\s*\(([^)]*)\)\s*uniform\s+([iu]?sampler\w+))"),
      "layout($1,set=1) uniform $2");
  source=std::regex_replace(source,std::regex(R"(layout\s*\(([^)]*std140[^)]*)\)\s*uniform)"),
      "layout($1,set=0) uniform");
  if(d.nativeScene) {
    source=std::regex_replace(source,std::regex(R"(#version\s+450)"),"#version 460");
    const auto line=source.find('\n');
    if(line==std::string::npos)throw std::invalid_argument("Vulkan frame compute: native shader requires a version line");
    source.insert(line+1,"#extension GL_EXT_ray_query : require\n#define RENDER_NATIVE_RAY_QUERY 1\n");
    if(shaderFloat64_&&source.find("RENDER_NATIVE_FLOAT64")!=std::string::npos)
      source.insert(line+1,"#extension GL_ARB_gpu_shader_fp64 : require\n#define RENDER_NATIVE_FLOAT64 1\n");
    const std::regex accelerationDeclaration(R"(layout\s*\([^)]*\)\s*uniform\s+accelerationStructureEXT)");
    if(std::distance(std::sregex_iterator(source.begin(),source.end(),accelerationDeclaration),std::sregex_iterator())!=1)
      throw std::invalid_argument("Vulkan frame compute: native shader requires one acceleration-structure declaration");
    source=std::regex_replace(source,accelerationDeclaration,"layout(set=2,binding=8) uniform accelerationStructureEXT");
  }
  shaderc::Compiler compiler; shaderc::CompileOptions options;
  options.SetTargetEnvironment(shaderc_target_env_vulkan,shaderc_env_version_vulkan_1_2);
  options.SetOptimizationLevel(shaderc_optimization_level_performance);
  const auto compiled=compiler.CompileGlslToSpv(source,shaderc_compute_shader,d.label.c_str(),options);
  if(compiled.GetCompilationStatus()!=shaderc_compilation_status_success)
    throw std::runtime_error("Vulkan frame compute "+d.label+": "+compiled.GetErrorMessage());
  std::vector<uint32_t> code(compiled.cbegin(),compiled.cend());
  for(size_t at=5;at<code.size();) {
    const uint32_t n=code[at]>>16,op=code[at]&65535;
    if(!n||n>code.size()-at) throw std::runtime_error("Invalid frame compute SPIR-V");
    if(op==16&&n>=6&&code[at+2]==17) {
      uint64_t total=1;
      for(unsigned axis=0;axis<3;++axis) {
        const auto value=code[at+3+axis];
        if(!value||value>properties_.limits.maxComputeWorkGroupSize[axis])
          throw std::invalid_argument("Frame compute local size exceeds device limit");
        total*=value;
      }
      if(total>properties_.limits.maxComputeWorkGroupInvocations)
        throw std::invalid_argument("Frame compute group exceeds device limit");
    }
    at+=n;
  }
  try {
    VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    sm.codeSize=code.size()*4; sm.pCode=code.data();
    Check(vkCreateShaderModule(device_,&sm,nullptr,&p.shader),"create frame compute shader");
    for(unsigned set=0;set<4;++set) {
      std::vector<VkDescriptorSetLayoutBinding> bindings;
      const auto mask=set==0?d.uniformMask:set==1?d.sampledMask:0u;
      const auto count=set==2?d.storageBuffers:set==3?d.storageImages:16u;
      const auto type=set==0?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:set==1?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
          set==2?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
      for(unsigned slot=0;slot<count;++slot) if(set>=2||(mask&(1u<<slot)))
        bindings.push_back({slot,type,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr});
      if(set==2&&d.nativeScene)
        bindings.push_back({8,VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr});
      VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
      layout.bindingCount=uint32_t(bindings.size()); layout.pBindings=bindings.data();
      Check(vkCreateDescriptorSetLayout(device_,&layout,nullptr,&p.frameLayouts[set]),"create frame compute descriptor layout");
    }
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,d.pushConstantBytes};
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount=4; layout.pSetLayouts=p.frameLayouts.data();
    if(d.pushConstantBytes) {layout.pushConstantRangeCount=1;layout.pPushConstantRanges=&push;}
    Check(vkCreatePipelineLayout(device_,&layout,nullptr,&p.layout),"create frame compute layout");
    VkComputePipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipeline.layout=p.layout; pipeline.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    pipeline.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pipeline.stage.module=p.shader;pipeline.stage.pName="main";
    Check(vkCreateComputePipelines(device_,VK_NULL_HANDLE,1,&pipeline,nullptr,&p.pipeline),"create frame compute pipeline");
    if(!frameComputeDescriptors_) {
      frameComputeDescriptors_=MakeFrameComputeDescriptorPool();activeFrameComputeDescriptors_=frameComputeDescriptors_;
    }
    const auto id=nextComputeId_++;computePipelines_.emplace(id,std::move(p));return {id,computeOwner_};
  } catch(...) {DestroyComputePipeline(p);throw;}
}

void VulkanBackend::DispatchFrameCompute(const RHICommand::DispatchFrameCompute& c) {
  if(!recording_) throw std::logic_error("Frame compute requires an active graphics frame");
  auto& p=GetComputePipeline(c.pipeline);const auto& d=p.frameDescription;
  if(!p.frame||c.pushBytes!=p.pushBytes||c.pushBytes>c.pushConstants.size())
    throw std::invalid_argument("Frame compute pipeline or push constants mismatch");
  if(!c.indirect) for(unsigned axis=0;axis<3;++axis) {
    const uint32_t values[]{c.groupsX,c.groupsY,c.groupsZ};
    if(!values[axis]||values[axis]>properties_.limits.maxComputeWorkGroupCount[axis])
      throw std::invalid_argument("Frame compute dispatch exceeds device limit");
  }
  EndRendering();
  std::array<VkDescriptorBufferInfo,24> bufferInfos{};
  std::array<VkDescriptorImageInfo,24> imageInfos{};
  std::array<VkWriteDescriptorSet,49> writes{};
  std::array<unsigned,49> writeSets{};unsigned writeCount=0;
  std::array<VkDescriptorSet,4> sets{};
  std::array<FrameDescriptorKey,4> keys{};
  // Handles may be pointers or integers depending on Vulkan platform headers.
  const auto bits=[](auto handle){uint64_t value=0;static_assert(sizeof(handle)<=sizeof(value));std::memcpy(&value,&handle,sizeof(handle));return value;};
  for(unsigned set=0;set<4;++set){keys[set][0]=computeOwner_;keys[set][1]=c.pipeline.id;keys[set][2]=set;}
  const auto add=[&](unsigned set,unsigned slot,VkDescriptorType type,const VkDescriptorBufferInfo* buffer,const VkDescriptorImageInfo* image) {
    auto& write=writes[writeCount];write={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstBinding=slot;
    write.descriptorType=type;write.descriptorCount=1;write.pBufferInfo=buffer;write.pImageInfo=image;writeSets[writeCount++]=set;
  };
  for(unsigned slot=0;slot<16;++slot) if(d.uniformMask&(1u<<slot)) {
    const auto binding=uniformBindings_[slot];auto* u=rhiContext_.resourcePool->uniformBufferTable.Get(binding.buffer);
    if(!u||binding.offset>=u->byteSize)throw std::invalid_argument("Frame compute missing uniform binding");
    const size_t bytes=binding.size?binding.size:u->byteSize-binding.offset;
    if(!bytes||bytes>u->byteSize-binding.offset||bytes>properties_.limits.maxUniformBufferRange)
      throw std::invalid_argument("Frame compute uniform range invalid");
    if(reuseUniformSnapshotContents_) {
      bufferInfos[slot]=ContentUniformSnapshot(u->rhi_id,uniformVersions_.at(u->rhi_id),binding.offset,bytes);
    } else {
    const auto key=std::tuple{u->rhi_id,uniformVersions_.at(u->rhi_id),size_t(binding.offset),bytes};
    auto snapshot=uniformSnapshots_.find(key);
    if(snapshot==uniformSnapshots_.end()) {
      const auto alignment=std::max<VkDeviceSize>(16,properties_.limits.minUniformBufferOffsetAlignment);
      uniformCursor_=(uniformCursor_+alignment-1)&~(alignment-1);
      if(uniformCursor_>uniformRing_.bytes||bytes>uniformRing_.bytes-uniformCursor_)
        throw std::runtime_error("Frame compute uniform snapshot budget exceeded");
      std::memcpy(static_cast<std::byte*>(uniformRing_.mapped)+uniformCursor_,uniforms_.at(u->rhi_id).data()+binding.offset,bytes);
      snapshot=uniformSnapshots_.emplace(key,VkDescriptorBufferInfo{uniformRing_.handle,uniformCursor_,bytes}).first;uniformCursor_+=bytes;
      ++hostFrame_.uniformSnapshotCopies;uniformSnapshotFrame_.copiedBytes+=bytes;
    }
    bufferInfos[slot]=snapshot->second;
    }
    const auto& snapshotBuffer=bufferInfos[slot];keys[0][4+3*slot]=bits(snapshotBuffer.buffer);
    keys[0][5+3*slot]=snapshotBuffer.offset;keys[0][6+3*slot]=snapshotBuffer.range;
    add(0,slot,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,&bufferInfos[slot],nullptr);
  }
  for(unsigned slot=0;slot<16;++slot) if(d.sampledMask&(1u<<slot)) {
    auto* image=LookupImage(textureBindings_[slot]);if(!image||image->samples!=1)image=&fallback_;
    for(unsigned out=0;out<d.storageImages;++out) if(image==LookupImage(c.images[out]))
      throw std::invalid_argument("Frame compute sampled and storage image alias");
    Transition(*image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    imageInfos[slot]={image->sampler,image->view,image->layout};
    keys[1][4+3*slot]=bits(image->view);keys[1][5+3*slot]=bits(image->sampler);keys[1][6+3*slot]=uint64_t(image->layout);
    add(1,slot,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,nullptr,&imageInfos[slot]);
  }
  for(unsigned slot=0;slot<d.storageBuffers;++slot) {
    auto& b=GetComputeBuffer(c.buffers[slot]);bufferInfos[16+slot]={b.allocation.handle,0,b.bytes};
    keys[2][4+3*slot]=c.buffers[slot].id;keys[2][5+3*slot]=bits(b.allocation.handle);keys[2][6+3*slot]=b.bytes;
    add(2,slot,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,&bufferInfos[16+slot],nullptr);
  }
  for(unsigned slot=0;slot<d.storageImages;++slot) {
    auto* image=LookupImage(c.images[slot]);
    if(!image||!image->storage||image->samples!=1)throw std::invalid_argument("Frame compute invalid storage image");
    for(unsigned other=0;other<slot;++other)if(c.images[other]==c.images[slot])
      throw std::invalid_argument("Frame compute duplicate storage image");
    Transition(*image,VK_IMAGE_LAYOUT_GENERAL);imageInfos[16+slot]={VK_NULL_HANDLE,image->view,image->layout};
    keys[3][4+3*slot]=bits(image->view);keys[3][5+3*slot]=bits(image->handle);keys[3][6+3*slot]=uint64_t(image->layout);
    add(3,slot,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,nullptr,&imageInfos[16+slot]);
  }
  VkWriteDescriptorSetAccelerationStructureKHR acceleration{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
  if(d.nativeScene) {
    const auto& scene=*d.nativeScene;
    const auto& binding=uniformBindings_[scene.geometryUniformBinding];
    auto* uniform=rhiContext_.resourcePool->uniformBufferTable.Get(binding.buffer);
    auto* texture=rhiContext_.resourcePool->TextureTable.Get(textureBindings_[scene.geometryTextureSlot]);
    const auto required=uint64_t(scene.geometryUniformOffset)+sizeof(glm::ivec4);
    if(!uniform||!texture||binding.offset>uniform->byteSize||required>uniform->byteSize-binding.offset||
        (binding.size&&required>binding.size))
      throw std::runtime_error("Vulkan frame compute: native ray query missing scene bindings");
    glm::ivec4 geometry;
    std::memcpy(&geometry,uniforms_.at(uniform->rhi_id).data()+binding.offset+scene.geometryUniformOffset,sizeof(geometry));
    auto& native=BuildAcceleration(texture->rhi_id,geometry,scene.surfaceCache);
    acceleration.accelerationStructureCount=1;acceleration.pAccelerationStructures=&native.tlas;
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.pNext=&acceleration;
    write.dstBinding=8;write.descriptorType=VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    write.descriptorCount=1;writes[writeCount]=write;writeSets[writeCount++]=2;
    // TLAS handles may be recycled; BuildAcceleration owns a monotonic lifetime.
    keys[2][28]=native.descriptorIdentity;
  }
  for(unsigned set=0;set<4;++set) {
    if(reuseUniformSnapshotContents_) {
      if(const auto found=frameComputeDescriptorSnapshots_.find(keys[set]);found!=frameComputeDescriptorSnapshots_.end()) {
        sets[set]=found->second;++uniformSnapshotFrame_.descriptorCacheHits;continue;
      }
    }
    VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocation.descriptorPool=activeFrameComputeDescriptors_;allocation.descriptorSetCount=1;allocation.pSetLayouts=&p.frameLayouts[set];
    auto result=vkAllocateDescriptorSets(device_,&allocation,&sets[set]);
    if(result==VK_ERROR_OUT_OF_POOL_MEMORY||result==VK_ERROR_FRAGMENTED_POOL) {
      // Never invalidate descriptors already referenced by this command buffer.
      if(frameComputeOverflowDescriptors_.size()>=3)throw std::runtime_error("Vulkan: frame compute descriptor overflow budget exceeded");
      const auto overflow=MakeFrameComputeDescriptorPool();
      try{frameComputeOverflowDescriptors_.push_back(overflow);}catch(...){vkDestroyDescriptorPool(device_,overflow,nullptr);throw;}
      activeFrameComputeDescriptors_=overflow;allocation.descriptorPool=overflow;
      result=vkAllocateDescriptorSets(device_,&allocation,&sets[set]);
    }
    Check(result,"allocate frame compute descriptor snapshot");++hostFrame_.descriptorAllocations;
    std::array<VkWriteDescriptorSet,17> setWrites{};unsigned count=0;
    for(unsigned i=0;i<writeCount;++i)if(writeSets[i]==set){auto write=writes[i];write.dstSet=sets[set];setWrites[count++]=write;}
    if(count)vkUpdateDescriptorSets(device_,count,setWrites.data(),0,nullptr);
    uniformSnapshotFrame_.descriptorWrites+=count;
    if(reuseUniformSnapshotContents_)frameComputeDescriptorSnapshots_.emplace(keys[set],sets[set]);
  }
  VkMemoryBarrier acquire{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  acquire.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT|VK_ACCESS_SHADER_WRITE_BIT;
  acquire.dstAccessMask=VK_ACCESS_UNIFORM_READ_BIT|VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
  vkCmdPipelineBarrier(command_,VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,0,1,&acquire,0,nullptr,0,nullptr);
  vkCmdBindPipeline(command_,VK_PIPELINE_BIND_POINT_COMPUTE,p.pipeline);
  vkCmdBindDescriptorSets(command_,VK_PIPELINE_BIND_POINT_COMPUTE,p.layout,0,4,sets.data(),0,nullptr);
  if(p.pushBytes)vkCmdPushConstants(command_,p.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,p.pushBytes,c.pushConstants.data());
  if(c.indirect) {
    auto& b=GetComputeBuffer(c.indirect);
    if(c.indirectOffset%4||c.indirectOffset>b.bytes||12>b.bytes-c.indirectOffset)
      throw std::invalid_argument("Frame compute indirect range invalid");
    vkCmdDispatchIndirect(command_,b.allocation.handle,c.indirectOffset);
  } else vkCmdDispatch(command_,c.groupsX,c.groupsY,c.groupsZ);
  VkMemoryBarrier release{VK_STRUCTURE_TYPE_MEMORY_BARRIER};release.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
  release.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
  vkCmdPipelineBarrier(command_,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
      0,1,&release,0,nullptr,0,nullptr);
  frameComputeRecorded_=true;
  ++computeStats_.dispatchedKernels;
}

std::vector<uint32_t> VulkanBackend::CompletedComputeWords(Compute::BufferHandle h,uint32_t count) const {
  if(!h)return {};
  if(recording_||h.device!=computeOwner_||!computeBuffers_.contains(h.id))
    throw std::logic_error("Compute diagnostics require a live buffer and a completed frame");
  const auto& b=computeBuffers_.at(h.id);
  if(size_t(count)>b.bytes/4)throw std::out_of_range("Compute diagnostics outside buffer");
  std::vector<uint32_t> words(count);std::memcpy(words.data(),b.allocation.mapped,size_t(count)*4);return words;
}
