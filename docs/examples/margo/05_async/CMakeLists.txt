add_executable (05_margo_server server.c)
target_link_libraries (05_margo_server margo)

add_executable (05_margo_client client.c)
target_link_libraries (05_margo_client margo)
