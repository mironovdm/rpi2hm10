#ifndef __MAIN_H
#define __MAIN_H

#include <pthread.h>
#include <stdint.h>

#include <gio/gio.h>

#include "dbus.h"


/* Connection timeout in milliseconds */
#define CONNECT_TIMEOUT_MS 5000
/* Pause after unsuccessful reconnection. */
#define RECONNECT_ATTEMPT_INTERVAL 5
#define NOTIFY_ACQ_ATTEMPT_INTERVAL 3

struct glib_loop_info {
    GMainLoop *loop;
    pthread_t loop_thread_id;
};

/* Bluez 'AcquireNotify' file descriptor and MTU */
struct ble_notify_fd {
    int fd;
    uint16_t mtu;
};

typedef struct {
    int ble_dev_connected;
    struct ble_notify_fd *notify_fdp;
    int server_sock;
    int client_sock;
    struct sockaddr_in *addr;
    struct chr_obj_path_info *chr_path_info;
    struct glib_loop_info *loop_info;
} AppContext;

struct scan_status {
    unsigned char signal_byte;
};

#endif
