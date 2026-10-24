// Included inside namespace Render. All native API calls stay on the RHI thread.
namespace OpenGLComputeDetail {
inline constexpr GLbitfield Visibility = GL_SHADER_STORAGE_BARRIER_BIT |
    GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT |
    GL_COMMAND_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT;

struct BufferRange {
    GLint name=0; GLint64 start=0,size=0;
    void Save(GLenum binding,GLenum startName,GLenum sizeName,GLuint index) {
        glGetIntegeri_v(binding,index,&name);
        glGetInteger64i_v(startName,index,&start);glGetInteger64i_v(sizeName,index,&size);
    }
    void Restore(GLenum target,GLuint index) const {
        if(name&&size>0)glBindBufferRange(target,index,GLuint(name),GLintptr(start),GLsizeiptr(size));
        else glBindBufferBase(target,index,GLuint(name));
    }
};
// Compute binds a separate program and buffer/image namespaces. Restore the
// actual GL state as well as leaving the graphics-side RHI caches untouched.
struct State {
    struct Image {GLint name=0,level=0,layered=0,layer=0,access=GL_READ_ONLY,format=GL_RGBA8;};
    GLint program=0,storage=0,uniform=0,indirect=0;
    std::vector<BufferRange> buffers;std::vector<Image> images;
    BufferRange push;bool hasPush=false;
    State(uint32_t bufferCount,uint32_t imageCount,bool pushConstants):buffers(bufferCount),images(imageCount),hasPush(pushConstants) {
        glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING,&storage);
        glGetIntegerv(GL_UNIFORM_BUFFER_BINDING,&uniform);glGetIntegerv(GL_DISPATCH_INDIRECT_BUFFER_BINDING,&indirect);
        for(GLuint i=0;i<bufferCount;++i)buffers[i].Save(GL_SHADER_STORAGE_BUFFER_BINDING,GL_SHADER_STORAGE_BUFFER_START,GL_SHADER_STORAGE_BUFFER_SIZE,i);
        if(hasPush)push.Save(GL_UNIFORM_BUFFER_BINDING,GL_UNIFORM_BUFFER_START,GL_UNIFORM_BUFFER_SIZE,15);
        for(GLuint i=0;i<imageCount;++i) {
            auto& value=images[i];glGetIntegeri_v(GL_IMAGE_BINDING_NAME,i,&value.name);
            glGetIntegeri_v(GL_IMAGE_BINDING_LEVEL,i,&value.level);glGetIntegeri_v(GL_IMAGE_BINDING_LAYERED,i,&value.layered);
            glGetIntegeri_v(GL_IMAGE_BINDING_LAYER,i,&value.layer);glGetIntegeri_v(GL_IMAGE_BINDING_ACCESS,i,&value.access);
            glGetIntegeri_v(GL_IMAGE_BINDING_FORMAT,i,&value.format);
        }
    }
    ~State() {
        for(GLuint i=0;i<buffers.size();++i)buffers[i].Restore(GL_SHADER_STORAGE_BUFFER,i);
        for(GLuint i=0;i<images.size();++i) {
            const auto& value=images[i];glBindImageTexture(i,GLuint(value.name),value.level,GLboolean(value.layered),
                value.layer,GLenum(value.access),GLenum(value.format));
        }
        if(hasPush)push.Restore(GL_UNIFORM_BUFFER,15);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,GLuint(storage));glBindBuffer(GL_UNIFORM_BUFFER,GLuint(uniform));
        glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER,GLuint(indirect));
        // A graphics resource may have been released while its program was
        // still current. Switching to compute completes that deferred delete;
        // restoring its saved numeric name would then raise INVALID_VALUE.
        // Live graphics programs retain their exact prior binding.
        glUseProgram(program&&glIsProgram(GLuint(program))?GLuint(program):0u);
    }
};
struct GenericBufferState {
    GLenum target;GLint previous=0;
    GenericBufferState(GLenum targetValue,GLenum binding):target(targetValue){glGetIntegerv(binding,&previous);}
    ~GenericBufferState(){glBindBuffer(target,GLuint(previous));}
};

inline std::string Source(std::string source,uint32_t pushBytes) {
    // Vulkan descriptor sets map to GL's independent UBO/sampler/SSBO/image
    // namespaces. Binding numbers remain unchanged in every namespace.
    source=std::regex_replace(source,std::regex(R"(\bset\s*=\s*[0-9]+\s*,\s*)"),"");
    source=std::regex_replace(source,std::regex(R"(,\s*set\s*=\s*[0-9]+)"),"");
    const std::regex pushBlock(R"(layout\s*\(\s*push_constant\s*\)\s*uniform\s+\w+\s*\{([^}]*)\}\s*\w*\s*;)");
    std::smatch match;
    const bool found=std::regex_search(source,match,pushBlock);
    if(found!=(pushBytes!=0))throw std::invalid_argument("OpenGL compute: push constant declaration/byte count mismatch");
    if(found) {
        if(std::distance(std::sregex_iterator(source.begin(),source.end(),pushBlock),std::sregex_iterator())!=1)
            throw std::invalid_argument("OpenGL compute: exactly one push constant block is supported");
        const auto body=std::regex_replace(match[1].str(),std::regex(R"(/\*[\s\S]*?\*/|//[^\r\n]*)"),"");
        // Scalars and vectors have identical offsets in Vulkan push-constant
        // std430 and GL std140. Arrays, structs and matrices can differ and
        // must use an explicit common std140 UBO rather than silent repacking.
        const std::regex member(R"(\s*(float|int|uint|bool|[iub]?vec[234])\s+\w+\s*;)");
        size_t consumed=0;uint32_t end=0;
        for(auto it=std::sregex_iterator(body.begin(),body.end(),member);it!=std::sregex_iterator();++it) {
            if(size_t(it->position())!=consumed)throw std::invalid_argument("OpenGL compute: push constants require scalar/vector members, no arrays/matrices/explicit offsets");
            const std::string type=(*it)[1].str();const char tail=type.back();
            const uint32_t size=tail>='2'&&tail<='4'?uint32_t(tail-'0')*4:4;
            const uint32_t alignment=size>=12?16:size;
            end=(end+alignment-1)/alignment*alignment+size;consumed=size_t(it->position()+it->length());
        }
        if(body.find_first_not_of(" \t\r\n",consumed)!=std::string::npos||!end||end>pushBytes)
            throw std::invalid_argument("OpenGL compute: unsupported push constant layout or insufficient bytes");
        source=std::regex_replace(source,std::regex(R"(layout\s*\(\s*push_constant\s*\))"),"layout(std140,binding=15)");
    }
    return source;
}
} // namespace OpenGLComputeDetail

inline void OpenglBackend::InitializeCompute() {
    computeAvailable_=GLAD_GL_VERSION_4_3&&glDispatchCompute&&glBindImageTexture&&glMemoryBarrier;
    if(!computeAvailable_)return;
    GLint64 bytes=0;GLint buffers=0,bindingPoints=0;
    glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE,&bytes);
    glGetIntegerv(GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS,&buffers);glGetIntegerv(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS,&bindingPoints);
    computeStatistics_.maxStorageBufferBytes=uint64_t(std::max<GLint64>(0,bytes));
    computeStatistics_.maxStorageBindings=uint32_t(std::max(0,std::min(buffers,bindingPoints)));
    glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS,&computeInvocations_);
    glGetIntegerv(GL_MAX_COMPUTE_IMAGE_UNIFORMS,&computeImageUnits_);
    GLint imageBindings=0;glGetIntegerv(GL_MAX_IMAGE_UNITS,&imageBindings);computeImageUnits_=std::min(computeImageUnits_,imageBindings);
    glGetIntegerv(GL_MAX_COMPUTE_UNIFORM_BLOCKS,&computeUniformBlocks_);
    glGetIntegerv(GL_MAX_COMPUTE_TEXTURE_IMAGE_UNITS,&computeTextureUnits_);
    for(GLuint axis=0;axis<3;++axis) {
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT,axis,&computeGroupCount_[axis]);
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE,axis,&computeGroupSize_[axis]);
    }
    if(const auto* renderer=glGetString(GL_RENDERER))computeStatistics_.deviceName=reinterpret_cast<const char*>(renderer);
    CheckComputeError("initialize capabilities");
}
inline void OpenglBackend::ShutdownCompute() {
    for(const auto& [id,p]:computePipelines_)if(p.name)glDeleteProgram(p.name);
    for(const auto& [id,b]:computeBuffers_)if(b.name)glDeleteBuffers(1,&b.name);
    computePipelines_.clear();computeBuffers_.clear();computeStorageTextures_.clear();
    if(computePushBuffer_)glDeleteBuffers(1,&computePushBuffer_);
    computePushBuffer_=0;computeAvailable_=false;computeFrameOpen_=false;
}
inline void OpenglBackend::CheckComputeError(const char* operation) {
    const auto error=glGetError();if(error==GL_NO_ERROR)return;
    ++computeStatistics_.validationErrors;
    throw std::runtime_error(std::string("OpenGL compute ")+operation+": GL error "+std::to_string(error));
}
inline OpenglBackend::ComputeBuffer& OpenglBackend::GetComputeBuffer(Compute::BufferHandle h) {
    if(h.device!=computeOwner_||!computeBuffers_.contains(h.id))throw std::invalid_argument("OpenGL compute: stale or foreign buffer handle");
    return computeBuffers_.at(h.id);
}
inline OpenglBackend::ComputePipeline& OpenglBackend::GetComputePipeline(Compute::PipelineHandle h) {
    if(h.device!=computeOwner_||!computePipelines_.contains(h.id))throw std::invalid_argument("OpenGL compute: stale or foreign pipeline handle");
    return computePipelines_.at(h.id);
}
inline Compute::BufferHandle OpenglBackend::ComputeCreateBuffer(size_t bytes,const void* initial,const std::string& label) {
    if(!computeAvailable_||computeFrameOpen_||!bytes||bytes>computeStatistics_.maxStorageBufferBytes||bytes>size_t(std::numeric_limits<GLsizeiptr>::max()))
        throw std::invalid_argument("OpenGL compute: create storage outside frame, nonzero size within device range required");
    OpenGLComputeDetail::GenericBufferState saved(GL_SHADER_STORAGE_BUFFER,GL_SHADER_STORAGE_BUFFER_BINDING);
    GLuint name=0;glGenBuffers(1,&name);
    try {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,name);glBufferData(GL_SHADER_STORAGE_BUFFER,GLsizeiptr(bytes),initial,GL_DYNAMIC_COPY);
        if(!initial) {
            // Buffer clears require a multiple of the clear element size. R8UI
            // makes every byte count valid, including diagnostic odd sizes.
            const GLubyte zero=0;glClearBufferData(GL_SHADER_STORAGE_BUFFER,GL_R8UI,GL_RED_INTEGER,GL_UNSIGNED_BYTE,&zero);
        }
        CheckComputeError("create storage buffer");
        const auto id=nextComputeId_++;computeBuffers_.emplace(id,ComputeBuffer{name,bytes,label});return {id,computeOwner_};
    } catch(...) {if(name)glDeleteBuffers(1,&name);throw;}
}
inline void OpenglBackend::ComputeUpload(Compute::BufferHandle h,const void* data,size_t bytes,size_t offset) {
    auto& buffer=GetComputeBuffer(h);
    if(computeFrameOpen_||offset>buffer.bytes||bytes>buffer.bytes-offset||(bytes&&!data))
        throw std::invalid_argument("OpenGL compute: upload requires an outside-frame valid byte range");
    if(!bytes)return;
    OpenGLComputeDetail::GenericBufferState saved(GL_SHADER_STORAGE_BUFFER,GL_SHADER_STORAGE_BUFFER_BINDING);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffer.name);glBufferSubData(GL_SHADER_STORAGE_BUFFER,GLintptr(offset),GLsizeiptr(bytes),data);
    CheckComputeError("upload storage buffer");
}
inline Compute::PipelineHandle OpenglBackend::MakeComputePipeline(const Compute::FramePipelineDesc& d,bool frame) {
    GLint maxUniformBindings=0;glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS,&maxUniformBindings);
    if(!computeAvailable_||computeFrameOpen_||d.source.empty()||d.storageBuffers>computeStatistics_.maxStorageBindings||
       (frame&&(d.storageBuffers>8||d.storageImages>8))||d.storageImages>uint32_t(computeImageUnits_)||
       (d.uniformMask|d.sampledMask)>65535u||std::popcount(d.uniformMask)+(d.pushConstantBytes?1:0)>computeUniformBlocks_||
       std::popcount(d.sampledMask)>computeTextureUnits_||d.pushConstantBytes>128||d.pushConstantBytes%4||
       (d.pushConstantBytes&&((d.uniformMask&(1u<<15))||maxUniformBindings<=15)))
        throw std::invalid_argument("OpenGL compute: invalid pipeline limits or push constants conflict with UBO15");
    const auto source=OpenGLComputeDetail::Source(d.source,d.pushConstantBytes);
    GLuint shader=glCreateShader(GL_COMPUTE_SHADER),program=0;
    try {
        const char* text=source.c_str();glShaderSource(shader,1,&text,nullptr);glCompileShader(shader);
        GLint status=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&status);
        if(!status) {GLint size=0;glGetShaderiv(shader,GL_INFO_LOG_LENGTH,&size);std::string log(size_t(std::max(size,1)),0);
            glGetShaderInfoLog(shader,size,nullptr,log.data());throw std::runtime_error("OpenGL compute "+d.label+" compile: "+log);}
        program=glCreateProgram();glAttachShader(program,shader);glLinkProgram(program);glGetProgramiv(program,GL_LINK_STATUS,&status);
        if(!status) {GLint size=0;glGetProgramiv(program,GL_INFO_LOG_LENGTH,&size);std::string log(size_t(std::max(size,1)),0);
            glGetProgramInfoLog(program,size,nullptr,log.data());throw std::runtime_error("OpenGL compute "+d.label+" link: "+log);}
        glDetachShader(program,shader);glDeleteShader(shader);shader=0;
        std::array<GLint,3> local{};glGetProgramiv(program,GL_COMPUTE_WORK_GROUP_SIZE,local.data());uint64_t invocations=1;
        for(unsigned axis=0;axis<3;++axis) {if(local[axis]<=0||local[axis]>computeGroupSize_[axis])throw std::invalid_argument("OpenGL compute: local workgroup axis exceeds device limit");invocations*=uint32_t(local[axis]);}
        if(invocations>uint32_t(computeInvocations_))throw std::invalid_argument("OpenGL compute: local workgroup size exceeds device limit");
        GLint blocks=0;glGetProgramInterfaceiv(program,GL_SHADER_STORAGE_BLOCK,GL_ACTIVE_RESOURCES,&blocks);
        for(GLuint i=0;i<uint32_t(blocks);++i) {const GLenum property=GL_BUFFER_BINDING;GLint binding=0;
            glGetProgramResourceiv(program,GL_SHADER_STORAGE_BLOCK,i,1,&property,1,nullptr,&binding);
            if(binding<0||uint32_t(binding)>=d.storageBuffers)throw std::invalid_argument("OpenGL compute: shader SSBO binding is outside declared layout");}
        glGetProgramiv(program,GL_ACTIVE_UNIFORM_BLOCKS,&blocks);
        for(GLuint i=0;i<uint32_t(blocks);++i) {GLint binding=0;glGetActiveUniformBlockiv(program,i,GL_UNIFORM_BLOCK_BINDING,&binding);
            if(binding==15&&d.pushConstantBytes)continue;
            if(binding<0||binding>=16||!(d.uniformMask&(1u<<binding)))throw std::invalid_argument("OpenGL compute: shader UBO binding is outside declared layout");}
        GLint uniforms=0;glGetProgramiv(program,GL_ACTIVE_UNIFORMS,&uniforms);
        for(GLuint i=0;i<uint32_t(uniforms);++i) {
            GLint block=0,type=0;glGetActiveUniformsiv(program,1,&i,GL_UNIFORM_BLOCK_INDEX,&block);
            if(block!=-1)continue;
            glGetActiveUniformsiv(program,1,&i,GL_UNIFORM_TYPE,&type);
            std::array<char,512> name{};GLsizei length=0;GLint count=0;GLenum nativeType=0;
            glGetActiveUniform(program,i,GLsizei(name.size()),&length,&count,&nativeType,name.data());
            const auto location=glGetUniformLocation(program,name.data());GLint binding=0;
            if(location<0)continue;glGetUniformiv(program,location,&binding);
            if(count!=1||binding<0||binding>=16||
               (type==GL_SAMPLER_2D?!(d.sampledMask&(1u<<binding)):
                type==GL_IMAGE_2D?uint32_t(binding)>=d.storageImages:true))
                throw std::invalid_argument("OpenGL compute: active sampler/image is outside declared 2D layout");
        }
        if(d.pushConstantBytes&&!computePushBuffer_) {
            OpenGLComputeDetail::GenericBufferState saved(GL_UNIFORM_BUFFER,GL_UNIFORM_BUFFER_BINDING);
            glGenBuffers(1,&computePushBuffer_);glBindBuffer(GL_UNIFORM_BUFFER,computePushBuffer_);
            glBufferData(GL_UNIFORM_BUFFER,128,nullptr,GL_STREAM_DRAW);
        }
        CheckComputeError("create pipeline");
        const auto id=nextComputeId_++;computePipelines_.emplace(id,ComputePipeline{program,d.storageBuffers,d.pushConstantBytes,frame,d});
        return {id,computeOwner_};
    } catch(...) {if(shader)glDeleteShader(shader);if(program)glDeleteProgram(program);throw;}
}
inline Compute::PipelineHandle OpenglBackend::ComputeCreatePipeline(const std::string& source,uint32_t bindings,uint32_t pushBytes,const std::string& label) {
    if(!bindings)throw std::invalid_argument("OpenGL compute: standalone pipeline requires storage bindings");
    Compute::FramePipelineDesc d;d.source=source;d.label=label;d.storageBuffers=bindings;d.pushConstantBytes=pushBytes;
    return MakeComputePipeline(d,false);
}
inline Compute::PipelineHandle OpenglBackend::ComputeCreateFramePipeline(const Compute::FramePipelineDesc& d) {return MakeComputePipeline(d,true);}
inline void OpenglBackend::WaitCompute() {
    GLsync fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);if(!fence)throw std::runtime_error("OpenGL compute: cannot create submission fence");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    for(;;) {
        const auto status=glClientWaitSync(fence,GL_SYNC_FLUSH_COMMANDS_BIT,1000000);
        if(status==GL_ALREADY_SIGNALED||status==GL_CONDITION_SATISFIED)break;
        if(status==GL_WAIT_FAILED||std::chrono::steady_clock::now()>deadline) {
            glDeleteSync(fence);throw std::runtime_error("OpenGL compute: submission fence failed or timed out");
        }
    }
    glDeleteSync(fence);
}
inline void OpenglBackend::ComputeSubmit(const Compute::Batch& batch) {
    if(!computeAvailable_||computeFrameOpen_)throw std::logic_error("OpenGL compute: standalone submission is outside-frame only");
    // Validate the whole batch before dispatching any work: rejected batches
    // must not partially mutate buffers or diagnostics.
    for(const auto& d:batch.dispatches) {
        const auto& p=GetComputePipeline(d.pipeline);
        if(p.frame||d.bindings.size()!=p.bindings||d.pushConstants.size()!=p.pushBytes)throw std::invalid_argument("OpenGL compute: pipeline/binding/push count mismatch");
        const uint32_t groups[]{d.groupsX,d.groupsY,d.groupsZ};
        for(unsigned axis=0;axis<3;++axis)if(!groups[axis]||groups[axis]>uint32_t(computeGroupCount_[axis]))throw std::invalid_argument("OpenGL compute: dispatch exceeds device limit");
        std::vector<bool> seen(p.bindings,false);
        for(const auto& binding:d.bindings) {
            GetComputeBuffer(binding.buffer);
            if(binding.binding>=p.bindings||seen[binding.binding]||
                (binding.access!=Compute::Access::ReadOnly&&binding.access!=Compute::Access::WriteOnly&&binding.access!=Compute::Access::ReadWrite))
                throw std::invalid_argument("OpenGL compute: invalid or duplicate storage binding");
            seen[binding.binding]=true;
        }
    }
    if(batch.dispatches.empty())return;
    const auto start=std::chrono::steady_clock::now();
    for(const auto& d:batch.dispatches) {
        const auto& p=GetComputePipeline(d.pipeline);OpenGLComputeDetail::State saved(p.bindings,0,p.pushBytes!=0);
        for(const auto& binding:d.bindings)glBindBufferBase(GL_SHADER_STORAGE_BUFFER,binding.binding,GetComputeBuffer(binding.buffer).name);
        if(p.pushBytes) {
            glBindBuffer(GL_UNIFORM_BUFFER,computePushBuffer_);glBufferData(GL_UNIFORM_BUFFER,128,nullptr,GL_STREAM_DRAW);
            glBufferSubData(GL_UNIFORM_BUFFER,0,p.pushBytes,d.pushConstants.data());glBindBufferBase(GL_UNIFORM_BUFFER,15,computePushBuffer_);
        }
        glUseProgram(p.name);glMemoryBarrier(OpenGLComputeDetail::Visibility);
        glDispatchCompute(d.groupsX,d.groupsY,d.groupsZ);glMemoryBarrier(OpenGLComputeDetail::Visibility);
        CheckComputeError("dispatch standalone kernel");++computeStatistics_.dispatchedKernels;
    }
    WaitCompute();++computeStatistics_.submittedBatches;
    computeStatistics_.lastSubmitMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
}
inline void OpenglBackend::DispatchFrameCompute(const RHICommand::DispatchFrameCompute& c) {
    if(!computeFrameOpen_)throw std::logic_error("OpenGL frame compute: active graphics frame required");
    const auto& p=GetComputePipeline(c.pipeline);const auto& d=p.description;
    if(!p.frame||c.pushBytes!=p.pushBytes||c.pushBytes>c.pushConstants.size())throw std::invalid_argument("OpenGL frame compute: pipeline/push count mismatch");
    if(!c.indirect) {const uint32_t groups[]{c.groupsX,c.groupsY,c.groupsZ};
        for(unsigned axis=0;axis<3;++axis)if(!groups[axis]||groups[axis]>uint32_t(computeGroupCount_[axis]))throw std::invalid_argument("OpenGL frame compute: dispatch exceeds device limit");}
    for(uint32_t slot=0;slot<d.storageBuffers;++slot)GetComputeBuffer(c.buffers[slot]);
    const ComputeBuffer* indirect=nullptr;
    if(c.indirect) {indirect=&GetComputeBuffer(c.indirect);
        if(c.indirectOffset%4||c.indirectOffset>indirect->bytes||12>indirect->bytes-c.indirectOffset)
            throw std::invalid_argument("OpenGL frame compute: indirect range invalid");}
    for(uint32_t slot=0;slot<16;++slot)if(d.uniformMask&(1u<<slot)) {
        GLint name=0;glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING,slot,&name);
        if(!name)throw std::invalid_argument("OpenGL frame compute: required UBO is unbound");
    }
    for(uint32_t slot=0;slot<16;++slot)if(d.sampledMask&(1u<<slot)) {
        const auto it=boundTextures.find(slot);
        const auto* image=it!=boundTextures.end()?rhiContext_.resourcePool->TextureTable.Get(it->second):nullptr;
        if(!image||image->samples!=1)throw std::invalid_argument("OpenGL frame compute: required sampled image is missing/unresolved");
        for(uint32_t output=0;output<d.storageImages;++output)if(it->second==c.images[output])throw std::invalid_argument("OpenGL frame compute: sampled/storage image alias");
    }
    std::array<const RHITextureSpec*,8> images{};
    for(uint32_t slot=0;slot<d.storageImages;++slot) {
        const auto* image=rhiContext_.resourcePool->TextureTable.Get(c.images[slot]);
        if(!image||image->samples!=1||!computeStorageTextures_.contains(image->rhi_id))throw std::invalid_argument("OpenGL frame compute: storage image requires live storage opt-in");
        for(uint32_t other=0;other<slot;++other)if(c.images[other]==c.images[slot])throw std::invalid_argument("OpenGL frame compute: duplicate storage image");
        images[slot]=image;
    }
    OpenGLComputeDetail::State saved(d.storageBuffers,d.storageImages,p.pushBytes!=0);
    for(uint32_t slot=0;slot<d.storageBuffers;++slot)glBindBufferBase(GL_SHADER_STORAGE_BUFFER,slot,GetComputeBuffer(c.buffers[slot]).name);
    for(uint32_t slot=0;slot<d.storageImages;++slot)glBindImageTexture(slot,images[slot]->rhi_id,0,GL_FALSE,0,GL_READ_WRITE,ToOpenGLInternalFormat(images[slot]->textureDataStoreType));
    if(p.pushBytes) {
        glBindBuffer(GL_UNIFORM_BUFFER,computePushBuffer_);glBufferData(GL_UNIFORM_BUFFER,128,nullptr,GL_STREAM_DRAW);
        glBufferSubData(GL_UNIFORM_BUFFER,0,p.pushBytes,c.pushConstants.data());glBindBufferBase(GL_UNIFORM_BUFFER,15,computePushBuffer_);
    }
    glUseProgram(p.name);glMemoryBarrier(OpenGLComputeDetail::Visibility);
    if(indirect) {glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER,indirect->name);glDispatchComputeIndirect(GLintptr(c.indirectOffset));}
    else glDispatchCompute(c.groupsX,c.groupsY,c.groupsZ);
    glMemoryBarrier(OpenGLComputeDetail::Visibility);CheckComputeError("dispatch frame kernel");++computeStatistics_.dispatchedKernels;
}
inline std::vector<std::byte> OpenglBackend::ComputeReadback(Compute::BufferHandle h,size_t bytes,size_t offset) {
    const auto& buffer=GetComputeBuffer(h);
    if(computeFrameOpen_||offset>buffer.bytes)throw std::invalid_argument("OpenGL compute: readback requires an outside-frame valid range");
    if(!bytes)bytes=buffer.bytes-offset;
    if(bytes>buffer.bytes-offset)throw std::out_of_range("OpenGL compute: readback outside storage buffer");
    std::vector<std::byte> result(bytes);if(!bytes)return result;
    OpenGLComputeDetail::GenericBufferState saved(GL_SHADER_STORAGE_BUFFER,GL_SHADER_STORAGE_BUFFER_BINDING);
    glMemoryBarrier(OpenGLComputeDetail::Visibility);glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffer.name);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,GLintptr(offset),GLsizeiptr(bytes),result.data());CheckComputeError("read storage buffer");return result;
}
inline std::vector<uint32_t> OpenglBackend::CompletedComputeWords(Compute::BufferHandle h,uint32_t count) const {
    auto* self=const_cast<OpenglBackend*>(this);const auto& buffer=self->GetComputeBuffer(h);
    if(size_t(count)>buffer.bytes/4)throw std::out_of_range("OpenGL compute: diagnostic words outside buffer");
    std::vector<uint32_t> result(count);if(!count)return result;
    const auto bytes=self->ComputeReadback(h,size_t(count)*4,0);std::memcpy(result.data(),bytes.data(),bytes.size());return result;
}
inline void OpenglBackend::ComputeCopyToVertexBuffer(Compute::BufferHandle h,RenderResourceHandle<VertexBufferSpec> mesh,size_t bytes,size_t sourceOffset,size_t destinationOffset) {
    const auto& source=GetComputeBuffer(h);const auto* target=rhiContext_.resourcePool->vertexBufferTable.Get(mesh);
    if(computeFrameOpen_||!target||!VAO2VertexBufferSpec.contains(target->rhi_id)||sourceOffset>source.bytes||bytes>source.bytes-sourceOffset||
        destinationOffset>target->vertexByteSize||bytes>target->vertexByteSize-destinationOffset)
        throw std::invalid_argument("OpenGL compute: invalid outside-frame copy-to-vertex range");
    if(!bytes)return;
    OpenGLComputeDetail::GenericBufferState read(GL_COPY_READ_BUFFER,GL_COPY_READ_BUFFER_BINDING),write(GL_COPY_WRITE_BUFFER,GL_COPY_WRITE_BUFFER_BINDING);
    glMemoryBarrier(OpenGLComputeDetail::Visibility);glBindBuffer(GL_COPY_READ_BUFFER,source.name);
    glBindBuffer(GL_COPY_WRITE_BUFFER,VAO2VertexBufferSpec.at(target->rhi_id).vbo);
    glCopyBufferSubData(GL_COPY_READ_BUFFER,GL_COPY_WRITE_BUFFER,GLintptr(sourceOffset),GLintptr(destinationOffset),GLsizeiptr(bytes));
    glMemoryBarrier(OpenGLComputeDetail::Visibility);CheckComputeError("copy storage to vertex buffer");
}
inline void OpenglBackend::ComputeDestroy(Compute::BufferHandle h) {
    if(!h)return;const auto& buffer=GetComputeBuffer(h);
    if(computeFrameOpen_)throw std::logic_error("OpenGL compute: destroy buffer outside a frame");
    glDeleteBuffers(1,&buffer.name);computeBuffers_.erase(h.id);
}
inline void OpenglBackend::ComputeDestroy(Compute::PipelineHandle h) {
    if(!h)return;const auto& program=GetComputePipeline(h);
    if(computeFrameOpen_)throw std::logic_error("OpenGL compute: destroy pipeline outside a frame");
    glDeleteProgram(program.name);computePipelines_.erase(h.id);
}
inline Compute::Statistics OpenglBackend::ComputeDiagnostics() const {
    auto result=computeStatistics_;result.available=result.frameAvailable=computeAvailable_;
    result.liveBuffers=computeBuffers_.size();result.livePipelines=computePipelines_.size();result.allocatedBytes=0;
    for(const auto& [id,buffer]:computeBuffers_)result.allocatedBytes+=buffer.bytes;
    return result;
}
