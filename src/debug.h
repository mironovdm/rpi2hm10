#ifndef __DEBUG_HHH_
#define __DEBUG_HHH_

#include <stdio.h>

extern int debug_ena;

void debug_init(void);
void debug(char *msg);

#define DEBUGF(...) if (debug_ena) printf(__VA_ARGS__)

#endif /* __H_DEBUG__ */
