/**
 * The program establishes a connection to HM-10 module and 
 * exposes it over TCP socket.
 * 
 * TODO: connect/send/exit mode
 * 
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <netdb.h>

#include <sys/time.h>       /* For portability [M.Kerrisk p.1331] */
#include <sys/select.h>

// #include <glib.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>

#include "main.h"
#include "argparse.h"

GDBusConnection *dbus_conn;

typedef struct app AppState;

struct app {
    struct bluez_notify_fd *notify_fd;
    int server_sock;
    int client_sock;
    struct sockaddr_in *addr;
} app;

extern struct cmd_args opts;

/* 
 * Will be chnaged to one by signal handler. 'start_main_loop()'
 * checks it on every loop to break when SIGINT or SIGTERM is received.
 */
static int got_interp_signal = 0;

static int exit_val = 0;

/* Bluez DBus bus name */
const char bluez_bus_name[] = "org.bluez";


void sig_handler(int signum)
{
	got_interp_signal = 1;
}

void failure(const char *errmsg)
{
    perror(errmsg);
    fprintf(stderr, "errno=%d\n", errno);
    exit_val = 1;
}

/**
 * Connect to HM-10 BLE module.
 */
int dev_connect(void)
{
    GVariant *res;
    GError *error = NULL;

    res = g_dbus_connection_call_sync(
        dbus_conn, bluez_bus_name, opts.dev_path, "org.bluez.Device1",  "Connect", NULL, NULL, 
        G_DBUS_CALL_FLAGS_NONE, CONNECT_TIMEOUT_MS, NULL, &error
    );

    if (res == NULL){
        fprintf(stderr, "D-BUS error: %s\n", error->message);
        g_error_free(error);
        return -1;
    }

    g_variant_unref(res);

    printf("Device %s connected\n", opts.dev_path);
    
    return 0;
}

/**
 * Disconnect HM-10 module.
 */
int dev_disconnect(void) {
    GVariant *res;
    GError *error = NULL;

    res = g_dbus_connection_call_sync(
        dbus_conn, bluez_bus_name, opts.dev_path,
        "org.bluez.Device1", "Disconnect", /* parameters */ NULL, 
        /* reply_type: */ NULL, G_DBUS_CALL_FLAGS_NONE, 
        -1, NULL, &error
    );

    if (res == NULL){
        printf("Error: %s\n", error->message);
        g_error_free(error);
        return -1;
    }

    g_variant_unref(res);
    
    return 0;
}

/* 
 * Write data to HM-10 characteristic.
 */
int ble_write_chr(const char * const data, size_t len)
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
        dbus_conn, bluez_bus_name, opts.chr_path,
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
    int fdlist_idx = 0;
    char *dbus_iface = "org.bluez.GattCharacteristic1";
    struct bluez_notify_fd *fd_handler;
    GVariant *res = NULL;
    GError *error = NULL;
    GVariantBuilder options;
    GVariant *params;
    GUnixFDList *fdlist = g_unix_fd_list_new();


    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    params = g_variant_new("(@a{sv})", g_variant_builder_end(&options));

    /* 
     * Here we have to use 'g_dbus_connection_call_with_unix_fd_list_sync'
     * to get returned FD.
     */
    res = g_dbus_connection_call_with_unix_fd_list_sync(
        dbus_conn, bluez_bus_name, opts.chr_path,
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

int fill_sockaddr(const char *host, const char *port, struct sockaddr_in **sa_ptr)
{
    int err;
    struct sockaddr_in *addr;
    struct addrinfo *addrinfo_res;
    struct addrinfo addrinfo_hints = {
        .ai_flags = AI_NUMERICSERV,
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM
    };

    err = getaddrinfo(host, port, &addrinfo_hints, &addrinfo_res);
    if (err != 0) {
        failure(gai_strerror(err));
        return -1;
    }

    assert(sizeof(struct sockaddr_in) == addrinfo_res->ai_addrlen);
    addr = malloc(addrinfo_res->ai_addrlen);
    if (addr) {
        memcpy(addr, addrinfo_res->ai_addr, addrinfo_res->ai_addrlen);
        *sa_ptr = addr;
        err = 0;
    } else {
        failure("Memory error");
        err = -1;
    }

    freeaddrinfo(addrinfo_res);

    return err;
}

int create_noblock_srv_socket(AppState *app)
{
    int sock;
    int err;
    char port_str[6] = {0};
    struct sockaddr_in *addr = NULL;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        return -1;

    err = fcntl(sock, F_SETFL, O_NONBLOCK);
    if (err < 0)
        return err;

    sprintf(port_str, "%u", (unsigned)opts.port);
    if (fill_sockaddr(opts.host, port_str, &addr) < 0) {
        fprintf(stderr, "Name resolve error");
        return 1;
    }

    if (bind(sock, (struct sockaddr *)addr, sizeof(struct sockaddr_in)) < 0) {
        fprintf(stderr, "Socket bind failed\n");
        return -1;
    }

    if (listen(sock, /*int backlog=*/1) < 0) {
        fprintf(stderr, "Socket lsiten failed\n");
        return -1;
    }

    app->server_sock = sock;
    app->addr = addr;

    return 0;
}

void set_client_sock_opts(int sock)
{
    int one = 1;
    int err = fcntl(sock, F_SETFL, O_NONBLOCK);
    if (err < 0) {
        failure("Failed to set client socket as non blocking");
        return;
    }

    /* We operate on small chunks of data so it seems that it is better
     * to disable Nagle's algorithm.
     */
    err = setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    if (err) {
        failure("Could not set TCP_NODELAY");
    }
}

void print_listen_report(AppState *app) {
    char *host_name = inet_ntoa(app->addr->sin_addr);
    if (!host_name) {
        host_name = opts.host;
    }

    unsigned port = ntohs(app->addr->sin_port);

    printf("Listening on %s:%u\n", host_name, port);
}

void run_server(AppState *app)
{
    fd_set readfds, active_readfds;
    int nfds, status;

    app->server_sock = create_noblock_srv_socket(app);
    if (app->server_sock < 0) {
        failure("Socket error");
        goto exit;
    }

    print_listen_report(app);

    nfds = MAX(app->server_sock, app->notify_fd->fd);

    if (nfds > FD_SETSIZE) {
        failure("fd is greater than FD_SETSIZE");
        goto exit;
    }

    FD_ZERO(&active_readfds);
    FD_SET(app->server_sock, &active_readfds);
    FD_SET(app->notify_fd->fd, &active_readfds);

    while (!got_interp_signal) {
        readfds = active_readfds;
        status = select(nfds+1, &readfds, NULL, NULL, NULL);

        if (!status)
            continue;

        if (status < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            
            failure("select() error");
            break;
        }

        /* Check and read client socket */
        if (app->client_sock && FD_ISSET(app->client_sock, &readfds)) {
            const int mtu = app->notify_fd->mtu;
            int len;
            char buf[mtu];

            memset(buf, 0, buf_size);
            
            len = recv(app->client_sock, buf, buf_size, 0);

            if (len < 0) {
                failure("notify data read error");
                break;
                // TODO: do no brake, just continue the loop
            }

            if (len == 0) {
                FD_CLR(app->client_sock, &active_readfds);
                nfds = MAX(app->server_sock, app->notify_fd->fd);
                close(app->client_sock);
                app->client_sock = 0;
            } else if (ble_write_chr(buf, len) < 0) {
                // TODO: reconnect place
                failure("failed to write data to characteristic");
                break;
            }   
        }

        /* Accepting new client connection */
        if (FD_ISSET(app->server_sock, &readfds)) {
            int accepted_sock = accept(app->server_sock, NULL, NULL);
            if (accepted_sock < 0) {
                failure("accept() error");
                break;
            }

            if (!app->client_sock) {
                set_client_sock_opts(accepted_sock);
                nfds = MAX(accepted_sock, nfds);
                if (nfds > FD_SETSIZE) {
                    failure("Socket number reached limit");
                    break;
                }
                FD_SET(accepted_sock, &active_readfds);
                app->client_sock = accepted_sock;
            } else {
                close(accepted_sock);
            }
        }

        /* Check data from BLE */
        if (FD_ISSET(app->notify_fd->fd, &readfds)) {
            ssize_t len;
            char buf[mtu] = {0};

            len = read(app->notify_fd->fd, buf, (size_t)buf_size);

            if (len < 0) {
                // TODO: BLE reconnect place? Or maybe drop client connection?
                failure("notify data read error");
                break;
            }

            if (len == 0) {
                // TODO: BLE reconnect place
                puts("notify descriptor has been closed. The remote device is "
                     "probably disconnected");
                break;
            }

            if (app->client_sock) {
                len = send(app->client_sock, buf, len, 0);
                if (len < 0) {
                    failure("send to socket failed\n");
                    break;
                }
                /* TODO: add option not to finish when client is disconnected */
            }
        }
    }

    while(1) {
        polling_loop();
        if (opts.reconnect) {
            /* Reconnect here */
            continue;
        }
    }

exit:

    close(app->notify_fd->fd);

    if (app->server_sock > 0)
        close(app->server_sock);

    if (app->client_sock > 0)
        close(app->client_sock);
}

// void init_cmd_args(int argc, char **argv)
// {
//     long port;

//     if (argc < 5) {
//         fprintf(
//             stderr, 
//             "Error: wrong number of arguments\n"
//             "Command usage:\n"
//             "rpi2hm10 [HOST] [PORT] [DEVICE_OBJ_PATH] [CHARACTERISTIC_OBJ_PATH]\n"
//         );
//         exit(1);
//     }
    
//     cmd_args.host = argv[1];
//     cmd_args.dev_obj_path = argv[3];
//     cmd_args.chr_obj_path = argv[4];

//     errno = 0;
//     port = strtol(argv[2], NULL, 10);
//     if (errno != 0 ||  port < 1 || port > UINT16_MAX ) {
//         fprintf(stderr, "Wrong port number");
//         exit(1);
//     }

//     cmd_args.port = (uint16_t)port;
// }

void init_sig_handlers(void)
{
    struct sigaction sig_action = {
        .sa_handler=sig_handler
    };
    sigaction(SIGINT, &sig_action, NULL);
    sigaction(SIGTERM, &sig_action, NULL);
}

static int g_error_handle(GError **err, const char *msg)
{
    const char *empty = "";

    if (*err == NULL)
        return 0;

    if (!msg) {
        msg = empty;
    }
    
    if ((*err)->message != NULL)
        printf("GError: %s, %s\n", msg, (*err)->message);
    else
        printf("GError: %s, no error message\n", msg);

    g_error_free(*err);
    *err = NULL;

    return -1;
}

static int create_dbus_conn(void)
{
    GError *err = NULL;

    gchar *sys_bus_addr_ptr = g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SYSTEM, NULL, &err);
    if (!sys_bus_addr_ptr) {
        return g_error_handle(&err, "D-BUS bus address resolve failed");
    }

    dbus_conn = g_dbus_connection_new_for_address_sync(
        sys_bus_addr_ptr, /* address */
        G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT
        | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION, /* flags */
        NULL, /* observer */
        NULL, /* cancelable */
        &err
    );
    if (!dbus_conn) {
        return g_error_handle(&err, "D-BUS connection failed");
    }

    g_free(sys_bus_addr_ptr);

    return 0;
}

void init_app(int argc, char *argv[]) {
    int err = parse_args(argc, argv);

    if (err == -ARG_HELP)
        exit(0);

    if (err < 0)
        exit(1);

    init_sig_handlers();

    if (create_dbus_conn() < 0)
        exit(1);
}

void before_exit(void)
{
    if (dbus_conn != NULL)
        g_object_unref(dbus_conn);
}

int main(int argc, char *argv[])
{
    init_app(argc, argv);

    if (dev_connect() < 0) {
        errno = 0;
        failure("failed to connect to BLE device");
        before_exit();
        return EXIT_FAILURE;
    }

    /*
     * Need to give some time to Bluez to discover services and 
     * initialize internal structures in the case when connection
     * has not yet been established. This is very simplified way 
     * and it needs to change this to some reliable.
     */
    sleep(1);

    app.notify_fd = acquire_notify_descr();
    if (app.notify_fd != NULL)
        run_server(&app);
    else
        failure("failed to get file handler");

    if (!opts.keep_ble_con)
        dev_disconnect();

    before_exit();

    return exit_val;
}
