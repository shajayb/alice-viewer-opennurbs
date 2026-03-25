cmake_minimum_required(VERSION 3.20)
project(AliceViewer LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(glfw3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)

add_executable(AliceViewer
    src/main.cpp
    src/AliceViewer.cpp
    include/AliceViewer.h
)

target_include_directories(AliceViewer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

find_package(OpenGL REQUIRED)
target_link_libraries(AliceViewer PRIVATE
    glfw
    imgui::imgui
    glad::glad
    OpenGL::GL
)