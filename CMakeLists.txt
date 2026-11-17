# CMakeLists.txt - Build the Rob Run external overlay.
#
# Prereqs:
#   - Visual Studio 2022 (MSVC v143) or Build Tools, x64 toolchain
#   - CMake 3.21+
#   - Dear ImGui sources checked out into vendor/imgui (see README)
#
# Build:
#   cmake -B build -S . -A x64
#   cmake --build build --config Release
# Output: build/Release/RobRunOverlay.exe

cmake_minimum_required(VERSION 3.21)
project(RobRunOverlay LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(FATAL_ERROR "This project targets Windows only.")
endif()

# --- ImGui -------------------------------------------------------------------
set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/vendor/imgui")

if(NOT EXISTS "${IMGUI_DIR}/imgui.h")
    message(FATAL_ERROR
        "Dear ImGui sources not found at ${IMGUI_DIR}.\n"
        "Clone them with:\n"
        "  git clone --depth 1 https://github.com/ocornut/imgui vendor/imgui")
endif()

set(IMGUI_SOURCES
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/imgui_demo.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_win32.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_dx11.cpp"
)

# --- Overlay executable ------------------------------------------------------
add_executable(RobRunOverlay WIN32
    src/main.cpp
    src/Overlay.cpp
    src/ESP.cpp
    src/Config.cpp
    src/Version.cpp
    user/DriverClient.cpp
    ${IMGUI_SOURCES}
)

target_include_directories(RobRunOverlay PRIVATE
    src
    user
    driver
    "${IMGUI_DIR}"
    "${IMGUI_DIR}/backends"
)

target_compile_definitions(RobRunOverlay PRIVATE
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(RobRunOverlay PRIVATE
    d3d11
    dxgi
    dwmapi
    psapi
    version
)

# Strip CRT down for a smaller binary (optional).
if(MSVC)
    target_compile_options(RobRunOverlay PRIVATE /W4 /WX /permissive- /MP)
    set_property(TARGET RobRunOverlay PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
