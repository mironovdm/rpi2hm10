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
    "rpi2hm [options] -d [dev_path] -c [chr_path]\n"
    "\n"
    "Options:\n"
    "  -d, --dev                Bluetooth device path, required. Must be in format: /org/bluez/hci0/dev_1A_2B_3C_4D_5E_6F\n"
    "  -c, --char               BLE characteristic. Must be in format: /org/bluez/hci0/dev_1A_2B_3C_4D_5E_6F/service0010/char0011\n"
    "  -h, --host               Host where socket connection will be exposed. Default: localhost\n"
    "  -p, --port               TCP port. Optional. Default: 3000\n"
    "  -k, --keep-ble-con       Do not disconnect from BLE device on exit\n";

static struct option long_opts[] = {
	{"dev", ARG_VAL_REQUIRED, NULL, 'd'},
    {"char", ARG_VAL_REQUIRED, NULL, 'c'},
    {"host", ARG_VAL_REQUIRED, NULL, 'h'},
    {"port", ARG_VAL_REQUIRED, NULL, 'p'},
    {"help", ARG_FLAG, NULL, 'i'},
    {"keep-ble-con", ARG_FLAG, NULL, 'k'}
};

struct cmd_args opts = {
    .port = -1
};

static char *copy_optarg(void) {
    char *ptr = malloc(strlen(optarg) + 1);
    if (ptr)
        return strcpy(ptr, optarg);
    perror("Error: no memory");
    exit(1);
}

static int opt_handle_host(void)
{
    opts.host = copy_optarg();

    if (opts.host == NULL)
        return -ENOMEM;

    return 0;
}

/* TODO: convert port number with strtol */
static int opt_handle_port(void)
{
    unsigned long port_num = 0;

    sscanf(optarg, "%lu", &port_num);
    if (port_num >= UINT16_MAX || port_num == 0) {
        fprintf(stderr, "Error: invalid port number\n");
        return -EINVAL;
    }
    opts.port = port_num;

    return 0;
}

static int opt_handle_dev_path(void)
{
    opts.dev_path = copy_optarg();

    if (opts.dev_path == NULL)
        return -ENOMEM;

    return 0;
}

static int opt_handle_char_path(void)
{
    opts.chr_path = copy_optarg();

    if (opts.chr_path == NULL)
        return -ENOMEM;

    return 0;
}

static inline int is_set_required_args(void)
{
    return opts.dev_path != NULL && opts.chr_path != NULL;
}

static inline void opt_handle_keep_ble_con(void)
{
    opts.keep_ble_con = 1;
}

static int parse_option(int opt) {
    switch (opt)
    {
        /* Host to listen on */
        case 'h':
            return opt_handle_host();

        /* Port to listen on */
        case 'p':
            return opt_handle_port();

        /* Device */
        case 'd':
            return opt_handle_dev_path();

        /* Characteristic */
        case 'c':
            return opt_handle_char_path();

        case 'i':
            puts(help_text);
            return -ARG_ERR_HELP;

        case 'k':
            opt_handle_keep_ble_con();
            break;

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
    if (opts.host == NULL)
        opts.host = "localhost";

    if (opts.port < 0)
        opts.port = 3000;
}

static int parse_options(int argc, char *argv[])
{
    int opt = 0;

    while ((opt = getopt_long(argc, argv, "h:p:d:c:rk", long_opts, NULL)) != -1) {
        if (parse_option(opt) < 0) {
            return -1;
        }
    }

    return 0;
}

int parse_args(int argc, char *argv[])
{
    int err = parse_options(argc, argv);

    if (err < 0)       
        return err;

    if (!is_set_required_args()) {
        fprintf(stderr, "Error: required parameters are not defined\n");
        return -1;
    }

    arg_set_default();

    return 0;
}
