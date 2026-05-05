cmake_minimum_required(VERSION 3.16)
project(Kemena3DStudio)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS ON)

# Path to the Kemena3D SDK output directory
# The default path below will be replaced by build_editor.py
set(KEMENA3D_SDK_DIR "C:/Kemena3D/" CACHE PATH "Path to Kemena3D SDK output directory")

# Kemena3D source directory – used to reference transitive deps not copied to the SDK output
get_filename_component(KEMENA3D_SOURCE_DIR "${KEMENA3D_SDK_DIR}" DIRECTORY)

# Whether to link against the static kemena3d library (set by build_editor.py)
option(KEMENA3D_LINK_STATIC "Link against the static kemena3d library" ON)

set(SOURCES
    main.cpp
    src/mainmenu.cpp
    src/commands.cpp
    src/manager.cpp
    src/panel_console.cpp
    src/panel_hierarchy.cpp
    src/panel_inspector.cpp
    src/panel_project.cpp
    src/panel_world.cpp
    src/panel_shader_editor.cpp
    src/panel_game.cpp
    src/panel_prefab.cpp
    src/splash_screen.cpp
    src/util.cpp
    resources.rc
)

add_executable(${PROJECT_NAME} ${SOURCES})

# Release builds use the GUI subsystem (no console window); Debug keeps the console
# Use generator expression so this works with both single-config and multi-config generators
set_target_properties(${PROJECT_NAME} PROPERTIES
    WIN32_EXECUTABLE "$<$<CONFIG:Release>:TRUE>"
)

# Output to bin/Debug or bin/Release under the project root
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_SOURCE_DIR}/bin/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/bin/Release"
    RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE}"
)

# Include directories – SDK path varies by configuration
# assimp/config.h is generated into the assimp build tree, not copied to the SDK output
target_include_directories(${PROJECT_NAME} PRIVATE
    include
    dep
    "$<$<CONFIG:Debug>:${KEMENA3D_SDK_DIR}/Debug/include>"
    "$<$<CONFIG:Release>:${KEMENA3D_SDK_DIR}/Release/include>"
    "$<$<CONFIG:Debug>:${KEMENA3D_SOURCE_DIR}/Dependencies/assimp/build_Debug/include>"
    "$<$<CONFIG:Release>:${KEMENA3D_SOURCE_DIR}/Dependencies/assimp/build_Release/include>"
)

# Preprocessor defines
target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<BOOL:${KEMENA3D_LINK_STATIC}>:KEMENA3D_STATIC>
    $<$<BOOL:${KEMENA3D_LINK_STATIC}>:DKTX_STATIC>
    NOMINMAX
)

# assimp and Jolt are both built with /MD (MultiThreadedDLL); force /MD for the
# whole studio link so CRT records match across all static dependencies.
if(MSVC AND KEMENA3D_LINK_STATIC)
    set_property(TARGET ${PROJECT_NAME} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()

# Compiler flags – separated by toolchain
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /W3
        $<$<CONFIG:Debug>:/Zi>
        $<$<CONFIG:Release>:/O2>
    )
    # WIN32_EXECUTABLE uses /SUBSYSTEM:WINDOWS which expects WinMain; override the
    # entry point so the standard int main() is used instead.
    target_link_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:/ENTRY:mainCRTStartup>
        # Suppress LNK4098: some deps rebuilt with /MD still embed MSVCRTD as a default lib
        $<$<AND:$<CONFIG:Debug>,$<BOOL:${KEMENA3D_LINK_STATIC}>>:/NODEFAULTLIB:msvcrtd>
    )
else()
    # GCC / MinGW
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall
        $<$<CONFIG:Debug>:-g>
        $<$<CONFIG:Release>:-O2>
    )
endif()

# Library search paths – SDK path varies by configuration
target_link_directories(${PROJECT_NAME} PRIVATE
    "$<$<CONFIG:Debug>:${KEMENA3D_SDK_DIR}/Debug/lib>"
    "$<$<CONFIG:Release>:${KEMENA3D_SDK_DIR}/Release/lib>"
)

# Linked libraries — common to both toolchains
target_link_libraries(${PROJECT_NAME} PRIVATE
    kemena3d
    opengl32
    winmm
    ole32
    setupapi
    gdi32
    imm32
    version
    oleaut32
    uuid
)

if(MSVC)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        SDL3-static
        $<$<CONFIG:Debug>:assimp-vc143-mtd>
        $<$<CONFIG:Release>:assimp-vc143-mt>
        $<$<CONFIG:Debug>:angelscriptd>
        $<$<CONFIG:Release>:angelscript>
        imgui
        imguizmo
        $<$<CONFIG:Debug>:libglew32d>
        $<$<CONFIG:Release>:libglew32>
        $<$<CONFIG:Debug>:zlibstaticd>
        $<$<CONFIG:Release>:zlibstatic>
        # Recast Navigation (MSVC names have -d suffix for Debug)
        $<$<CONFIG:Debug>:Recast-d>
        $<$<CONFIG:Debug>:Detour-d>
        $<$<CONFIG:Debug>:DetourCrowd-d>
        $<$<CONFIG:Debug>:DetourTileCache-d>
        $<$<CONFIG:Release>:Recast>
        $<$<CONFIG:Release>:Detour>
        $<$<CONFIG:Release>:DetourCrowd>
        $<$<CONFIG:Release>:DetourTileCache>
        # Jolt Physics (MSVC build tree — not installed to SDK output)
        "$<$<CONFIG:Debug>:${KEMENA3D_SOURCE_DIR}/Dependencies/jolt/Build/build_Debug/Debug/Jolt.lib>"
        "$<$<CONFIG:Release>:${KEMENA3D_SOURCE_DIR}/Dependencies/jolt/Build/build_Release/Release/Jolt.lib>"
    )
else()
    # GCC / MinGW
    target_link_libraries(${PROJECT_NAME} PRIVATE
        SDL3
        $<$<CONFIG:Debug>:assimpd>
        $<$<CONFIG:Release>:assimp>
        angelscript
        $<$<CONFIG:Debug>:libglew32d>
        $<$<CONFIG:Release>:libglew32>
        zlibstatic
        # Recast Navigation
        "$<$<CONFIG:Debug>:${KEMENA3D_SOURCE_DIR}/Dependencies/recast/dist/lib/Debug/libRecast.a>"
        "$<$<CONFIG:Debug>:${KEMENA3D_SOURCE_DIR}/Dependencies/recast/dist/lib/Debug/libDetour.a>"
        "$<$<CONFIG:Debug>:${KEMENA3D_SOURCE_DIR}/Dependencies/recast/dist/lib/Debug/libDetourCrowd.a>"
        "$<$<CONFIG:Debug>:${KEMENA3D_SOURCE_DIR}/Dependencies/recast/dist/lib/Debug/libDetourTileCache.a>"
        "$<$<CONFIG:Release>:${KEMENA3D_SOURCE_DIR}/Dependencies/recast/dist/lib/Release/libRecast.a>"
        "$<$<CONFIG:Release>:${KEMENA3D_SOURCE_DIR}/Dependencies/recast/dist/lib/Release/libDetour.a>"
        "$<$<CONFIG:Release>:${KEMENA3D_SOURCE_DIR}/Dependencies/recast/dist/lib/Release/libDetourCrowd.a>"
        "$<$<CONFIG:Release>:${KEMENA3D_SOURCE_DIR}/Dependencies/recast/dist/lib/Release/libDetourTileCache.a>"
    )

    # MinGW-only linker flags: static runtime, strip symbols in Release
    target_link_options(${PROJECT_NAME} PRIVATE
        -static-libstdc++
        -static-libgcc
        -static
        $<$<CONFIG:Release>:-s>
    )
endif()

