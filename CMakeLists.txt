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
    "${FRUITY_MPHREAD_ROOT}/Testing/TestSyluxMuzzleGuard.cpp"
    "${FRUITY_MPHREAD_ROOT}/Testing/TestOpenGlUniformState.cpp"
    "${FRUITY_MPHREAD_ROOT}/Testing/TestNativeTouchState.cpp"
    "${FRUITY_MPHREAD_ROOT}/Testing/TestNativeAimRules.cpp"
    "${FRUITY_MPHREAD_ROOT}/Testing/TestDialancheNativeCollision.cpp"
    "${FRUITY_MPHREAD_ROOT}/Testing/TestWeavelAltFormParity.cpp"
    "${FRUITY_MPHREAD_ROOT}/Testing/TestIntentTouchPayload.cpp"
    "${FRUITY_MPHREAD_ROOT}/Testing/TestFrameTelemetry.cpp"
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

# The window, event loop and menus on the desktop: a QWindow with Qt Quick
# menus rendered on the GPU into a texture the game composites
# (MphRead.Native.Qt). The renderer keeps its own frame loop.
set(FRUITY_QT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src/MphRead.Native.Qt")
# Qt owns every native menu; GLFW remains only for desktop gamepads.
if(ANDROID)
    list(FILTER MPHREAD_NATIVE_SOURCES EXCLUDE REGEX "/NativeRuntime/Gui/.*[.]cpp$")
    list(FILTER MPHREAD_NATIVE_SOURCES EXCLUDE REGEX "/NativeRuntime/OpenTK/(GL|GpuTrace)[.]cpp$")
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
    # ICU libraries must be ordered by dependency:
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
option(FRUITY_NEW_TELEMETRY "Intercept application C++ new in a diagnostic build" OFF)
option(FRUITY_PERF_TELEMETRY "Enable opt-in sampled CPU phases and API counters" OFF)
if(FRUITY_NEW_TELEMETRY OR FRUITY_PERF_TELEMETRY)
    target_compile_definitions(fruity_mphread_native PUBLIC FRUITY_PERF_TELEMETRY)
endif()
if(FRUITY_NEW_TELEMETRY)
    set_property(SOURCE "${FRUITY_MPHREAD_ROOT}/NativeRuntime/FrameTelemetry.cpp"
        APPEND PROPERTY COMPILE_DEFINITIONS FRUITY_NEW_TELEMETRY)
endif()
# A release is stamped with its tag's version (release_cpp.yml passes
# -DFRUITY_RELEASE_VERSION=0.11.0). Without one the build calls itself a local
# build, and the updater -- a dedicated server's included -- leaves it alone.
set(FRUITY_RELEASE_VERSION "" CACHE STRING
    "MAJOR.MINOR.PATCH stamped into a release build; empty for a local build")
if(FRUITY_RELEASE_VERSION)
    if(NOT FRUITY_RELEASE_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
        message(FATAL_ERROR
            "FRUITY_RELEASE_VERSION must be MAJOR.MINOR.PATCH, not '${FRUITY_RELEASE_VERSION}'")
    endif()
    set_property(SOURCE
            "${FRUITY_MPHREAD_ROOT}/Mods/Update/BuildVersion.cpp"
            "${FRUITY_MPHREAD_ROOT}/Mods/ThumbnailLog.cpp"
        APPEND PROPERTY COMPILE_DEFINITIONS
            "MPHREAD_ENTRY_ASSEMBLY_INFORMATIONAL_VERSION=\"${FRUITY_RELEASE_VERSION}\"")
    message(STATUS "Release version: ${FRUITY_RELEASE_VERSION}")
endif()
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
    add_executable(fruity_sylux_muzzle_guard_tests "${FRUITY_MPHREAD_ROOT}/Testing/TestSyluxMuzzleGuard.cpp")
    target_link_libraries(fruity_sylux_muzzle_guard_tests PRIVATE fruity_mphread_native)
    target_compile_features(fruity_sylux_muzzle_guard_tests PRIVATE cxx_std_20)
    add_test(NAME FruityPrime.SyluxMuzzleGuard COMMAND fruity_sylux_muzzle_guard_tests)
    add_executable(fruity_native_aim_rules_tests "${FRUITY_MPHREAD_ROOT}/Testing/TestNativeAimRules.cpp")
    target_compile_features(fruity_native_aim_rules_tests PRIVATE cxx_std_20)
    add_test(NAME FruityPrime.NativeAimRules COMMAND fruity_native_aim_rules_tests)
    add_executable(fruity_raw_mouse_motion_tests "${CMAKE_CURRENT_SOURCE_DIR}/tools/testing/TestRawMouseMotion.cpp")
    target_compile_features(fruity_raw_mouse_motion_tests PRIVATE cxx_std_20)
    add_test(NAME FruityPrime.RawMouseMotion COMMAND fruity_raw_mouse_motion_tests)
    add_executable(fruity_native_touch_state_tests
        "${FRUITY_MPHREAD_ROOT}/Testing/TestNativeTouchState.cpp"
        "${FRUITY_MPHREAD_ROOT}/Mods/Input/NativeTouchState.cpp"
        "${FRUITY_MPHREAD_ROOT}/Mods/Input/DsTouchSurface.cpp"
        "${FRUITY_MPHREAD_ROOT}/Mods/Input/MouseMotionTouchSource.cpp"
        "${FRUITY_MPHREAD_ROOT}/Entities/Players/MorphBallTouchRules.cpp"
        "${FRUITY_MPHREAD_ROOT}/Entities/Players/MorphBallBoostStateMachine.cpp")
    target_compile_features(fruity_native_touch_state_tests PRIVATE cxx_std_20)
    add_test(NAME FruityPrime.NativeTouchState COMMAND fruity_native_touch_state_tests)
    add_executable(fruity_dialanche_native_collision_tests
        "${FRUITY_MPHREAD_ROOT}/Testing/TestDialancheNativeCollision.cpp"
        "${FRUITY_MPHREAD_ROOT}/Entities/Players/DialancheNativeCollision.cpp")
    target_compile_features(fruity_dialanche_native_collision_tests PRIVATE cxx_std_20)
    add_test(NAME FruityPrime.DialancheNativeCollision COMMAND fruity_dialanche_native_collision_tests)
    add_executable(fruity_weavel_alt_form_parity_tests
        "${FRUITY_MPHREAD_ROOT}/Testing/TestWeavelAltFormParity.cpp")
    target_link_libraries(fruity_weavel_alt_form_parity_tests PRIVATE fruity_mphread_native)
    target_compile_features(fruity_weavel_alt_form_parity_tests PRIVATE cxx_std_20)
    add_test(NAME FruityPrime.WeavelAltFormParity COMMAND fruity_weavel_alt_form_parity_tests)
    add_executable(fruity_intent_touch_payload_tests "${FRUITY_MPHREAD_ROOT}/Testing/TestIntentTouchPayload.cpp")
    target_link_libraries(fruity_intent_touch_payload_tests PRIVATE fruity_mphread_native)
    target_compile_features(fruity_intent_touch_payload_tests PRIVATE cxx_std_20)
    add_test(NAME FruityPrime.IntentTouchPayload COMMAND fruity_intent_touch_payload_tests)
    add_executable(fruity_opengl_uniform_tests
        "${FRUITY_MPHREAD_ROOT}/Testing/TestOpenGlUniformState.cpp"
        "${FRUITY_MPHREAD_ROOT}/NativeRuntime/FrameTelemetry.cpp")
    target_compile_definitions(fruity_opengl_uniform_tests PRIVATE FRUITY_NEW_TELEMETRY FRUITY_PERF_TELEMETRY)
    target_compile_features(fruity_opengl_uniform_tests PRIVATE cxx_std_20)
    add_test(NAME FruityPrime.OpenGlUniformState COMMAND fruity_opengl_uniform_tests)
    add_executable(fruity_frame_telemetry_tests
        "${FRUITY_MPHREAD_ROOT}/Testing/TestFrameTelemetry.cpp"
        "${FRUITY_MPHREAD_ROOT}/NativeRuntime/FrameTelemetry.cpp")
    target_compile_definitions(fruity_frame_telemetry_tests PRIVATE FRUITY_NEW_TELEMETRY FRUITY_PERF_TELEMETRY)
    target_compile_features(fruity_frame_telemetry_tests PRIVATE cxx_std_20)
    add_test(NAME FruityPrime.FrameTelemetry COMMAND fruity_frame_telemetry_tests)
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

    # The Reflex controller fixture uses fake dispatch only: it needs headers,
    # but no loader, VMA, shader compiler or enabled production Vulkan backend.
    find_path(FRUITY_REFLEX_VULKAN_INCLUDE_DIR NAMES vulkan/vulkan.h
        HINTS ${Vulkan_INCLUDE_DIRS})
    if(FRUITY_REFLEX_VULKAN_INCLUDE_DIR)
        add_executable(fruity_vulkan_reflex_tests
            "${FRUITY_MPHREAD_ROOT}/Testing/TestVulkanNvidiaReflex.cpp"
            "${FRUITY_MPHREAD_ROOT}/NativeRuntime/Rhi/Vulkan/VulkanNvidiaReflex.cpp")
        target_compile_definitions(fruity_vulkan_reflex_tests PRIVATE FRUITY_HAS_VULKAN)
        target_include_directories(fruity_vulkan_reflex_tests PRIVATE ${FRUITY_REFLEX_VULKAN_INCLUDE_DIR})
        target_compile_features(fruity_vulkan_reflex_tests PRIVATE cxx_std_20)
        add_test(NAME FruityPrime.VulkanNvidiaReflex COMMAND fruity_vulkan_reflex_tests)
    endif()

    if(TARGET fruity_vulkan_shaders)
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
            "${FRUITY_MPHREAD_ROOT}/NativeRuntime/Rhi/Vulkan/VulkanFeatureProbe.cpp"
            "${FRUITY_MPHREAD_ROOT}/NativeRuntime/Rhi/Vulkan/VulkanLegacy.cpp")
        target_compile_definitions(fruity_vulkan_feature_probe_tests PRIVATE FRUITY_HAS_VULKAN)
        target_include_directories(fruity_vulkan_feature_probe_tests PRIVATE ${Vulkan_INCLUDE_DIRS})
        target_compile_features(fruity_vulkan_feature_probe_tests PRIVATE cxx_std_20)
        add_test(NAME FruityPrime.VulkanFeatureProbe COMMAND fruity_vulkan_feature_probe_tests)


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

# MPHREAD_SHELL / MPHREAD_QT: the desktop one-window shell, drawn by Qt
# Quick. Android supplies its own Activity/View stack.
target_compile_definitions(fruity_mphread_native PUBLIC MPHREAD_QT)
if(NOT ANDROID)
    target_compile_definitions(fruity_mphread_native PUBLIC MPHREAD_SHELL)
endif()

if(WIN32)
    target_compile_definitions(fruity_mphread_native PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    # The launcher's file picker is the platform's own common dialog.
    target_link_libraries(fruity_mphread_native PUBLIC ws2_32 bcrypt comdlg32 iphlpapi dbghelp)
endif()

# The renderer's GL entry points and the sound mixer's OpenAL ones, which the
# C# build gets from OpenTK's own native dependencies.
if(NOT ANDROID)
    find_package(OpenGL REQUIRED)
    target_link_libraries(fruity_mphread_native PUBLIC OpenGL::GL)

    # Qt Quick draws the menus and decodes the images: no Skia, FreeType or
    # JPEG on the desktop. GLFW is still the desktop gamepads' API.
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

# The desktop window platform, the shell and its Qt Quick menus. Their
# objects go into the MphRead.Native archive, since the renderer calls the
# shell and the window platform from inside it.
find_package(Qt6 6.9 REQUIRED COMPONENTS Core Gui Qml Quick QuickShapes QuickEffects)
fruity_collect_owner("${FRUITY_QT_ROOT}" MPHREAD_QT_SOURCES MPHREAD_QT_HEADERS)
if(ANDROID)
    list(FILTER MPHREAD_QT_SOURCES EXCLUDE REGEX "/(Shell|UiHost|UiCapture|UiChecks|QtRendererPlatform)[.]cpp$")
endif()
fruity_report_owner("MphRead.Native.Qt" MPHREAD_QT_SOURCES MPHREAD_QT_HEADERS)
add_library(fruity_mphread_qt OBJECT ${MPHREAD_QT_SOURCES} ${MPHREAD_QT_HEADERS})
set_target_properties(fruity_mphread_qt PROPERTIES AUTOMOC ON)
target_compile_features(fruity_mphread_qt PUBLIC cxx_std_20)
if(FRUITY_NEW_TELEMETRY OR FRUITY_PERF_TELEMETRY)
    target_compile_definitions(fruity_mphread_qt PRIVATE FRUITY_PERF_TELEMETRY)
endif()
target_compile_definitions(fruity_mphread_qt PRIVATE MPHREAD_QT)
if(NOT ANDROID)
    target_compile_definitions(fruity_mphread_qt PRIVATE MPHREAD_SHELL)
endif()
# The menus render through Vulkan too when the renderer does: the same
# backend definition and headers the RHI is compiled with.
target_compile_definitions(fruity_mphread_qt PRIVATE
    $<$<IN_LIST:FRUITY_HAS_VULKAN,$<TARGET_PROPERTY:fruity_mphread_native,COMPILE_DEFINITIONS>>:FRUITY_HAS_VULKAN>)
# Qt's platform layer on macOS talks to AppKit through the Objective-C
# runtime (window restoration), and the Vulkan surface is a CAMetalLayer.
if(APPLE)
    target_link_libraries(fruity_mphread_qt PRIVATE objc "-framework CoreFoundation")
    if(TARGET fruity_vulkan_shaders)
        target_link_libraries(fruity_mphread_qt PRIVATE "-framework QuartzCore")
    endif()
endif()
if(Vulkan_FOUND AND FRUITY_VMA_INCLUDE_DIR)
    target_include_directories(fruity_mphread_qt PRIVATE ${Vulkan_INCLUDE_DIRS} "${FRUITY_VMA_INCLUDE_DIR}")
endif()
target_include_directories(fruity_mphread_qt PRIVATE
    "${FRUITY_MPHREAD_ROOT}" "${FRUITY_NCSFPLAY_ROOT}")
target_link_libraries(fruity_mphread_qt PUBLIC Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
if(WIN32)
    target_link_libraries(fruity_mphread_native PUBLIC user32)
    if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
        add_executable(fruity_windows_raw_mouse_tests
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/testing/TestWindowsRawMouseInput.cpp"
            "${FRUITY_QT_ROOT}/Platform/WindowsRawMouseInput.cpp")
        target_compile_features(fruity_windows_raw_mouse_tests PRIVATE cxx_std_20)
        target_compile_definitions(fruity_windows_raw_mouse_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        target_link_libraries(fruity_windows_raw_mouse_tests PRIVATE Qt6::Core user32)
        add_test(NAME FruityPrime.WindowsRawMouseInput COMMAND fruity_windows_raw_mouse_tests)
        add_executable(fruity_qt_raw_mouse_window_tests "${CMAKE_CURRENT_SOURCE_DIR}/tools/testing/TestQtRawMouseWindow.cpp")
        target_compile_features(fruity_qt_raw_mouse_window_tests PRIVATE cxx_std_20)
        target_compile_definitions(fruity_qt_raw_mouse_window_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        target_link_libraries(fruity_qt_raw_mouse_window_tests PRIVATE fruity_mphread_native)

    endif()
endif()

if(UNIX AND NOT APPLE AND NOT ANDROID)
    # Qt loads its xcb platform plugin dynamically, so link libxcb explicitly
    # for the Vulkan presentation probe's default-visual query.
    find_library(FRUITY_XCB_LIBRARY NAMES xcb REQUIRED)
    target_link_libraries(fruity_mphread_qt PRIVATE "${FRUITY_XCB_LIBRARY}")
    # fruity_mphread_qt is an OBJECT library whose objects are inserted into
    # fruity_mphread_native below. Its private link dependencies therefore do
    # not reach the final executable; carry XCB on the archive that owns them.
    target_link_libraries(fruity_mphread_native PUBLIC "${FRUITY_XCB_LIBRARY}")
endif()
if(FRUITY_STOP_TOKEN_OPTIONS)
    target_compile_options(fruity_mphread_qt PRIVATE ${FRUITY_STOP_TOKEN_OPTIONS})
endif()
target_sources(fruity_mphread_native PRIVATE $<TARGET_OBJECTS:fruity_mphread_qt>)
target_link_libraries(fruity_mphread_native PUBLIC Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
if(NOT ANDROID)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    target_link_libraries(fruity_mphread_qt PRIVATE Qt6::Test)
    target_link_libraries(fruity_mphread_native PUBLIC Qt6::Test)
endif()

function(fruity_add_qml target)
    # The menus, compiled into the binary (qrc:/qt/qml/FruityPrime/Ui).
    qt_policy(SET QTP0001 NEW)
    qt_policy(SET QTP0004 NEW)
    set_target_properties(${target} PROPERTIES AUTOMOC ON)
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
    qt_add_qml_module(${target}
        URI FruityPrime.Ui
        IMPORT_PATH "${FRUITY_QT_ROOT}/qmlimports"
        VERSION 1.0
        OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/qml/FruityPrime/Ui"
        NO_RESOURCE_TARGET_PATH
        RESOURCE_PREFIX /qt/qml/FruityPrime/Ui
        QML_FILES ${FRUITY_QML_RELATIVE}
        RESOURCES src/MphRead/Assets/Backgrounds/launcher-bg.jpg ${FRUITY_QML_FONTS})

endfunction()

# The game binary. Its only source is the entry-point shim; everything else is
# the library above.
if(NOT ANDROID)
    qt_add_executable(fruity_prime "${CMAKE_CURRENT_SOURCE_DIR}/src/FruityPrime/Main.cpp")
    if(WIN32)
        # A GUI binary, as the C# WinExe was: double-clicked it opens the game
        # window and no console. The command-line modes attach to the parent
        # console, or open one, themselves (ConsoleWindow::Prepare).
        set_target_properties(fruity_prime PROPERTIES WIN32_EXECUTABLE TRUE)
    elseif(APPLE)
        set_target_properties(fruity_prime PROPERTIES MACOSX_BUNDLE TRUE)
    elseif(UNIX)
        set_target_properties(fruity_prime PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
    endif()
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

    fruity_add_qml(fruity_prime)

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
    # The Qt runtime, its plugins and the QML modules the menus import,
    # beside the binary, so a build folder runs as it stands (and is what a
    # package ships). windeployqt finds the imports by scanning the QML.
    if(WIN32)
        find_program(FRUITY_WINDEPLOYQT NAMES windeployqt windeployqt6
            HINTS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}" NO_DEFAULT_PATH)
        if(FRUITY_WINDEPLOYQT)
            add_custom_command(TARGET fruity_prime POST_BUILD
                COMMAND "${FRUITY_WINDEPLOYQT}" --no-translations --no-system-d3d-compiler
                    --no-opengl-sw --no-compiler-runtime
                    --qmldir "${FRUITY_QT_ROOT}/qml" "$<TARGET_FILE:fruity_prime>"
                VERBATIM)
        else()
            message(FATAL_ERROR "windeployqt is required to deploy the Qt runtime.")
        endif()
    endif()
    include(GNUInstallDirs)
    install(TARGETS fruity_prime BUNDLE DESTINATION . RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    if(APPLE)
        set(FRUITY_ASSET_INSTALL_DIR "FruityPrime.app/Contents/MacOS")
    else()
        set(FRUITY_ASSET_INSTALL_DIR "${CMAKE_INSTALL_BINDIR}")
    endif()
    install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/MphRead/Assets/fruity-prime-logo.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/MphRead/Assets/fruity-prime-mark.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/MphRead/Assets/Backgrounds/launcher-bg.jpg"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/MphRead/Assets/Geo/ipcountry.bin.gz"
        DESTINATION "${FRUITY_ASSET_INSTALL_DIR}")
    qt_generate_deploy_qml_app_script(TARGET fruity_prime OUTPUT_SCRIPT FRUITY_QT_DEPLOY_SCRIPT
        NO_TRANSLATIONS)
    install(SCRIPT "${FRUITY_QT_DEPLOY_SCRIPT}")
    if(MSVC)
        get_filename_component(FRUITY_MSVC_BIN "${CMAKE_LINKER}" DIRECTORY)
        set(FRUITY_DUMPBIN "${FRUITY_MSVC_BIN}/dumpbin.exe")
        configure_file(cmake/DeployWindowsDependencies.cmake.in windows-dependencies.in @ONLY)
        file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/windows-dependencies-$<CONFIG>.cmake"
            INPUT "${CMAKE_CURRENT_BINARY_DIR}/windows-dependencies.in")
        install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/windows-dependencies-$<CONFIG>.cmake")
    endif()
    add_custom_target(fruity_package
        COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}"
            --config "$<CONFIG>" --prefix "${CMAKE_BINARY_DIR}/dist"
        DEPENDS fruity_prime VERBATIM)
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

    find_library(FRUITY_ANDROID_LOG_LIBRARY log REQUIRED)
    find_library(FRUITY_ANDROID_PLATFORM_LIBRARY android REQUIRED)
    find_library(FRUITY_ANDROID_EGL_LIBRARY EGL REQUIRED)
    find_library(FRUITY_ANDROID_GLESV3_LIBRARY GLESv3 REQUIRED)
    find_library(FRUITY_ANDROID_JNIGRAPHICS_LIBRARY jnigraphics REQUIRED)
    find_library(FRUITY_ANDROID_OPENSL_LIBRARY OpenSLES REQUIRED)

    target_link_libraries(fruity_mphread_native PUBLIC
        CURL::libcurl
        LibArchive::LibArchive
        "${FRUITY_ANDROID_GLESV3_LIBRARY}"
        "${FRUITY_ANDROID_OPENSL_LIBRARY}")

    fruity_collect_owner("${FRUITY_ANDROID_ROOT}" MPHREAD_ANDROID_SOURCES MPHREAD_ANDROID_HEADERS)
    fruity_report_owner("MphRead.Native.Android" MPHREAD_ANDROID_SOURCES MPHREAD_ANDROID_HEADERS)

    qt_add_executable(fruity_mphread_native_android)
    fruity_add_qml(fruity_mphread_native_android)
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
        OUTPUT_NAME "FruityPrime"
        QT_ANDROID_MIN_SDK_VERSION "28"
        QT_ANDROID_TARGET_SDK_VERSION "${FRUITY_ANDROID_COMPILE_SDK}")
else()
    find_package(CURL REQUIRED)
    find_package(LibArchive REQUIRED)
    target_link_libraries(fruity_mphread_native PUBLIC CURL::libcurl LibArchive::LibArchive)
endif()
