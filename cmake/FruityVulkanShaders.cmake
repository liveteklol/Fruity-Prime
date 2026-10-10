# One compiler and target environment for local and CI Vulkan shader builds.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(FRUITY_GLSLC NAMES glslc
    HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
        "${CMAKE_ANDROID_NDK}/shader-tools/windows-x86_64" "${CMAKE_ANDROID_NDK}/shader-tools/linux-x86_64"
        "${CMAKE_ANDROID_NDK}/shader-tools/darwin-x86_64" "${ANDROID_NDK}/shader-tools/windows-x86_64"
        "${ANDROID_NDK}/shader-tools/linux-x86_64" "${ANDROID_NDK}/shader-tools/darwin-x86_64"
        "$ENV{VCPKG_INSTALLATION_ROOT}/installed/x64-windows/tools/shaderc"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/shaderc"
    NO_CMAKE_FIND_ROOT_PATH
    REQUIRED)
set(FRUITY_VULKAN_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/vulkan-shaders")
set(_fruity_shader_sources "${CMAKE_CURRENT_SOURCE_DIR}/src/MphRead.Native/Shaders.cpp")
set(_fruity_shader_generator "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate-vulkan-scene-shaders.py")
set(_fruity_generated_glsl)
set(_fruity_generated_spirv)
foreach(_program main composite cel shift backdrop)
    foreach(_stage vert frag)
        list(APPEND _fruity_generated_glsl "${FRUITY_VULKAN_SHADER_DIR}/${_program}.${_stage}")
        list(APPEND _fruity_generated_spirv "${FRUITY_VULKAN_SHADER_DIR}/${_program}.${_stage}.spv")
    endforeach()
endforeach()
# main_fast: the main program over storage-buffer records (performance mode).
# main_fast2: the cpp-port renderer's layout for meshes in the global geometry buffer.
set(_fruity_fast_spirv)
foreach(_program main_fast main_fast2 composite_fast)
    foreach(_stage vert frag)
        list(APPEND _fruity_generated_glsl "${FRUITY_VULKAN_SHADER_DIR}/${_program}.${_stage}")
        list(APPEND _fruity_fast_spirv "${FRUITY_VULKAN_SHADER_DIR}/${_program}.${_stage}.spv")
    endforeach()
endforeach()
add_custom_command(
    OUTPUT ${_fruity_generated_glsl} "${FRUITY_VULKAN_SHADER_DIR}/bindings.json"
        "${FRUITY_VULKAN_SHADER_DIR}/main_fast2.json"
    COMMAND Python3::Interpreter "${_fruity_shader_generator}"
        --source "${_fruity_shader_sources}" --output "${FRUITY_VULKAN_SHADER_DIR}"
    DEPENDS "${_fruity_shader_generator}" "${_fruity_shader_sources}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/scene_shader_abi.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/MphRead.Native/NativeRuntime/Rhi/SceneShaderAbi.def"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/MphRead.Native/NativeRuntime/Rhi/SceneShaderAbi.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/MphRead.Native/NativeRuntime/Rhi/VertexSemantics.hpp"
    VERBATIM)
# Vulkan 1.1 is SPIR-V 1.3: the backend's legacy path runs 1.1 devices (a
# Mali-G78 goes no further), and a 1.3 device reads 1.3 SPIR-V as well.
foreach(_source IN LISTS _fruity_generated_glsl)
    add_custom_command(
        OUTPUT "${_source}.spv"
        COMMAND "${FRUITY_GLSLC}" --target-env=vulkan1.1 -O0
            "${_source}" -o "${_source}.spv"
        DEPENDS "${_source}" "${FRUITY_GLSLC}"
        VERBATIM)
endforeach()
set(_fruity_shader_header "${FRUITY_VULKAN_SHADER_DIR}/FruityVulkanSceneShaders.hpp")
add_custom_command(
    OUTPUT "${_fruity_shader_header}"
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed-vulkan-scene-shaders.py"
        --directory "${FRUITY_VULKAN_SHADER_DIR}" --output "${_fruity_shader_header}"
    DEPENDS ${_fruity_generated_spirv} "${FRUITY_VULKAN_SHADER_DIR}/bindings.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed-vulkan-scene-shaders.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/reflect_scene_spirv.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/scene_shader_abi.py"
    VERBATIM)
set(_fruity_fast_header "${FRUITY_VULKAN_SHADER_DIR}/FruityVulkanFastShaders.hpp")
add_custom_command(
    OUTPUT "${_fruity_fast_header}"
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed-vulkan-fast-shaders.py"
        --directory "${FRUITY_VULKAN_SHADER_DIR}" --output "${_fruity_fast_header}"
    DEPENDS ${_fruity_fast_spirv} "${FRUITY_VULKAN_SHADER_DIR}/main_fast2.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed-vulkan-fast-shaders.py"
    VERBATIM)
add_custom_target(fruity_vulkan_shaders DEPENDS "${_fruity_shader_header}" "${_fruity_fast_header}")
target_sources(fruity_mphread_native PRIVATE "${_fruity_shader_header}" "${_fruity_fast_header}")
target_include_directories(fruity_mphread_native PRIVATE "${FRUITY_VULKAN_SHADER_DIR}")
add_dependencies(fruity_mphread_native fruity_vulkan_shaders)

set(_rhi_fixture_source "${CMAKE_CURRENT_SOURCE_DIR}/src/MphRead.Native/Testing/RhiConformanceShaderSource.hpp")
set(_rhi_fixture_builder "${CMAKE_CURRENT_SOURCE_DIR}/tools/build-rhi-conformance-shaders.py")
add_custom_command(
    OUTPUT "${FRUITY_VULKAN_SHADER_DIR}/conformance.vert" "${FRUITY_VULKAN_SHADER_DIR}/conformance.frag"
    COMMAND Python3::Interpreter "${_rhi_fixture_builder}" --phase generate --source "${_rhi_fixture_source}"
        --directory "${FRUITY_VULKAN_SHADER_DIR}"
    DEPENDS "${_rhi_fixture_source}" "${_rhi_fixture_builder}" VERBATIM)
foreach(_stage vert frag)
    add_custom_command(OUTPUT "${FRUITY_VULKAN_SHADER_DIR}/conformance.${_stage}.spv"
        COMMAND "${FRUITY_GLSLC}" --target-env=vulkan1.1 -DFRUITY_VULKAN=1 -O0
            "${FRUITY_VULKAN_SHADER_DIR}/conformance.${_stage}" -o "${FRUITY_VULKAN_SHADER_DIR}/conformance.${_stage}.spv"
        DEPENDS "${FRUITY_VULKAN_SHADER_DIR}/conformance.${_stage}" "${FRUITY_GLSLC}" VERBATIM)
endforeach()
set(_rhi_fixture_header "${FRUITY_VULKAN_SHADER_DIR}/FruityRhiConformanceShaders.hpp")
add_custom_command(OUTPUT "${_rhi_fixture_header}"
    COMMAND Python3::Interpreter "${_rhi_fixture_builder}" --phase embed --source "${_rhi_fixture_source}"
        --directory "${FRUITY_VULKAN_SHADER_DIR}" --output "${_rhi_fixture_header}"
    DEPENDS "${FRUITY_VULKAN_SHADER_DIR}/conformance.vert.spv" "${FRUITY_VULKAN_SHADER_DIR}/conformance.frag.spv"
        "${_rhi_fixture_builder}" VERBATIM)
add_custom_target(fruity_rhi_conformance_shaders DEPENDS "${_rhi_fixture_header}")
add_dependencies(fruity_mphread_native fruity_rhi_conformance_shaders)
