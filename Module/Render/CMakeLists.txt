# Render 模块 CMakeLists.txt

set(RENDER_MODULE_PUBLIC_HDR
    ${CMAKE_CURRENT_SOURCE_DIR}/Public/render_module.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Public/RHI/RHI_device.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Public/RHI/RHI_texture.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Public/RHI/RHI_buffer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Public/RHI/RHI_input_layout.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Public/RHI/rhi_common.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Public/RHI/rhi_handles.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Public/RHI/rhi_handle_allocator.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Public/RHI/rhi_command.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Public/RHI/rhi_frontend.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Public/RHI/rhi_texture_validator.h
)

set(RENDER_MODULE_PRIVATE_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/Private/render_module.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Private/Backend/Opengl/gl_device.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Private/Backend/Opengl/gl_device.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Private/Backend/Opengl/gl_texture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Private/Backend/Opengl/gl_texture.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Private/Backend/Opengl/gl_texture_convert.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Private/Backend/Opengl/gl_texture_convert.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Private/Backend/Opengl/gl_buffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Private/Backend/Opengl/gl_buffer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Private/Backend/Opengl/gl_input_layout.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Private/Backend/Opengl/gl_input_layout.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Private/RHI/RHI_texture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Private/RHI/rhi_command_queue.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Private/RHI/rhi_command_queue.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Private/RHI/rhi_backend_resource_table.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Private/RHI/rhi_backend_resource_table.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Private/RHI/rhi_backend_executor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Private/Backend/Opengl/gl_backend_executor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Private/Backend/Opengl/gl_backend_executor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Private/Runtime/render_thread.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Private/Runtime/render_thread.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Public/RHI/rhi_frontend.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Private/RHI/rhi_texture_validator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Private/Backend/Opengl/gl_texture_validator.cpp
)

add_library(render_module STATIC ${RENDER_MODULE_PUBLIC_HDR} ${RENDER_MODULE_PRIVATE_SRC})

target_include_directories(render_module PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Public
)

target_link_libraries(render_module PUBLIC
    glad
    application_module
    jsonloader
)

target_compile_options(render_module PRIVATE
    -Wno-nonportable-include-path
    -Wno-pragma-pack
    -Wno-ignored-pragma-intrinsic
    -Wno-ignored-attributes
)

# RHI Buffer 测试程序
add_executable(rhi_buffer_test ${CMAKE_CURRENT_SOURCE_DIR}/Test/rhi_buffer_test.cpp)

target_include_directories(rhi_buffer_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Public
    ${CMAKE_CURRENT_SOURCE_DIR}/../Application/public
)

target_link_libraries(rhi_buffer_test PRIVATE
    render_module
    glad
    glfw3
)

target_compile_options(rhi_buffer_test PRIVATE
    -Wno-nonportable-include-path
    -Wno-pragma-pack
    -Wno-ignored-pragma-intrinsic
    -Wno-ignored-attributes
)

# RHI 第一阶段命令代理测试程序
add_executable(rhi_command_proxy_test ${CMAKE_CURRENT_SOURCE_DIR}/Test/rhi_command_proxy_test.cpp)

target_include_directories(rhi_command_proxy_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Public
    ${CMAKE_CURRENT_SOURCE_DIR}/../Application/public
)

target_link_libraries(rhi_command_proxy_test PRIVATE
    render_module
    glad
    glfw3
    application_module
    module_manager
)

target_compile_options(rhi_command_proxy_test PRIVATE
    -Wno-nonportable-include-path
    -Wno-pragma-pack
    -Wno-ignored-pragma-intrinsic
    -Wno-ignored-attributes
)

# RHI 纹理验证器测试程序
add_executable(rhi_texture_validator_test ${CMAKE_CURRENT_SOURCE_DIR}/Test/rhi_texture_validator_test.cpp)

target_include_directories(rhi_texture_validator_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Public
    ${CMAKE_CURRENT_SOURCE_DIR}/../Application/public
)

target_link_libraries(rhi_texture_validator_test PRIVATE
    render_module
    glad
    glfw3
    application_module
    module_manager
    jsonloader
)

target_compile_options(rhi_texture_validator_test PRIVATE
    -Wno-nonportable-include-path
    -Wno-pragma-pack
    -Wno-ignored-pragma-intrinsic
    -Wno-ignored-attributes
)

# RHI 空纹理创建测试程序（工具层 API）
add_executable(rhi_empty_texture_test ${CMAKE_CURRENT_SOURCE_DIR}/Test/rhi_empty_texture_test.cpp)

target_include_directories(rhi_empty_texture_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Public
    ${CMAKE_CURRENT_SOURCE_DIR}/../Application/public
    ${CMAKE_CURRENT_SOURCE_DIR}/Private
)

target_link_libraries(rhi_empty_texture_test PRIVATE
    render_module
    glad
    glfw3
    application_module
    module_manager
)

target_compile_options(rhi_empty_texture_test PRIVATE
    -Wno-nonportable-include-path
    -Wno-pragma-pack
    -Wno-ignored-pragma-intrinsic
    -Wno-ignored-attributes
)
