vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO mcneel/opennurbs
    REF v8.24.25281.15001
    SHA512 E115F27ABC40861A00EF445C65D9A7EC8B6E7772742C30A31BF7F0D4AC54B557C4B58B84BFE13592BE46B64711E1CA3AC016A61FD32A31EF6135702FCFD51D2C
    HEAD_REF 8.x
)

# Remove Z_PREFIX definition to use standard zlib symbols
if(EXISTS "${SOURCE_PATH}/CMakeLists.txt")
    file(READ "${SOURCE_PATH}/CMakeLists.txt" _contents)
    string(REPLACE "-DZ_PREFIX" "" _contents "${_contents}")
    file(WRITE "${SOURCE_PATH}/CMakeLists.txt" "${_contents}")
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DOPENNURBS_BUILD_EXAMPLES=OFF
        -DOPENNURBS_ZLIB_EXTERNAL=ON
)

vcpkg_cmake_install()

# Remove conflicting zlib files
file(REMOVE "${CURRENT_PACKAGES_DIR}/lib/zlib.lib" "${CURRENT_PACKAGES_DIR}/lib/libzlib.a" "${CURRENT_PACKAGES_DIR}/lib/libz.a")
file(REMOVE "${CURRENT_PACKAGES_DIR}/debug/lib/zlib.lib" "${CURRENT_PACKAGES_DIR}/debug/lib/libzlib.a" "${CURRENT_PACKAGES_DIR}/debug/lib/libz.a")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/include/zlib")

# Ensure all headers are installed to include/ root, preserving structure
file(COPY "${SOURCE_PATH}/" DESTINATION "${CURRENT_PACKAGES_DIR}/include" FILES_MATCHING PATTERN "*.h")

# Clean up any nested include folders
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/include/OpenNURBS")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/include/opennurbsStatic")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

# Manual config file with transitive dependencies
if(WIN32)
    set(LIB_EXT ".lib")
    set(LIB_PREFIX "")
else()
    set(LIB_EXT ".a")
    set(LIB_PREFIX "lib")
endif()

set(MAIN_LIB "${LIB_PREFIX}opennurbsStatic${LIB_EXT}")

if(NOT EXISTS "${CURRENT_PACKAGES_DIR}/lib/${MAIN_LIB}")
    if(EXISTS "${CURRENT_PACKAGES_DIR}/lib/${LIB_PREFIX}OpenNURBS${LIB_EXT}")
        set(MAIN_LIB "${LIB_PREFIX}OpenNURBS${LIB_EXT}")
    endif()
endif()

set(CONFIG_CONTENT "
find_package(ZLIB REQUIRED)
add_library(opennurbs::opennurbs STATIC IMPORTED)
set_target_properties(opennurbs::opennurbs PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES \"\${CMAKE_CURRENT_LIST_DIR}/../../include\"
    IMPORTED_LOCATION \"\${CMAKE_CURRENT_LIST_DIR}/../../lib/${MAIN_LIB}\"
    INTERFACE_COMPILE_DEFINITIONS \"z_deflate=deflate;z_inflate=inflate;z_deflateEnd=deflateEnd;z_inflateEnd=inflateEnd;z_deflateInit_=deflateInit_;z_inflateInit_=inflateInit_;z_crc32=crc32;z_adler32=adler32;z_inflateReset=inflateReset\"
")

if(WIN32)
    string(APPEND CONFIG_CONTENT "    INTERFACE_LINK_LIBRARIES \"shlwapi.lib;ZLIB::ZLIB\"\n")
else()
    # Use grouping for static libraries on Linux to resolve circular dependencies
    string(APPEND CONFIG_CONTENT "    INTERFACE_LINK_LIBRARIES \"uuid;pthread;dl;ZLIB::ZLIB\"\n")
endif()


string(APPEND CONFIG_CONTENT ")\n")

string(APPEND CONFIG_CONTENT "if(EXISTS \"\${CMAKE_CURRENT_LIST_DIR}/../../debug/lib/${MAIN_LIB}\")\n")
string(APPEND CONFIG_CONTENT "    set_target_properties(opennurbs::opennurbs PROPERTIES\n")
string(APPEND CONFIG_CONTENT "        IMPORTED_LOCATION_DEBUG \"\${CMAKE_CURRENT_LIST_DIR}/../../debug/lib/${MAIN_LIB}\"\n")

if(WIN32)
    string(APPEND CONFIG_CONTENT "        INTERFACE_LINK_LIBRARIES_DEBUG \"shlwapi.lib;ZLIB::ZLIB\"\n")
else()
    string(APPEND CONFIG_CONTENT "        INTERFACE_LINK_LIBRARIES_DEBUG \"uuid;pthread;dl;ZLIB::ZLIB\"\n")
endif()

string(APPEND CONFIG_CONTENT "    )\n")
string(APPEND CONFIG_CONTENT "endif()\n")

file(WRITE "${CURRENT_PACKAGES_DIR}/share/opennurbs/opennurbsConfig.cmake" "${CONFIG_CONTENT}")

# License
if(EXISTS "${SOURCE_PATH}/license.txt")
    file(INSTALL "${SOURCE_PATH}/license.txt" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
elseif(EXISTS "${SOURCE_PATH}/LICENSE")
    file(INSTALL "${SOURCE_PATH}/LICENSE" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright) 
endif()
