cmake_minimum_required(VERSION 3.24)
project(FruityPrime VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)
# Headers only: vulkan-1 is loaded at run time through QVulkanInstance.
find_package(Vulkan QUIET)
if(NOT TARGET Vulkan::Headers)
    find_path(VULKAN_HEADERS_DIR vulkan/vulkan.h HINTS "$ENV{VULKAN_SDK}/include" NO_CMAKE_FIND_ROOT_PATH REQUIRED)
    add_library(Vulkan::Headers INTERFACE IMPORTED)
    target_include_directories(Vulkan::Headers INTERFACE "${VULKAN_HEADERS_DIR}")
endif()

find_program(GLSLC glslc HINTS "$ENV{VULKAN_SDK}/bin" REQUIRED)

set(SHADER_SOURCES
    src/render/shaders/room.vert
    src/render/shaders/room.frag
    src/render/shaders/hud.vert
    src/render/shaders/hud.frag
)
set(SHADER_OUTPUTS)
foreach(shader ${SHADER_SOURCES})
    get_filename_component(name ${shader} NAME)
    set(spv ${CMAKE_CURRENT_BINARY_DIR}/shaders/${name}.spv)
    add_custom_command(
        OUTPUT ${spv}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/shaders
        COMMAND ${GLSLC} --target-env=vulkan1.0 -O -o ${spv} ${CMAKE_CURRENT_SOURCE_DIR}/${shader}
        DEPENDS ${shader}
        COMMENT "glslc ${name}"
        VERBATIM)
    list(APPEND SHADER_OUTPUTS ${spv})
endforeach()

# miniaudio (public domain / MIT-0): the audio device, loaded at run time
# (PulseAudio or ALSA on Linux, WASAPI on Windows) -- nothing to install.
add_library(fp_miniaudio STATIC third_party/miniaudio/miniaudio.cpp)
target_include_directories(fp_miniaudio PUBLIC third_party/miniaudio)
# Playback only; the same for the implementation and its users, or ma_device differs between them.
target_compile_definitions(fp_miniaudio PUBLIC MA_NO_DECODING MA_NO_ENCODING MA_NO_GENERATION MA_NO_RESOURCE_MANAGER
    MA_NO_NODE_GRAPH MA_NO_ENGINE)
target_compile_options(fp_miniaudio PRIVATE -w)
find_package(Threads REQUIRED)
target_link_libraries(fp_miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
if(NOT WIN32)
    target_link_libraries(fp_miniaudio PUBLIC m)
endif()

# miniz (MIT): raw deflate, the demo files' compression.
add_library(fp_miniz STATIC third_party/miniz/miniz.c)
target_include_directories(fp_miniz PUBLIC third_party/miniz)
target_compile_options(fp_miniz PRIVATE -w)

add_library(fp_formats STATIC
    src/formats/Enums.h
    src/formats/Raw.h
    src/formats/Model.h
    src/formats/Model.cpp
    src/formats/Rooms.h
    src/formats/Rooms.cpp
    src/formats/Entities.h
    src/formats/Entities.cpp
    src/formats/Animation.h
    src/formats/Animation.cpp
    src/formats/Mat4.h
    src/formats/Metadata.h
    src/formats/Metadata.cpp
    src/formats/MetadataTables.cpp
    src/formats/HudAssets.h
    src/formats/HudAssets.cpp
    src/formats/RoomTable.inc
)
target_include_directories(fp_formats PUBLIC src)

qt_add_executable(FruityPrime
    src/app/main.cpp
    src/render/Scene.h
    src/render/Scene.cpp
    src/render/SceneRenderer.h
    src/render/SceneRenderer.cpp
    src/game/SceneBuilder.h
    src/game/SceneBuilder.cpp
    src/game/Collision.h
    src/game/Collision.cpp
    src/game/Player.h
    src/game/Player.cpp
    src/game/PlayerSound.cpp
    src/game/World.h
    src/game/World.cpp
    src/game/Modes.cpp
    src/game/NetGame.h
    src/game/NetGame.cpp
    src/game/ServerGame.cpp
    src/game/HitArbiter.cpp
    src/game/NetPrediction.h
    src/game/NetPrediction.cpp
    src/game/MatchRoom.h
    src/game/MatchRoom.cpp
    src/game/MatchSetup.h
    src/game/MatchSetup.cpp
    src/game/AreaVolumes.cpp
    src/game/Beams.cpp
    src/game/Bombs.cpp
    src/game/Halfturret.cpp
    src/game/Effects.h
    src/game/Rng.h
    src/game/AiData.h
    src/game/AiData.cpp
    src/game/WorldEntities.h
    src/game/PlayerAi.h
    src/game/PlayerAiUtil.h
    src/game/PlayerAi.cpp
    src/game/PlayerAiFuncs1.cpp
    src/game/PlayerAiFuncs2.cpp
    src/game/PlayerAiFuncs3.cpp
    src/game/PlayerAiFuncs4.cpp
    src/game/PlayerAiAim.cpp
    src/game/PlayerAiMove.cpp
    src/game/PlayerAiFind.cpp
    src/game/Effects.cpp
    src/game/Hud.h
    src/game/Hud.cpp
    src/game/InputScript.h
    src/game/InputScript.cpp
    src/game/CameraSequence.h
    src/game/CameraSequence.cpp
    src/net/NetProtocol.h
    src/net/NetProtocol.cpp
    src/net/NetTransport.h
    src/net/NetTransport.cpp
    src/net/NetClient.h
    src/net/NetClient.cpp
    src/net/NetServer.cpp
    src/net/NetServerLobby.cpp
    src/net/NetMaster.h
    src/net/NetMaster.cpp
    src/net/NetDemo.h
    src/net/NetDemo.cpp
    src/audio/SoundIds.h
    src/audio/Mixer.h
    src/audio/Mixer.cpp
    src/audio/SoundData.h
    src/audio/SoundData.cpp
    src/audio/Sfx.h
    src/audio/Sfx.cpp
    src/audio/Sequencer.h
    src/audio/Sequencer.cpp
    src/audio/Music.h
    src/audio/Music.cpp
    src/render/Overlay.h
    src/app/GameWindow.h
    src/app/GameWindow.cpp
    src/app/UiOverlay.h
    src/app/UiOverlay.cpp
    src/app/Settings.h
    src/app/Settings.cpp
    src/app/InputBindings.h
    src/app/InputBindings.cpp
    src/app/Gamepad.h
    src/app/Gamepad.cpp
    src/app/Launcher.h
    src/app/Launcher.cpp
    src/render/VulkanWindow.h
    src/render/VulkanWindow.cpp
)
set_target_properties(FruityPrime PROPERTIES WIN32_EXECUTABLE ON)
target_link_libraries(FruityPrime PRIVATE fp_formats fp_miniaudio fp_miniz Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2
    Vulkan::Headers)

# The screens, compiled in (qrc:/qt/qml/FruityPrimeUi/...).
set_source_files_properties(src/ui/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)
qt_add_qml_module(FruityPrime
    URI FruityPrimeUi
    VERSION 1.0
    QML_FILES
        src/ui/Main.qml
        src/ui/StartScreen.qml
        src/ui/PlayScreen.qml
        src/ui/SettingsScreen.qml
        src/ui/PauseScreen.qml
        src/ui/UiButton.qml
        src/ui/UiRow.qml
        src/ui/UiChoice.qml
        src/ui/Theme.qml
)

qt_add_resources(FruityPrime shaders
    PREFIX /shaders
    BASE ${CMAKE_CURRENT_BINARY_DIR}/shaders
    FILES ${SHADER_OUTPUTS}
)

if(WIN32)
    target_link_libraries(FruityPrime PRIVATE psapi ws2_32)
endif()

if(MSVC)
    target_compile_options(fp_formats PRIVATE /W4)
    target_compile_options(FruityPrime PRIVATE /W4)
else()
    target_compile_options(fp_formats PRIVATE -Wall -Wextra)
    target_compile_options(FruityPrime PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()
