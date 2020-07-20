#ifndef _H_ARGEPARSE
#define _H_ARGEPARSE

#include <stdbool.h>

struct app_options {
    char mac_addr[6];
    bool reconnect;
    unsigned arg_flags;
};

int parse_args(int, char *[]);

#endif /* end _H_ARGEPARSE */