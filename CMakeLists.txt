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
find_package(ZLIB REQUIRED)
find_package(CURL REQUIRED)
find_package(glm CONFIG REQUIRED)

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
    glm::glm
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
    set_target_properties(AliceViewer PROPERTIES LINK_FLAGS "/FORCE:MULTIPLE /alternatename:z_deflate=deflate /alternatename:z_inflate=inflate /alternatename:z_deflateEnd=deflateEnd /alternatename:z_inflateEnd=inflateEnd /alternatename:z_deflateInit_=deflateInit_ /alternatename:z_inflateInit_=inflateInit_ /alternatename:z_crc32=crc32 /alternatename:z_adler32=adler32 /alternatename:z_inflateReset=inflateReset")
else()
    # On Linux, wrap opennurbs and zlib in linker groups to resolve circular dependencies
    target_link_libraries(AliceViewer PRIVATE -Wl,--start-group opennurbs::opennurbs ZLIB::ZLIB -Wl,--end-group)
    set_target_properties(AliceViewer PROPERTIES LINK_FLAGS "-Wl,--allow-multiple-definition -Wl,--defsym,z_deflate=deflate -Wl,--defsym,z_inflate=inflate -Wl,--defsym,z_deflateEnd=deflateEnd -Wl,--defsym,z_inflateEnd=inflateEnd -Wl,--defsym,z_deflateInit_=deflateInit_ -Wl,--defsym,z_inflateInit_=inflateInit_ -Wl,--defsym,z_crc32=crc32 -Wl,--defsym,z_adler32=adler32 -Wl,--defsym,z_inflateReset=inflateReset")
endif()

# Auto-generate convenience batch scripts into the build directory
if(WIN32)
    file(WRITE ${CMAKE_BINARY_DIR}/temp_build.bat
        "@echo off\n"
        "call \"C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Professional\\VC\\Auxiliary\\Build\\vcvars64.bat\"\n"
        "ninja -C .\n"
    )

    file(WRITE ${CMAKE_BINARY_DIR}/run_all.bat
        "@echo off\n"
        "set \"PATH=C:\\Program Files\\LLVM\\bin;%PATH%\"\n"
        "call \"C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Professional\\VC\\Auxiliary\\Build\\vcvars64.bat\"\n"
        "ninja -C .\n"
        ".\\bin\\AliceViewer.exe --headless --headless-capture > executor_console.log 2>&1\n"
    )
endif()
