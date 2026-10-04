cmake_minimum_required(VERSION 3.16)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
project(lcd_aio CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# Fetch Tortellini (header-only INI parser)
FetchContent_Declare(
    tortellini
    GIT_REPOSITORY https://github.com/Qix-/tortellini.git
    GIT_TAG b47a39af410e600ece54ba1d8b990f5699e02407
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR ""
)
FetchContent_MakeAvailable(tortellini)

# Fetch Raylib
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(raylib)

# Fetch Dear ImGui (v1.92.7 compatible with rlImGui Raylib_5_5)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.7
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()

# Fetch rlImGui (Dear ImGui backend for Raylib 5.5)
FetchContent_Declare(
    rlImGui
    GIT_REPOSITORY https://github.com/raylib-extras/rlImGui.git
    GIT_TAG Raylib_5_5
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(rlImGui)
if(NOT rlImGui_POPULATED)
    FetchContent_Populate(rlImGui)
endif()

add_library(imgui_raylib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${rlimgui_SOURCE_DIR}/rlImGui.cpp
)
target_include_directories(imgui_raylib PUBLIC
    ${imgui_SOURCE_DIR}
    ${rlimgui_SOURCE_DIR}
)
target_link_libraries(imgui_raylib PUBLIC raylib)

# Compile Windows resource (icon) safely without path-with-spaces issues
set(APP_RC_OBJ "")
if(WIN32)
    set(APP_RC_OBJ "${CMAKE_CURRENT_BINARY_DIR}/app_icon.obj")
    if(MINGW)
        add_custom_command(
            OUTPUT ${APP_RC_OBJ}
            COMMAND windres -i "resource.rc" -o "${APP_RC_OBJ}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/resources"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/resource.rc" "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.ico"
        )
    elseif(MSVC)
        add_custom_command(
            OUTPUT ${APP_RC_OBJ}
            COMMAND rc /fo "${APP_RC_OBJ}" "resource.rc"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/resources"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/resource.rc" "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.ico"
        )
    endif()
endif()

# Main GUI executable (no console window) with embedded Windows icon resource
add_executable(lcd_aio WIN32
    src/main.cpp
    src/widgets.cpp
    src/config_manager.cpp
    src/undo_manager.cpp
    src/hw_monitor.cpp
    src/jl_panel.cpp
    src/tray_manager.cpp
    src/audio_visualizer.cpp
    src/video_player.cpp
    src/video_cropper.cpp
    src/window_streamer.cpp
    ${APP_RC_OBJ}
)

# Include paths: local src, project root (for jl_lcd.h), tortellini, and imgui
target_include_directories(lcd_aio PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}
    ${tortellini_SOURCE_DIR}/include
    ${imgui_SOURCE_DIR}
    ${rlimgui_SOURCE_DIR}
)

target_link_libraries(lcd_aio PRIVATE
    imgui_raylib
    raylib
    ole32
    oleaut32
    setupapi
    mfplat
    mfreadwrite
    mfuuid
    shell32
    comdlg32
    pdh
)

if(MINGW)
    target_link_options(lcd_aio PRIVATE -mwindows -static -static-libgcc -static-libstdc++)
endif()
