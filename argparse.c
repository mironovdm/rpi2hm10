#include <errno.h>
#include <unistd.h>
#include <getopt.h>
#include <stdio.h>
#include "argparse.h"

static struct app_options opts = {0};

#define ARG_HOST (1<<1)
#define ARG_PORT (1<<2)
#define ARG_DEV (1<<3)
#define ARG_CHR (1<<4)
#define ARG_RECON (1<<5)
#define REQUIRED_MASK ((unsigned)(ARG_DEV | ARG_CHR))

static int parse_addr(void) {
    /* return EINVAL; */
    opts.arg_flags |= ARG_ADDR;
    return 0;
}

static void opt_handle_reconnect(void)
{
    opt.reconnect = 1;
    opt.arg_flags |= ARG_RECON;
}

static int is_set_required_args(void) {
    return (opts.arg_flags & REQUIRED_MASK) == REQUIRED_MASK;
}

static int parse_option(int opt) {
    switch (opt)
    {
        case 'h':
            return opt_handle_host();

        case 'p':
            return opt_handle_port();

        case 'r':
            opts.reconnect = 1;
            break;

        /* Device */
        case 'd':
            break;

        /* Characteristic */
        case 'c':
            break;

        case '?':
            fprintf(stderr, "Error: Unknown argument -%c(int value=%d)\n", (char)opt, opt);
            return -1;
    }

    return 0;
}

int parse_args(int argc, char *argv[])
{
    int opt = 0;

    while ((opt = getopt(argc, argv, "h:p:r:")) == 0) {
        printf("opterr=%c, int value=%d\n", (char)opterr, opterr);
        printf("optarg: %s\n", optarg);

        if (parse_option(opt) < 0) {
            return -1;
        }
    }

    if (!is_set_required_args()) {
        return -1;
    }

    return 0;
}
