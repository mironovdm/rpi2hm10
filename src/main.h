#include <stdint.h>


/* Connection timeout in milliseconds */
#define CONNECT_TIMEOUT_MS 5000
/* Pause after unsuccessful reconnection. */
#define RECONNECT_ATTEMPT_INTERVAL 5

/* Bluez 'AcquireNotify' file descriptor and MTU */
struct ble_notify_fd {
    int fd;
    uint16_t mtu;
};
