/**
 * The program establishes a connection to HM-10 module and 
 * exposes it over TCP socket.
 */

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

#include <sys/time.h>       /* For portability [M.Kerrisk p.1331] */
#include <sys/select.h>

// #include <glib.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>


/* Connection timeout in milliseconds */
#define CONNECT_TIMEOUT_MS 5000

/* If 1 then HM-10 will be disconnected on exit. */
#define CFG_DISCONNECT_ON_EXIT 0

/* Command line arguments storage */
struct {
    char *dev_obj_path;
    char *chr_obj_path;
    char *host;
    uint16_t port;
} cmd_args;

/* Connection to DBus and its status */
struct {
    GDBusConnection *conn;
    int is_connected;
} dbus_conn;

/* Bluez 'AcquireNotify' file descriptor and MTU */
struct bluez_notify_fd {
    int fd;
    uint16_t mtu;
};

/* FDs and sockets wich are used in the main loop */
struct {
    struct bluez_notify_fd *notify_fd;
    int srv_sock;
    int client_sock;
} io_fds;

/* 
 * Will be chnaged to one by signal handler. 'start_main_loop()'
 * checks it on every loop to break when SIGINT or SIGTERM is received.
 */
int got_interp_signal = 0;

static int exit_val = 0;

/* Bluez DBus bus name */
const char bluez_bus_name[] = "org.bluez";


void sig_handler(int signum)
{
    if (signum == SIGINT || signum == SIGTERM)
	   got_interp_signal = 1;
}

void failure(const char *errmsg)
{
    perror(errmsg);
    fprintf(stderr, "errno: %d\n", errno);
    exit_val = 1;
}

/**
 * Connect to HM-10 BLE module.
 * Returns 0 on success and -1 on error.
 */
int dev_connect(void)
{
    GVariant *res;
    GError *error = NULL;

    res = g_dbus_connection_call_sync(
        dbus_conn.conn, bluez_bus_name, cmd_args.dev_obj_path,
        "org.bluez.Device1", "Connect", NULL, NULL, G_DBUS_CALL_FLAGS_NONE,
        CONNECT_TIMEOUT_MS, NULL, &error
    );

    if (res == NULL){
        fprintf(stderr, "ERROR: %s\n", error->message);
        g_error_free(error);
        return -1;
    }

    g_variant_unref(res);
    dbus_conn.is_connected = 1;
    
    return 0;
}

/**
 * Disconnect HM-10 module.
 * Returns 0 on success and -1 on error.
 */
int dev_disconnect(void) {
    GVariant *res;
    GError *error = NULL;

    res = g_dbus_connection_call_sync(
        dbus_conn.conn, bluez_bus_name, cmd_args.dev_obj_path,
        "org.bluez.Device1", "Disconnect", /* parameters */ NULL, 
        /* reply_type: */ NULL, G_DBUS_CALL_FLAGS_NONE, 
        -1, NULL, &error
    );

    if (res == NULL){
        printf("ERROR: %s\n", error->message);
        g_error_free(error);
        return -1;
    }

    g_variant_unref(res);
    
    return 0;
}

/* 
 * Write data to HM-10 characteristic.
 */
int write_chr(const char * const data, size_t len)
{
    GVariantBuilder options_builder;
    GVariant *params, *val, *options;
    GError *error;
    GVariant *res;
    char *dbus_iface;
    char *method;

    error = NULL;

    /* 
     * Need to use byte array to have an ability to transmit
     * binary data with zeros.
     */
    GBytes *bytes = g_bytes_new(data, len);
    val = g_variant_new_from_bytes(G_VARIANT_TYPE_BYTESTRING, bytes, 1);

    /* Prepare empty options */
    g_variant_builder_init(&options_builder, G_VARIANT_TYPE_VARDICT);
    options = g_variant_builder_end(&options_builder);

    /* Create tuple of (val, options) for the WriteValue method */
    GVariant * tup[] = {val, options};
    params = g_variant_new_tuple(tup, 2);

    dbus_iface = "org.bluez.GattCharacteristic1";
    method = "WriteValue";
    
    res = g_dbus_connection_call_sync(
        dbus_conn.conn, bluez_bus_name, cmd_args.chr_obj_path,
        dbus_iface, method, params, 
        /* reply type */ NULL, G_DBUS_CALL_FLAGS_NONE, /* timeout */ -1, 
        /* cancellable */ NULL, &error
    );

    if (res == NULL) {
        printf("ERROR: %s\n", error->message);
        g_error_free(error);
        return -1;
    }

    g_variant_unref(res);
    
    return 0;
}

/*
 * Call 'AcquireNotify' on the characteristic interface to 
 * get file handler for receiving data from remote Bluetooth 
 * device.
 */
struct bluez_notify_fd *acquire_notify_descr(void)
{
    GVariantBuilder options = {0};
    GVariant *params, *res;
    GError *error;
    GUnixFDList *fdlist; 
    int fdlist_idx;
    struct bluez_notify_fd *fd_handler;
    char *dbus_iface;

    res = NULL;
    error = NULL;
    fdlist = g_unix_fd_list_new();

    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    params = g_variant_new("(@a{sv})", g_variant_builder_end(&options));

    dbus_iface = "org.bluez.GattCharacteristic1";

    /* 
     * Here we have to use 'g_dbus_connection_call_with_unix_fd_list_sync'
     * to get returned FD.
     */
    res = g_dbus_connection_call_with_unix_fd_list_sync(
        dbus_conn.conn, bluez_bus_name, cmd_args.chr_obj_path,
        dbus_iface, "AcquireNotify", params, NULL, 
        G_DBUS_CALL_FLAGS_NONE, -1, NULL, &fdlist, NULL, &error
    );

    if (res == NULL) {
        fprintf(stderr, "ERROR: %s\n", error->message);
        g_error_free(error);
        return NULL;
    }

    fd_handler = malloc(sizeof(struct bluez_notify_fd));

    g_variant_get(res, "(hq)", &fdlist_idx, &fd_handler->mtu);
    fd_handler->fd = g_unix_fd_list_get(fdlist, fdlist_idx, &error);
    g_object_unref(fdlist);

    if (fd_handler->fd < 0) {
        fprintf(stderr, "ERROR: %s\n", error->message);
        g_error_free(error);
        free(fd_handler);
        return NULL;
    }

    g_variant_unref(res);
    
    return fd_handler;
}

/*
 * Create non-blocking TCP listening socket.
 * Used for accepting new connection in 
 * the function 'start_main_loop'.
 */
int create_noblock_srv_socket(void)
{
    int sock, status;
    struct sockaddr_in addr = {0};

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (socket < 0)
        return sock;

    status = fcntl(sock, F_SETFL, O_NONBLOCK);
    if (status < 0)
        return status;

    addr.sin_family = AF_INET;
    addr.sin_port = (in_port_t)htons(cmd_args.port);
    /* TODO: get string host name */
    addr.sin_addr.s_addr = (in_addr_t)htonl(INADDR_LOOPBACK);

    status = bind(sock, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));
    if (status < 0)
        return status;
    
    if (listen(sock, 1) == 0)
        return sock;

    return -1;
}

void start_main_loop()
{
    fd_set readfds, active_readfds;
    int nfds, ready;

    io_fds.srv_sock = create_noblock_srv_socket();
    if (io_fds.srv_sock < 0) {
        failure("create stream socket error");
        goto exit;
    }

    printf("Listening on %s:%d\n", cmd_args.host, cmd_args.port);

    nfds = MAX(io_fds.srv_sock, io_fds.notify_fd->fd);
    
    if (nfds > FD_SETSIZE) {
        failure("fd is greater than FD_SETSIZE");
        goto exit;
    }

    FD_ZERO(&active_readfds);
    FD_SET(io_fds.srv_sock, &active_readfds);
    FD_SET(io_fds.notify_fd->fd, &active_readfds);

    while (1) {
        if (got_interp_signal)
            break;
        
        readfds = active_readfds;
        ready = select(nfds+1, &readfds, NULL, NULL, NULL);

        if (ready < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            
            failure("select() error");
            break;
        }
        
        if (ready == 0)
            continue;

        if (io_fds.client_sock && FD_ISSET(io_fds.client_sock, &readfds)) {
            int len;
            int buf_size = io_fds.notify_fd->mtu;
            char buf[buf_size];

            memset(buf, 0, buf_size);
            
            len = recv(io_fds.client_sock, buf, buf_size, 0);

            if (len < 0) {
                failure("notify data read error");
                break;
            }

            if (len) {
                if (write_chr(buf, len) < 0) {
                    failure("failed to write data to characteristic");
                    break;
                }
            } else {
                FD_CLR(io_fds.client_sock, &active_readfds);
                nfds = MAX(io_fds.srv_sock, io_fds.notify_fd->fd);
                close(io_fds.client_sock);
                io_fds.client_sock = 0;
            }
        }

        if (FD_ISSET(io_fds.srv_sock, &readfds)) {
            int accepted_sock = accept(io_fds.srv_sock, NULL, NULL);
            if (accepted_sock < 0) {
                failure("accept() error");
                break;
            }

            if (!io_fds.client_sock) {
                int status = fcntl(accepted_sock, F_SETFL, O_NONBLOCK);
                if (status < 0) {
                    failure("failed to set client sock non block");
                    break;
                }
                
                nfds = accepted_sock < nfds ? nfds : accepted_sock;
                FD_SET(accepted_sock, &active_readfds);
                io_fds.client_sock = accepted_sock;
            }
            else {
                /* Maybe would be better to set backlog to zero? */
                close(accepted_sock);
            }
        }

        if (FD_ISSET(io_fds.notify_fd->fd, &readfds)) {
            ssize_t len;
            int buf_size = io_fds.notify_fd->mtu;
            char buf[buf_size];

            memset(buf, 0, buf_size);

            len = read(io_fds.notify_fd->fd, buf, (size_t)buf_size);

            if (len < 0) {
                failure("notify data read error");
                break;
            }

            if (len == 0) {
                puts("notify descriptor has been closed. The remote device is "
                     "probably disconnected");
                break;
            }

            if (io_fds.client_sock) {
                len = send(io_fds.client_sock, buf, len, 0);
                if (len < 0) {
                    failure("send to socket failed\n");
                    break;
                }

            }
        }
    }

exit:

    close(io_fds.notify_fd->fd);

    if (io_fds.srv_sock > 0)
        close(io_fds.srv_sock);

    if (io_fds.client_sock > 0)
        close(io_fds.client_sock);
}

void init_cmd_args(int argc, char **argv)
{
    long port;

    if (argc < 5) {
        fprintf(
            stderr, 
            "Error: wrong number of arguments\n"
            "Command usage:\n"
            "rpi2hm10 [HOST] [PORT] [DEVICE_OBJ_PATH] [CHARACTERISTIC_OBJ_PATH]\n"
        );
        exit(1);
    }
    
    cmd_args.host = argv[1];
    cmd_args.dev_obj_path = argv[3];
    cmd_args.chr_obj_path = argv[4];

    errno = 0;
    port = strtol(argv[2], NULL, 10);
    if (errno != 0 ||  port < 1 || port > UINT16_MAX ) {
        fprintf(stderr, "Wrong port number");
        exit(1);
    }

    cmd_args.port = (uint16_t)port;
}

void parse_args(int argc, char **argv)
{
    
}

/**
 * Initialize SIGINT and SIGTERM handlers.
 * Handler is the same for both signals.
 */
void init_sig_handlers(void)
{
    struct sigaction sig_action = {
        .sa_handler=sig_handler
    };
    sigaction(SIGINT, &sig_action, NULL);
    sigaction(SIGTERM, &sig_action, NULL);
}

void setup_application(int argc, char **argv) {
    if (parse_args(argc, argv))
        exit(EXIT_FAILURE);

    init_sig_handlers();
}

int main(int argc, char *argv[])
{
    int status;

    init_cmd_args(argc, argv);
    setup_application(argc, argv);

    dbus_conn.conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, NULL);
    if (dbus_conn.conn == NULL) {
        failure("connect to DBus failed");
        goto free_and_exit;
    }

    status = dev_connect();
    if (status < 0) {
        failure("Error::481 ");
        goto free_and_exit;
    }

    /*
     * Need to give some time to Bluez to discover services and 
     * initialize internal structures in the case when connection
     * has not yet been established. This is very simplified way 
     * and it needs to change this.
     */
    sleep(1);

    io_fds.notify_fd = acquire_notify_descr();
    if (io_fds.notify_fd == NULL) {
        failure("failed to get file handler");
        goto free_and_exit;
    }

    start_main_loop();

free_and_exit:

    if (CFG_DISCONNECT_ON_EXIT && dbus_conn.is_connected)
        dev_disconnect();
    
    if (dbus_conn.conn != NULL)
        g_object_unref(dbus_conn.conn);

    return exit_val;
}
