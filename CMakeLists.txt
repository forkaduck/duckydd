cmake_minimum_required(VERSION 3.15)
project(duckydd C)
set(CMAKE_C_STANDARD 99)

include(${CMAKE_ROOT}/Modules/ExternalProject.cmake)

# set path variables for installation
set (INSTALL_DIR "/usr/bin")
set (DAEMON_CONF_DIR "/etc/duckydd")
set (SYSTEMD_UNIT_DIR "/usr/lib/systemd/system/")

# set build type
set (CMAKE_BUILD_TYPE Release)

# set source and include directories
include_directories (include)
file (GLOB SOURCES "src/*.c")

add_executable(duckydd ${SOURCES})


ExternalProject_Add(sslib
	PREFIX ${CMAKE_SOURCE_DIR}/safestringlib
	GIT_REPOSITORY "https://github.com/intel/safestringlib"
	UPDATE_COMMAND git pull
	SOURCE_DIR "${CMAKE_SOURCE_DIR}/safestringlib/safeclib"
	BINARY_DIR "${CMAKE_SOURCE_DIR}/safestringlib/build"
	BUILD_ALWAYS true
	BUILD_COMMAND make
	INSTALL_COMMAND ""
	LOG_INSTALL 0
	LOG_UPDATE 0
)

# find libs
find_library(UDEV_LIBPATH udev)

add_library(safestringlib STATIC IMPORTED)
set_target_properties(safestringlib PROPERTIES
        IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/safestringlib/build/libsafestring_static.a"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/safestringlib/include")

target_link_libraries(duckydd "${UDEV_LIBPATH}" safestringlib)


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

# reload the systemd daemon so that it can find the service file
execute_process(COMMAND bash -c "sudo systemctl daemon-reload" RESULT_VARIABLE RETURNV)
if(RETURNV EQUAL "1")
    message(FATA_ERROR "sudo systemctl daemon-reload returned non 0")
endif()

