#include <stdint.h>


/* Connection timeout in milliseconds */
#define CONNECT_TIMEOUT_MS 5000

/* Bluez 'AcquireNotify' file descriptor and MTU */
struct bluez_notify_fd {
    int fd;
    uint16_t mtu;
};
