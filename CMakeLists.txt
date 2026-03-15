cmake_minimum_required(VERSION 3.20)
project(MyProject LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# 设置 Debug 构建类型和使用 /MDd
set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
foreach(flag_var CMAKE_C_FLAGS_DEBUG CMAKE_CXX_FLAGS_DEBUG)
    string(REPLACE "/MD" "/MDd" ${flag_var} "${${flag_var}}")
endforeach()

# 全局 include / lib
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/Engine/include)
link_directories(${CMAKE_SOURCE_DIR}/lib/debug)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib/debug)

# 添加子目录（重要）
# add_subdirectory(engine)
# add_subdirectory(app)
# add_subdirectory(scripts)
add_subdirectory(${CMAKE_SOURCE_DIR}/Engine)
# add_subdirectory(${CMAKE_SOURCE_DIR}/Application)
# add_subdirectory(${CMAKE_SOURCE_DIR}/2D/Application)