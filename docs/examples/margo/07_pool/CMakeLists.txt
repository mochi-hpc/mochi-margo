add_executable (07_margo_server server.c)
target_include_directories (07_margo_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../06_provider)
target_link_libraries (07_margo_server 06_alpha_server margo)
