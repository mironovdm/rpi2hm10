/**
 * The program establishes a connection to HM-10 module and 
 * exposes it over TCP socket.
 */

#include <assert.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>       /* For portability [M.Kerrisk p.1331] */
#include <unistd.h>

// #include <glib.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <glib-object.h>

#include "argparse.h"
#include "bluetooth.h"
#include "dbus.h"
#include "debug.h"
#include "main.h"

GDBusConnection *dbus_conn;

extern struct cmd_args opts;

/* 
 * Will be chnaged to one by signal handler. 'start_main_loop()'
 * checks it on every loop to break when SIGINT or SIGTERM is received.
 */
static int got_interp_signal = 0;

static int exit_val = 0;

static int ble_start_scan(AppContext *app);
static int ble_stop_scan(AppContext *app);

void AppState_close_fd(AppContext *app)
{
    if (!app->notify_fdp)
        return;
    
    close(app->notify_fdp->fd);
    free(app->notify_fdp);
    app->notify_fdp = NULL;
}


void app_sig_handler(int signum)
{
	got_interp_signal = 1;
}

int is_interrupted()
{
    return got_interp_signal == 1;
}

void failure(const char *errmsg)
{
    if (errno) {
        perror(errmsg);
        fprintf(stderr, "errno=%d\n", errno);
    } else {
        fprintf(stderr, "%s\n", errmsg);
    }
    exit_val = 1;
}

/*
 * Call 'AcquireNotify' on the characteristic interface to 
 * get file handler for receiving data from remote Bluetooth 
 * device.
 */
struct ble_notify_fd *ble_acquire_notify_fd(GError **errp)
{
    char *error_msg;
    char *dbus_iface = "org.bluez.GattCharacteristic1";
    struct ble_notify_fd *fd_handler;
    GVariant *res = NULL;
    GError *error = NULL;
    GVariantBuilder options;
    GVariant *params;
    GUnixFDList *fdlist = g_unix_fd_list_new();
    int fdlist_idx = 0;

    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    params = g_variant_new("(@a{sv})", g_variant_builder_end(&options));

    /* 
     * Here we have to use 'g_dbus_connection_call_with_unix_fd_list_sync'
     * to get file descriptor.
     */
    res = g_dbus_connection_call_with_unix_fd_list_sync(
        dbus_conn, DBUS_UNAME_ORG_BLUEZ, opts.chr_path,
        dbus_iface, "AcquireNotify", params, NULL, 
        G_DBUS_CALL_FLAGS_NONE, -1, NULL, &fdlist, NULL, &error
    );

    if (res == NULL) {
        error_msg = "Bluetooth notify acquire failed";
        goto error_exit;
    }

    fd_handler = malloc(sizeof(struct ble_notify_fd));

    /* (hq) - tuple of h-handle(index of FD array) and q-uint16 */
    g_variant_get(res, "(hq)", &fdlist_idx, &fd_handler->mtu);
    fd_handler->fd = g_unix_fd_list_get(fdlist, fdlist_idx, &error);
    g_object_unref(fdlist);

    if (fd_handler->fd < 0) {
        error_msg = "Get FD failed";
        free(fd_handler);
        goto error_exit;
    }

    if (res)
        g_variant_unref(res);
    
    return fd_handler;

error_exit:

    if (res)
        g_variant_unref(res);

    gerror_print(error_msg, error);
    if (errp)
        *errp = g_error_copy(error);
    g_error_free(error);
    return NULL;
}

/* 
 * Write data to HM-10 characteristic.
 */
int ble_write_chr(const char * const data, size_t len)
{
    GVariantBuilder options_builder;
    GVariant *params, *val, *options;
    GError *error = NULL;
    GVariant *res;
    char *dbus_iface;
    char *method;

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
        dbus_conn, DBUS_UNAME_ORG_BLUEZ, opts.chr_path,
        dbus_iface, method, params, 
        /* reply type */ NULL, G_DBUS_CALL_FLAGS_NONE, /* timeout */ -1, 
        /* cancellable */ NULL, &error
    );

    if (res == NULL) {
        gerror_handle(error, "BLE write error");
        return -1;
    }

    g_variant_unref(res);
    
    return 0;
}

void ble_after_connect(AppContext *app)
{
    AppState_close_fd(app);

    /*
     * Need to give some time to Bluez to discover services and 
     * initialize internal structures in the case when connection
     * has not yet been established. Some more reliable way is
     * needed.
     * 
     * Listen event on D-Bus:
     * @ MGMT Event: Device Connected (0x000b) plen 41
     */
    sleep(1);
}

/**
 * Connect to HM-10 BLE module.
 */
int ble_dev_connect(AppContext *app)
{
    GVariant *res = NULL;
    GError *error = NULL;

    res = g_dbus_connection_call_sync(
        dbus_conn, DBUS_UNAME_ORG_BLUEZ, opts.dev_path, "org.bluez.Device1",  "Connect", NULL, NULL, 
        G_DBUS_CALL_FLAGS_NONE, CONNECT_TIMEOUT_MS, NULL, &error
    );

    if (res == NULL){
        gerror_print("D-Bus connect error", error);
        g_error_free(error);
        app->ble_state->is_connected = 0;
        return -1;
    }

    g_variant_unref(res);

    app->ble_state->is_connected = 1;
    printf("Device %s connected\n", opts.dev_path);
    
    return 0;
}

int is_gerror_disconnected(GError *err)
{
    return strstr(err->message, BLUEZ_ERR_NOT_CONNECTED) != NULL;
}

void sleep_progressive(int *reconnect_delay, int max_delay)
{
    sleep(*reconnect_delay);
    if (*reconnect_delay < max_delay)
        (*reconnect_delay)++;
}

int ble_dev_reconnect(AppContext *app)
{
    int reconnect_delay, reacquire_delay;
    puts("Reconnecting...");

    while (!is_interrupted()) {
        reconnect_delay = 1;
        reacquire_delay = 1;

        while (ble_dev_connect(app) < 0) {
            sleep_progressive(&reconnect_delay, RECONNECT_ATTEMPT_MAX_INTERVAL);
            
            if (is_interrupted())
                goto out;
        }

        ble_after_connect(app);

        while (!is_interrupted()) {
            GError *err = NULL;

            if ((app->notify_fdp = ble_acquire_notify_fd(&err)))
                goto out;
            
            if (is_gerror_disconnected(err)) {
                app->ble_state->is_connected = 0;
                break;
            }
            
            debug("BLE FD acquire error");

            sleep_progressive(&reacquire_delay, ACQUIRE_NOTIFY_FD_ATTEMPT_MAX_INTERVAL);
        };
    }
out:
    if (is_interrupted())
        return -1;

    debug("Reconnected");
    
    return 0;
}

/**
 * Disconnect HM-10 module.
 */
int ble_dev_disconnect(AppContext *app) {
    GVariant *res;
    GError *error = NULL;

    res = g_dbus_connection_call_sync(
        dbus_conn, DBUS_UNAME_ORG_BLUEZ, opts.dev_path,
        "org.bluez.Device1", "Disconnect", /* parameters */ NULL, 
        /* reply_type: */ NULL, G_DBUS_CALL_FLAGS_NONE, 
        -1, NULL, &error
    );

    if (res == NULL){
        printf("Error: %s\n", error->message);
        g_error_free(error);
        return -1;
    }

    app->ble_state->is_connected = 0;
    g_variant_unref(res);
    
    return 0;
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

int create_noblock_srv_socket(AppContext *app)
{
    int sock;
    int status;
    int flags;
    int optval;
    char port_str[6] = {0};
    struct sockaddr_in *addr = NULL;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        return -1;

    /* Set socket as non-blocking */
    flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1)
        return flags;
    status = fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    if (status < 0) {
        failure("can't set socket to nonblock");
        return -1;
    }

    optval = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));

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

void print_listen_report(AppContext *app) {
    unsigned port;
    char *host_name = inet_ntoa(app->addr->sin_addr);

    if (!host_name) {
        host_name = opts.host;
    }
    port = ntohs(app->addr->sin_port);

    printf("Listening on %s:%u\n", host_name, port);
}

int ble_reconnect_with_fdset_update(AppContext *app, fd_set *fds, int *nfds)
{
    int old_notify_fd = app->notify_fdp->fd;
    int status;

    status = ble_dev_reconnect(app);
    if (status)
        return status;
    
    FD_CLR(old_notify_fd, fds);

    if (app->notify_fdp->fd > FD_SETSIZE) {
        failure("descriptor larger than FD_SETSIZE");
        return -1;
    }
    *nfds = MAX(app->notify_fdp->fd, *nfds);
    FD_SET(app->notify_fdp->fd, fds);

    return 0;
}

int handle_server_socket(AppContext *app, fd_set *fds, fd_set *active_fds, int *nfds)
{
    int accepted_sock;

    if (!FD_ISSET(app->server_sock, fds)) {
        return 0;
    }

    debug("select(): server socket");
    accepted_sock = accept(app->server_sock, NULL, NULL);
    
    if (accepted_sock < 0) {
        DEBUGF("server socker error: %d", errno);
        return -1;
    }

    /* Reject new connections if there is already one. */
    if (app->client_sock) {
        close(accepted_sock);
        return 0;
    }

    puts("client connected");
    set_client_sock_opts(accepted_sock);
    
    *nfds = MAX(accepted_sock, *nfds);
    if (*nfds > FD_SETSIZE)
        return -1;

    FD_SET(accepted_sock, active_fds);
    app->client_sock = accepted_sock;

    return 0;
}

int handle_client_socket(AppContext *app, fd_set *fds, fd_set *active_fds, int *nfds)
{
    ssize_t len;
    char buf[app->notify_fdp->mtu];

    if (!app->client_sock || !FD_ISSET(app->client_sock, fds)) {
        return 0;
    }

    debug("select(): client socket");
    memset(buf, 0, sizeof(buf));
    
    len = recv(app->client_sock, buf, sizeof buf, 0);

    if (len < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        puts("client EAGAIN");
        return 0;
    }

    if (len < 0) {
        failure("socket read error");
        return -1;
    }

    DEBUGF("client -> BLE, %u bytes\n", (unsigned)len);

    /* 0 is returned if peer closed the connection. */
    if (len == 0) {
        FD_CLR(app->client_sock, active_fds);
        close(app->client_sock);
        app->client_sock = 0;

        return 0;
    }

    if (app->ble_state->is_scaning) {
        ble_stop_scan(app);
        if (ble_reconnect_with_fdset_update(app, active_fds, nfds))
            return -1;
    }

    while (1) {
        int status = ble_write_chr(buf, len);
        if (!status)
            break;
        
        /* Assume that error is returnd in case the connection is lost */

        if (!opts.reconnect)
            return -1;
            
        status = ble_reconnect_with_fdset_update(app, active_fds, nfds);
        if (status)
            return -1;
    }

    return 0;
}

int handle_ble_fd(
    AppContext *app, fd_set *fds, fd_set *active_fds, int *nfds, bool *notify_client_flag
)
{
    ssize_t len;
    char buf[app->notify_fdp->mtu];

    memset(buf, 0, sizeof(buf));

    if (!FD_ISSET(app->notify_fdp->fd, fds)) {
        return 0;
    }

    DEBUGF("select(): BLE FD\n");

    len = read(app->notify_fdp->fd, buf, sizeof buf);
    if (len < 0) {
        fprintf(stderr, "BLE read error");
        return -1;
    }
    if (len == 0) {
        puts("BLE FD returns 0, probably disconnected");
        
        app->ble_state->is_connected = false;
        FD_CLR(app->notify_fdp->fd, active_fds);
        close(app->notify_fdp->fd);
        
        DEBUGF("notify_client_flag=%d\n", *notify_client_flag);

        if (*notify_client_flag == false)
            ble_start_scan(app);

        return 0;
    }

     /* Nowhere to transmit the data because client still not connected */
    if (!app->client_sock) {
        return 0;
    }

    DEBUGF("client <- BLE, %zu bytes\n", len);

    /* TODO: check for partial send */
    len = send(app->client_sock, buf, len, 0);
    if (len < 0) {
        fprintf(stderr, "Socket write error");
        return -1;
    }

    return 0;
}

int notify_client(AppContext *app, bool *notify_client_flag)
{
    const char buf[] = "AT+WAKE";
    int status;

    if (!*notify_client_flag || !app->client_sock)
        return 0;

    status = write(app->client_sock, buf, sizeof(buf)-1);

    if (!status || status < 0)
        return -1;
    
    *notify_client_flag = false;
    if (!app->ble_state->is_connected)
        ble_start_scan(app);

    return 0;
}

int handle_ble_scan_event_fd(
    AppContext *app, fd_set *fds, fd_set *active_fds, int *nfds, bool *notif_client
)
{
    uint64_t event_counter = 0;

    if (!FD_ISSET(app->ble_state->event_fd, fds)) {
        return 0;
    }

    DEBUGF("connect request found\n");

    read(app->ble_state->event_fd, &event_counter, sizeof(event_counter));

    ble_stop_scan(app);

    if (ble_reconnect_with_fdset_update(app, active_fds, nfds))
        return -1;

    *notif_client = true;

    return 0;
}

void run_server(AppContext *app)
{
    fd_set current_fds, active_fds;
    int nfds, status;
    bool notify_client_flag = false;

    status = create_noblock_srv_socket(app);
    if (status < 0) {
        failure("Socket error");
        goto exit;
    }

    print_listen_report(app);

    nfds = MAX(app->server_sock, app->notify_fdp->fd);
    nfds = MAX(nfds, app->ble_state->event_fd);

    if (nfds > FD_SETSIZE) {
        failure("fd is greater than FD_SETSIZE");
        goto exit;
    }

    FD_ZERO(&active_fds);
    FD_SET(app->server_sock, &active_fds);
    FD_SET(app->notify_fdp->fd, &active_fds);
    FD_SET(app->ble_state->event_fd, &active_fds);

    while (!is_interrupted()) {
        current_fds = active_fds;
        status = select(nfds+1, &current_fds, NULL, NULL, NULL);
        debug("select() returned");

        if (!status) {
            debug("restart select()");
            continue;
        }

        if (status < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                continue;
            
            failure("select() error");
            break;
        }

        status = handle_server_socket(app, &current_fds, &active_fds, &nfds);
        status |= handle_client_socket(app, &current_fds, &active_fds, &nfds);
        status |= handle_ble_fd(app, &current_fds, &active_fds, &nfds, &notify_client_flag);
        status |= handle_ble_scan_event_fd(app, &current_fds, &active_fds, &nfds, &notify_client_flag);

        status |= notify_client(app, &notify_client_flag);

        if (status) {
            break;
        }
    }

    puts("after while");

exit:

    AppState_close_fd(app);

    if (app->server_sock >= 0) {
        debug("Close server socket");
        close(app->server_sock);
    }

    if (app->client_sock > 0) {
        debug("Close client socket");
        close(app->client_sock);
    }
}

int init_sig_handlers(void)
{
    int status;
    struct sigaction sig_action = {
        .sa_handler=app_sig_handler
    };
    sigemptyset(&sig_action.sa_mask);

    status = sigaction(SIGINT, &sig_action, NULL);
    status |= sigaction(SIGTERM, &sig_action, NULL);
    if (status) {
        perror("set signal error");
        return -1;
    }

    return 0;
}

static int create_dbus_conn(void)
{
    GError *err = NULL;

    gchar *sys_bus_addr_ptr = g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SYSTEM, NULL, &err);
    if (!sys_bus_addr_ptr) {
        return gerror_handle(err, "D-BUS bus address resolve failed");
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
        return gerror_handle(err, "D-BUS connection failed");
    }

    g_free(sys_bus_addr_ptr);

    return 0;
}

void close_dbus_conn()
{
    GError *err = NULL;
    gboolean status;

    status = g_dbus_connection_close_sync(dbus_conn, NULL, &err);
    g_object_unref(dbus_conn);

    if (!status) {
        failure(NULL);
        gerror_handle(err, NULL);
    }
}

void init_app(AppContext *app, int argc, char *argv[]) {
    int status = parse_args(argc, argv);

    if (status == -ARG_HELP)
        exit(0);
    if (status < 0)
        exit(1);

    app->chr_path_info = chr_obj_path_info_create(opts.chr_path);
    if (!app->chr_path_info) {
        fputs("Invalid characteristic object path\n", stderr);
        exit(1);
    }

    if (init_sig_handlers()) {
        fputs("Failed to set sighandlers\n", stderr);
        exit(1);
    }

    if (create_dbus_conn()) {
        fputs("D-Bus connection failed\n", stderr);
        exit(1);
    }

    app->loop_info = calloc(1, sizeof(struct loop_info_glib));
    app->ble_state = calloc(1, sizeof(struct ble_state));
    app->ble_state->event_fd = eventfd(0, EFD_NONBLOCK);
    app->ble_state->is_scaning = false;
}

void before_exit(AppContext *app)
{
    if (!dbus_conn)
        return;

    AppState_close_fd(app);
    
    if (app->ble_state->is_connected && !opts.keep_ble_con) {
        debug("BLE disconnect");
        ble_dev_disconnect(app);
    }

    free(app->loop_info);

    close(app->ble_state->event_fd);
    free(app->ble_state);

    close_dbus_conn();
}

void dbus_sig_handler(GDBusConnection *connection,
                        const gchar *sender_name,
                        const gchar *object_path,
                        const gchar *interface_name,
                        const gchar *signal_name,
                        GVariant *parameters,
                        gpointer user_data)
{
    printf("Callback triggered for iface %s\n", sender_name);
}

// void test_scan_over_manager(AppContext *app)
// {
//     GVariant *res;
//     GError *gerror = NULL;

//     res = g_dbus_connection_call_sync(
//         dbus_conn,
//         bluez_bus_name,
//         "/org/bluez/hci0",
//         "org.bluez.Adapter1",
//         "StartDiscovery",
//         /* parameters= */ NULL, 
//         /* reply_type= */ NULL,
//         G_DBUS_CALL_FLAGS_NONE,
//         /* timeout= */ -1, 
//         /* cancellable= */ NULL,
//         &gerror
//     );

//     if (!res) {
//         gerror_handle(gerror, "StartDiscovery error");
//         return;
//     }


//     puts("discovery started");

//     const char *uniq_conn_name = g_dbus_connection_get_unique_name(dbus_conn);

//     if (!uniq_conn_name) {
//         puts("fuck! Didn't get name");
//         return;
//     }

//     if (uniq_conn_name)
//         printf("got name: %s\n", uniq_conn_name);




//     GDBusObjectManager *manager = g_dbus_object_manager_client_new_sync(
//         dbus_conn,
//         G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
//         /* name= */ "org.bluez",
//         /* object_path= */ "/",
//         NULL, NULL, NULL,
//         /* cancelable= */ NULL,
//         &gerror
//     );

//     if (!manager) {
//         gerror_handle(gerror, "Manager constructor error");
//         return;
//     }

//     puts("Manager created");




//     // GDBusObject *hci_object = g_dbus_object_manager_get_object(manager, "/org/bluez/hci0");
//     // if (!hci_object) {
//     //     puts("no hci0 object");
//     //     return;
//     // }
//     // puts("Got hci0 d-bus object");




//     // GList *ifaces = g_dbus_object_get_interfaces(hci_object);
//     // if (!ifaces) {
//     //     puts("no ifaces");
//     //     return;
//     // }

//     // for (GList *l = ifaces; l != NULL; l = l->next)
//     // {
//     //     GDBusInterface *ifaceitem = G_DBUS_INTERFACE(l->data);
//     //     GDBusInterfaceInfo *info = g_dbus_interface_get_info(ifaceitem);
//     //     if (!info)
//     //         puts("no iface info");
//     //     else
//     //         puts("got info");

//     //     gulong handler_id = g_signal_connect(ifaceitem, "PropertiesChanged", G_CALLBACK(dbus_sig_handler), NULL);
//     //     if (!handler_id) {
//     //         puts("g_signal_connect() failed, handler ID is 0");
//     //         // return;
//     //     }

//     //     /* Do something with @element_data. */
//     // }



//     /*
//         interface=org.freedesktop.DBus.Properties; member=PropertiesChanged
//     */




//     // GDBusInterface *iface = g_dbus_object_get_interface(hci_object, "org.freedesktop.DBus.Properties");
//     // if (iface == NULL) {
//     //     puts("no interface");
//     //     return;
//     // }

//     GDBusInterface *iface = g_dbus_object_manager_get_interface(manager, "/org/bluez/hci0", "org.freedesktop.DBus.Properties");
//     if (iface == NULL) {
//         puts("no interface");
//         return;
//     }

//     if (!G_IS_DBUS_INTERFACE(iface)) {
//         puts("not interface");
//         return;
//     }

//     // const char *name = g_dbus_proxy_get_interface_info (iface);
//     // if(!name) {
//     //     puts("no name");
//     //     return;
//     // }

//     // puts("name");
//     // puts(name);


//     GDBusInterfaceInfo *info = g_dbus_proxy_get_interface_info(iface);
//     if (!info) {
//         puts("no info");
//         // return;
//     }

//     gulong handler_id = g_signal_connect(iface, "::PropertiesChanged", G_CALLBACK(dbus_sig_handler), NULL);
//     if (!handler_id) {
//         puts("g_signal_connect() failed");
//         return;
//     }

//     sleep(5);

//     g_object_unref(iface);
//     // g_object_unref(hci_object);
// }

/* TODO remove */
// void scan_advertise(AppContext *app)
// {
//     GVariant *res;
//     GError *gerror = NULL;

//     GMainLoop *loop = g_main_loop_new(NULL, 0);

//     const char *uniq_name = g_dbus_connection_get_unique_name(dbus_conn);
//     if (!uniq_name) {
//         puts("fuck! Didn't get name");
//         return;
//     }

//     guint subscr_id = g_dbus_connection_signal_subscribe(
//         dbus_conn,
//         DBUS_UNAME_ORG_BLUEZ,
//         DBUS_OBJ_IFACE_PROPERTIES,
//         DBUS_OBJ_SIGNAL_PROPERTIES_CHANGED,
//         "/org/bluez/hci0/dev_5C_12_03_6A_24_E7",
//         NULL,
//         G_DBUS_SIGNAL_FLAGS_NONE,
//         dbus_sig_handler,
//         NULL,
//         NULL
//     );
//     printf("subscriber id %d\n", subscr_id);


//     pthread_t thread_id;
//     pthread_create(&thread_id, NULL, (void *(*)(void *))g_main_loop_run, loop);

//     puts("main loop started");
//     sleep(1);


//     res = g_dbus_connection_call_sync(
//         dbus_conn,
//         DBUS_UNAME_ORG_BLUEZ,
//         "/org/bluez/hci0",
//         "org.bluez.Adapter1",
//         "StartDiscovery",
//         /* parameters= */ NULL, 
//         /* reply_type= */ NULL,
//         G_DBUS_CALL_FLAGS_NONE,
//         /* timeout= */ -1, 
//         /* cancellable= */ NULL,
//         &gerror
//     );
//     if (!res) {
//         gerror_handle(gerror, "StartDiscovery error");
//         return;
//     }
//     puts("enable discovery");


//     sleep(7);
//     g_main_loop_quit(loop);
//     pthread_join(thread_id, NULL);
//     puts("main loop stopped");

// }

/**
 * TODO: Review. Memory can leak here
 */
 void dbus_signal_callback(
    GDBusConnection *connection,
    const gchar *sender_name,
    const gchar *object_path,
    const gchar *interface_name,
    const gchar *signal_name,
    GVariant *params,
    gpointer user_data
)
{
    const char *iface;
    unsigned char flag_byte;
    GVariant *data = NULL;
    GVariantIter *iter = NULL;
    GVariant *service_data_dict = NULL;
    GVariant *service_data_entry = NULL;
    GVariant *service_data_entry_value = NULL;

    printf(
        "Got D-Bus signal: sender=%s, path=%s, iface=%s, signame=%s\n",
        sender_name, object_path, interface_name, signal_name
    );

    if (!params) {
        DEBUGF("empty params\n");
        return;
    }

    if (!g_variant_check_format_string(params, "(s@a{sv}as)", TRUE))
        return;
    
    /* Event contains string (eg "org.bluez.Device1"), dict {sv} 
     * and a string array. See example entry in the docs.
     * Type "@a{sv}" - returns GVariant "a{sv}", while "a{sv}"
     * return an iterator of "{sv}" entries of a dict.
     */
    g_variant_get(params, "(s@a{sv}as)", &iface, &data, NULL);
    
    if (!iface || strcmp(iface, "org.bluez.Device1") != 0)
        goto out;

    service_data_dict = g_variant_lookup_value(data, "ServiceData", G_VARIANT_TYPE_VARDICT);
    if (!service_data_dict)
        goto out;
    
    DEBUGF("ServiceData updated\n");

    /*
     * Service data is a dict of {UUID: data}, with format "s":"ay".
     * HM-10 has one UUID in ServiceData dict, so we need the first entry.
     */
    iter = g_variant_iter_new(service_data_dict);
    service_data_entry = g_variant_iter_next_value(iter);

    /* Get value of the first dict entry */
    g_variant_get(service_data_entry, "{sv}", NULL, &service_data_entry_value);
    g_variant_iter_free(iter);

    /* Get the first byte (AT+FLAG[byte]) of the ServiceData value */

    iter = g_variant_iter_new(service_data_entry_value);
    if (!g_variant_iter_loop(iter, "y", &flag_byte))
        goto out;

    if (flag_byte != 0) {
        /* THIS IS A REQUEST FOR CONNECTION FROM DEVICE CONNECTED TO HM-10 !!! */
        uint64_t counter = 1;
        struct scan_cb_params *cb_params = (struct scan_cb_params *)user_data;

        write(cb_params->event_fd, &counter, sizeof(counter));
    }

    DEBUGF("flag value %hhu\n", flag_byte);

out:
    if (iface)
        g_free((void *)iface);
    
    if (data)
        g_variant_unref(data);

    if (service_data_dict)
        g_variant_unref(service_data_dict);

    if (iter)
        g_variant_iter_free(iter);

}

static int prepare_dbus_loop(AppContext *app)
{
    int ret;

    if (!app->loop_info->loop)
        app->loop_info->loop = g_main_loop_new(NULL /* context */, 0 /* is_running */);

    ret = pthread_create(
        &app->loop_info->thread_id,
        NULL/* attrs */,
        (thread_start_routine_t)g_main_loop_run,
        app->loop_info->loop
    );
    if (ret)
        return ret;

    return 0;
}

static int ble_start_scan(AppContext *app)
{
    int status;
    struct scan_cb_params *callback_params = 0;

    if (prepare_dbus_loop(app) != 0)
        return -1;

    callback_params = malloc(sizeof(struct scan_cb_params));
    if (!callback_params)
        return -1;
    callback_params->event_fd = app->ble_state->event_fd;

    app->signal_subscr_id = dbus_enable_signals(
        dbus_conn,
        dbus_signal_callback,
        app->chr_path_info->dev,
        DBUS_OBJ_SIGNAL_PROPERTIES_CHANGED,
        callback_params
    );

    /* Rpi radio chip has combined Bluetooth and WiFi. I noticed
     * that scan for BR/EDR affects WiFi performance and cause
     * Wifi freezes, but LE only scan has less impact on WiFi.
     * Filter will be clear automatically after StopDiscovery.
     */
    status = dbus_hci_set_discovery_filter(
        dbus_conn, app->chr_path_info->hci, DISCOVERY_FILTER_TRANSPORT_LE
    );
    if (status != 0)
        return -1;

    if (dbus_hci_start_discovery(dbus_conn, app->chr_path_info->hci))
        return -1;

    app->ble_state->is_scaning = true;

    DEBUGF("Discovery started: transport mode=LE\n");
    
    return 0;
}

static int ble_stop_scan(AppContext *app)
{
    g_dbus_connection_signal_unsubscribe(dbus_conn, app->signal_subscr_id);
    app->signal_subscr_id = 0;

    free(app->cb_params_ptr);

    if (dbus_hci_stop_discovery(dbus_conn, app->chr_path_info->hci))
        return -1;

    g_main_loop_quit(app->loop_info->loop);
    if (pthread_join(app->loop_info->thread_id, NULL /* void** return */)) {
        failure("loop thread join failed");
        return -1;
    }
    g_main_loop_unref(app->loop_info->loop);
    *(app->loop_info) = (const struct loop_info_glib){0};

    app->ble_state->is_scaning = false;

    return 0;
}

int main(int argc, char *argv[])
{
    AppContext app = {0};

    debug_init();
    init_app(&app, argc, argv);


    // start_scan(&app, cb_params);
    // sleep(100);
    // stop_scan(&app);

    /* TODO reconnect on start option */
    if (ble_dev_reconnect(&app) < 0) {
        failure("failed to connect to BLE device");
        before_exit(&app);
        return EXIT_FAILURE;
    }
    ble_after_connect(&app);

    // test_scan(&app);

    // return exit_val;

    app.notify_fdp = ble_acquire_notify_fd(NULL);
    if (app.notify_fdp != NULL)
        run_server(&app);

    before_exit(&app);

    return exit_val;
}
