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

# Force Static Runtime flags for x64-windows-static
if(MSVC)
    add_compile_options(/EHsc) # Enable exceptions
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(/MTd /Zi /Ob0 /Od /RTC1)
        add_definitions(-D_DEBUG -D_ITERATOR_DEBUG_LEVEL=2)
    else()
        add_compile_options(/MT /O2 /Ob2)
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

# Structural Hardening: OpenNURBS Integration
target_compile_definitions(AliceViewer PRIVATE
    z_deflate=deflate
    z_inflate=inflate
    z_deflateEnd=deflateEnd
    z_inflateEnd=inflateEnd
    z_deflateInit_=deflateInit_
    z_inflateInit_=inflateInit_
    z_crc32=crc32
    z_adler32=adler32
    z_inflateReset=inflateReset
)

if(MSVC)
    target_link_libraries(AliceViewer PRIVATE shlwapi.lib uuid.lib Rpcrt4.lib)
    set_target_properties(AliceViewer PROPERTIES LINK_FLAGS "/FORCE:MULTIPLE")
else()
    # On Linux, wrap opennurbs and zlib in linker groups to resolve circular dependencies
    target_link_libraries(AliceViewer PRIVATE -Wl,--start-group opennurbs z -Wl,--end-group)
    set_target_properties(AliceViewer PROPERTIES LINK_FLAGS "-Wl,--allow-multiple-definition")
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
