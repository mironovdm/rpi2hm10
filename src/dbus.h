#ifndef __DBUS_H
#define __DBUS_H

#define DBUS_ORG_BLUEZ_BUS "org.bluez"
#define DBUS_IFACE_PROPERTIES "org.freedesktop.DBus.Properties"

#define DBUS_SIGNAL_PROPERTIES_CHANGED "PropertiesChanged"

struct chr_obj_path_info {
    const char *path;
    const char *dev;
    const char *hci;
};


void gerror_print(char msg[static 1], GError *err);
static int gerror_handle(GError *err, const char *msg);

unsigned dbus_enable_signals(
    DBusConnection *conn,
    DbusSignalHandler func_sig_handler,
    const char *dev_obj_path,
    char *signal_name
);

int dbus_hci_start_discovery(DBusConnection *conn, char *hci_obj_path)
int dbus_hci_stop_discovery(DBusConnection *conn, char *hci_obj_path);

/**
 * Returns 'struct chr_obj_path' with device path and HCI path, extracted from
 * full characteristic object path.
 */
struct chr_obj_path_info *chr_obj_path_info_create(const char *char_obj_path_str);

#endif
