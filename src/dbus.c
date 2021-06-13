#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <gio/gio.h>

#include "dbus.h"

/**
 * Extract device object path from char path.
 * Example:
 * In: /org/bluez/hci0/dev_xx_xx/service0010/char0020
 * Out: /org/bluez/hci0/dev_xx_xx
 */
static int _extract_path_before(const char *before_string, const char *object_path_str, const char **device_path_ptr)
{
    char *pos = NULL;
    char *dev_path = NULL;
    ptrdiff_t dev_path_size;
    
    pos = strstr(object_path_str, before_string);
    if (!pos)
        return -EINVAL;

    dev_path_size = pos - object_path_str;
    if (dev_path_size == 0)
        return -EINVAL;

    dev_path = malloc(dev_path_size + 1);
    if (!dev_path)
        return errno;

    memcpy(dev_path, object_path_str, dev_path_size);
    dev_path[dev_path_size] = '\0';

    *device_path_ptr = dev_path;

    return 0;
}

void gerror_print(char msg[static 1], GError *err)
{
    char *empty = "(NULL)";
    char *gerror_msg;

    if (err->message)
        gerror_msg = err->message;
    else
        gerror_msg = empty;

    fprintf(
        stderr, "%s: domain=%u, code=%d, message=%s\n",
        msg, err->domain, err->code, gerror_msg
    );
}

static int gerror_handle(GError *err, const char *msg)
{
    const char *empty = "";

    if (err == NULL)
        return 0;

    if (!msg) {
        msg = empty;
    }
    
    gerror_print("GError", err);
    g_error_free(err);
    err = NULL;

    return -1;
}

struct chr_obj_path_info *chr_obj_path_info_create(const char *char_obj_path_str)
{
    struct chr_obj_path_info *obj_path = malloc(sizeof(struct chr_obj_path_info));
    if (!obj_path)
        return NULL;
    
    if (_extract_path_before("/service", char_obj_path_str, &obj_path->dev) < 0) {
        free(obj_path);
        return NULL;
    }

    if (_extract_path_before("/dev_", char_obj_path_str, &obj_path->hci) < 0) {
        free(obj_path);
        return NULL;
    }

    obj_path->path = char_obj_path_str;

    return obj_path;
}

unsigned dbus_enable_signals(
    DBusConnection *conn,
    DbusSignalHandler func_sig_handler,
    const char *dev_obj_path,
    char *signal_name
)
{
    guint subscr_id = g_dbus_connection_signal_subscribe(
        conn,
        DBUS_ORG_BLUEZ_BUS,
        DBUS_IFACE_PROPERTIES,
        DBUS_SIGNAL_PROPERTIES_CHANGED,
        dev_obj_path,
        NULL,
        G_DBUS_SIGNAL_FLAGS_NONE,
        func_sig_handler,
        NULL,
        NULL
    );

    return subscr_id;
}

int _dbus_hci_discovery_call_method_sync(DBusConnection *conn, char *hci_obj_path, char *method)
{
    GError *gerr = NULL;
    GVariant *res = g_dbus_connection_call_sync(
        conn,
        DBUS_ORG_BLUEZ_BUS,
        hci_obj_path, // "/org/bluez/hci0",
        "org.bluez.Adapter1",
        method,
        NULL, /* parameters= */
        NULL, /* reply_type= */
        G_DBUS_CALL_FLAGS_NONE,
        -1, /* timeout= */
        NULL, /* cancellable= */
        &gerr
    );

    if (!res) {
        gerror_handle(gerr, "StartDiscovery failed");
        return -1;
    }

    g_variant_unref(res);

    return 0;
}

int dbus_hci_start_discovery(DBusConnection *conn, char *hci_obj_path)
{
    return _dbus_hci_discovery_call_method_sync(conn, hci_obj_path, "StartDiscovery");
}

int dbus_hci_stop_discovery(DBusConnection *conn, char *hci_obj_path)
{
    return _dbus_hci_discovery_call_method_sync(conn, hci_obj_path, "StopDiscovery");
}
