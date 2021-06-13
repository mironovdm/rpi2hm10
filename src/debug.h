#ifndef __DEBUG_HHH_
#define __DEBUG_HHH_

#include <stdio.h>

extern int debug_ena;

void debug_init(void);
void debug(char *msg);

typedef void (*DbusSignalHandler)(
    GDBusConnection *connection,
    const gchar *sender_name,
    const gchar *object_path,
    const gchar *interface_name,
    const gchar *signal_name,
    GVariant *parameters,
    gpointer user_data
);

#define DEBUGF(...) if (debug_ena) printf(__VA_ARGS__)

#endif /* __H_DEBUG__ */
