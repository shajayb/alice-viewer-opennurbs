vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO mcneel/opennurbs
    REF v8.24.25281.15001
    SHA512 E115F27ABC40861A00EF445C65D9A7EC8B6E7772742C30A31BF7F0D4AC54B557C4B58B84BFE13592BE46B64711E1CA3AC016A61FD32A31EF6135702FCFD51D2C
    HEAD_REF 8.x
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DOPENNURBS_BUILD_EXAMPLES=OFF
)

vcpkg_cmake_install()

# Ensure all headers are installed to include/ root, preserving structure
file(COPY "${SOURCE_PATH}/" DESTINATION "${CURRENT_PACKAGES_DIR}/include" FILES_MATCHING PATTERN "*.h")

# Clean up any nested include folders
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/include/OpenNURBS")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/include/opennurbsStatic")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

# Manual config file with transitive dependencies
set(LIB_NAME "opennurbsStatic")
if(WIN32)
    set(LIB_EXT ".lib")
    set(ZLIB_NAME "zlib.lib")
else()
    set(LIB_EXT ".a")
    set(LIB_NAME "libopennurbsStatic")
    # Detect zlib name on Linux (McNeel's zlib built with Z_PREFIX)
    if(EXISTS "${CURRENT_PACKAGES_DIR}/lib/libzlib.a")
        set(ZLIB_NAME "libzlib.a")
    elseif(EXISTS "${CURRENT_PACKAGES_DIR}/lib/libopennurbs_zlib.a")
        set(ZLIB_NAME "libopennurbs_zlib.a")
    elseif(EXISTS "${CURRENT_PACKAGES_DIR}/lib/libopennurbs_public_zlib.a")
        set(ZLIB_NAME "libopennurbs_public_zlib.a")
    elseif(EXISTS "${CURRENT_PACKAGES_DIR}/lib/libz.a")
        set(ZLIB_NAME "libz.a")
    else()
        set(ZLIB_NAME "libzlib.a") # Fallback
    endif()
endif()

set(CONFIG_CONTENT "
add_library(opennurbs::opennurbs STATIC IMPORTED)
set_target_properties(opennurbs::opennurbs PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES \"\${CMAKE_CURRENT_LIST_DIR}/../../include\"
    IMPORTED_LOCATION \"\${CMAKE_CURRENT_LIST_DIR}/../../lib/${LIB_NAME}${LIB_EXT}\"
")

if(WIN32)
    string(APPEND CONFIG_CONTENT "    INTERFACE_LINK_LIBRARIES \"shlwapi.lib;\${CMAKE_CURRENT_LIST_DIR}/../../lib/${ZLIB_NAME}\"\n")
else()
    string(APPEND CONFIG_CONTENT "    INTERFACE_LINK_LIBRARIES \"uuid;pthread;dl;\${CMAKE_CURRENT_LIST_DIR}/../../lib/${ZLIB_NAME}\"\n")
endif()

string(APPEND CONFIG_CONTENT ")\n")

string(APPEND CONFIG_CONTENT "if(EXISTS \"\${CMAKE_CURRENT_LIST_DIR}/../../debug/lib/${LIB_NAME}${LIB_EXT}\")\n")
string(APPEND CONFIG_CONTENT "    set_target_properties(opennurbs::opennurbs PROPERTIES\n")
string(APPEND CONFIG_CONTENT "        IMPORTED_LOCATION_DEBUG \"\${CMAKE_CURRENT_LIST_DIR}/../../debug/lib/${LIB_NAME}${LIB_EXT}\"\n")

if(WIN32)
    string(APPEND CONFIG_CONTENT "        INTERFACE_LINK_LIBRARIES_DEBUG \"shlwapi.lib;\${CMAKE_CURRENT_LIST_DIR}/../../debug/lib/${ZLIB_NAME}\"\n")
else()
    # In debug config, the zlib name might be the same or have a 'd' suffix, but openNURBS usually keeps it same for static
    string(APPEND CONFIG_CONTENT "        INTERFACE_LINK_LIBRARIES_DEBUG \"uuid;pthread;dl;\${CMAKE_CURRENT_LIST_DIR}/../../debug/lib/${ZLIB_NAME}\"\n")
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
