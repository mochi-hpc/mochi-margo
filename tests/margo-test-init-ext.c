/*
 * (C) 2015 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <margo.h>
#include <margo-logging.h>

char* readfile(const char* filename)
{
    FILE* f = fopen(filename, "r");
    if (!f) {
        perror("fopen");
        fprintf(stderr, "\tCould not open json file \"%s\"\n", filename);
        exit(EXIT_FAILURE);
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* string = malloc(fsize + 1);
    fread(string, 1, fsize, f);
    fclose(f);
    string[fsize] = 0;
    return string;
}

int main(int argc, char** argv)
{
    margo_set_global_log_level(MARGO_LOG_TRACE);

    margo_instance_id      mid;
    struct margo_init_info args = {0};
    args.json_config            = argc > 1 ? readfile(argv[1]) : NULL;

    mid = margo_init_ext("na+sm", MARGO_CLIENT_MODE, &args);

    char* config = margo_get_config(mid);
    fprintf(stderr, "----------------------------\n");
    fprintf(stderr, "%s\n", config);
    free(config);

    free((char*)args.json_config);

    margo_finalize(mid);

    return 0;
}
