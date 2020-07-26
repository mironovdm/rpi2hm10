#include <errno.h>
#include <inttypes.h>
#include <unistd.h>
#include <getopt.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "argparse.h"

const char *help_text = ""
    "Raspberry Pi to HM-10 connector\n"
    "\n"
    "Usage:\n"
    "rpi2hm [options] -d [dev_path] -c [chr_path]\n\n"
    "Options:\n"
    "  -d, --dev     Bluetooth device path, required. Must be in format: \n"
    "                /org/bluez/hci0/dev_1A_2B_3C_4D_5E_6F\n"
    "  -c, --char    BLE characteristic. Must be in format:\n"
    "                /org/bluez/hci0/dev_1A_2B_3C_4D_5E_6F/service0010/char0011\n"
    "  -h, --host    Host where socket connection will be exposed. Default: localhost\n"
    "  -p, --port    TCP port. Optional. Default: 3000\n"
    "  -r, --reconnect    Do not exit when BLE connection is losts, will try to\n"
    "                     reconnect if there is data to send.\n";

static struct option long_opts[] = {
	{"dev", 1, NULL, 'd'},
    {"char", 1, NULL, 'c'},
    {"host", 1, NULL, 'h'},
    {"port", 1, NULL, 'p'},
    {"reconnect", 0, NULL, 'r'},
    {"help", 0, NULL, 'i'}
};

struct app_options opts = {0};

#define ARG_SET(flag) (opts.arg_flags & flag)

static char *copy_optarg(void) {
    char *ptr = malloc(strlen(optarg) + 1);
    if (ptr)
        return strcpy(ptr, optarg);
    perror("Error: no memory");
    exit(1);
}

static void opt_handle_host()
{
    opts.host = copy_optarg();
    opts.arg_flags |= ARG_HOST;
}

/* TODO: convert port number with strtol */
static int opt_handle_port()
{
    unsigned long port_num = 0;

    sscanf(optarg, "%lu", &port_num);
    if (port_num >= UINT16_MAX || port_num == 0) {
        fprintf(stderr, "Error: invalid port number\n");
        return -EINVAL;
    }
    opts.port = port_num;
    opts.arg_flags |= ARG_PORT;

    return 0;
}

static void opt_handle_dev_path()
{
    opts.dev_path = copy_optarg();
    opts.arg_flags |= ARG_DEV_PATH;
}

static void opt_handle_char_path()
{
    opts.chr_path = copy_optarg();
    opts.arg_flags |= ARG_CHR_PATH;
}

static inline void opt_handle_reconnect(void)
{
    opts.reconnect = 1;
    opts.arg_flags |= ARG_RECON;
}

static inline int is_set_required_args(void) {
    return (opts.arg_flags & REQUIRED_ARG_MASK) == REQUIRED_ARG_MASK;
}

static int parse_option(int opt) {
    switch (opt)
    {
        /* Host to listen on */
        case 'h':
            opt_handle_host();
            break;

        /* Port to listen on */
        case 'p':
            return opt_handle_port();

        /* Reconnection mode */
        case 'r':
            opt_handle_reconnect();
            break;

        /* Device */
        case 'd':
            opt_handle_dev_path();
            break;

        /* Characteristic */
        case 'c':
            opt_handle_char_path();
            break;

        case 'i':
            puts(help_text);
            exit(0);

        case '?':
            fprintf(stderr, "Error: Unknown argument -%c(int value=%d)\n", (char)opt, opt);
            return -1;
    }

    return 0;
}

/**
 * Set default values for not defined arguments
 */
static void arg_set_default(void)
{
    if (!ARG_SET(ARG_HOST)) {
        opts.host = "localhost";
    }

    if (!ARG_SET(ARG_PORT)) {
        opts.port = 3000;
    }
}

static int parse_options(int argc, char *argv[])
{
    int opt = 0;

    while ((opt = getopt_long(argc, argv, "h:p:d:c:r", long_opts, NULL)) != -1) {
        if (parse_option(opt) < 0) {
            return -EINVAL;
        }
    }

    return 0;
}

int parse_args(int argc, char *argv[])
{
    if (parse_options(argc, argv) < 0)
        return -EINVAL;

    if (!is_set_required_args()) {
        fprintf(stderr, "Error: required parameters are not defined\n");
        return -EINVAL;
    }

    arg_set_default();

    return 0;
}
