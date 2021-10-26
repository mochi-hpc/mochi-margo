/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include "helper-server.h"
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>

int HS_start(const char* server_addr,
             struct margo_init_info* margo_args,
             HS_function_t init_server_fn,
             HS_function_t run_server_fn,
             void* uargs, char* addr,
             hg_size_t* addr_size)
{
    int p[2], pid = 0;
    if(pipe(p) < 0)
        exit(-1);

    if((pid = fork()) == 0) {
        // child process (server)
        close(p[0]);
        margo_instance_id mid = margo_init_ext(
                server_addr, MARGO_SERVER_MODE, margo_args);
        margo_enable_remote_shutdown(mid);
        if(init_server_fn) {
            init_server_fn(mid, uargs);
        }
        if(addr) {
            hg_addr_t self_addr = HG_ADDR_NULL;
            margo_addr_self(mid, &self_addr);
            margo_addr_to_string(mid, addr, addr_size, self_addr);
            margo_addr_free(mid, self_addr);
            write(p[1], addr_size, sizeof(*addr_size));
            write(p[1], addr, *addr_size);
        }
        close(p[1]);
        int ret = 0;
        if(run_server_fn) {
            ret = run_server_fn(mid, uargs);
        } else {
            margo_wait_for_finalize(mid);
        }
        exit(ret);
    } else {
        // parent process (unit test)
        close(p[1]);
        if(addr) {
            memset(addr, 0, *addr_size);
            read(p[0], addr_size, sizeof(*addr_size));
            read(p[0], addr, *addr_size);
        }
        close(p[0]);
    }
    return pid;
}

int HS_stop(int pid, int k)
{
    int kret;
    if(k) {
        kret = kill(pid, SIGKILL);
    }
    int status;
    waitpid(pid, &status, 0);
    return k ? kret : WIFEXITED(status);
}
