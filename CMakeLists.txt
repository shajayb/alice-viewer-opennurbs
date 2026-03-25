cmake_minimum_required(VERSION 3.20)
project(AliceViewer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find dependencies
find_package(glfw3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

# Add executable
add_executable(AliceViewer
    src/main.cpp
    src/AliceViewer.cpp
    include/AliceViewer.h
)

# Include directories
target_include_directories(AliceViewer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Link libraries
find_package(OpenGL REQUIRED)
target_link_libraries(AliceViewer PRIVATE
    glfw
    imgui::imgui
    OpenGL::GL
)
