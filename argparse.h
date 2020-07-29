#ifndef _H_ARGEPARSE
#define _H_ARGEPARSE

#include <stdbool.h>
#include <stdint.h>

#define ARG_HOST (1<<1)
#define ARG_PORT (1<<2)
#define ARG_DEV_PATH (1<<3)
#define ARG_CHR_PATH (1<<4)
#define ARG_RECON (1<<5)

#define REQUIRED_ARG_MASK ((unsigned)(ARG_DEV_PATH | ARG_CHR_PATH))

#define ARG_ERR_HELP (512)

struct app_options {
    char *host;
    uint16_t port;
    char *dev_path;
    char *chr_path;
    bool reconnect;
    unsigned arg_flags; /* Bit mask of args that were passed to program */
};

extern const char *help_text;
extern struct app_options opts;

int parse_args(int, char *[]);

#endif /* end _H_ARGEPARSE */