#ifndef __MAIN_H
#define __MAIN_H

#include <pthread.h>
#include <stdint.h>

#include "dbus.h"


/* Connection timeout in milliseconds */
#define CONNECT_TIMEOUT_MS 5000
/* Pause after unsuccessful reconnection. */
#define RECONNECT_ATTEMPT_MAX_INTERVAL 5
#define ACQUIRE_NOTIFY_FD_ATTEMPT_MAX_INTERVAL 5

struct loop_info_glib {
    GMainLoop *loop;
    pthread_t thread_id;
};

/* Bluez 'AcquireNotify' file descriptor and MTU */
struct ble_notify_fd {
    int fd;
    uint16_t mtu;
};

struct ble_state {
    bool is_connected;
    int event_fd;
    bool is_scaning;
};

typedef struct {
    struct ble_notify_fd *notify_fdp;
    int server_sock;
    int client_sock;
    struct sockaddr_in *addr;
    struct chr_obj_path_info *chr_path_info;
    struct loop_info_glib *loop_info;
    guint signal_subscr_id;
    struct ble_state *ble_state;
    struct scan_cb_params *cb_params_ptr; /* Not thread safe */
} AppContext;

typedef void *(*thread_start_routine_t)(void *);

struct net_buf {
    char *client_buf;
    char *ble_buf;
    int incoming_conn;
};

#endif
