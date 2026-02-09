add_executable (12_margo_simple simple.c)
target_link_libraries (12_margo_simple margo)

add_executable (12_margo_parent parent.c)
target_link_libraries (12_margo_parent margo)
