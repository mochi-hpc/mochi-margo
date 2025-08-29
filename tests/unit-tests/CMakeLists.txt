add_executable (margo-addr
    munit/munit.c
    margo-addr.c
    helper-server.c
)

add_executable (margo-init
    munit/munit.c
    margo-init.c
    helper-server.c
)

add_executable (margo-identity
    munit/munit.c
    margo-identity.c
    helper-server.c
)

add_executable (margo-config-hg-log
    munit/munit.c
    margo-config-hg-log.c
    helper-server.c
)

add_executable (margo-config
    munit/munit.c
    margo-config.c
    helper-server.c
)

configure_file (
    ${CMAKE_CURRENT_SOURCE_DIR}/test-configs.json
    ${CMAKE_CURRENT_BINARY_DIR}/test-configs.json
    COPYONLY
)

add_executable (margo-elasticity
    munit/munit.c
    margo-elasticity.c
    helper-server.c
)

add_executable (margo-pool
    munit/munit.c
    margo-pool.c
    helper-server.c
)

add_executable (margo-eventual
    munit/munit.c
    margo-eventual.c
    helper-server.c
)

add_executable (margo-abt-pool
    munit/munit.c
    margo-abt-pool.c
    helper-server.c
)

add_executable (margo-logging
    munit/munit.c
    margo-logging.c
    helper-server.c
)

add_executable (margo-after-abt-init
    munit/munit.c
    margo-after-abt-init.c
    helper-server.c
)

add_executable (margo-scheduling
    munit/munit.c
    margo-scheduling.c
    helper-server.c
)

add_executable (margo-comm-error
    munit/munit.c
    margo-comm-error.c
    helper-server.c
)

add_executable (margo-comm-finalize
    munit/munit.c
    margo-comm-finalize.c
    helper-server.c
)

add_executable (margo-timer
    munit/munit.c
    margo-timer.c
)

add_executable (margo-bulk
    munit/munit.c
    margo-bulk.c
)

add_executable (margo-forward
    munit/munit.c
    margo-forward.c
    helper-server.c
)

add_executable (margo-monitoring
    munit/munit.c
    margo-monitoring.c
)

add_executable (margo-sanity-warnings
    munit/munit.c
    margo-sanity-warnings.c
)

add_executable (margo-migrate-progress
    munit/munit.c
    margo-migrate-progress.c
    helper-server.c
)

target_link_libraries (margo-addr margo)
target_link_libraries (margo-init margo)
target_link_libraries (margo-identity margo)
target_link_libraries (margo-config-hg-log margo)
target_link_libraries (margo-config margo)
target_link_libraries (margo-elasticity margo)
target_link_libraries (margo-pool margo)
target_link_libraries (margo-eventual margo)
target_link_libraries (margo-abt-pool margo)
target_link_libraries (margo-logging margo)
target_link_libraries (margo-after-abt-init margo)
target_link_libraries (margo-scheduling margo)
target_link_libraries (margo-comm-error margo)
target_link_libraries (margo-comm-finalize margo)
target_link_libraries (margo-timer margo)
target_link_libraries (margo-bulk margo)
target_link_libraries (margo-forward margo margo-hg-shim)
target_link_libraries (margo-monitoring margo)
target_link_libraries (margo-sanity-warnings margo)
target_link_libraries (margo-migrate-progress margo)

add_test (NAME margo-addr COMMAND margo-addr)
add_test (NAME margo-init COMMAND margo-init)
add_test (NAME margo-identity COMMAND margo-identity)
add_test (NAME margo-config-hg-log COMMAND margo-config-hg-log)
add_test (NAME margo-config COMMAND margo-config)
add_test (NAME margo-elasticity COMMAND margo-elasticity)
add_test (NAME margo-pool COMMAND margo-pool)
add_test (NAME margo-eventual COMMAND margo-eventual)
add_test (NAME margo-abt-pool COMMAND margo-abt-pool)
add_test (NAME margo-logging COMMAND margo-logging)
add_test (NAME margo-after-abt-init COMMAND margo-after-abt-init)
add_test (NAME margo-scheduling COMMAND margo-scheduling)
add_test (NAME margo-comm-error COMMAND margo-comm-error)
add_test (NAME margo-comm-finalize COMMAND margo-comm-finalize)
add_test (NAME margo-timer COMMAND margo-timer)
add_test (NAME margo-bulk COMMAND margo-bulk)
add_test (NAME margo-forward COMMAND margo-forward)
add_test (NAME margo-monitoring COMMAND margo-monitoring)
add_test (NAME margo-sanity-warnings COMMAND margo-sanity-warnings)
add_test (NAME margo-migrate-progress COMMAND margo-migrate-progress)
