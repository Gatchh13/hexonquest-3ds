cmake_minimum_required(VERSION 3.13)

# ---------------------------------------------------------------------------
# Toolchain bootstrap (devkitARM / 3DS)
# ---------------------------------------------------------------------------
if(NOT DEFINED ENV{DEVKITPRO})
    message(FATAL_ERROR "Please set DEVKITPRO in your environment (source /etc/profile.d/devkit-env.sh)")
endif()
if(NOT DEFINED ENV{DEVKITARM})
    message(FATAL_ERROR "Please set DEVKITARM in your environment (usually $DEVKITPRO/devkitARM)")
endif()

set(DEVKITPRO $ENV{DEVKITPRO})
set(DEVKITARM $ENV{DEVKITARM})

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER   "${DEVKITARM}/bin/arm-none-eabi-gcc")
set(CMAKE_CXX_COMPILER "${DEVKITARM}/bin/arm-none-eabi-g++")
set(CMAKE_ASM_COMPILER "${DEVKITARM}/bin/arm-none-eabi-gcc")
set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_CXX_COMPILER_WORKS 1)

project(Polytopia3DS CXX C ASM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Architecture / codegen flags (ARM11 MPCore, 3DS)
# ---------------------------------------------------------------------------
set(ARCH_FLAGS "-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -mword-relocations")
set(COMMON_FLAGS "${ARCH_FLAGS} -fomit-frame-pointer -ffunction-sections -fdata-sections -Wall -O2 -DARM11 -D_3DS")

set(CMAKE_C_FLAGS   "${COMMON_FLAGS} -std=gnu11")
set(CMAKE_CXX_FLAGS "${COMMON_FLAGS} -std=gnu++17 -fno-rtti -fno-exceptions")
set(CMAKE_EXE_LINKER_FLAGS "${ARCH_FLAGS} -specs=3dsx.specs -Wl,--gc-sections -L${DEVKITPRO}/libctru/lib -L${DEVKITPRO}/portlibs/3ds/lib")

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${DEVKITPRO}/libctru/include
    ${DEVKITPRO}/portlibs/3ds/include
)

link_directories(
    ${DEVKITPRO}/libctru/lib
    ${DEVKITPRO}/portlibs/3ds/lib
)

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
set(GAME_SOURCES
    source/main.cpp
    source/core/Engine.cpp
    source/core/Settings.cpp
    source/core/Logger.cpp
    source/world/HexGrid.cpp
    source/render/Camera.cpp
    source/render/Renderer.cpp
    source/input/InputManager.cpp
    source/game/Pathfinder.cpp
    source/game/GameState.cpp
    source/ai/AIController.cpp
    source/save/SaveManager.cpp
    source/audio/AudioManager.cpp
)

add_executable(polytopia3ds.elf ${GAME_SOURCES})

target_link_libraries(polytopia3ds.elf
    citro2d
    citro3d
    ctru
    m
)

set_target_properties(polytopia3ds.elf PROPERTIES
    LINK_FLAGS "-specs=3dsx.specs"
    SUFFIX ""
)

# ---------------------------------------------------------------------------
# Post-build: elf -> 3dsx (+smdh)
# ---------------------------------------------------------------------------
set(APP_TITLE "Hexonquest")
set(APP_DESCRIPTION "A Polytopia-inspired 3DS strategy game")
set(APP_AUTHOR "Homebrew Dev")
set(SMDH_FILE "${CMAKE_BINARY_DIR}/polytopia3ds.smdh")
set(THREEDSX_FILE "${CMAKE_BINARY_DIR}/polytopia3ds.3dsx")

find_program(SMDHTOOL smdhtool PATHS ${DEVKITPRO}/tools/bin)
find_program(THREEDSXTOOL 3dsxtool PATHS ${DEVKITPRO}/tools/bin)

add_custom_command(
    OUTPUT ${SMDH_FILE}
    COMMAND ${SMDHTOOL} --create "${APP_TITLE}" "${APP_DESCRIPTION}" "${APP_AUTHOR}"
            ${CMAKE_SOURCE_DIR}/assets/icon.png ${SMDH_FILE}
    DEPENDS polytopia3ds.elf
    COMMENT "Generating SMDH"
)

add_custom_command(
    OUTPUT ${THREEDSX_FILE}
    COMMAND ${THREEDSXTOOL} $<TARGET_FILE:polytopia3ds.elf> ${THREEDSX_FILE} --smdh=${SMDH_FILE}
    DEPENDS polytopia3ds.elf ${SMDH_FILE}
    COMMENT "Generating 3DSX"
)

add_custom_target(polytopia3ds ALL DEPENDS ${THREEDSX_FILE})
