# Exact, idempotent compatibility patch for the pinned Assimp 6.0.5 sources.
# Explicit source checkouts are inspected with CHECK_ONLY; patching is opt-in:
# cmake -DASSIMP_SOURCE_DIR=<writable-6.0.5> -P <this-file>
cmake_minimum_required(VERSION 3.24)
if(NOT ASSIMP_SOURCE_DIR OR NOT EXISTS "${ASSIMP_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "ASSIMP_SOURCE_DIR must name an Assimp 6.0.5 source checkout")
endif()
file(READ "${ASSIMP_SOURCE_DIR}/CMakeLists.txt" _assimp_project)
if(NOT _assimp_project MATCHES "PROJECT\\(Assimp VERSION 6[.]0[.]5[ \r\n]")
    message(FATAL_ERROR "The material double-precision patch applies only to official Assimp 6.0.5")
endif()

set(_validator_file "${ASSIMP_SOURCE_DIR}/code/PostProcessing/ValidateDataStructure.cpp")
set(_material_file "${ASSIMP_SOURCE_DIR}/include/assimp/material.inl")
file(READ "${_validator_file}" _validator)
file(READ "${_material_file}" _material)
string(REPLACE "\r\n" "\n" _validator "${_validator}")
string(REPLACE "\r\n" "\n" _material "${_material}")
set(_validator_original_sha256 "ffccce680bae1a1e3101701dd9f77047d0a9941511c52a8cc55049311deb5733")
set(_material_original_sha256 "631748d13e2b3f54b79b47c07eacf1a31bbef6f7cc3e6c9ab0d3d82fc2d5450c")
set(_old_uv [=[            if (aiPTI_Float != prop->mType || prop->mDataLength < sizeof(aiUVTransform)) {
                ReportError("Material property %s%i is expected to be 5 floats large (size is %i)",
                        prop->mKey.data, prop->mIndex, prop->mDataLength);
            }]=])
set(_new_uv [=[            // OpenGLLearn Assimp 6.0.5 material DOUBLE compatibility patch v1.
            // Properties retain their encoded scalar type. The UV getter converts
            // both layouts into ai_real, independently of this build's precision.
            const unsigned int componentSize = aiPTI_Float == prop->mType ? sizeof(float) :
                    (aiPTI_Double == prop->mType ? sizeof(double) : 0);
            if (!componentSize || prop->mDataLength < 5 * componentSize) {
                ReportError("Material property %s%i is expected to contain 5 float or double components (size is %i)",
                        prop->mKey.data, prop->mIndex, prop->mDataLength);
            }]=])
set(_old_scalar [=[        } else if (aiPTI_Integer == prop->mType) {
            if (prop->mDataLength < sizeof(int)) {
                ReportError("aiMaterial::mProperties[%i].mDataLength is "
                            "too small to contain an integer (%i, needed: %i)",
                        i, prop->mDataLength, static_cast<int>(sizeof(int)));
            }
        }]=])
set(_new_scalar [=[        } else if (aiPTI_Double == prop->mType) {
            if (prop->mDataLength < sizeof(double)) {
                ReportError("aiMaterial::mProperties[%i].mDataLength is "
                            "too small to contain a double (%i, needed: %i)",
                        i, prop->mDataLength, static_cast<int>(sizeof(double)));
            }
        } else if (aiPTI_Integer == prop->mType) {
            if (prop->mDataLength < sizeof(int)) {
                ReportError("aiMaterial::mProperties[%i].mDataLength is "
                            "too small to contain an integer (%i, needed: %i)",
                        i, prop->mDataLength, static_cast<int>(sizeof(int)));
            }
        }]=])

# The legacy explicit template specializations also need correct ai_real tags.
# Ordinary AddProperty overloads already select Float/Double correctly upstream.
set(_types aiUVTransform aiColor4D aiColor3D aiVector3D)
set(_components mRotation a b x)
foreach(_index RANGE 0 3)
    list(GET _types ${_index} _type)
    list(GET _components ${_index} _component)
    # Include the explicit-template signature: the corrected trailing lines
    # are also present in the ordinary overloads and must not reverse those.
    set(_specialization "template<>\nAI_FORCE_INLINE aiReturn aiMaterial::AddProperty<${_type}>(const ${_type}* pInput,\n        const unsigned int pNumValues,\n        const char* pKey,\n        unsigned int type,\n        unsigned int index) {\n    return AddBinaryProperty((const void*)pInput,\n        pNumValues * sizeof(${_type}),\n")
    set(_old_${_type} "${_specialization}        pKey,type,index,aiPTI_Float);\n}")
    set(_new_${_type} "${_specialization}        pKey,type,index,ai_real_to_property_type_info(pInput->${_component}));\n}")
endforeach()

# Reverse exact replacement bodies and check the complete original file hash.
# This accepts the original or our exact result, never an unknown modified file.
set(_validator_unpatched "${_validator}")
string(REPLACE "${_new_uv}" "${_old_uv}" _validator_unpatched "${_validator_unpatched}")
string(REPLACE "${_new_scalar}" "${_old_scalar}" _validator_unpatched "${_validator_unpatched}")
string(SHA256 _validator_hash "${_validator_unpatched}")
if(NOT _validator_hash STREQUAL _validator_original_sha256)
    message(FATAL_ERROR "Assimp material validator differs from official 6.0.5 or this exact patch; no files were modified")
endif()
set(_material_unpatched "${_material}")
foreach(_type IN LISTS _types)
    string(REPLACE "${_new_${_type}}" "${_old_${_type}}" _material_unpatched "${_material_unpatched}")
endforeach()
string(SHA256 _material_hash "${_material_unpatched}")
if(NOT _material_hash STREQUAL _material_original_sha256)
    message(FATAL_ERROR "Assimp material header differs from official 6.0.5 or this exact patch; no files were modified")
endif()

# Compute the single expected final result, also repairing a previously
# interrupted application only after BOTH complete source hashes were checked.
set(_validator_patched "${_validator_unpatched}")
string(REPLACE "${_old_uv}" "${_new_uv}" _validator_patched "${_validator_patched}")
string(REPLACE "${_old_scalar}" "${_new_scalar}" _validator_patched "${_validator_patched}")
set(_material_patched "${_material_unpatched}")
foreach(_type IN LISTS _types)
    string(REPLACE "${_old_${_type}}" "${_new_${_type}}" _material_patched "${_material_patched}")
endforeach()
if(_validator STREQUAL _validator_patched AND _material STREQUAL _material_patched)
    message(STATUS "Assimp 6.0.5 material double-precision compatibility patch already applied")
    return()
endif()
if(CHECK_ONLY)
    message(FATAL_ERROR
        "Assimp 6.0.5 DOUBLE material encoding/validation requires its compatibility patch. No files were modified. "
        "Explicitly apply it before configuring:\n"
        "\"${CMAKE_COMMAND}\" \"-DASSIMP_SOURCE_DIR=${ASSIMP_SOURCE_DIR}\" -P \"${CMAKE_CURRENT_LIST_FILE}\"")
endif()
file(WRITE "${_validator_file}" "${_validator_patched}")
file(WRITE "${_material_file}" "${_material_patched}")
message(STATUS "Applied Assimp 6.0.5 material float/double encoding and validation compatibility patch")
