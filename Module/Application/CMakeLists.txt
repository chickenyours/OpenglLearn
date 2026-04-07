# Application 模块 CMakeLists.txt

set(APPLICATION_MODULE_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/public/application_module.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Private/application_module.cpp
)

add_library(application_module STATIC ${APPLICATION_MODULE_SRC})

target_include_directories(application_module PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/public
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
