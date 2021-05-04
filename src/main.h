#ifndef __MAIN_H
#define __MAIN_H
#include <stdint.h>


/* Connection timeout in milliseconds */
#define CONNECT_TIMEOUT_MS 5000
/* Pause after unsuccessful reconnection. */
#define RECONNECT_ATTEMPT_INTERVAL 5
#define NOTIFY_ACQ_ATTEMPT_INTERVAL 3

/* Bluez 'AcquireNotify' file descriptor and MTU */
struct ble_notify_fd {
    int fd;
    uint16_t mtu;
};

struct app_state {
    int ble_dev_connected;
    struct ble_notify_fd *notify_fdp;
    int server_sock;
    int client_sock;
    struct sockaddr_in *addr;
};

typedef struct app_state AppState;


#define BLUEZ_ERR_INTERFACE "org.bluez.Error"
#define BLUEZ_ERR_NOT_CONNECTED (BLUEZ_ERR_INTERFACE ".NotConnected")

#endif
