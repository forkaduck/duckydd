cmake_minimum_required(VERSION 3.18..3.19)

# Enable compilation commands file
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(${CMAKE_ROOT}/Modules/ExternalProject.cmake)

# Set build type
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	message(STATUS "Setting build type to 'Release' as none was specified.")
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
	
	# Set the possible values of build type for cmake-gui
	set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif ()

message("CMAKE_BUILD_TYPE is ${CMAKE_BUILD_TYPE}")

project(duckydd C)
set(CMAKE_C_STANDARD 99)

# Set path variables for installation
set (INSTALL_DIR "/usr/bin")
set (DAEMON_CONF_DIR "/etc/duckydd")
set (SYSTEMD_UNIT_DIR "/usr/lib/systemd/system/")

# Set source and include directories
include_directories(${CMAKE_SOURCE_DIR}/include)
file (GLOB SOURCES "src/*.c")

add_executable(duckydd ${SOURCES})

# Set compile flags
set (CMAKE_C_FLAGS_DEBUG "-g3")
set (CMAKE_C_FLAGS_RELEASE "-O3 -fpie -D_FORTIFY_SOURCE=2")
set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -fstack-protector-strong")

# Options
option(ENABLE_XKB_EXTENSION "Enable linking to xkbcommon" ON)
message("Compile xcbcommon support? ${ENABLE_XKB_EXTENSION}")

# Run config script
add_custom_target(
    Genconf ALL ./gen_configh.sh ${ENABLE_XKB_EXTENSION}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    VERBATIM USES_TERMINAL
    BYPRODUCTS ${CMAKE_SOURCE_DIR}/include/config.h
    COMMENT "Generating config.h"
)
add_dependencies(duckydd Genconf)

# Find libs
find_library(UDEV_LIB udev)
find_library(MATH_LIB m)


# Find xkb libs
if(${ENABLE_XKB_EXTENSION})
find_library(XKBCOMMON_LIB xkbcommon)
find_library(XKBCOMMON_X11_LIB xkbcommon-x11)
find_library(XCB_LIB xcb)
endif()

# Compile safestringlib
ExternalProject_Add(safestringlib_download
	PREFIX ${CMAKE_SOURCE_DIR}/lib/safestringlib
	GIT_REPOSITORY "https://github.com/intel/safestringlib"
    GIT_PROGRESS true
    UPDATE_COMMAND ""
    UPDATE_DISCONNECTED true
	SOURCE_DIR "${CMAKE_SOURCE_DIR}/lib/safestringlib/safeclib"
	BINARY_DIR "${CMAKE_SOURCE_DIR}/lib/safestringlib/build"
	BUILD_ALWAYS false
	BUILD_COMMAND make
	INSTALL_COMMAND ""
	LOG_INSTALL 0
	LOG_UPDATE 0
)

# Add safestringlib
add_library(safestringlib STATIC IMPORTED)
set_target_properties(safestringlib PROPERTIES
        IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/lib/safestringlib/build/libsafestring_static.a"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/lib/safestringlib/include")

add_dependencies(duckydd safestringlib safestringlib_download)

# Link to libraries
target_link_libraries(duckydd 
	PRIVATE
		${UDEV_LIB}
        ${MATH_LIB}
		safestringlib
)

# Link to xkb specific libraries
if(${ENABLE_XKB_EXTENSION})
target_link_libraries(duckydd 
	PRIVATE
		${XKBCOMMON_LIB}
		${XKBCOMMON_X11_LIB}
		${XCB_LIB}
)
endif()

# Install the release build
install(TARGETS duckydd
        RUNTIME
        DESTINATION ${INSTALL_DIR}
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_WRITE WORLD_READ
        CONFIGURATIONS Release)

install(FILES ${CMAKE_SOURCE_DIR}/duckydd.conf
        DESTINATION ${DAEMON_CONF_DIR}
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ GROUP_WRITE WORLD_READ
        CONFIGURATIONS Release
        )

install(FILES ${CMAKE_SOURCE_DIR}/duckydd.service
        DESTINATION ${SYSTEMD_UNIT_DIR}
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ GROUP_WRITE WORLD_READ
        CONFIGURATIONS Release
        )

