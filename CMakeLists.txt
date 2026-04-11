cmake_minimum_required(VERSION 3.15)

set(VCPKG_OVERLAY_PORTS "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg-overlay-ports/ports" CACHE PATH "Local vcpkg overlay ports")
if(WIN32)
    set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "")
endif()

project(AliceViewer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Force static runtime
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# --- NEW: Force consistent output directory ---
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin)

# Include directories
include_directories(include)

# Add the executable
add_executable(AliceViewer
    src/main.cpp
    src/AliceViewer.cpp
    src/sketch.cpp
)

# Dependencies via vcpkg
find_package(glad CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(opennurbs CONFIG REQUIRED)
# OVERRIDE: Using CURL for robust REST/TLS compliance with Gemini API.
find_package(CURL CONFIG REQUIRED)
find_package(Stb REQUIRED)

# Link libraries
target_link_libraries(AliceViewer PRIVATE
    glad::glad
    glfw
    imgui::imgui
    OpenGL::GL
    opennurbs::opennurbs
    CURL::libcurl
)
target_include_directories(AliceViewer PRIVATE ${Stb_INCLUDE_DIR})

# Optional: Copy DLLs on Windows if using shared libs
if(WIN32)
    add_custom_command(TARGET AliceViewer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:glfw>
        $<TARGET_FILE_DIR:AliceViewer>
    )
endif()
