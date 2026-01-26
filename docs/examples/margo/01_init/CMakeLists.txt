add_executable (01_margo_server server.c)
target_link_libraries (01_margo_server margo)

add_executable (01_margo_client client.c)
target_link_libraries (01_margo_client margo)
