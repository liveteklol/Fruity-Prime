cmake_minimum_required(VERSION 3.25)

project(FruityPrimeNative LANGUAGES CXX)

include(CTest)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FruityNativeConfig.cmake")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)

# std::stop_token, std::stop_source and std::jthread stand in for the C#
# CancellationToken and the decoder/network worker threads. libc++ before 20
# (the NDK's and Apple's) ships them only behind -fexperimental-library; GCC's
# libstdc++ and MSVC's STL have them outright. Ask for the flag only where the
# plain compile fails, and only if it then succeeds.
include(CheckCXXSourceCompiles)
set(_fruity_stop_token_probe [=[
#include <stop_token>
#include <thread>
int main()
{
    std::stop_source source;
    std::jthread worker([](std::stop_token token) { (void)token.stop_requested(); });
    return source.stop_requested() ? 1 : 0;
}
]=])
set(CMAKE_REQUIRED_FLAGS "${CMAKE_CXX20_STANDARD_COMPILE_OPTION}")
check_cxx_source_compiles("${_fruity_stop_token_probe}" FRUITY_STOP_TOKEN_BUILTIN)
set(FRUITY_STOP_TOKEN_OPTIONS "")
if(NOT FRUITY_STOP_TOKEN_BUILTIN)
    set(CMAKE_REQUIRED_FLAGS "${CMAKE_CXX20_STANDARD_COMPILE_OPTION} -fexperimental-library")
    set(CMAKE_REQUIRED_LINK_OPTIONS "-fexperimental-library")
    check_cxx_source_compiles("${_fruity_stop_token_probe}" FRUITY_STOP_TOKEN_EXPERIMENTAL)
    unset(CMAKE_REQUIRED_LINK_OPTIONS)
    if(FRUITY_STOP_TOKEN_EXPERIMENTAL)
        set(FRUITY_STOP_TOKEN_OPTIONS "-fexperimental-library")
    else()
        message(FATAL_ERROR
            "This standard library has no std::stop_token/std::jthread, even with "
            "-fexperimental-library.")
    endif()
endif()
unset(CMAKE_REQUIRED_FLAGS)

function(fruity_collect_owner owner_root out_sources out_headers)
    file(GLOB_RECURSE _sources CONFIGURE_DEPENDS "${owner_root}/*.cpp")
    file(GLOB_RECURSE _headers CONFIGURE_DEPENDS "${owner_root}/*.hpp" "${owner_root}/*.h")
    list(SORT _sources)
    list(SORT _headers)

    if(NOT _sources)
        message(FATAL_ERROR "No C++ sources were found under ${owner_root}")
    endif()

    set(${out_sources} "${_sources}" PARENT_SCOPE)
    set(${out_headers} "${_headers}" PARENT_SCOPE)
endfunction()

function(fruity_report_owner label sources_var headers_var)
    list(LENGTH ${sources_var} _source_count)
    list(LENGTH ${headers_var} _header_count)
    message(STATUS "${label}: ${_source_count} C++ sources, ${_header_count} headers")
endfunction()

set(FRUITY_NCSFPLAY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src/NcsfPlay.Native")
set(FRUITY_MPHREAD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src/MphRead.Native")
set(FRUITY_ANDROID_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src/MphRead.Native.Android")

fruity_collect_owner("${FRUITY_NCSFPLAY_ROOT}" NCSFPLAY_NATIVE_SOURCES NCSFPLAY_NATIVE_HEADERS)
fruity_collect_owner("${FRUITY_MPHREAD_ROOT}" MPHREAD_NATIVE_SOURCES MPHREAD_NATIVE_HEADERS)

# Phase 4 terminal static gate. The phase plan requires the equivalent of:
#   rg -n "GenLists|NewList|EndList|CallList|DeleteLists" src/MphRead.Native
# to return zero. Enforce that invariant during every configure so CI cannot
# build a tree that accidentally reintroduces OpenGL display-list ownership.
set(FRUITY_PHASE4_DISPLAY_LIST_REGEX "GenLists|NewList|EndList|CallList|DeleteLists")
foreach(FRUITY_PHASE4_SOURCE IN LISTS MPHREAD_NATIVE_SOURCES MPHREAD_NATIVE_HEADERS)
    file(READ "${FRUITY_PHASE4_SOURCE}" FRUITY_PHASE4_SOURCE_TEXT)
    if(FRUITY_PHASE4_SOURCE_TEXT MATCHES "${FRUITY_PHASE4_DISPLAY_LIST_REGEX}")
        message(FATAL_ERROR
            "Phase 4 display-list static gate failed: ${FRUITY_PHASE4_SOURCE}")
    endif()
    if(FRUITY_PHASE4_SOURCE_TEXT MATCHES "GL::Begin\\(|::glBegin\\(|::glEnd\\(")
        message(FATAL_ERROR
            "Phase 4 immediate-mode static gate failed: ${FRUITY_PHASE4_SOURCE}")
    endif()
endforeach()
unset(FRUITY_PHASE4_SOURCE_TEXT)

# Phase 5 terminal static gate: no shader source reads a GLSL built-in vertex
# attribute. tools/check-phase5-shader-interface.py is the full audit.
foreach(FRUITY_PHASE5_SHADER_SOURCE
        "${FRUITY_MPHREAD_ROOT}/Shaders.cpp"
        "${FRUITY_MPHREAD_ROOT}/Mods/Render/EsShaders.cpp")
    file(READ "${FRUITY_PHASE5_SHADER_SOURCE}" FRUITY_PHASE5_SOURCE_TEXT)
    if(FRUITY_PHASE5_SOURCE_TEXT MATCHES "gl_(Vertex|Normal|Color|MultiTexCoord|SecondaryColor|FogCoord)[^A-Za-z_]")
        message(FATAL_ERROR
            "Phase 5 built-in vertex attribute gate failed: ${FRUITY_PHASE5_SHADER_SOURCE}")
    endif()
endforeach()
unset(FRUITY_PHASE5_SOURCE_TEXT)
unset(FRUITY_PHASE5_SHADER_SOURCE)
unset(FRUITY_PHASE4_SOURCE)
unset(FRUITY_PHASE4_DISPLAY_LIST_REGEX)

# Runtime decoder tests have their own executable below. Keep its main() out
# of the production archive while retaining the repository's Native/Testing
# source location.
set(FRUITY_RENDERER_GEOMETRY_TEST_SOURCE
    "${FRUITY_MPHREAD_ROOT}/Testing/TestRendererGeometry.cpp")
set(FRUITY_RENDERER_GPU_MESH_TEST_SOURCE
    "${FRUITY_MPHREAD_ROOT}/Testing/TestRendererGpuMesh.cpp")
set(FRUITY_RHI_LIFETIME_TEST_SOURCE
    "${FRUITY_MPHREAD_ROOT}/Testing/TestRhiLifetime.cpp")
set(FRUITY_MEMORY_ADMISSION_TEST_SOURCE
    "${FRUITY_MPHREAD_ROOT}/Testing/TestMemoryAdmission.cpp")
set(FRUITY_VULKAN_UNIFORMS_TEST_SOURCE
    "${FRUITY_MPHREAD_ROOT}/Testing/TestVulkanSceneUniforms.cpp")
set(FRUITY_VULKAN_DESCRIPTOR_TEST_SOURCE
    "${FRUITY_MPHREAD_ROOT}/Testing/TestVulkanDescriptorAllocator.cpp")
set(FRUITY_VULKAN_UPLOAD_TEST_SOURCE
    "${FRUITY_MPHREAD_ROOT}/Testing/TestVulkanUploadArena.cpp")
set(FRUITY_VULKAN_PIPELINE_CACHE_TEST_SOURCE
    "${FRUITY_MPHREAD_ROOT}/Testing/TestVulkanPipelineCache.cpp")
set(FRUITY_VULKAN_FEATURE_PROBE_TEST_SOURCE
    "${FRUITY_MPHREAD_ROOT}/Testing/TestVulkanFeatureProbe.cpp")
set(FRUITY_VULKAN_COMMAND_SLOTS_TEST_SOURCE
    "${FRUITY_MPHREAD_ROOT}/Testing/TestVulkanCommandSlots.cpp")
set(FRUITY_VULKAN_FRAME_SLOTS_TEST_SOURCE
    "${FRUITY_MPHREAD_ROOT}/Testing/TestVulkanFrameSlots.cpp")
set(FRUITY_VULKAN_FRAME_SCHEDULER_TEST_SOURCE
    "${FRUITY_MPHREAD_ROOT}/Testing/TestVulkanFrameScheduler.cpp")
set(FRUITY_VULKAN_SYNCHRONIZATION_TEST_SOURCE
    "${FRUITY_MPHREAD_ROOT}/Testing/TestVulkanSynchronization.cpp")
set(FRUITY_VULKAN_RGB_TRANSFER_TEST_SOURCE
    "${FRUITY_MPHREAD_ROOT}/Testing/TestVulkanRgbTransfer.cpp")
set(FRUITY_VULKAN_RESOURCES_TEST_SOURCE
    "${FRUITY_MPHREAD_ROOT}/Testing/TestVulkanResources.cpp")
set(FRUITY_SHADER_INTERFACE_TEST_SOURCE
    "${FRUITY_MPHREAD_ROOT}/Testing/TestShaderInterface.cpp")
set(FRUITY_GOLDEN_CAPTURE_VALIDATION_TEST_SOURCE
    "${FRUITY_MPHREAD_ROOT}/Testing/TestGoldenCaptureValidation.cpp")
set(FRUITY_GOLDEN_CAPTURE_CPP_SOURCE
    "${FRUITY_MPHREAD_ROOT}/Mods/Render/GoldenCapture.cpp")
set(FRUITY_GOLDEN_CAPTURE_HEADER_SOURCE
    "${FRUITY_MPHREAD_ROOT}/Mods/Render/GoldenCapture.hpp")
set(FRUITY_GOLDEN_CAPTURE_VALIDATION_SOURCE
    "${FRUITY_MPHREAD_ROOT}/Mods/Render/GoldenCaptureValidation.hpp")
set(FRUITY_GOLDEN_CAPTURE_PROVENANCE_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(FRUITY_GOLDEN_CAPTURE_PROVENANCE_HEADER
    "${FRUITY_GOLDEN_CAPTURE_PROVENANCE_DIR}/GoldenCaptureProvenance.generated.hpp")
set_source_files_properties(
    "${FRUITY_GOLDEN_CAPTURE_PROVENANCE_HEADER}"
    PROPERTIES GENERATED TRUE)
# This target deliberately runs for every native build so source_commit cannot
# go stale when an incremental worktree changes commits without changing the
# harness files. The generator only rewrites the header when provenance changes.
find_package(Git QUIET)
add_custom_target(fruity_golden_capture_provenance
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DGOLDEN_CAPTURE_CPP=${FRUITY_GOLDEN_CAPTURE_CPP_SOURCE}"
        "-DGOLDEN_CAPTURE_HEADER=${FRUITY_GOLDEN_CAPTURE_HEADER_SOURCE}"
        "-DGOLDEN_CAPTURE_VALIDATION=${FRUITY_GOLDEN_CAPTURE_VALIDATION_SOURCE}"
        "-DOUTPUT_FILE=${FRUITY_GOLDEN_CAPTURE_PROVENANCE_HEADER}"
        "-DGOLDEN_CAPTURE_GIT_EXECUTABLE=${GIT_EXECUTABLE}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateGoldenCaptureProvenance.cmake"
    BYPRODUCTS "${FRUITY_GOLDEN_CAPTURE_PROVENANCE_HEADER}"
    DEPENDS
        "${FRUITY_GOLDEN_CAPTURE_CPP_SOURCE}"
        "${FRUITY_GOLDEN_CAPTURE_HEADER_SOURCE}"
        "${FRUITY_GOLDEN_CAPTURE_VALIDATION_SOURCE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateGoldenCaptureProvenance.cmake"
    COMMENT "Refreshing GoldenCapture source provenance"
    VERBATIM)
list(REMOVE_ITEM MPHREAD_NATIVE_SOURCES
    "${FRUITY_MPHREAD_ROOT}/Testing/TestVulkanNvidiaReflex.cpp"
    "${FRUITY_VULKAN_FRAME_SLOTS_TEST_SOURCE}"
    "${FRUITY_VULKAN_FRAME_SCHEDULER_TEST_SOURCE}"
    "${FRUITY_VULKAN_SYNCHRONIZATION_TEST_SOURCE}"
    "${FRUITY_VULKAN_RGB_TRANSFER_TEST_SOURCE}"
    "${FRUITY_VULKAN_RESOURCES_TEST_SOURCE}"
    "${FRUITY_MPHREAD_ROOT}/Testing/TestVulkanErrors.cpp"
    "${FRUITY_MPHREAD_ROOT}/Testing/TestReadback.cpp"
    "${FRUITY_RENDERER_GEOMETRY_TEST_SOURCE}"
    "${FRUITY_RENDERER_GPU_MESH_TEST_SOURCE}"
    "${FRUITY_SHADER_INTERFACE_TEST_SOURCE}"
    "${FRUITY_RHI_LIFETIME_TEST_SOURCE}"
    "${FRUITY_MEMORY_ADMISSION_TEST_SOURCE}"
    "${FRUITY_VULKAN_PIPELINE_CACHE_TEST_SOURCE}"
    "${FRUITY_VULKAN_FEATURE_PROBE_TEST_SOURCE}"
    "${FRUITY_VULKAN_COMMAND_SLOTS_TEST_SOURCE}"
    "${FRUITY_VULKAN_DESCRIPTOR_TEST_SOURCE}"
    "${FRUITY_VULKAN_UPLOAD_TEST_SOURCE}"
    "${FRUITY_VULKAN_UNIFORMS_TEST_SOURCE}"
    "${FRUITY_GOLDEN_CAPTURE_VALIDATION_TEST_SOURCE}")

# The window, event loop and menus. "avalonia" is the Skia-drawn Avalonia port
# over GLFW; "qt" is a QWindow with Qt Quick menus rendered into a texture the
# game composites (MphRead.Native.Qt). Both keep the renderer's own frame loop.
set(FRUITY_UI "avalonia" CACHE STRING "Window and menu toolkit: avalonia or qt")
set_property(CACHE FRUITY_UI PROPERTY STRINGS avalonia qt)
set(FRUITY_QT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src/MphRead.Native.Qt")
if(FRUITY_UI STREQUAL "qt" AND NOT ANDROID)
    # Avalonia's asset resolver is plain file lookup the game uses too.
    list(FILTER MPHREAD_NATIVE_SOURCES EXCLUDE REGEX "/NativeRuntime/Avalonia/(Base|Controls|Media|Panels|Scroll|Text|Threading|TopLevel)\\.cpp$")
    list(FILTER MPHREAD_NATIVE_SOURCES EXCLUDE REGEX "/NativeRuntime/Skia/.*\\.cpp$")
    list(FILTER MPHREAD_NATIVE_SOURCES EXCLUDE REGEX "/NativeRuntime/OpenTK/RendererPlatform\\.cpp$")
    list(FILTER MPHREAD_NATIVE_SOURCES EXCLUDE REGEX "/Mods/Launcher/Gui/.*\\.cpp$")
    # Except the address-to-country table, which draws nothing.
    list(APPEND MPHREAD_NATIVE_SOURCES "${FRUITY_MPHREAD_ROOT}/Mods/Launcher/Gui/GeoCountry.cpp")
    # The front screen's photograph is a QML Image.
    list(FILTER MPHREAD_NATIVE_SOURCES EXCLUDE REGEX "/Mods/Render/LauncherPhoto\\.cpp$")
elseif(NOT FRUITY_UI STREQUAL "avalonia")
    message(FATAL_ERROR "FRUITY_UI must be avalonia or qt, not '${FRUITY_UI}'")
endif()

# Android owns the Activity/View/EGL shell under MphRead.Native.Android, but
# the launcher controls and their CPU Skia/Avalonia implementation are shared
# C++ code and must be present in the final .so. Only the desktop window/Ganesh
# pieces stay out of the Android common archive.
if(ANDROID)
    list(FILTER MPHREAD_NATIVE_SOURCES EXCLUDE REGEX "/NativeRuntime/Gui/.*\\.cpp$")
    list(FILTER MPHREAD_NATIVE_SOURCES EXCLUDE REGEX "/NativeRuntime/Skia/SkiaGpu\\.cpp$")
    list(FILTER MPHREAD_NATIVE_SOURCES EXCLUDE REGEX "/NativeRuntime/OpenTK/RendererPlatform\\.cpp$")
    list(FILTER MPHREAD_NATIVE_SOURCES EXCLUDE REGEX "/NativeRuntime/OpenTK/GL\\.cpp$")
    list(FILTER MPHREAD_NATIVE_SOURCES EXCLUDE REGEX "/NativeRuntime/OpenTK/GpuTrace\\.cpp$")
endif()

fruity_report_owner("NcsfPlay.Native" NCSFPLAY_NATIVE_SOURCES NCSFPLAY_NATIVE_HEADERS)
fruity_report_owner("MphRead.Native" MPHREAD_NATIVE_SOURCES MPHREAD_NATIVE_HEADERS)

add_library(fruity_ncsfplay_native STATIC)
add_library(FruityPrime::NcsfPlay.Native ALIAS fruity_ncsfplay_native)
target_sources(fruity_ncsfplay_native PRIVATE ${NCSFPLAY_NATIVE_SOURCES} ${NCSFPLAY_NATIVE_HEADERS})
target_include_directories(fruity_ncsfplay_native PUBLIC "${FRUITY_NCSFPLAY_ROOT}")
target_compile_features(fruity_ncsfplay_native PUBLIC cxx_std_20)
if(FRUITY_STOP_TOKEN_OPTIONS)
    target_compile_options(fruity_ncsfplay_native PUBLIC ${FRUITY_STOP_TOKEN_OPTIONS})
    target_link_options(fruity_ncsfplay_native PUBLIC ${FRUITY_STOP_TOKEN_OPTIONS})
endif()
target_link_libraries(fruity_ncsfplay_native PUBLIC Threads::Threads ZLIB::ZLIB)
set_target_properties(fruity_ncsfplay_native PROPERTIES OUTPUT_NAME "NcsfPlay.Native")

# TagList's invariant collation: Windows and Apple ask the system, Android asks
# the JVM, and everywhere else it is ICU's -- as it is for .NET on Linux.
if(NOT WIN32 AND NOT APPLE AND NOT ANDROID)
    # Static ICU from the Skia vcpkg prefix must be ordered by dependency:
    # i18n -> uc -> data. Linking uc before i18n leaves ICU common symbols
    # unresolved with GNU ld.
    find_package(ICU REQUIRED COMPONENTS i18n uc data)
    target_link_libraries(fruity_ncsfplay_native PUBLIC
        ICU::i18n
        ICU::uc
        ICU::data)
endif()

add_library(fruity_mphread_native STATIC)
option(FRUITY_REQUIRE_VULKAN "Require desktop Vulkan development files" OFF)
option(FRUITY_REQUIRE_SKIA_VULKAN "Require a Skia with Ganesh Vulkan, so the launcher can draw into a Vulkan window" OFF)
if(NOT ANDROID)
    if(FRUITY_REQUIRE_VULKAN)
        find_package(Vulkan 1.3 REQUIRED)
    else()
        find_package(Vulkan 1.3 QUIET)
    endif()
    if(Vulkan_FOUND)
        find_path(FRUITY_VMA_INCLUDE_DIR NAMES vk_mem_alloc.h PATH_SUFFIXES vma)
        if(FRUITY_VMA_INCLUDE_DIR)
            include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FruityVulkanShaders.cmake")
            target_compile_definitions(fruity_mphread_native PRIVATE
                FRUITY_HAS_VULKAN
                VMA_STATIC_VULKAN_FUNCTIONS=0
                VMA_DYNAMIC_VULKAN_FUNCTIONS=1)
            target_include_directories(fruity_mphread_native PRIVATE
                ${Vulkan_INCLUDE_DIRS}
                "${FRUITY_VMA_INCLUDE_DIR}")
        elseif(FRUITY_REQUIRE_VULKAN)
            message(FATAL_ERROR
                "Desktop Vulkan was required, but Vulkan Memory Allocator (vk_mem_alloc.h) was not found.")
        else()
            message(STATUS "Vulkan SDK found but VMA is missing; disabling the optional Vulkan backend")
        endif()
    endif()
else()
    # The same Vulkan backend on Android: the NDK's own headers, VMA from
    # vcpkg, the loader from libvulkan.so at run time and the NDK's glslc.
    find_path(FRUITY_ANDROID_VULKAN_INCLUDE_DIR NAMES vulkan/vulkan.h)
    find_path(FRUITY_VMA_INCLUDE_DIR NAMES vk_mem_alloc.h PATH_SUFFIXES vma)
    if(FRUITY_ANDROID_VULKAN_INCLUDE_DIR AND FRUITY_VMA_INCLUDE_DIR)
        include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FruityVulkanShaders.cmake")
        target_compile_definitions(fruity_mphread_native PRIVATE
            FRUITY_HAS_VULKAN
            VMA_STATIC_VULKAN_FUNCTIONS=0
            VMA_DYNAMIC_VULKAN_FUNCTIONS=1)
        target_include_directories(fruity_mphread_native PRIVATE "${FRUITY_VMA_INCLUDE_DIR}")
        message(STATUS "Android Vulkan backend: on")
    else()
        message(STATUS "Android Vulkan backend: off (Vulkan headers or VMA not found)")
    endif()
endif()
add_library(FruityPrime::MphRead.Native ALIAS fruity_mphread_native)
target_sources(fruity_mphread_native PRIVATE
    ${MPHREAD_NATIVE_SOURCES}
    ${MPHREAD_NATIVE_HEADERS}
    "${FRUITY_GOLDEN_CAPTURE_PROVENANCE_HEADER}")
target_include_directories(fruity_mphread_native
    PUBLIC "${FRUITY_MPHREAD_ROOT}"
    PRIVATE "${FRUITY_GOLDEN_CAPTURE_PROVENANCE_DIR}")
add_dependencies(fruity_mphread_native fruity_golden_capture_provenance)
target_compile_features(fruity_mphread_native PUBLIC cxx_std_20)
if(FRUITY_STOP_TOKEN_OPTIONS)
    target_compile_options(fruity_mphread_native PUBLIC ${FRUITY_STOP_TOKEN_OPTIONS})
    target_link_options(fruity_mphread_native PUBLIC ${FRUITY_STOP_TOKEN_OPTIONS})
endif()
target_link_libraries(fruity_mphread_native PUBLIC fruity_ncsfplay_native Threads::Threads ZLIB::ZLIB)
# The C# JIT never fuses a * b + c into one fused multiply-add, so neither may
# the port: a fused result rounds once where the C# rounds twice, and every
# transform, collision test and inverse would drift in the last bit. GCC in
# ISO mode and MSVC already keep them apart; Clang (macOS, the Android NDK)
# contracts by default, and on arm64, where FMA is always there, it does.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(fruity_mphread_native PUBLIC -ffp-contract=off)
endif()
set_target_properties(fruity_mphread_native PROPERTIES OUTPUT_NAME "MphRead.Native")

# Phase 3's geometry decoder is deliberately pure CPU code. Build and run its
# focused tests whenever a host native owner is built; Android cross-compiles
# the production decoder but cannot execute the host test binary.
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(fruity_renderer_geometry_tests
        "${FRUITY_RENDERER_GEOMETRY_TEST_SOURCE}"
        "${FRUITY_MPHREAD_ROOT}/RendererGeometry.cpp")
    target_include_directories(fruity_renderer_geometry_tests PRIVATE "${FRUITY_MPHREAD_ROOT}")
    target_compile_features(fruity_renderer_geometry_tests PRIVATE cxx_std_20)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(fruity_renderer_geometry_tests PRIVATE -ffp-contract=off)
    endif()

    add_test(NAME FruityPrime.RendererGeometry COMMAND fruity_renderer_geometry_tests)
    add_custom_target(fruity_renderer_geometry_check
        COMMAND "$<TARGET_FILE:fruity_renderer_geometry_tests>"
        DEPENDS fruity_renderer_geometry_tests
        COMMENT "Running native renderer geometry decoder tests"
        VERBATIM)

    add_executable(fruity_renderer_gpu_mesh_tests
        "${FRUITY_RENDERER_GPU_MESH_TEST_SOURCE}"
        "${FRUITY_MPHREAD_ROOT}/RendererGpuMesh.cpp")
    target_include_directories(fruity_renderer_gpu_mesh_tests PRIVATE "${FRUITY_MPHREAD_ROOT}")
    target_compile_features(fruity_renderer_gpu_mesh_tests PRIVATE cxx_std_20)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(fruity_renderer_gpu_mesh_tests PRIVATE -ffp-contract=off)
    endif()

    add_test(NAME FruityPrime.RendererGpuMesh COMMAND fruity_renderer_gpu_mesh_tests)
    add_custom_target(fruity_renderer_gpu_mesh_check
        COMMAND "$<TARGET_FILE:fruity_renderer_gpu_mesh_tests>"
        DEPENDS fruity_renderer_gpu_mesh_tests
        COMMENT "Running native renderer GPU mesh cache tests"
        VERBATIM)

    # Phase 5's shader interface: one vertex-semantic table, desktop shaders
    # that read only explicit inputs, and the Vulkan interface's std140 packing.
    add_executable(fruity_shader_interface_tests
        "${FRUITY_SHADER_INTERFACE_TEST_SOURCE}"
        "${FRUITY_MPHREAD_ROOT}/Shaders.cpp")
    target_include_directories(fruity_shader_interface_tests PRIVATE "${FRUITY_MPHREAD_ROOT}")
    target_compile_features(fruity_shader_interface_tests PRIVATE cxx_std_20)
    add_test(NAME FruityPrime.ShaderInterface COMMAND fruity_shader_interface_tests)
    add_custom_target(fruity_shader_interface_check
        COMMAND "$<TARGET_FILE:fruity_shader_interface_tests>"
        DEPENDS fruity_shader_interface_tests
        COMMENT "Running native shader interface tests"
        VERBATIM)

    # Phase 10's GPU lifetime contract: retirement, frames in flight, idle.
    add_executable(fruity_rhi_lifetime_tests "${FRUITY_RHI_LIFETIME_TEST_SOURCE}")
    target_include_directories(fruity_rhi_lifetime_tests PRIVATE "${FRUITY_MPHREAD_ROOT}")
    target_compile_features(fruity_rhi_lifetime_tests PRIVATE cxx_std_20)
    add_test(NAME FruityPrime.RhiLifetime COMMAND fruity_rhi_lifetime_tests)
    add_executable(fruity_memory_admission_tests "${FRUITY_MEMORY_ADMISSION_TEST_SOURCE}"
        "${FRUITY_MPHREAD_ROOT}/NativeRuntime/Rhi/OpenGL/OpenGlMemory.cpp")
    target_compile_features(fruity_memory_admission_tests PRIVATE cxx_std_20)
    add_test(NAME FruityPrime.MemoryAdmission COMMAND fruity_memory_admission_tests)
    add_executable(fruity_readback_tests "${FRUITY_MPHREAD_ROOT}/Testing/TestReadback.cpp"
        "${FRUITY_MPHREAD_ROOT}/NativeRuntime/Rhi/Readback.cpp")
    target_compile_features(fruity_readback_tests PRIVATE cxx_std_20)
    target_link_libraries(fruity_readback_tests PRIVATE Threads::Threads)
    add_test(NAME FruityPrime.Readback COMMAND fruity_readback_tests)

    if(Vulkan_FOUND)
        add_executable(fruity_vulkan_error_tests "${FRUITY_MPHREAD_ROOT}/Testing/TestVulkanErrors.cpp")
        target_compile_features(fruity_vulkan_error_tests PRIVATE cxx_std_20)
        target_link_libraries(fruity_vulkan_error_tests PRIVATE Vulkan::Vulkan)
        add_test(NAME FruityPrime.VulkanErrors COMMAND fruity_vulkan_error_tests)
        add_executable(fruity_vulkan_uniform_tests "${FRUITY_VULKAN_UNIFORMS_TEST_SOURCE}")
        target_include_directories(fruity_vulkan_uniform_tests PRIVATE "${FRUITY_MPHREAD_ROOT}" "${FRUITY_VULKAN_SHADER_DIR}")
        target_compile_features(fruity_vulkan_uniform_tests PRIVATE cxx_std_20)
        add_dependencies(fruity_vulkan_uniform_tests fruity_vulkan_shaders)
        add_test(NAME FruityPrime.VulkanSceneUniforms COMMAND fruity_vulkan_uniform_tests)
        add_test(NAME FruityPrime.SceneShaderAbiDrift COMMAND Python3::Interpreter
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/test-scene-shader-abi.py" --directory "${FRUITY_VULKAN_SHADER_DIR}")

        add_executable(fruity_vulkan_descriptor_tests
            "${FRUITY_VULKAN_DESCRIPTOR_TEST_SOURCE}"
            "${FRUITY_MPHREAD_ROOT}/NativeRuntime/Rhi/Vulkan/VulkanDescriptorAllocator.cpp")
        target_compile_definitions(fruity_vulkan_descriptor_tests PRIVATE FRUITY_HAS_VULKAN)
        target_include_directories(fruity_vulkan_descriptor_tests PRIVATE ${Vulkan_INCLUDE_DIRS})
        target_compile_features(fruity_vulkan_descriptor_tests PRIVATE cxx_std_20)
        add_test(NAME FruityPrime.VulkanDescriptorAllocator COMMAND fruity_vulkan_descriptor_tests)

        add_executable(fruity_vulkan_upload_tests
            "${FRUITY_VULKAN_UPLOAD_TEST_SOURCE}"
            "${FRUITY_MPHREAD_ROOT}/NativeRuntime/Rhi/Vulkan/VulkanUploadArena.cpp")
        target_compile_definitions(fruity_vulkan_upload_tests PRIVATE FRUITY_HAS_VULKAN)
        target_include_directories(fruity_vulkan_upload_tests PRIVATE ${Vulkan_INCLUDE_DIRS})
        target_compile_features(fruity_vulkan_upload_tests PRIVATE cxx_std_20)
        add_test(NAME FruityPrime.VulkanUploadArena COMMAND fruity_vulkan_upload_tests)

        add_executable(fruity_vulkan_pipeline_cache_tests
            "${FRUITY_VULKAN_PIPELINE_CACHE_TEST_SOURCE}"
            "${FRUITY_MPHREAD_ROOT}/NativeRuntime/Rhi/Vulkan/VulkanPipelineCache.cpp")
        target_compile_definitions(fruity_vulkan_pipeline_cache_tests PRIVATE FRUITY_HAS_VULKAN)
        target_include_directories(fruity_vulkan_pipeline_cache_tests PRIVATE ${Vulkan_INCLUDE_DIRS})
        target_compile_features(fruity_vulkan_pipeline_cache_tests PRIVATE cxx_std_20)
        target_link_libraries(fruity_vulkan_pipeline_cache_tests PRIVATE Threads::Threads)
        add_test(NAME FruityPrime.VulkanPipelineCache COMMAND fruity_vulkan_pipeline_cache_tests)

        add_executable(fruity_vulkan_feature_probe_tests
            "${FRUITY_VULKAN_FEATURE_PROBE_TEST_SOURCE}"
            "${FRUITY_MPHREAD_ROOT}/NativeRuntime/Rhi/Vulkan/VulkanFeatureProbe.cpp")
        target_compile_definitions(fruity_vulkan_feature_probe_tests PRIVATE FRUITY_HAS_VULKAN)
        target_include_directories(fruity_vulkan_feature_probe_tests PRIVATE ${Vulkan_INCLUDE_DIRS})
        target_compile_features(fruity_vulkan_feature_probe_tests PRIVATE cxx_std_20)
        add_test(NAME FruityPrime.VulkanFeatureProbe COMMAND fruity_vulkan_feature_probe_tests)
        add_executable(fruity_vulkan_reflex_tests
            "${FRUITY_MPHREAD_ROOT}/Testing/TestVulkanNvidiaReflex.cpp"
            "${FRUITY_MPHREAD_ROOT}/NativeRuntime/Rhi/Vulkan/VulkanNvidiaReflex.cpp")
        target_compile_definitions(fruity_vulkan_reflex_tests PRIVATE FRUITY_HAS_VULKAN)
        target_include_directories(fruity_vulkan_reflex_tests PRIVATE ${Vulkan_INCLUDE_DIRS})
        target_compile_features(fruity_vulkan_reflex_tests PRIVATE cxx_std_20)
        add_test(NAME FruityPrime.VulkanNvidiaReflex COMMAND fruity_vulkan_reflex_tests)


        add_executable(fruity_vulkan_command_slots_tests
            "${FRUITY_VULKAN_COMMAND_SLOTS_TEST_SOURCE}"
            "${FRUITY_MPHREAD_ROOT}/NativeRuntime/Rhi/Vulkan/VulkanCommandSlots.cpp"
            "${FRUITY_MPHREAD_ROOT}/NativeRuntime/Rhi/Vulkan/VulkanTransferScratch.cpp"
            "${FRUITY_MPHREAD_ROOT}/NativeRuntime/Rhi/Vulkan/VulkanDescriptorAllocator.cpp"
            "${FRUITY_MPHREAD_ROOT}/NativeRuntime/Rhi/Vulkan/VulkanUploadArena.cpp")
        target_compile_definitions(fruity_vulkan_command_slots_tests PRIVATE FRUITY_HAS_VULKAN)
        target_include_directories(fruity_vulkan_command_slots_tests PRIVATE ${Vulkan_INCLUDE_DIRS})
        target_compile_features(fruity_vulkan_command_slots_tests PRIVATE cxx_std_20)
        add_test(NAME FruityPrime.VulkanCommandSlots COMMAND fruity_vulkan_command_slots_tests)
        add_executable(fruity_vulkan_frame_slots_tests
            "${FRUITY_VULKAN_FRAME_SLOTS_TEST_SOURCE}"
            "${FRUITY_MPHREAD_ROOT}/NativeRuntime/Rhi/Vulkan/VulkanFrameSlots.cpp"
            "${FRUITY_MPHREAD_ROOT}/NativeRuntime/Rhi/Vulkan/VulkanDescriptorAllocator.cpp")
        target_compile_definitions(fruity_vulkan_frame_slots_tests PRIVATE FRUITY_HAS_VULKAN)
        target_include_directories(fruity_vulkan_frame_slots_tests PRIVATE ${Vulkan_INCLUDE_DIRS})
        target_compile_features(fruity_vulkan_frame_slots_tests PRIVATE cxx_std_20)
        add_test(NAME FruityPrime.VulkanFrameSlots COMMAND fruity_vulkan_frame_slots_tests)
        add_executable(fruity_vulkan_frame_scheduler_tests "${FRUITY_VULKAN_FRAME_SCHEDULER_TEST_SOURCE}")
        target_include_directories(fruity_vulkan_frame_scheduler_tests PRIVATE ${Vulkan_INCLUDE_DIRS})
        target_compile_features(fruity_vulkan_frame_scheduler_tests PRIVATE cxx_std_20)
        add_test(NAME FruityPrime.VulkanFrameScheduler COMMAND fruity_vulkan_frame_scheduler_tests)
        add_executable(fruity_vulkan_synchronization_tests
            "${FRUITY_VULKAN_SYNCHRONIZATION_TEST_SOURCE}"
            "${FRUITY_MPHREAD_ROOT}/NativeRuntime/Rhi/Vulkan/VulkanSynchronization.cpp")
        target_compile_definitions(fruity_vulkan_synchronization_tests PRIVATE FRUITY_HAS_VULKAN)
        target_include_directories(fruity_vulkan_synchronization_tests PRIVATE ${Vulkan_INCLUDE_DIRS})
        target_compile_features(fruity_vulkan_synchronization_tests PRIVATE cxx_std_20)
        add_test(NAME FruityPrime.VulkanSynchronization COMMAND fruity_vulkan_synchronization_tests)
        add_executable(fruity_vulkan_rgb_transfer_tests
            "${FRUITY_VULKAN_RGB_TRANSFER_TEST_SOURCE}"
            "${FRUITY_MPHREAD_ROOT}/NativeRuntime/Rhi/Vulkan/VulkanRgbTransfer.cpp"
            "${FRUITY_MPHREAD_ROOT}/NativeRuntime/Rhi/Vulkan/VulkanTransferScratch.cpp")
        target_compile_definitions(fruity_vulkan_rgb_transfer_tests PRIVATE FRUITY_HAS_VULKAN)
        target_include_directories(fruity_vulkan_rgb_transfer_tests PRIVATE ${Vulkan_INCLUDE_DIRS})
        target_compile_features(fruity_vulkan_rgb_transfer_tests PRIVATE cxx_std_20)
        add_test(NAME FruityPrime.VulkanRgbTransfer COMMAND fruity_vulkan_rgb_transfer_tests)
        if(FRUITY_VMA_INCLUDE_DIR)
            add_executable(fruity_vulkan_resources_tests
                "${FRUITY_VULKAN_RESOURCES_TEST_SOURCE}"
                "${FRUITY_MPHREAD_ROOT}/NativeRuntime/Rhi/Vulkan/VulkanResources.cpp")
            target_compile_definitions(fruity_vulkan_resources_tests PRIVATE FRUITY_HAS_VULKAN)
            target_include_directories(fruity_vulkan_resources_tests PRIVATE ${Vulkan_INCLUDE_DIRS} "${FRUITY_VMA_INCLUDE_DIR}")
            target_compile_features(fruity_vulkan_resources_tests PRIVATE cxx_std_20)
            add_test(NAME FruityPrime.VulkanResources COMMAND fruity_vulkan_resources_tests)
        endif()
    endif()

    add_executable(fruity_golden_capture_validation_tests
        "${FRUITY_GOLDEN_CAPTURE_VALIDATION_TEST_SOURCE}")
    target_include_directories(fruity_golden_capture_validation_tests PRIVATE "${FRUITY_MPHREAD_ROOT}")
    target_compile_features(fruity_golden_capture_validation_tests PRIVATE cxx_std_20)
    if(FRUITY_GOLDEN_PARITY_ADAPTER)
        target_compile_definitions(
            fruity_golden_capture_validation_tests
            PRIVATE FRUITY_GOLDEN_PARITY_ADAPTER_BUILD=1)
    endif()
    add_test(
        NAME FruityPrime.GoldenCaptureValidation
        COMMAND fruity_golden_capture_validation_tests)
    add_custom_target(fruity_golden_capture_validation_check
        COMMAND "$<TARGET_FILE:fruity_golden_capture_validation_tests>"
        DEPENDS fruity_golden_capture_validation_tests
        COMMENT "Running golden capture validation tests"
        VERBATIM)

    add_dependencies(fruity_mphread_native
        fruity_renderer_geometry_check
        fruity_renderer_gpu_mesh_check
        fruity_golden_capture_validation_check)
endif()

# Both native heads compile the launcher's in-tree Avalonia-compatible controls
# unless the desktop build picks the Qt menus. MPHREAD_SHELL is the desktop
# one-window shell, whichever toolkit draws it; MPHREAD_AVALONIA_SHELL is that
# shell drawn by the Avalonia port. Android supplies its own Activity/View stack.
if(ANDROID)
    target_compile_definitions(fruity_mphread_native PUBLIC MPHREAD_AVALONIA)
elseif(FRUITY_UI STREQUAL "qt")
    target_compile_definitions(fruity_mphread_native PUBLIC MPHREAD_QT MPHREAD_SHELL)
else()
    target_compile_definitions(fruity_mphread_native PUBLIC MPHREAD_AVALONIA MPHREAD_SHELL MPHREAD_AVALONIA_SHELL)
endif()

if(WIN32)
    target_compile_definitions(fruity_mphread_native PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    # The launcher's file picker is the platform's own common dialog.
    target_link_libraries(fruity_mphread_native PUBLIC ws2_32 bcrypt comdlg32 iphlpapi)
endif()

# The renderer's GL entry points and the sound mixer's OpenAL ones, which the
# C# build gets from OpenTK's own native dependencies.
if(NOT ANDROID)
    find_package(OpenGL REQUIRED)
    target_link_libraries(fruity_mphread_native PUBLIC OpenGL::GL)

    # The native launcher is rendered by real Skia Ganesh into the current
    # OpenGL context. vcpkg exports unofficial::skia::skia, while MSYS2 ships
    # Skia through pkg-config as skia.pc.
    if(FRUITY_UI STREQUAL "qt")
        # Qt Quick draws the menus and decodes the images; no Skia.
    else()
    find_package(unofficial-skia CONFIG QUIET)
    if(TARGET unofficial::skia::skia)
        target_link_libraries(fruity_mphread_native PUBLIC unofficial::skia::skia)
        # Ganesh Vulkan draws the launcher into a window that presents through
        # Vulkan. Only a Skia built with skia_use_vulkan exports SK_VULKAN; a
        # header set alone is not taken as support.
        get_target_property(_fruity_skia_definitions unofficial::skia::skia INTERFACE_COMPILE_DEFINITIONS)
        if(FRUITY_VMA_INCLUDE_DIR AND Vulkan_FOUND AND "${_fruity_skia_definitions}" MATCHES "SK_VULKAN")
            target_compile_definitions(fruity_mphread_native PRIVATE FRUITY_SKIA_VULKAN)
            set(FRUITY_HAS_SKIA_VULKAN ON)
            message(STATUS "Skia Ganesh Vulkan: available for the Vulkan window's launcher")
        else()
            message(STATUS "Skia Ganesh Vulkan: not in this Skia; a Vulkan window cannot draw the launcher")
        endif()
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(FRUITY_SKIA QUIET IMPORTED_TARGET skia)
        set(FRUITY_SKIA_TARGET "")
        if(TARGET PkgConfig::FRUITY_SKIA)
            set(FRUITY_SKIA_TARGET PkgConfig::FRUITY_SKIA)
        endif()

        # build-cpp.bat is intentionally local/untracked in some developer
        # checkouts. Make a fresh MSYS2 checkout self-healing instead of
        # requiring that helper to know every new native dependency.
        if(MINGW AND NOT FRUITY_SKIA_TARGET)
            get_filename_component(_fruity_mingw_bin "${CMAKE_COMMAND}" DIRECTORY)
            get_filename_component(_fruity_mingw_prefix "${_fruity_mingw_bin}" DIRECTORY)
            get_filename_component(_fruity_msys_root "${_fruity_mingw_prefix}" DIRECTORY)
            find_program(FRUITY_PACMAN_EXECUTABLE
                NAMES pacman.exe pacman
                HINTS "${_fruity_msys_root}/usr/bin" "C:/msys64/usr/bin")
            if(FRUITY_PACMAN_EXECUTABLE)
                message(STATUS "Skia not found; installing mingw-w64-x86_64-skia with MSYS2 pacman")
                execute_process(
                    COMMAND "${FRUITY_PACMAN_EXECUTABLE}" -S --needed --noconfirm mingw-w64-x86_64-skia
                    RESULT_VARIABLE FRUITY_SKIA_INSTALL_RESULT)
                if(FRUITY_SKIA_INSTALL_RESULT EQUAL 0)
                    # Use a fresh prefix so FindPkgConfig cannot reuse the
                    # negative result from before pacman installed Skia.
                    pkg_check_modules(FRUITY_SKIA_INSTALLED REQUIRED IMPORTED_TARGET skia)
                    set(FRUITY_SKIA_TARGET PkgConfig::FRUITY_SKIA_INSTALLED)
                endif()
            endif()
        endif()

        if(NOT FRUITY_SKIA_TARGET)
            message(FATAL_ERROR
                "Skia Ganesh was not found. With MSYS2 install "
                "mingw-w64-x86_64-skia; with vcpkg install skia[gl,freetype].")
        endif()
        target_link_libraries(fruity_mphread_native PUBLIC ${FRUITY_SKIA_TARGET})
    endif()
    if(FRUITY_REQUIRE_SKIA_VULKAN AND NOT FRUITY_HAS_SKIA_VULKAN)
        # A build whose launcher cannot draw into a Vulkan window is a build
        # in which -rhi vulkan always fails; refuse it here, not at run time.
        message(FATAL_ERROR
            "A Skia with Ganesh Vulkan is required (FRUITY_REQUIRE_SKIA_VULKAN), and this one has none. "
            "MSYS2's mingw-w64 Skia is built without Vulkan: build with MSVC and vcpkg skia[gl,freetype,vulkan] "
            "(tools/build/build-cpp.bat msvc deps).")
    endif()

    # The launcher's text, which Avalonia rasterises on the C# side.
    find_package(Freetype REQUIRED)
    target_link_libraries(fruity_mphread_native PUBLIC Freetype::Freetype)

    # The launcher's photograph is a JPEG, which Skia decodes on the C# side.
    find_package(JPEG REQUIRED)
    target_link_libraries(fruity_mphread_native PUBLIC JPEG::JPEG)
    endif()

    # The window (avalonia) and, for now, the desktop gamepads (both).
    find_package(glfw3 CONFIG REQUIRED)
    target_link_libraries(fruity_mphread_native PUBLIC glfw)

    find_library(FRUITY_OPENAL_LIBRARY NAMES openal OpenAL OpenAL32)
    if(FRUITY_OPENAL_LIBRARY)
        # Sfx.cpp calls al*/alc* directly on every desktop platform. macOS
        # therefore needs an actual link dependency too; the runtime dylib
        # search policy is separate from resolving these link-time symbols.
        target_link_libraries(fruity_mphread_native PUBLIC "${FRUITY_OPENAL_LIBRARY}")
    else()
        message(FATAL_ERROR
            "OpenAL was not found. The sound mixer calls it directly, as OpenTK does "
            "in the C# build.")
    endif()
endif()

# FRUITY_UI=qt: the window platform, the shell and its Qt Quick menus. Their
# objects go into the MphRead.Native archive, since the renderer calls the
# shell and the window platform from inside it.
if(FRUITY_UI STREQUAL "qt" AND NOT ANDROID)
    find_package(Qt6 6.9 REQUIRED COMPONENTS Core Gui Qml Quick QuickShapes QuickEffects)
    fruity_collect_owner("${FRUITY_QT_ROOT}" MPHREAD_QT_SOURCES MPHREAD_QT_HEADERS)
    fruity_report_owner("MphRead.Native.Qt" MPHREAD_QT_SOURCES MPHREAD_QT_HEADERS)
    add_library(fruity_mphread_qt OBJECT ${MPHREAD_QT_SOURCES} ${MPHREAD_QT_HEADERS})
    set_target_properties(fruity_mphread_qt PROPERTIES AUTOMOC ON)
    target_compile_features(fruity_mphread_qt PUBLIC cxx_std_20)
    target_compile_definitions(fruity_mphread_qt PRIVATE MPHREAD_QT MPHREAD_SHELL)
    # The menus render through Vulkan too when the renderer does: the same
    # backend definition and headers the RHI is compiled with.
    target_compile_definitions(fruity_mphread_qt PRIVATE
        $<$<IN_LIST:FRUITY_HAS_VULKAN,$<TARGET_PROPERTY:fruity_mphread_native,COMPILE_DEFINITIONS>>:FRUITY_HAS_VULKAN>)
    if(Vulkan_FOUND AND FRUITY_VMA_INCLUDE_DIR)
        target_include_directories(fruity_mphread_qt PRIVATE ${Vulkan_INCLUDE_DIRS} "${FRUITY_VMA_INCLUDE_DIR}")
    endif()
    target_include_directories(fruity_mphread_qt PRIVATE
        "${FRUITY_MPHREAD_ROOT}" "${FRUITY_NCSFPLAY_ROOT}")
    target_link_libraries(fruity_mphread_qt PUBLIC Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
    if(FRUITY_STOP_TOKEN_OPTIONS)
        target_compile_options(fruity_mphread_qt PRIVATE ${FRUITY_STOP_TOKEN_OPTIONS})
    endif()
    target_sources(fruity_mphread_native PRIVATE $<TARGET_OBJECTS:fruity_mphread_qt>)
    target_link_libraries(fruity_mphread_native PUBLIC Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
endif()

# The game binary. Its only source is the entry-point shim; everything else is
# the library above.
if(NOT ANDROID)
    add_executable(fruity_prime "${CMAKE_CURRENT_SOURCE_DIR}/src/FruityPrime/Main.cpp")
    target_link_libraries(fruity_prime PRIVATE fruity_mphread_native)
    if(MSVC AND FRUITY_GOLDEN_PARITY_ADAPTER)
        # Exact MSVC Actions builds compile these two production owners, but
        # link.exe still cannot resolve Weapons::{Current,WeaponsMP} or
        # EntityBase::HandleMessage from MphRead.Native.lib. /WHOLEARCHIVE does
        # not repair that archive boundary and additionally pulls TestMisc.obj,
        # whose LoadPngRgb owner is absent. Keep the production source bytes and
        # native validation intact; bypass only that archive boundary for the
        # runner-owned parity build.
        target_sources(fruity_prime PRIVATE
            "${FRUITY_MPHREAD_ROOT}/Metadata/Weapons.cpp"
            "${FRUITY_MPHREAD_ROOT}/Entities/EntityBase.cpp")
        target_compile_definitions(fruity_prime PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    elseif(MSVC)
        # Preserve the canonical MSVC link policy outside the parity adapter.
        target_link_options(fruity_prime PRIVATE
            "/WHOLEARCHIVE:$<TARGET_FILE:fruity_mphread_native>")
    endif()
    set_target_properties(fruity_prime PROPERTIES OUTPUT_NAME "FruityPrime")

    if(FRUITY_UI STREQUAL "qt")
        # The menus, compiled into the binary (qrc:/qt/qml/FruityPrime/Ui).
        qt_policy(SET QTP0001 NEW)
        qt_policy(SET QTP0004 NEW)
        set_target_properties(fruity_prime PROPERTIES AUTOMOC ON)
        set_source_files_properties(src/MphRead.Native.Qt/qml/Theme.qml
            PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
        file(GLOB FRUITY_QML_FILES CONFIGURE_DEPENDS "${FRUITY_QT_ROOT}/qml/*.qml")
        set(FRUITY_QML_RELATIVE "")
        foreach(_qml IN LISTS FRUITY_QML_FILES)
            file(RELATIVE_PATH _rel "${CMAKE_CURRENT_SOURCE_DIR}" "${_qml}")
            get_filename_component(_name "${_qml}" NAME)
            # Flat in the module, beside its qmldir.
            set_source_files_properties("${_rel}" PROPERTIES QT_RESOURCE_ALIAS "${_name}")
            list(APPEND FRUITY_QML_RELATIVE "${_rel}")
        endforeach()
        set_source_files_properties(src/MphRead/Assets/Backgrounds/launcher-bg.jpg
            PROPERTIES QT_RESOURCE_ALIAS launcher-bg.jpg)
        set(FRUITY_QML_FONTS "")
        foreach(_font IN ITEMS Inter-Variable PixelifySans-Regular PixelifySans-SemiBold PixelifySans-Bold
                JetBrainsMono-Regular JetBrainsMono-Bold heyNovember Roboto-Bold)
            set(_path src/MphRead/Assets/Fonts/${_font}.ttf)
            set_source_files_properties(${_path} PROPERTIES QT_RESOURCE_ALIAS fonts/${_font}.ttf)
            list(APPEND FRUITY_QML_FONTS ${_path})
        endforeach()
        qt_add_qml_module(fruity_prime
            URI FruityPrime.Ui
            VERSION 1.0
            OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/qml/FruityPrime/Ui"
            NO_RESOURCE_TARGET_PATH
            RESOURCE_PREFIX /qt/qml/FruityPrime/Ui
            QML_FILES ${FRUITY_QML_RELATIVE}
            RESOURCES src/MphRead/Assets/Backgrounds/launcher-bg.jpg ${FRUITY_QML_FONTS})
    endif()

    # The launcher's window icon and front-screen logo. The C# build embeds
    # them as Avalonia resources; here they are the files beside the binary
    # that an avares: URI resolves to.
    add_custom_command(TARGET fruity_prime POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${FRUITY_MPHREAD_ROOT}/Assets/fruity-prime-logo.png"
            "${FRUITY_MPHREAD_ROOT}/Assets/fruity-prime-mark.png"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/MphRead/Assets/Backgrounds/launcher-bg.jpg"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/MphRead/Assets/Geo/ipcountry.bin.gz"
            "$<TARGET_FILE_DIR:fruity_prime>"
        VERBATIM)

    if(WIN32)
        # The executable's own icon, the csproj's ApplicationIcon.
        enable_language(RC)
        set(FRUITY_APP_ICON "${FRUITY_MPHREAD_ROOT}/Assets/fruity-prime.ico")
        file(TO_CMAKE_PATH "${FRUITY_APP_ICON}" FRUITY_APP_ICON)
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/FruityPrime.rc"
            "1 ICON \"${FRUITY_APP_ICON}\"\n")
        target_sources(fruity_prime PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/FruityPrime.rc")
    endif()

    if(MINGW)
        # The compiler's own runtime goes into the binary. Nobody running the
        # game has the toolchain installed, and these three DLLs are the ones
        # they would otherwise be missing.
        target_link_options(fruity_prime PRIVATE
            -static-libgcc -static-libstdc++
            -Wl,-Bstatic,--whole-archive -lwinpthread
            -Wl,--no-whole-archive,-Bdynamic)

        # Everything else the binary loads -- GLFW, OpenAL, zlib, curl,
        # libarchive and whatever those need in turn -- is copied next to it,
        # so the build output runs as it stands.
        find_program(FRUITY_OBJDUMP NAMES objdump)
        get_filename_component(FRUITY_TOOLCHAIN_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
        if(FRUITY_OBJDUMP)
            add_custom_command(TARGET fruity_prime POST_BUILD
                COMMAND "${CMAKE_COMMAND}"
                    "-DBINARY=$<TARGET_FILE:fruity_prime>"
                    "-DSEARCH_DIR=${FRUITY_TOOLCHAIN_BIN}"
                    "-DOBJDUMP=${FRUITY_OBJDUMP}"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/build/copy-runtime-dlls.cmake"
                VERBATIM)
        endif()
    endif()
    # The Qt DLLs copied beside the binary make Qt look for its plugins and
    # QML modules beside it too. A build folder points them back at the Qt
    # it was built against; tools/qt/build-windows.sh deploys real copies.
    if(FRUITY_UI STREQUAL "qt" AND WIN32)
        file(GENERATE OUTPUT "$<TARGET_FILE_DIR:fruity_prime>/qt.conf" CONTENT
"[Paths]
Prefix = ${QT6_INSTALL_PREFIX}
Plugins = ${QT6_INSTALL_PLUGINS}
QmlImports = ${QT6_INSTALL_QML}
")
    endif()
endif()

if(CMAKE_DL_LIBS)
    target_link_libraries(fruity_mphread_native PUBLIC "${CMAKE_DL_LIBS}")
endif()

if(ANDROID)
    list(FIND FRUITY_ANDROID_ABIS "${ANDROID_ABI}" _android_abi_index)
    if(_android_abi_index EQUAL -1)
        message(FATAL_ERROR
            "ANDROID_ABI '${ANDROID_ABI}' is not in FRUITY_ANDROID_ABIS: ${FRUITY_ANDROID_ABIS}")
    endif()

    if(ANDROID_PLATFORM MATCHES "^android-([0-9]+)$")
        if(CMAKE_MATCH_1 LESS FRUITY_ANDROID_MIN_API)
            message(FATAL_ERROR
                "ANDROID_PLATFORM ${ANDROID_PLATFORM} is below minimum API ${FRUITY_ANDROID_MIN_API}")
        endif()
    endif()

    # The final shared object owns the complete runtime dependency graph.
    # vcpkg supplies these libraries for the selected Android ABI; unlike the
    # old header-only compile gate this is a real link and therefore catches
    # missing runtime ownership before an APK is produced.
    find_package(CURL REQUIRED)
    find_package(LibArchive REQUIRED)
    find_package(Freetype REQUIRED)
    find_package(JPEG REQUIRED)

    find_library(FRUITY_ANDROID_LOG_LIBRARY log REQUIRED)
    find_library(FRUITY_ANDROID_PLATFORM_LIBRARY android REQUIRED)
    find_library(FRUITY_ANDROID_EGL_LIBRARY EGL REQUIRED)
    find_library(FRUITY_ANDROID_GLESV3_LIBRARY GLESv3 REQUIRED)
    find_library(FRUITY_ANDROID_JNIGRAPHICS_LIBRARY jnigraphics REQUIRED)
    find_library(FRUITY_ANDROID_OPENSL_LIBRARY OpenSLES REQUIRED)

    target_link_libraries(fruity_mphread_native PUBLIC
        CURL::libcurl
        LibArchive::LibArchive
        Freetype::Freetype
        JPEG::JPEG
        "${FRUITY_ANDROID_GLESV3_LIBRARY}"
        "${FRUITY_ANDROID_OPENSL_LIBRARY}")

    fruity_collect_owner("${FRUITY_ANDROID_ROOT}" MPHREAD_ANDROID_SOURCES MPHREAD_ANDROID_HEADERS)
    fruity_report_owner("MphRead.Native.Android" MPHREAD_ANDROID_SOURCES MPHREAD_ANDROID_HEADERS)

    add_library(fruity_mphread_native_android SHARED)
    add_library(FruityPrime::MphRead.Native.Android ALIAS fruity_mphread_native_android)
    target_sources(fruity_mphread_native_android PRIVATE ${MPHREAD_ANDROID_SOURCES} ${MPHREAD_ANDROID_HEADERS})
    target_include_directories(fruity_mphread_native_android PUBLIC
        "${FRUITY_ANDROID_ROOT}"
        "${FRUITY_MPHREAD_ROOT}"
        "${FRUITY_NCSFPLAY_ROOT}")
    target_compile_features(fruity_mphread_native_android PUBLIC cxx_std_20)
    target_link_libraries(fruity_mphread_native_android PRIVATE
        fruity_mphread_native
        fruity_ncsfplay_native
        "${FRUITY_ANDROID_PLATFORM_LIBRARY}"
        "${FRUITY_ANDROID_LOG_LIBRARY}"
        "${FRUITY_ANDROID_EGL_LIBRARY}"
        "${FRUITY_ANDROID_GLESV3_LIBRARY}"
        "${FRUITY_ANDROID_JNIGRAPHICS_LIBRARY}"
        "${FRUITY_ANDROID_OPENSL_LIBRARY}")
    target_link_options(fruity_mphread_native_android PRIVATE "-Wl,--no-undefined")
    set_target_properties(fruity_mphread_native_android PROPERTIES
        OUTPUT_NAME "FruityPrime")
else()
    find_package(CURL REQUIRED)
    find_package(LibArchive REQUIRED)
    target_link_libraries(fruity_mphread_native PUBLIC CURL::libcurl LibArchive::LibArchive)
endif()
