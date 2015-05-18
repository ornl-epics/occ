/**
 * OCC API socket implementation.
 *
 * This is a replacement for real OCC library. It doesn't talk to OCC hardware
 * or driver, instead it listens to TCP/IP socket and waits for client to
 * connect. Except for initialization parameters in occ_open() function, all
 * the rest of functionality interfaces are the same as OCC library talking to
 * OCC board.
 *
 * When initialized, the library starts listening on specified port. Incoming
 * client connection every time a function transfering data is invoked. There's
 * no asynchronous checking for client connect.
 */

#include "occlib_hw.h"

#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#define OCC_HANDLE_MAGIC        0x0cc0cc
#define MAX_OCC_PACKET_SIZE     (1800*8)
#define BUFFER_SIZE             (1000*MAX_OCC_PACKET_SIZE)

struct occ_handle {
    uint32_t magic;
    bool rx_enabled;
    int listen_socket;
    int client_socket;
    uint8_t buffer[BUFFER_SIZE];
    uint32_t buffer_len;
};

static int parse_host(const char *address, struct sockaddr_in *sockaddr) {
    char hostname[128];
    char *ptr;
    unsigned port;
    struct hostent *he;

    strncpy(hostname, address, sizeof(hostname));
    ptr = strchr(hostname, ':');
    if (ptr == NULL)
        return -EINVAL;

    *ptr = '\0';
    ptr++;

    if (sscanf(ptr, "%u", &port) != 1)
        return -EINVAL;

    he = gethostbyname(hostname);
    if (he == NULL)
        return -EINVAL;

    bzero((char *)sockaddr, sizeof(struct sockaddr_in));
    sockaddr->sin_family = AF_INET;
    bcopy((char *)he->h_addr, (char *)&sockaddr->sin_addr.s_addr, he->h_length);
    sockaddr->sin_port = htons(port);

    return 0;
}

// Use <host>:<port> notation for address, ie. localhost:7654
static int open_socket(const char *address) {
    struct sockaddr_in sockaddr;
    int ret;
    int sock = 0;

    ret = parse_host(address, &sockaddr);
    if (ret != 0)
        return ret;

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0)
        return -errno;

    if (bind(sock, (struct sockaddr *)&sockaddr, sizeof(sockaddr)) < 0)
        return -errno;

    if (listen(sock, 1) < 0)
        return -errno;

    return sock;
}

int occ_open(const char *address, occ_interface_type type, struct occ_handle **handle) {

    if (type != OCC_INTERFACE_SOCKET)
        return -EINVAL;

    *handle = malloc(sizeof(struct occ_handle));
    if (*handle == NULL)
        return -ENOMEM;

    memset(*handle, 0, sizeof(struct occ_handle));
    (*handle)->magic = OCC_HANDLE_MAGIC;

    (*handle)->listen_socket = open_socket(address);
    if ((*handle)->listen_socket < 0) {
        int ret = (*handle)->listen_socket;
        free(*handle);
        *handle = NULL;
        return ret;
    }
    (*handle)->client_socket = -1;

    return 0;
}

int occ_close(struct occ_handle *handle) {

    if (handle != NULL && handle->magic == OCC_HANDLE_MAGIC) {
        (void)close(handle->listen_socket);
        if (handle->client_socket < 0)
            (void)close(handle->client_socket);

        free(handle);
    }

    return 0;
}

int occ_enable_rx(struct occ_handle *handle, bool enable) {

    if (handle == NULL || handle->magic != OCC_HANDLE_MAGIC)
        return -EINVAL;

    handle->rx_enabled = enable;

    return 0;
}

int occ_enable_error_packets(struct occ_handle *handle, bool enable) {

    if (handle == NULL || handle->magic != OCC_HANDLE_MAGIC)
        return -EINVAL;

    return 0;
}

int occ_status(struct occ_handle *handle, occ_status_t *status, bool fast_status) {

    if (handle == NULL || handle->magic != OCC_HANDLE_MAGIC || status == NULL)
        return -EINVAL;

    memset(status, 0, sizeof(occ_status_t));

    status->dma_size = 0;
    status->board = OCC_BOARD_NONE;
    status->interface = OCC_INTERFACE_SOCKET;
    status->firmware_ver = 0x000F0001;
    status->optical_signal = OCC_OPT_CONNECTED;
    status->rx_enabled = handle->rx_enabled;

    return 0;
}

int occ_reset(struct occ_handle *handle) {

    if (handle == NULL || handle->magic != OCC_HANDLE_MAGIC)
        return -EINVAL;

    handle->buffer_len = 0;

    handle->rx_enabled = false;
    if (handle->client_socket < 0) {
        close(handle->client_socket);
        handle->client_socket = -1;
    }

    return 0;
}

static size_t _occ_data_align(size_t size) {
    return (size + 3) & ~3;
}

static int check_client(struct occ_handle *handle) {
    if (handle->client_socket < 0) {
        struct pollfd fds;
        struct sockaddr client;

        fds.fd = handle->listen_socket;
        fds.events = POLLIN;
        fds.revents = 0;

        // Non-blocking check
        if (poll(&fds, 1, 0) == 0 || fds.revents != POLLIN)
            return -ENOTCONN;

        // There should be client waiting now - accept() won't block
        socklen_t len = sizeof(struct sockaddr);
        handle->client_socket = accept(handle->listen_socket, &client, &len);
        if (handle->client_socket < 0)
            return -ECONNRESET;
    }

    return 0;
}

int occ_send(struct occ_handle *handle, const void *data, size_t count) {
    if (handle == NULL || handle->magic != OCC_HANDLE_MAGIC || _occ_data_align(count) != count)
        return -EINVAL;

    if (check_client(handle) != 0)
        return -ENOTCONN;

    int ret = write(handle->client_socket, data, count);
    if (ret == -1) {
        ret = -errno;
        close(handle->client_socket);
        handle->client_socket = -1;
    }
    return ret;
}

static int wait_for_ready_read(struct occ_handle *handle, uint32_t timeout) {

    struct pollfd pollfd;
    uint32_t timeout_remain = timeout;
    int ret;

    while (!handle->rx_enabled || check_client(handle) != 0) {
        if (timeout > 0 && timeout_remain-- == 0)
            return -ETIME;
        usleep(1000);
    }

    pollfd.fd = handle->client_socket;
    pollfd.events = POLLIN;
    ret = poll(&pollfd, 1, timeout > 0 ? timeout_remain : -1);
    if (ret == -1)
        return -errno;
    else if (ret == 0)
        return -ETIME;
    else if (pollfd.revents & POLLERR)
        return -ECONNRESET;

    return 0;
}

int occ_data_wait(struct occ_handle *handle, void **address, size_t *count, uint32_t timeout) {
    int ret;

    if (handle == NULL || handle->magic != OCC_HANDLE_MAGIC)
        return -EINVAL;

    ret = wait_for_ready_read(handle, timeout);
    if (ret != 0)
        return ret;

    ret = read(handle->client_socket, &handle->buffer[handle->buffer_len], sizeof(handle->buffer) - handle->buffer_len);
    if (ret <= 0) {
        ret = (ret == -1 ? -errno : -ECONNRESET);
        close(handle->client_socket);
        handle->client_socket = -1;
        return ret;
    }

    handle->buffer_len += ret;
    *address = handle->buffer;
    *count = handle->buffer_len;

    return 0;
}

int occ_data_ack(struct occ_handle *handle, size_t count) {

    if (handle == NULL || handle->magic != OCC_HANDLE_MAGIC || _occ_data_align(count) != count)
        return -EINVAL;

    if (count > handle->buffer_len)
        count = handle->buffer_len;

    // Move not consumed data from the end of the buffer to the front, no effect if count == buffer_len
//fprintf(stderr, "memmove(%d)\n", handle->buffer_len - count);
    memmove(handle->buffer, &handle->buffer[count], handle->buffer_len - count);

    handle->buffer_len -= count;
    return 0;
}

int occ_read(struct occ_handle *handle, void *data, size_t count, uint32_t timeout) {
    int ret;

    ret = wait_for_ready_read(handle, timeout);
    if (ret != 0)
        return ret;

    ret = read(handle->client_socket, data, count);
    if (ret == -1)
        return -errno;

    return ret;
}

int occ_io_read(struct occ_handle *handle, uint8_t bar, uint32_t offset, uint32_t *data, uint32_t count) {

    if (handle == NULL || handle->magic != OCC_HANDLE_MAGIC || offset % 4 != 0)
        return -EINVAL;

    return -ENOSYS;
}

int occ_io_write(struct occ_handle *handle, uint8_t bar, uint32_t offset, const uint32_t *data, uint32_t count) {

    if (handle == NULL || handle->magic != OCC_HANDLE_MAGIC || offset % 4 != 0)
        return -EINVAL;

    return -ENOSYS;
}
