#include <stdlib.h>

#include "debug.h"

int debug_ena = 0;

void debug_init()
{
    if (getenv("RPIBLE_DEBUG")) {
        debug_ena = 1;
    }
}

void debug(char *msg)
{
    if (debug_ena)
        puts(msg);
}
