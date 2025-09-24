enable_testing ()

add_executable (margo-test-init-ext
    margo-test-init-ext.c
)

add_executable (margo-test-sleep
    margo-test-sleep.c
)

add_executable (margo-test-server
    margo-test-server.c
    my-rpc.c
)

add_executable (margo-test-client
    margo-test-client.c
    my-rpc.c
)

add_executable (margo-test-client-timeout
    margo-test-client-timeout.c
    my-rpc.c
)

target_link_libraries (margo-test-init-ext margo)
target_link_libraries (margo-test-sleep margo)
target_link_libraries (margo-test-server margo)
target_link_libraries (margo-test-client margo)
target_link_libraries (margo-test-client-timeout margo)

add_test (NAME sleep COMMAND ${CMAKE_SOURCE_DIR}/tests/sleep.sh)
add_test (NAME basic COMMAND ${CMAKE_SOURCE_DIR}/tests/basic.sh)
add_test (NAME basic-ded-pool COMMAND ${CMAKE_SOURCE_DIR}/tests/basic-ded-pool.sh)
add_test (NAME basic-prio COMMAND ${CMAKE_SOURCE_DIR}/tests/basic-prio.sh)
add_test (NAME basic-ded-pool-prio COMMAND ${CMAKE_SOURCE_DIR}/tests/basic-ded-pool-prio.sh)
add_test (NAME timeout COMMAND ${CMAKE_SOURCE_DIR}/tests/timeout.sh)
add_test (NAME margo-info COMMAND margo-info)

set_tests_properties (sleep PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
set_tests_properties (basic PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
set_tests_properties (basic-ded-pool PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
set_tests_properties (basic-prio PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
set_tests_properties (basic-ded-pool-prio PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
set_tests_properties (timeout PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
set_tests_properties (margo-info PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests)

add_subdirectory (unit-tests)
