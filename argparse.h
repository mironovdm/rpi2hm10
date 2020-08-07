#ifndef __H_ARGEPARSE_
#define __H_ARGEPARSE_

#include <stdbool.h>
#include <stdint.h>

#define ARG_VAL_REQUIRED 1
#define ARG_NO_VAL 0

/* Not an error actually, just indicates the presence of "--help" argument */
#define ARG_ERR_HELP (512)

struct cmd_args {
    uint16_t port;
    char *host;
    char *dev_path;
    char *chr_path;
    int reconnect;
    int keep_ble_con;   /* We don't have to disconnect BLE device on every exit. We can keep
                           keep connection and use connected device on next launch */
                        /* @TODO: check if we can use already connected device */
    unsigned arg_flags; /* Bit mask of args that were passed to program */
};

extern const char *help_text;
extern struct cmd_args opts;

int parse_args(int, char *[]);

#endif /* end _H_ARGEPARSE */