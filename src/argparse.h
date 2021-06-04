#ifndef __H_ARGEPARSE_
#define __H_ARGEPARSE_

#include <stdbool.h>
#include <stdint.h>

#define ARG_HELP (512)

struct cmd_args {
    uint16_t port;
    char *host;
    char *dev_path;
    char *chr_path;
    int reconnect;
    int keep_ble_con;   /* Don't disconnect device on exit. */
                        /* @TODO: check if we can use already connected device */
    unsigned arg_flags; /* Bit mask of args that were passed to program */
};

extern const char *help_text;

int parse_args(int, char *[static 1]);

#endif /* end _H_ARGEPARSE */
