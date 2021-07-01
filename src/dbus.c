#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <stdio.h>

#include <gio/gio.h>

#include "dbus.h"

/**
 * Extract device object path from char path.
 * Example:
 * In: /org/bluez/hci0/dev_xx_xx/service0010/char0020
 * Out: /org/bluez/hci0/dev_xx_xx
 */
static int _extract_path_before(
    const char *before_string, const char *object_path_str, const char **device_path_ptr
)
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

/**
 * Print gerror and free.
 */
int gerror_handle(GError *err, const char *msg)
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
    GDBusConnection *conn,
    DbusSignalHandler func_sig_handler,
    const char *dev_obj_path,
    char *signal_name,
    struct scan_cb_params *cb_params
)
{
    guint subscr_id = g_dbus_connection_signal_subscribe(
        conn,
        DBUS_UNAME_ORG_BLUEZ,
        DBUS_OBJ_IFACE_PROPERTIES,
        DBUS_OBJ_SIGNAL_PROPERTIES_CHANGED,
        dev_obj_path,
        NULL,
        G_DBUS_SIGNAL_FLAGS_NONE,
        func_sig_handler,
        (gpointer)cb_params,
        NULL
    );

    return subscr_id;
}

int _dbus_hci_call_method_sync(
    GDBusConnection *conn, const char *hci_obj_path, const char *method, GVariant *params
)
{
    GError *gerr = NULL;
    GVariant *res = g_dbus_connection_call_sync(
        conn,
        DBUS_UNAME_ORG_BLUEZ,
        hci_obj_path,
        DBUS_OBJ_IFACE_ADAPTER1,
        method,
        params,
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

int dbus_hci_start_discovery(GDBusConnection *conn, const char *hci_obj_path)
{
    return _dbus_hci_call_method_sync(conn, hci_obj_path, "StartDiscovery", NULL);
}

int dbus_hci_stop_discovery(GDBusConnection *conn, const char *hci_obj_path)
{
    return _dbus_hci_call_method_sync(conn, hci_obj_path, "StopDiscovery", NULL);
}

int dbus_hci_set_discovery_filter(
    GDBusConnection *conn, const char *hci_obj_path, const char *transport
)
{
    GVariantBuilder builder;
    GVariant *params_asv;
    GVariant *params_tuple;

    g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&builder, "{sv}", "Transport", g_variant_new_string(transport));
    params_asv = g_variant_builder_end(&builder);
    params_tuple = g_variant_new_tuple(&params_asv, 1);

    /* Floating params will be consumed by g_dbus_connection_call_sync */
    return _dbus_hci_call_method_sync(conn, hci_obj_path, "SetDiscoveryFilter", params_tuple);
}