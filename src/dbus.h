#ifndef __DBUS_H
#define __DBUS_H

#include <gio/gio.h>

#define DBUS_UNAME_ORG_BLUEZ "org.bluez"
#define DBUS_OBJ_IFACE_PROPERTIES "org.freedesktop.DBus.Properties"
#define DBUS_OBJ_IFACE_ADAPTER1 (DBUS_UNAME_ORG_BLUEZ ".Adapter1")

#define BLUEZ_ERR_INTERFACE "org.bluez.Error"
#define BLUEZ_ERR_NOT_CONNECTED (BLUEZ_ERR_INTERFACE ".NotConnected")

#define DBUS_OBJ_SIGNAL_PROPERTIES_CHANGED "PropertiesChanged"

#define DISCOVERY_FILTER_TRANSPORT_KEY "Transport"
#define DISCOVERY_FILTER_TRANSPORT_LE "le"

struct chr_obj_path_info {
    const char *path;
    const char *dev;
    const char *hci;
};

typedef void (*DbusSignalHandler)(
    GDBusConnection *connection,
    const gchar *sender_name,
    const gchar *object_path,
    const gchar *interface_name,
    const gchar *signal_name,
    GVariant *parameters,
    gpointer user_data
);

struct scan_cb_params {
    int event_fd;
};


void gerror_print(char msg[static 1], GError *err);
int gerror_handle(GError *err, const char *msg);

unsigned dbus_enable_signals(
    GDBusConnection *conn,
    DbusSignalHandler func_sig_handler,
    const char *dev_obj_path,
    char *signal_name,
    struct scan_cb_params *cb_params
);
int dbus_hci_start_discovery(GDBusConnection *conn, const char *hci_obj_path);
int dbus_hci_stop_discovery(GDBusConnection *conn, const char *hci_obj_path);

/**
 * Call SetDiscoveryFilter with Transport=le
 */
int dbus_hci_set_discovery_filter(
    GDBusConnection *conn, const char *hci_obj_path, const char *transport
);

/**
 * Returns 'struct chr_obj_path' with device object path and HCI object path, 
 * extracted from full characteristic object path.
 * Return NULL if data couldn't be extracted.
 */
struct chr_obj_path_info *chr_obj_path_info_create(const char *char_obj_path_str);

#endif
