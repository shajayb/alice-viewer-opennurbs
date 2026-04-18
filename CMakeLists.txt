cmake_minimum_required(VERSION 3.10)
project(AliceViewer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# vcpkg dependencies
find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(opennurbs CONFIG REQUIRED)
find_package(CURL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# Include directories
include_directories(include)
include_directories("docs/code snippets")

# Force Dynamic Runtime flags for x64-windows
if(MSVC)
    add_compile_options(/EHsc) # Enable exceptions
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(/MDd /Zi /Ob0 /Od /RTC1)
        add_definitions(-D_DEBUG -D_ITERATOR_DEBUG_LEVEL=2)
    else()
        add_compile_options(/MD /O2 /Ob2)
        add_definitions(-DNDEBUG)
    endif()
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

find_package(OpenGL REQUIRED)

# AliceViewer
add_executable(AliceViewer
    src/AliceViewer.cpp
    src/sketch.cpp
    src/main.cpp
)
target_link_libraries(AliceViewer
    PRIVATE
    glfw
    glad::glad
    imgui::imgui
    opennurbs::opennurbs
    CURL::libcurl
    nlohmann_json::nlohmann_json
    ${OPENGL_LIBRARIES}
)
if(MSVC)
    set_target_properties(AliceViewer PROPERTIES LINK_FLAGS "/FORCE:MULTIPLE")
endif()

# AliceOrchestrator
# add_executable(AliceOrchestrator
#     src/AliceOrchestrator.cpp
# )
# target_link_libraries(AliceOrchestrator
#     PRIVATE
#     CURL::libcurl
#     nlohmann_json::nlohmann_json
# )

# APITest
# add_executable(APITest src/APITest.cpp)
# target_link_libraries(APITest PRIVATE CURL::libcurl nlohmann_json::nlohmann_json)

# TilesetMicroTest
# add_executable(TilesetMicroTest src/TilesetMicroTest.cpp)
# target_link_libraries(TilesetMicroTest PRIVATE opennurbs::opennurbs CURL::libcurl nlohmann_json::nlohmann_json)
