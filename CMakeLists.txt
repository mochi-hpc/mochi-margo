cmake_minimum_required (VERSION 3.12)
project (margo C)
enable_testing ()

# --- Version Information ---------------------------------------------------

set (MARGO_VERSION_MAJOR 0)
set (MARGO_VERSION_MINOR 19)
set (MARGO_VERSION_PATCH 2)
set (MARGO_VERSION "${MARGO_VERSION_MAJOR}.${MARGO_VERSION_MINOR}.${MARGO_VERSION_PATCH}")
set (MARGO_VERSION_NUM "$((MARGO_VERSION_MAJOR*100000+MARGO_VERSION_MINOR*100+MARGO_VERSION_PATCH))")

# --- Compiler and Build Options --------------------------------------------

set (CMAKE_C_STANDARD 99)
set (CMAKE_C_STANDARD_REQUIRED ON)

option (ENABLE_COVERAGE "Enable code coverage" OFF)
option (ENABLE_TESTS "Enable tests" OFF)

if (ENABLE_COVERAGE)
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --coverage -O0")
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage -lgcov")
endif ()

# --- Find Dependencies -----------------------------------------------------

find_package (mercury REQUIRED)
find_package (PkgConfig REQUIRED)
pkg_check_modules (ARGOBOTS REQUIRED IMPORTED_TARGET argobots>=1.1)
pkg_check_modules (JSONC REQUIRED IMPORTED_TARGET json-c)
find_package (Threads REQUIRED)

# --- Optional Dependencies -------------------------------------------------

option (ENABLE_PLUMBER "Enable support for mochi-plumber" OFF)
if (ENABLE_PLUMBER)
    pkg_check_modules (PLUMBER REQUIRED IMPORTED_TARGET mochi-plumber)
    set (OPTIONAL_PLUMBER PkgConfig::PLUMBER)
    add_definitions (-DHAVE_MOCHI_PLUMBER)
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
