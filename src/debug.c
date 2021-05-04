#include <stdlib.h>

#include "debug.h"

int debug_ena = 0;

void debug_init()
{
    char *debug_env = getenv("RPIBLE_DEBUG");
    if (debug_env) {
        debug_ena = 1;
    }
}

void debug(char *msg)
{
    if (debug_ena)
        puts(msg);
}
