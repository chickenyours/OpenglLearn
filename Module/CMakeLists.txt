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
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Public/RHI/rhi_common.h
)

set(RENDER_MODULE_PRIVATE_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Private/render_module.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Private/Platform/Opengl/gl_device.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Private/Platform/Opengl/gl_device.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Private/Platform/Opengl/gl_texture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Private/Platform/Opengl/gl_texture.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Private/Platform/Opengl/gl_texture_convert.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Private/Platform/Opengl/gl_texture_convert.h
)

add_library(render_module STATIC ${RENDER_MODULE_PUBLIC_HDR} ${RENDER_MODULE_PRIVATE_SRC})

target_include_directories(render_module PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Public
    ${CMAKE_CURRENT_SOURCE_DIR}
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
