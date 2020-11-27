cmake_minimum_required(VERSION 3.15)

# enable compilation commands file
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(${CMAKE_ROOT}/Modules/ExternalProject.cmake)

# set build type
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	message(STATUS "Setting build type to 'Release' as none was specified.")
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
	
	# Set the possible values of build type for cmake-gui
	set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif ()

message("CMAKE_BUILD_TYPE is ${CMAKE_BUILD_TYPE}")

project(duckydd C)
set(CMAKE_C_STANDARD 99)

# set path variables for installation
set (INSTALL_DIR "/usr/bin")
set (DAEMON_CONF_DIR "/etc/duckydd")
set (SYSTEMD_UNIT_DIR "/usr/lib/systemd/system/")

# The version numbering
set (VERSION_MAJOR 0)
set (VERSION_MINOR 3)
set (PATCH_LEVEL 0)

set (VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${PATCH_LEVEL}")
add_compile_definitions(VER="${VERSION}")

# set source and include directories
include_directories(${CMAKE_SOURCE_DIR}/include)
file (GLOB SOURCES "src/*.c")

add_executable(duckydd ${SOURCES})

set (CMAKE_C_FLAGS_DEBUG "-g3")
set (CMAKE_C_FLAGS_RELEASE "-O3")

set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -fstack-protector-strong")

# compile safestringlib
ExternalProject_Add(sslib
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

# find libs
find_library(UDEV_LIB udev)
find_library(XKBCOMMON_LIB xkbcommon)
find_library(XKBCOMMON_X11_LIB xkbcommon-x11)
find_library(XCB_LIB xcb)
find_library(MATH_LIB m)

add_library(safestringlib STATIC IMPORTED)
set_target_properties(safestringlib PROPERTIES
        IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/lib/safestringlib/build/libsafestring_static.a"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/lib/safestringlib/include")

add_dependencies(duckydd safestringlib sslib)
target_link_libraries(duckydd 
	PRIVATE
		${UDEV_LIB}
		${XKBCOMMON_LIB}
		${XKBCOMMON_X11_LIB}
		${XCB_LIB}
        ${MATH_LIB}
		safestringlib
)

# install the release build
install(TARGETS duckydd
        RUNTIME
        DESTINATION ${INSTALL_DIR}
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_EXECUTE
        CONFIGURATIONS Release)

install(FILES ${CMAKE_SOURCE_DIR}/duckydd.conf
        DESTINATION ${DAEMON_CONF_DIR}
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_EXECUTE
        CONFIGURATIONS Release
        )

install(FILES ${CMAKE_SOURCE_DIR}/duckydd.service
        DESTINATION ${SYSTEMD_UNIT_DIR}
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_EXECUTE
        CONFIGURATIONS Release
        )

