/**
 * The program establishes a connection to HM-10 module and 
 * exposes it over TCP socket.
 * 
 * TODO: connect/send/exit mode
 * 
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

/* Bluez DBus bus name */
const char bluez_bus_name[] = "org.bluez";


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
        dbus_conn, bluez_bus_name, opts.chr_path,
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
        dbus_conn, bluez_bus_name, opts.chr_path,
        dbus_iface, method, params, 
        /* reply type */ NULL, G_DBUS_CALL_FLAGS_NONE, /* timeout */ -1, 
        /* cancellable */ NULL, &error
    );

    if (res == NULL) {
        gerror_print("BLE write error", error);
        g_error_free(error);
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
        dbus_conn, bluez_bus_name, opts.dev_path, "org.bluez.Device1",  "Connect", NULL, NULL, 
        G_DBUS_CALL_FLAGS_NONE, CONNECT_TIMEOUT_MS, NULL, &error
    );

    if (res == NULL){
        gerror_print("D-Bus connect error", error);
        g_error_free(error);
        app->ble_dev_connected = 0;
        return -1;
    }

    g_variant_unref(res);

    app->ble_dev_connected = 1;
    printf("Device %s connected\n", opts.dev_path);
    
    return 0;
}

int is_gerror_disconnected(GError *err)
{
    return strstr(err->message, BLUEZ_ERR_NOT_CONNECTED) != NULL;
}

int ble_dev_reconnect(AppContext *app)
{
    puts("Reconnecting...");

    while (!is_interrupted()) {
        while (ble_dev_connect(app) < 0) {
            sleep(RECONNECT_ATTEMPT_INTERVAL);
            if (is_interrupted())
                goto out;
        }

        ble_after_connect(app);

        while (!is_interrupted()) {
            GError *err = NULL;

            app->notify_fdp = ble_acquire_notify_fd(&err);
            if (app->notify_fdp)
                goto out;
            
            if (is_gerror_disconnected(err)) {
                app->ble_dev_connected = 0;
                break;
            }
            
            debug("BLE FD acquire error");
            sleep(NOTIFY_ACQ_ATTEMPT_INTERVAL);
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

    app->ble_dev_connected = 0;
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
    int old_fd = app->notify_fdp->fd;
    int status;

    status = ble_dev_reconnect(app);
    if (status)
        return status;
    
    FD_CLR(old_fd, fds);
    *nfds = MAX(app->notify_fdp->fd, *nfds);
    if (*nfds > FD_SETSIZE) {
        failure("file descriptor larger than FD_SETSIZE");
        return -1;
    }
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
    int status;
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
        *nfds = MAX(app->server_sock, app->notify_fdp->fd);
        if (*nfds > FD_SETSIZE)
            return -1;
        close(app->client_sock);
        app->client_sock = 0;
        return 0;
    } 
    
    if (!ble_write_chr(buf, len))
        return 0;

    fputs("BLE disconnected\n", stderr);
    if (!opts.reconnect)
        return -1;
    
    status = ble_reconnect_with_fdset_update(app, active_fds, nfds);
    if (status)
        return -1;
    
    return 0;
}

/* TODO: check if zero is returned when a device is disconnected */
int handle_ble_fd(AppContext *app, fd_set *fds, fd_set *active_fds, int *nfds)
{
    ssize_t len;
    char buf[app->notify_fdp->mtu];

    memset(buf, 0, sizeof(buf));

    if (!FD_ISSET(app->notify_fdp->fd, fds)) {
        return 0;
    }

    debug("select(): BLE FD");

    len = read(app->notify_fdp->fd, buf, sizeof buf);
    if (len < 0) {
        fprintf(stderr, "BLE read error");
        return -1;
    }
    if (len == 0) {
        fputs("BLE disconnected\n", stderr);
        if (opts.reconnect) {
            int status = ble_reconnect_with_fdset_update(app, active_fds, nfds);
            if (status)
                return -1;
            return 0;
        }
        return -1;
    }

    if (!app->client_sock) {
        return 0;
    }

    DEBUGF("client <- BLE, %zu bytes\n", len);

    /* TODO: check for partial send */
    len = send(app->client_sock, buf, len, 0);
    if (len < 0) {
        fprintf(stderr, "Error");
        return -1;
    }

    return 0;
}

void run_server(AppContext *app)
{
    fd_set current_fds, active_fds;
    int nfds, status;

    status = create_noblock_srv_socket(app);
    if (status < 0) {
        failure("Socket error");
        goto exit;
    }

    print_listen_report(app);

    nfds = MAX(app->server_sock, app->notify_fdp->fd);

    if (nfds > FD_SETSIZE) {
        failure("fd is greater than FD_SETSIZE");
        goto exit;
    }

    FD_ZERO(&active_fds);
    FD_SET(app->server_sock, &active_fds);
    FD_SET(app->notify_fdp->fd, &active_fds);

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
        status |= handle_ble_fd(app, &current_fds, &active_fds, &nfds);
        
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
}

void before_exit(AppContext *app)
{
    if (!dbus_conn)
        return;

    AppState_close_fd(app);
    
    if (app->ble_dev_connected && !opts.keep_ble_con) {
        debug("BLE disconnect");
        ble_dev_disconnect(app);
    }

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

void test_scan_over_manager(AppContext *app)
{
    GVariant *res;
    GError *gerror = NULL;

    res = g_dbus_connection_call_sync(
        dbus_conn,
        bluez_bus_name,
        "/org/bluez/hci0",
        "org.bluez.Adapter1",
        "StartDiscovery",
        /* parameters= */ NULL, 
        /* reply_type= */ NULL,
        G_DBUS_CALL_FLAGS_NONE,
        /* timeout= */ -1, 
        /* cancellable= */ NULL,
        &gerror
    );

    if (!res) {
        gerror_handle(gerror, "StartDiscovery error");
        return;
    }


    puts("discovery started");

    const char *uniq_conn_name = g_dbus_connection_get_unique_name(dbus_conn);

    if (!uniq_conn_name) {
        puts("fuck! Didn't get name");
        return;
    }

    if (uniq_conn_name)
        printf("got name: %s\n", uniq_conn_name);




    GDBusObjectManager *manager = g_dbus_object_manager_client_new_sync(
        dbus_conn,
        G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
        /* name= */ "org.bluez",
        /* object_path= */ "/",
        NULL, NULL, NULL,
        /* cancelable= */ NULL,
        &gerror
    );

    if (!manager) {
        gerror_handle(gerror, "Manager constructor error");
        return;
    }

    puts("Manager created");




    // GDBusObject *hci_object = g_dbus_object_manager_get_object(manager, "/org/bluez/hci0");
    // if (!hci_object) {
    //     puts("no hci0 object");
    //     return;
    // }
    // puts("Got hci0 d-bus object");




    // GList *ifaces = g_dbus_object_get_interfaces(hci_object);
    // if (!ifaces) {
    //     puts("no ifaces");
    //     return;
    // }

    // for (GList *l = ifaces; l != NULL; l = l->next)
    // {
    //     GDBusInterface *ifaceitem = G_DBUS_INTERFACE(l->data);
    //     GDBusInterfaceInfo *info = g_dbus_interface_get_info(ifaceitem);
    //     if (!info)
    //         puts("no iface info");
    //     else
    //         puts("got info");

    //     gulong handler_id = g_signal_connect(ifaceitem, "PropertiesChanged", G_CALLBACK(dbus_sig_handler), NULL);
    //     if (!handler_id) {
    //         puts("g_signal_connect() failed, handler ID is 0");
    //         // return;
    //     }

    //     /* Do something with @element_data. */
    // }



    /*
        interface=org.freedesktop.DBus.Properties; member=PropertiesChanged
    */




    // GDBusInterface *iface = g_dbus_object_get_interface(hci_object, "org.freedesktop.DBus.Properties");
    // if (iface == NULL) {
    //     puts("no interface");
    //     return;
    // }

    GDBusInterface *iface = g_dbus_object_manager_get_interface(manager, "/org/bluez/hci0", "org.freedesktop.DBus.Properties");
    if (iface == NULL) {
        puts("no interface");
        return;
    }

    if (!G_IS_DBUS_INTERFACE(iface)) {
        puts("not interface");
        return;
    }

    // const char *name = g_dbus_proxy_get_interface_info (iface);
    // if(!name) {
    //     puts("no name");
    //     return;
    // }

    // puts("name");
    // puts(name);


    GDBusInterfaceInfo *info = g_dbus_proxy_get_interface_info(iface);
    if (!info) {
        puts("no info");
        // return;
    }

    gulong handler_id = g_signal_connect(iface, "::PropertiesChanged", G_CALLBACK(dbus_sig_handler), NULL);
    if (!handler_id) {
        puts("g_signal_connect() failed");
        return;
    }

    sleep(5);

    g_object_unref(iface);
    // g_object_unref(hci_object);
}

void scan_advertise(AppContext *app)
{
    GVariant *res;
    GError *gerror = NULL;

    GMainLoop *loop = g_main_loop_new(NULL, 0);

    const char *uniq_name = g_dbus_connection_get_unique_name(dbus_conn);
    if (!uniq_name) {
        puts("fuck! Didn't get name");
        return;
    }

    guint subscr_id = g_dbus_connection_signal_subscribe(
        dbus_conn,
        DBUS_ORG_BLUEZ_BUS,
        DBUS_IFACE_PROPERTIES,
        DBUS_SIGNAL_PROPERTIES_CHANGED,
        "/org/bluez/hci0/dev_5C_12_03_6A_24_E7",
        NULL,
        G_DBUS_SIGNAL_FLAGS_NONE,
        dbus_sig_handler,
        NULL,
        NULL
    );
    printf("subscriber id %d\n", subscr_id);


    pthread_t thread_id;
    pthread_create(&thread_id, NULL, (void *(*)(void *))g_main_loop_run, loop);

    puts("main loop started");
    sleep(1);


    res = g_dbus_connection_call_sync(
        dbus_conn,
        DBUS_ORG_BLUEZ_BUS,
        "/org/bluez/hci0",
        "org.bluez.Adapter1",
        "StartDiscovery",
        /* parameters= */ NULL, 
        /* reply_type= */ NULL,
        G_DBUS_CALL_FLAGS_NONE,
        /* timeout= */ -1, 
        /* cancellable= */ NULL,
        &gerror
    );
    if (!res) {
        gerror_handle(gerror, "StartDiscovery error");
        return;
    }
    puts("enable discovery");


    sleep(7);
    g_main_loop_quit(loop);
    pthread_join(thread_id, NULL);
    puts("main loop stopped");

}

 void handle_dbus_signal(
    GDBusConnection *connection,
    const gchar *sender_name,
    const gchar *object_path,
    const gchar *interface_name,
    const gchar *signal_name,
    GVariant *parameters,
    gpointer user_data
)
{
    printf("Got D-Bus signal: %s \n", interface_name);
    return;
}

int prepare_dbus_loop(AppContext *app)
{
    if (!app->loop_info)
        app->loop_info->loop = g_main_loop_new(
            NULL, /* context */
            0 /* is_running */
        );

    pthread_create(
        &app->loop_info->loop_thread_id, NULL/* attrs */, g_main_loop_run, (void *)loop
    );
}

int prepare_scan(AppContext *app)
{
    int status = dbus_enable_signals(
        dbus_conn, handle_dbus_signal, app->chr_path_info.dev, DBUS_SIGNAL_PROPERTIES_CHANGED
    );
    if (status < 0)
        return -1;

    if (prepare_dbus_loop(app) < 0)
        return -1;
    
    return 0;    
}

void stop_dbus_loop(AppContext *app)
{
    g_main_loop_quit(app->loop_info->loop);
}

/**
 * Scan for advertising message and monitor 
 * signaling bit in advertising message.
 */
void start_scan(AppContext *app)
{
    if (prepare_scan(app) < 0))
        return -1;

    if (dbus_hci_start_discovery(dbus_conn, app->chr_path_info->hci) == NULL)
        return -1;
    
    return 0;
}

int  stop_scan(AppContext *app)
{
    stop_dbus_loop();
}

int main(int argc, char *argv[])
{
    AppContext app = {0};

    debug_init();
    init_app(&app, argc, argv);

    // /* TODO reconnect on start option */
    // if (ble_dev_reconnect(&app) < 0) {
    //     failure("failed to connect to BLE device");
    //     before_exit(&app);
    //     return EXIT_FAILURE;
    // }
    // ble_after_connect(&app);

    test_scan(&app);

    return exit_val;

    // app.notify_fdp = ble_acquire_notify_fd(NULL);
    // if (app.notify_fdp != NULL)
    //     run_server(&app);

    // before_exit(&app);

    // return exit_val;
}
