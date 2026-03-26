cmake_minimum_required(VERSION 3.15)
project(AliceViewer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories (assuming you move 'include' to the root)
include_directories(include)

# Add the executable with the new sketch paradigm files
add_executable(AliceViewer
    src/main.cpp
    src/AliceViewer.cpp
    src/sketch.cpp
)

# Assuming vcpkg is used for dependencies
find_package(glad CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

# Link the required libraries (OpenGL, GLFW, GLAD, ImGui)
target_link_libraries(AliceViewer PRIVATE
    glad::glad
    glfw
    imgui::imgui
    opengl32
)

# If ImGui does not export imgui::imgui target with bindings, you may need to add them.
# vcpkg's imgui provides imgui::imgui, imgui::imgui_impl_glfw, imgui::imgui_impl_opengl3
# If so, uncomment the following instead:
# target_link_libraries(AliceViewer PRIVATE glad::glad glfw imgui::imgui imgui::imgui_impl_glfw imgui::imgui_impl_opengl3 opengl32)
