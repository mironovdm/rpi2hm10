#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>

#include <sys/time.h>       /* For portability [M.Kerrisk p.1331] */
#include <sys/select.h>

// #include <glib.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>

#include "main.h"
#include "argparse.h"

int check_g_error(GError **err)
{
    if (*err == NULL)
        return 0;
    
    if ((*err)->message != NULL)
        printf("GError: %s\n", err->message);
    else
        printf("GError: no error message\n");

    g_error_free(*err);

    return -1;
}

int main(int argc, char *argv[])
{
    GDBusConnection *con;
    GError *err = NULL;

    gchar *sys_bus_addr = g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SYSTEM, NULL, &err);
    if (check_g_error(&err) < 0)
        return 1;

    con = g_dbus_connection_new_for_address_sync(
        (gchar *)sys_bus_addr,
        G_DBUS_CONNECTION_FLAGS_NONE, /* flags */
        NULL, /* *observer */
        NULL, /* *cancellable */
        &err
    );
    if (check_g_error(&err) < 0)
        return 1;

    g_dbus_connection_close_sync(con, NULL, &err);
    check_g_error(&err);

    return 0;
}
