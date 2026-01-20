add_library (margo SHARED
    margo-core.c
    margo-config.c
    margo-abt-config.c
    margo-hg-config.c
    margo-abt-profiling.c
    margo-bulk-pool.c
    margo-globals.c
    margo-handle-cache.c
    margo-init.c
    margo-identity.c
    margo-logging.c
    margo-timer.c
    margo-util.c
    margo-prio-pool.c
    margo-efirst-pool.c
    margo-monitoring.c
    margo-default-monitoring.c
    ${OPTIONAL_PLUMBER_SRC}
)

add_library (margo-hg-shim SHARED
    margo-hg-shim.c
)

add_executable (margo-info
    margo-info.c
)

target_include_directories (margo PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries (margo
    PUBLIC
        mercury
        PkgConfig::ARGOBOTS
        PkgConfig::JSONC
        Threads::Threads
        ${OPTIONAL_PLUMBER_DEPS}
)

target_link_libraries (margo-hg-shim
    PUBLIC
        margo
)

target_link_libraries (margo-info
    PRIVATE
        margo
)

install (TARGETS margo-info DESTINATION ${CMAKE_INSTALL_BINDIR})
