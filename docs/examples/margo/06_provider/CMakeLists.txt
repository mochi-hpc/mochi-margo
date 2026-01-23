add_library (06_alpha_client alpha-client.c)
target_include_directories(06_alpha_client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries (06_alpha_client PkgConfig::MARGO)

add_library (06_alpha_server alpha-server.c)
target_include_directories(06_alpha_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries (06_alpha_server PkgConfig::MARGO)

add_executable (06_margo_server server.c)
target_include_directories(06_margo_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries (06_margo_server 06_alpha_server PkgConfig::MARGO)

add_executable (06_margo_client client.c)
target_include_directories(06_margo_client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries (06_margo_client 06_alpha_client PkgConfig::MARGO)
