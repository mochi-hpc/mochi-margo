cmake_minimum_required (VERSION 3.12)
project (margo C)
enable_testing ()

# --- Version Information ---------------------------------------------------

set (MARGO_VERSION_MAJOR 0)
set (MARGO_VERSION_MINOR 23)
set (MARGO_VERSION_PATCH 0)
set (MARGO_VERSION "${MARGO_VERSION_MAJOR}.${MARGO_VERSION_MINOR}.${MARGO_VERSION_PATCH}")
math (EXPR MARGO_VERSION_NUM
      "${MARGO_VERSION_MAJOR}*100000+${MARGO_VERSION_MINOR}*100+${MARGO_VERSION_PATCH}")

# --- Compiler and Build Options --------------------------------------------

set (CMAKE_C_STANDARD 99)
set (CMAKE_C_STANDARD_REQUIRED ON)

option (ENABLE_COVERAGE "Enable code coverage" OFF)
option (ENABLE_TESTS "Enable tests" OFF)
option (ENABLE_ASAN "Enable address sanitizer" OFF)
option (ENABLE_EXAMPLES "Build examples in the docs folder" OFF)

if (ENABLE_COVERAGE)
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --coverage -O0")
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage -lgcov")
endif ()

if (ENABLE_ASAN)
    set (CMAKE_C_FLAGS "-fno-omit-frame-pointer -g -Wall -fsanitize=address")
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
endif ()

set (CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
set (CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# --- Find Dependencies -----------------------------------------------------

find_package (mercury REQUIRED)
find_package (PkgConfig REQUIRED)
pkg_check_modules (ARGOBOTS REQUIRED IMPORTED_TARGET argobots>=1.2)
pkg_check_modules (JSONC REQUIRED IMPORTED_TARGET json-c)
find_package (Threads REQUIRED)
set (PC_REQUIRES "mercury argobots >= 1.2 json-c")

# --- Optional Dependencies -------------------------------------------------

option (ENABLE_PLUMBER "Enable support for mochi-plumber" OFF)
if (ENABLE_PLUMBER)
    pkg_check_modules (libfabric REQUIRED IMPORTED_TARGET libfabric)
    pkg_check_modules (hwloc REQUIRED IMPORTED_TARGET hwloc)
    set (OPTIONAL_PLUMBER_DEPS PkgConfig::libfabric PkgConfig::hwloc)
    set (OPTIONAL_PLUMBER_SRC mochi-plumber.c)
    set (PC_REQUIRE "${PC_REQUIRE} libfabric hwloc")
    add_definitions (-DHAVE_MOCHI_PLUMBER)
    message (STATUS "Plumber is enabled")
endif ()

# --- Configure Headers -----------------------------------------------------

configure_file (
    "${PROJECT_SOURCE_DIR}/include/margo-version.h.in"
    "${PROJECT_BINARY_DIR}/include/margo-version.h"
)

# --- Add Subdirectories ----------------------------------------------------

add_subdirectory (src)
add_subdirectory (examples)
if (ENABLE_TESTS)
    add_subdirectory (tests)
endif ()
if (ENABLE_EXAMPLES)
    add_subdirectory (docs/examples/margo)
endif ()

# --- Installation ----------------------------------------------------------

include (GNUInstallDirs)

install (TARGETS margo margo-hg-shim
    EXPORT margo-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install (DIRECTORY ${PROJECT_SOURCE_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

install (DIRECTORY ${PROJECT_BINARY_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

include (CMakePackageConfigHelpers)

write_basic_package_version_file (
      "margo-config-version.cmake"
      VERSION ${MARGO_VERSION}
      COMPATIBILITY AnyNewerVersion)

# generate our config file for find_package()
set (INCLUDE_INSTALL_DIR include/)
set (LIB_INSTALL_DIR lib/)
configure_package_config_file (margo-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/margo-config.cmake
    INSTALL_DESTINATION ${LIB_INSTALL_DIR}/margo/cmake
    PATH_VARS INCLUDE_INSTALL_DIR)

install (FILES "${CMAKE_CURRENT_BINARY_DIR}/margo-config.cmake"
               "${CMAKE_CURRENT_BINARY_DIR}/margo-config-version.cmake"
         DESTINATION "share/cmake/margo" )
install (EXPORT margo-targets
         DESTINATION "share/cmake/margo"
         FILE "margo-targets.cmake")

# --- Pkg-config Generation -------------------------------------------------

set (prefix ${CMAKE_INSTALL_PREFIX})
set (exec_prefix "\${prefix}")
set (libdir "\${exec_prefix}/${CMAKE_INSTALL_LIBDIR}")
set (includedir "\${prefix}/${CMAKE_INSTALL_INCLUDEDIR}")

configure_file (
    "${PROJECT_SOURCE_DIR}/maint/margo.pc.in"
    "${PROJECT_BINARY_DIR}/maint/margo.pc"
    @ONLY
)

configure_file (
    "${PROJECT_SOURCE_DIR}/maint/margo-hg-shim.pc.in"
    "${PROJECT_BINARY_DIR}/maint/margo-hg-shim.pc"
    @ONLY
)

install (FILES
    "${PROJECT_BINARY_DIR}/maint/margo.pc"
    "${PROJECT_BINARY_DIR}/maint/margo-hg-shim.pc"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig"
)
