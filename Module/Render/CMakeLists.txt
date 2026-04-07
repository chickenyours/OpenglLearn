# Render 模块 CMakeLists.txt

set(RENDER_MODULE_PUBLIC_HDR
    ${CMAKE_CURRENT_SOURCE_DIR}/Public/render_module.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Public/RHI/RHI_device.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Public/RHI/RHI_texture.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Public/RHI/rhi_common.h
)

set(RENDER_MODULE_PRIVATE_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/Private/render_module.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Private/Platform/Opengl/gl_device.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Private/Platform/Opengl/gl_device.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Private/Platform/Opengl/gl_texture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Private/Platform/Opengl/gl_texture.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Private/Platform/Opengl/gl_texture_convert.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Private/Platform/Opengl/gl_texture_convert.h
)

add_library(render_module STATIC ${RENDER_MODULE_PUBLIC_HDR} ${RENDER_MODULE_PRIVATE_SRC})

target_include_directories(render_module PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Public
)

target_link_libraries(render_module PUBLIC
    glad
    application_module
)

target_compile_options(render_module PRIVATE
    -Wno-nonportable-include-path
    -Wno-pragma-pack
    -Wno-ignored-pragma-intrinsic
    -Wno-ignored-attributes
)
