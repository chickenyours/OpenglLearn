# Use real targets, never raw .lib names found in lib/debug. All consumers and
# third-party code inherit the same build configuration and CRT selection.
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_LIBRARY_TYPE STATIC)
add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/glfw-3.4" "${CMAKE_BINARY_DIR}/third_party/glfw" EXCLUDE_FROM_ALL)
add_library(glfw3 ALIAS glfw)

add_library(glad STATIC "${CMAKE_SOURCE_DIR}/third_party/glad-generated/src/glad.c")
target_include_directories(glad PUBLIC "${CMAKE_SOURCE_DIR}/include")
if(CMAKE_DL_LIBS)
    target_link_libraries(glad PUBLIC ${CMAKE_DL_LIBS})
endif()

add_library(jsonloader STATIC
    "${CMAKE_SOURCE_DIR}/third_party/jsoncpp-1.9.5/src/lib_json/json_reader.cpp"
    "${CMAKE_SOURCE_DIR}/third_party/jsoncpp-1.9.5/src/lib_json/json_value.cpp"
    "${CMAKE_SOURCE_DIR}/third_party/jsoncpp-1.9.5/src/lib_json/json_writer.cpp")
target_include_directories(jsonloader PUBLIC "${CMAKE_SOURCE_DIR}/include")
target_include_directories(jsonloader PRIVATE "${CMAKE_SOURCE_DIR}/third_party/jsoncpp-1.9.5/include")
target_compile_features(jsonloader PUBLIC cxx_std_11)

# find_program also works when the compiler is configured by its absolute path.
get_filename_component(_llvm_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
find_program(OPENGLLEARN_LLVM_READOBJ NAMES llvm-readobj HINTS "${_llvm_bin}")
find_program(OPENGLLEARN_LLVM_OBJDUMP NAMES llvm-objdump HINTS "${_llvm_bin}")
