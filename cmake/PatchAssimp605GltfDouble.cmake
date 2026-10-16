# Opt-in compatibility patch for the official Assimp 6.0.5 source tree.
# Usage: cmake -DASSIMP_SOURCE_DIR=<writable-source> -P <this-file>
# Add -DCHECK_ONLY=ON to validate without modifying the source checkout.
# FetchContent uses this only for its pinned, project-owned source checkout.
# Explicit MODEL_ASSIMP_SOURCE_DIR checkouts are never silently modified.
cmake_minimum_required(VERSION 3.24)
if(NOT ASSIMP_SOURCE_DIR OR NOT EXISTS "${ASSIMP_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "ASSIMP_SOURCE_DIR must name a writable Assimp 6.0.5 source checkout")
endif()
file(READ "${ASSIMP_SOURCE_DIR}/CMakeLists.txt" _assimp_project)
if(NOT _assimp_project MATCHES "PROJECT\\(Assimp VERSION 6[.]0[.]5[ \r\n]")
    message(FATAL_ERROR "The glTF double-precision patch applies only to official Assimp 6.0.5")
endif()
set(_file "${ASSIMP_SOURCE_DIR}/code/AssetLib/glTF2/glTF2Asset.inl")
file(READ "${_file}" _source)
string(REPLACE "\r\n" "\n" _source "${_source}")
set(_original_sha256 "e601439bd71753c22737fcff908ac5ca5aaa11fced396a2e6f01001158c26a2e")
set(_old_include "#include <assimp/DefaultLogger.hpp>")
set(_new_include "${_old_include}\n#include <type_traits>\n#include <utility>")
set(_old_template [=[template <class T>
size_t Accessor::ExtractData(T *&outData, const std::vector<unsigned int> *remappingIndices) {]=])
set(_new_template [=[// OpenGLLearn Assimp 6.0.5 glTF DOUBLE compatibility patch v1.
// A glTF FLOAT accessor always stores 32-bit floats, even when ai_real is double.
// Recognize scalar/XYZ/colour/tangent layouts rather than guessing from sizeof:
// raw float arrays (inverse-bind matrices), vec4f and integer weights stay raw.
template <class T, class = void> struct OpenGLLearnGltfRealXYZ : std::false_type {};
template <class T> struct OpenGLLearnGltfRealXYZ<T, std::void_t<
        decltype(std::declval<T>().x), decltype(std::declval<T>().y), decltype(std::declval<T>().z)>>
    : std::integral_constant<bool,
        std::is_same<decltype(std::declval<T>().x), ai_real>::value &&
        std::is_same<decltype(std::declval<T>().y), ai_real>::value &&
        std::is_same<decltype(std::declval<T>().z), ai_real>::value> {};
template <class T, class = void> struct OpenGLLearnGltfRealColor : std::false_type {};
template <class T> struct OpenGLLearnGltfRealColor<T, std::void_t<
        decltype(std::declval<T>().r), decltype(std::declval<T>().g), decltype(std::declval<T>().b)>>
    : std::integral_constant<bool,
        std::is_same<decltype(std::declval<T>().r), ai_real>::value &&
        std::is_same<decltype(std::declval<T>().g), ai_real>::value &&
        std::is_same<decltype(std::declval<T>().b), ai_real>::value> {};
template <class T, class = void> struct OpenGLLearnGltfRealTangent : std::false_type {};
template <class T> struct OpenGLLearnGltfRealTangent<T, std::void_t<
        decltype(std::declval<T>().xyz.x), decltype(std::declval<T>().w)>>
    : std::integral_constant<bool,
        OpenGLLearnGltfRealXYZ<decltype(std::declval<T>().xyz)>::value &&
        std::is_same<decltype(std::declval<T>().w), ai_real>::value> {};

template <class T>
size_t Accessor::ExtractData(T *&outData, const std::vector<unsigned int> *remappingIndices) {]=])
set(_old_allocate "    outData = new T[usedCount];")
set(_new_allocate [=[    outData = new T[usedCount];

#ifdef ASSIMP_DOUBLE_PRECISION
    if constexpr (std::is_same<T, ai_real>::value || OpenGLLearnGltfRealXYZ<T>::value ||
            OpenGLLearnGltfRealColor<T>::value || OpenGLLearnGltfRealTangent<T>::value) {
        static_assert(std::is_standard_layout<T>::value && std::is_trivially_copyable<T>::value,
                "glTF numerical conversion requires a contiguous ai_real layout");
        static_assert(sizeof(T) % sizeof(ai_real) == 0, "Incomplete ai_real component layout");
        if (componentType == ComponentType_FLOAT) {
            const size_t components = GetNumComponents();
            constexpr size_t capacity = sizeof(T) / sizeof(ai_real);
            if (components > capacity || elemSize != components * sizeof(float)) {
                delete[] outData;
                outData = nullptr;
                throw DeadlyImportError("GLTF: invalid FLOAT component layout in ", getContextForErrorMessages(id, name));
            }
            for (size_t i = 0; i < usedCount; ++i) {
                const size_t srcIdx = remappingIndices != nullptr ? (*remappingIndices)[i] : i;
                if (srcIdx >= count) {
                    delete[] outData;
                    outData = nullptr;
                    throw DeadlyImportError("GLTF: index ", srcIdx, " >= count ", count, " in ", getContextForErrorMessages(id, name));
                }
                // Preserve the upstream packed component order. In particular,
                // CreateNodeAnim performs its existing XYZW -> quaternion reorder.
                auto *destination = reinterpret_cast<unsigned char *>(outData + i);
                for (size_t component = 0; component < capacity; ++component) {
                    float encoded = 0;
                    if (component < components) {
                        memcpy(&encoded, data + srcIdx * stride + component * sizeof(float), sizeof(float));
                    }
                    const ai_real value = static_cast<ai_real>(encoded);
                    memcpy(destination + component * sizeof(ai_real), &value, sizeof(ai_real));
                }
            }
            return usedCount;
        }
    }
#endif]=])

# Only the exact original or the exact previously patched file is accepted.
# Reversing our three edits validates idempotence without trusting a marker alone.
set(_unpatched "${_source}")
if(_source MATCHES "OpenGLLearn Assimp 6[.]0[.]5 glTF DOUBLE compatibility patch v1")
    string(REPLACE "${_new_allocate}" "${_old_allocate}" _unpatched "${_unpatched}")
    string(REPLACE "${_new_template}" "${_old_template}" _unpatched "${_unpatched}")
    string(REPLACE "${_new_include}" "${_old_include}" _unpatched "${_unpatched}")
    string(SHA256 _unpatched_sha256 "${_unpatched}")
    if(NOT _unpatched_sha256 STREQUAL _original_sha256)
        message(FATAL_ERROR "Assimp glTF compatibility patch was modified; refusing to overwrite it")
    endif()
    message(STATUS "Assimp 6.0.5 glTF double-precision compatibility patch already applied")
    return()
endif()
string(SHA256 _source_sha256 "${_source}")
if(NOT _source_sha256 STREQUAL _original_sha256)
    message(FATAL_ERROR "Assimp glTF source differs from official 6.0.5; refusing to patch an unknown file")
endif()
if(CHECK_ONLY)
    message(FATAL_ERROR
        "Assimp 6.0.5 DOUBLE has an unpatched glTF FLOAT reader. No files were modified. "
        "Explicitly apply the compatibility patch before configuring:\n"
        "\"${CMAKE_COMMAND}\" \"-DASSIMP_SOURCE_DIR=${ASSIMP_SOURCE_DIR}\" -P \"${CMAKE_CURRENT_LIST_FILE}\"")
endif()
string(REPLACE "${_old_include}" "${_new_include}" _source "${_source}")
string(REPLACE "${_old_template}" "${_new_template}" _source "${_source}")
string(REPLACE "${_old_allocate}" "${_new_allocate}" _source "${_source}")
file(WRITE "${_file}" "${_source}")
message(STATUS "Applied Assimp 6.0.5 glTF FLOAT-to-ai_real compatibility patch")
