vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO mcneel/opennurbs
    REF v8.24.25281.15001
    SHA512 E115F27ABC40861A00EF445C65D9A7EC8B6E7772742C30A31BF7F0D4AC54B557C4B58B84BFE13592BE46B64711E1CA3AC016A61FD32A31EF6135702FCFD51D2C
    HEAD_REF 8.x
)

vcpkg_cmake_configure(
    SOURCE_PATH "\"
    OPTIONS
        -DOPENNURBS_BUILD_EXAMPLES=OFF
)

vcpkg_cmake_install()

# Ensure all headers are installed to include/ root
file(GLOB HEADERS "\/*.h")
file(INSTALL \ DESTINATION "\/include")

# Clean up any nested include folders
file(REMOVE_RECURSE "\/include/OpenNURBS")
file(REMOVE_RECURSE "\/include/opennurbsStatic")
file(REMOVE_RECURSE "\/debug/include")

# Manual config file with transitive dependencies
set(LIB_NAME "opennurbsStatic")
if(WIN32)
    set(LIB_EXT ".lib")
else()
    set(LIB_EXT ".a")
    set(LIB_NAME "libopennurbsStatic")
endif()

set(CONFIG_CONTENT "
add_library(opennurbs::opennurbs STATIC IMPORTED)
set_target_properties(opennurbs::opennurbs PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES \"\\\/../../include\"
    IMPORTED_LOCATION \"\\\/../../lib/\\\"
")

if(WIN32)
    string(APPEND CONFIG_CONTENT "    INTERFACE_LINK_LIBRARIES \"shlwapi.lib;\\\/../../lib/zlib.lib\"\n")
else()
    string(APPEND CONFIG_CONTENT "    INTERFACE_LINK_LIBRARIES \"z\"\n")
endif()

string(APPEND CONFIG_CONTENT ")\n")

string(APPEND CONFIG_CONTENT "if(EXISTS \"\\\/../../debug/lib/\\\")\n")
string(APPEND CONFIG_CONTENT "    set_target_properties(opennurbs::opennurbs PROPERTIES\n")
string(APPEND CONFIG_CONTENT "        IMPORTED_LOCATION_DEBUG \"\\\/../../debug/lib/\\\"\n")

if(WIN32)
    string(APPEND CONFIG_CONTENT "        INTERFACE_LINK_LIBRARIES_DEBUG \"shlwapi.lib;\\\/../../debug/lib/zlib.lib\"\n")
else()
    string(APPEND CONFIG_CONTENT "        INTERFACE_LINK_LIBRARIES_DEBUG \"z\"\n")
endif()

string(APPEND CONFIG_CONTENT "    )\n")
string(APPEND CONFIG_CONTENT "endif()\n")

file(WRITE "\/share/opennurbs/opennurbsConfig.cmake" "\")

# License
if(EXISTS "\/license.txt")
    file(INSTALL "\/license.txt" DESTINATION "\/share/\" RENAME copyright)
elseif(EXISTS "\/LICENSE")
    file(INSTALL "\/LICENSE" DESTINATION "\/share/\" RENAME copyright) 
endif()
