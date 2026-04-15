# Module 子目录 CMakeLists.txt
# 注意：不要重复查找依赖包，根 CMakeLists.txt 已经配置好了

# Application 模块
set(APPLICATION_MODULE_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/Application/public/application_module.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Application/Private/application_module.cpp
)

add_library(application_module STATIC ${APPLICATION_MODULE_SRC})

target_include_directories(application_module PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Application/public
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(application_module PUBLIC
    glfw3
    glad
)

target_compile_options(application_module PRIVATE
    -Wno-nonportable-include-path
    -Wno-pragma-pack
    -Wno-ignored-pragma-intrinsic
    -Wno-ignored-attributes
)

# Render 模块
set(RENDER_MODULE_PUBLIC_HDR
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Public/render_module.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Public/RHI/RHI_device.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Public/RHI/RHI_texture.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Public/RHI/RHI_buffer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Public/RHI/RHI_input_layout.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Public/RHI/rhi_common.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Public/RHI/rhi_handles.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Public/RHI/rhi_handle_allocator.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Public/RHI/rhi_command.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Public/RHI/rhi_frontend.h
)

set(RENDER_MODULE_PRIVATE_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Private/render_module.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Private/Backend/Opengl/gl_device.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Private/Backend/Opengl/gl_device.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Private/Backend/Opengl/gl_texture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Private/Backend/Opengl/gl_texture.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Private/Backend/Opengl/gl_texture_convert.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Private/Backend/Opengl/gl_texture_convert.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Private/Backend/Opengl/gl_buffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Private/Backend/Opengl/gl_buffer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Private/Backend/Opengl/gl_input_layout.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Private/Backend/Opengl/gl_input_layout.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Private/RHI/RHI_texture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Private/RHI/rhi_command_queue.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Private/RHI/rhi_command_queue.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Private/RHI/rhi_dual_command_queue.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Private/RHI/rhi_dual_command_queue.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Private/RHI/rhi_backend_resource_table.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Private/RHI/rhi_backend_resource_table.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Private/RHI/rhi_backend_executor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Private/Backend/Opengl/gl_backend_executor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Private/Backend/Opengl/gl_backend_executor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Private/Runtime/render_thread.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Private/Runtime/render_thread.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Public/RHI/rhi_frontend.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Private/RHI/rhi_texture_validator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Private/Backend/Opengl/gl_texture_validator.cpp
    ${CMAKE_SOURCE_DIR}/include/stb_image.cpp
)

add_library(render_module STATIC ${RENDER_MODULE_PUBLIC_HDR} ${RENDER_MODULE_PRIVATE_SRC})

target_include_directories(render_module PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Public
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(render_module PUBLIC
    glad
    application_module
    jsonloader
    OpenGL32
)

target_compile_options(render_module PRIVATE
    -Wno-nonportable-include-path
    -Wno-pragma-pack
    -Wno-ignored-pragma-intrinsic
    -Wno-ignored-attributes
)

# ResourceManager 模块
set(RESOURCE_MANAGER_MODULE_PUBLIC_HDR
    ${CMAKE_CURRENT_SOURCE_DIR}/ResourceManager/Public/resource_manager_module.h
)

set(RESOURCE_MANAGER_MODULE_PRIVATE_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/ResourceManager/Private/resource_manager_module.cpp
)

add_library(resource_manager_module STATIC ${RESOURCE_MANAGER_MODULE_PUBLIC_HDR} ${RESOURCE_MANAGER_MODULE_PRIVATE_SRC})

target_include_directories(resource_manager_module PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/ResourceManager/Public
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(resource_manager_module PUBLIC
    module_manager
)

target_compile_options(resource_manager_module PRIVATE
    -Wno-nonportable-include-path
    -Wno-pragma-pack
    -Wno-ignored-pragma-intrinsic
    -Wno-ignored-attributes
)

# 主模块系统库
set(MODULE_MANAGER_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/module.h
    ${CMAKE_CURRENT_SOURCE_DIR}/module_manager.h
    ${CMAKE_CURRENT_SOURCE_DIR}/module_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/module_include_gen.h
    ${CMAKE_CURRENT_SOURCE_DIR}/module_context.h
    ${CMAKE_CURRENT_SOURCE_DIR}/module_context.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/module_context_gen.h
)

add_library(module_manager STATIC ${MODULE_MANAGER_SRC})

target_include_directories(module_manager PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Application/public
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Public
)

target_link_libraries(module_manager PUBLIC
    application_module
    render_module
    resource_manager_module
)

# 测试程序
add_executable(module_test ${CMAKE_CURRENT_SOURCE_DIR}/Test/test.cpp)

target_include_directories(module_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Application/public
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Public
)

target_link_libraries(module_test PRIVATE
    module_manager
    application_module
    render_module
    glfw3
    glad
)

target_compile_options(module_test PRIVATE
    -Wno-nonportable-include-path
    -Wno-pragma-pack
    -Wno-ignored-pragma-intrinsic
    -Wno-ignored-attributes
)

# ModuleContext 测试程序
add_executable(module_context_test ${CMAKE_CURRENT_SOURCE_DIR}/Test/module_context_test.cpp)

target_include_directories(module_context_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Application/public
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Public
)

target_link_libraries(module_context_test PRIVATE
    module_manager
    application_module
    render_module
    glfw3
    glad
)

target_compile_options(module_context_test PRIVATE
    -Wno-nonportable-include-path
    -Wno-pragma-pack
    -Wno-ignored-pragma-intrinsic
    -Wno-ignored-attributes
)

# RHI Buffer 测试程序
add_executable(rhi_buffer_test ${CMAKE_CURRENT_SOURCE_DIR}/Render/Test/rhi_buffer_test.cpp)

target_include_directories(rhi_buffer_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Application/public
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Public
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Private
)

target_link_libraries(rhi_buffer_test PRIVATE
    render_module
    application_module
    glfw3
    glad
)

target_compile_options(rhi_buffer_test PRIVATE
    -Wno-nonportable-include-path
    -Wno-pragma-pack
    -Wno-ignored-pragma-intrinsic
    -Wno-ignored-attributes
)

# RHI 第一阶段命令代理测试程序
add_executable(rhi_command_proxy_test ${CMAKE_CURRENT_SOURCE_DIR}/Render/Test/rhi_command_proxy_test.cpp)

target_include_directories(rhi_command_proxy_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Application/public
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Public
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Private
)

target_link_libraries(rhi_command_proxy_test PRIVATE
    render_module
    application_module
    module_manager
    glfw3
    glad
)

target_compile_options(rhi_command_proxy_test PRIVATE
    -Wno-nonportable-include-path
    -Wno-pragma-pack
    -Wno-ignored-pragma-intrinsic
    -Wno-ignored-attributes
)

# RHI 纹理验证器测试程序
add_executable(rhi_texture_validator_test ${CMAKE_CURRENT_SOURCE_DIR}/Render/Test/rhi_texture_validator_test.cpp)

target_include_directories(rhi_texture_validator_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Application/public
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Public
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Private
)

target_link_libraries(rhi_texture_validator_test PRIVATE
    render_module
    application_module
    module_manager
    glfw3
    glad
    jsonloader
)

target_compile_options(rhi_texture_validator_test PRIVATE
    -Wno-nonportable-include-path
    -Wno-pragma-pack
    -Wno-ignored-pragma-intrinsic
    -Wno-ignored-attributes
)

# RHI 空纹理创建测试程序（工具层 API）
add_executable(rhi_empty_texture_test ${CMAKE_CURRENT_SOURCE_DIR}/Render/Test/rhi_empty_texture_test.cpp)

target_include_directories(rhi_empty_texture_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Application/public
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Public
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Private
)

target_link_libraries(rhi_empty_texture_test PRIVATE
    render_module
    application_module
    module_manager
    glfw3
    glad
)

target_compile_options(rhi_empty_texture_test PRIVATE
    -Wno-nonportable-include-path
    -Wno-pragma-pack
    -Wno-ignored-pragma-intrinsic
    -Wno-ignored-attributes
)
