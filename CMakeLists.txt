project(1c-task LANGUAGES CXX C)

cmake_minimum_required(VERSION 3.16)
project(ImGui_SDL3_OpenGL_Demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
# ============================================================
# Dependencies — all fetched from source
# ============================================================
include(FetchContent)

# --- SDL3 ---
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.2.14
    GIT_SHALLOW    TRUE
)
set(SDL_PIPEWIRE OFF CACHE BOOL "Disable PipeWire audio" FORCE)
FetchContent_MakeAvailable(SDL3)

# --- Dear ImGui ---
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

# --- ImPlot ---
FetchContent_Declare(
    implot
    GIT_REPOSITORY https://github.com/epezent/implot.git
    GIT_TAG        v0.17
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(implot)

# --- SQLite ---
FetchContent_Declare(
    sqlite3
    URL https://www.sqlite.org/2025/sqlite-amalgamation-3490100.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(sqlite3)

find_package(OpenGL REQUIRED)
# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------
add_executable(app src/main.cpp src/database.cpp)

# --- ImGui static lib (no upstream CMakeLists) ---
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC SDL3::SDL3)

# --- ImPlot static lib (no upstream CMakeLists) ---
add_library(implot STATIC
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp
)
target_include_directories(implot PUBLIC ${implot_SOURCE_DIR})
target_link_libraries(implot PUBLIC imgui)

# --- SQLite static lib ---
add_library(sqlite3 STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
target_include_directories(sqlite3 PUBLIC ${sqlite3_SOURCE_DIR})
target_compile_definitions(sqlite3 PRIVATE SQLITE_THREADSAFE=1)

target_link_libraries(app PRIVATE
    imgui
    implot
    SDL3::SDL3
    OpenGL::GL
    sqlite3
)

target_include_directories(app PRIVATE
    include
    src
)


# On Windows, you might need to link against opengl32 explicitly.
if (WIN32)
    target_link_libraries(app PRIVATE opengl32)
endif()

# ---------------------------------------------------------------------------
# Platform-specific settings
# ---------------------------------------------------------------------------
# For macOS, you may need to set a deployment target if you encounter issues.
if (APPLE)
    set_target_properties(app PROPERTIES
        MACOSX_DEPLOYMENT_TARGET "10.15"
    )
endif()
