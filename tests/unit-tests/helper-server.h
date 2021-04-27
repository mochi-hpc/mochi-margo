/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#ifndef __HELPER_SERVER
#define __HELPER_SERVER

#include <stdio.h>
#include <margo.h>

/**
 * @brief Type of a server function.
 */
typedef int (*HS_function_t)(margo_instance_id, void*);

/**
 * @brief Fork the calling process to start the server function.
 * This function will not return on the child process.
 *
 * IMPORTANT: it is the responsibility of the server_fn to call
 * margo_finalize or margo_wait_for_finalize on the provided
 * margo instance.
 *
 * Passing NULL as server_fn will simply create a margo instance
 * and call margo_wait_for_finalize on it, which can be useful if
 * the server just needs to have a margo instance running.
 *
 * @param[in] server_addr Server address or procotol (e.g. "na+sm")
 * @param[in] margo_args Arguments for the server's margo instance
 * @param[in] server_fn Server function
 * @param[in] uargs User arguments for the server function
 * @param[out] addr Address of the server (relevant on parent only)
 * @param[inout] addr_size Size of the server's address (idem)
 *
 * @return PID of the server
 */
int HS_start(const char* server_addr,
             struct margo_init_info* margo_args,
             HS_function_t init_server_fn,
             HS_function_t run_server_fn,
             void* uargs, char* addr,
             hg_size_t* addr_size);

/**
 * @brief Stops the server. If kill == 0, this function
 * will simply wait for the server to terminate, and return
 * the exit value of the the server. If kill == 1 this funcion
 * will send a SIGKILL to the server and return the return value
 * of the kill function.
 *
 * Note that if kill == 0, it is the responsibility of the caller
 * to put mechanisms in place to signal the server that it should
 * terminate (e.g. sending a margo_shutdown_remote_instance).
 *
 * @param pid PID of the server
 * @param k whether to kill the server or not.
 *
 * @return The exit value of the server process, or of the kill function.
 */
int HS_stop(int pid, int k);

#endif
