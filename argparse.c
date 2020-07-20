#include <errno.h>
#include <unistd.h>
#include <getopt.h>
#include <stdio.h>
#include <inttypes.h>
#include "argparse.h"

const char *help_text = ""
    "Raspberry Pi to HM-10 connector\n"
    "\n"
    "Usage:\n"
    "rpi2hm -d [dev_path] -c [chr_path] [optional]"
    "Options:\n"
    "  -d    Bluetooth device path, required. Must be in format: \n"
    "        /org/bluez/hci0/dev_1A_2B_3C_4D_5E_6F\n"
    "  -c    BLE characteristic. Must be in format:\n"
    "        /org/bluez/hci0/dev_1A_2B_3C_4D_5E_6F/service0010/char0011\n"
    "  -h    Host where socket connection will be exposed. Default: localhost\n"
    "  -p    TCP port. Optional. Default: 3000\n"
    "  -r    Reconnect to remote BLE device when connection is lost. By default\n";
    "        reconnect is not enabled.\n"

#define COPY_OPTARG(ptr) \
    ptr = malloc(strlen(optarg) + 1); \
    strcpy(ptr, optarg);

struct app_options opts = {0};

static char *copy_optarg(void) {
    char *ptr = NULL;
    ptr = malloc(strlen(optarg) + 1);
    return strcpy(ptr, optarg);
}

static int opt_handle_host()
{
    opts.host = copy_optarg();
    opts.arg_flags |= ARG_HOST;
    
    return 0;
}

static int opt_handle_port()
{
    unsigned long port_num = 0;

    sscanf(optarg, "%lu", &port_num);
    if (port_num >= UINT16_MAX) {
        perror("Invalid port")
        return -EINVAL;
    }
    opts.arg_flags |= ARG_PORT;

    return 0;
}

static int opt_handle_dev_path()
{
    opts.dev_path = copy_optarg();
    opts.arg_flags |= ARG_DEV_PATH;
    
    return 0;
}

static int opt_handle_char_path()
{
    opts.chr_path = copy_optarg();
    opts.arg_flags |= ARG_CHR_PATH;
    return 0;
}

static void opt_handle_reconnect(void)
{
    opts.reconnect = 1;
    opts.arg_flags |= ARG_RECON;
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
            opt_handle_reconnect();
            break;

        /* Device */
        case 'd':
            return opt_handle_dev_path();

        /* Characteristic */
        case 'c':
            return opt_handle_char_path();

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
