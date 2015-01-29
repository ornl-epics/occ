/**
 * OCC API PIPE implementation.
 *
 * This is a replacement for real OCC library. It doesn't talk to OCC hardware
 * or driver, instead it works with 2 named pipes for interprocess communication.
 * Names of the pipes are determined from the devfile path passed to occ_open()
 * and a suffix .tx or .rx is added to that path respectively. .tx pipe is used
 * when software is using occ_send() function for sending data to OCC link.
 * .rx is used to receive data from OCC link, using occ_data_wait() or occ_read().
 * There must be another process(es) connected to those two named pipes to feed the
 * .rx or to consume .tx.
 * On Linux only, occ_open() succeeds even when pipe's other end is not connected.
 */

#include "occlib_hw.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define OCC_HANDLE_MAGIC        0x0cc0cc
#define PIPE_TX_SUFFIX          ".tx"
#define PIPE_RX_SUFFIX          ".rx"
#define MAX_OCC_PACKET_SIZE     (1800*8)
#define BUFFER_SIZE             (16*MAX_OCC_PACKET_SIZE)

struct occ_handle {
    uint32_t magic;
    bool rx_enabled;
    int fd_tx;
    int fd_rx;
    uint8_t buffer[BUFFER_SIZE];
    uint32_t buffer_len;
};

static int open_pipe(const char *path, int flags, mode_t mode) {
    struct stat st;
    int ret = stat(path, &st);
    if (ret == -1) {
        if (errno != ENOENT)
            return -errno;
        if (mknod(path, S_IFIFO | mode, 0) == -1)
            return -errno;
    } else if ((st.st_mode & S_IFIFO) != S_IFIFO)
        return -EINVAL;

    // Works on Linux only, opening pipe for read/write doesn't block the open
    flags &= ~(O_RDONLY | O_WRONLY);
    flags |= O_RDWR;

    ret = open(path, flags);
    if (ret == -1)
        return -errno;

    return ret;
}

int occ_open(const char *pipe_names, occ_interface_type type, struct occ_handle **handle) {
    int ret;
    char rxpath[PATH_MAX], txpath[PATH_MAX];
    char *saveptr;

    *handle = malloc(sizeof(struct occ_handle));
    if (*handle == NULL) {
        return -ENOMEM;
    }

    memset(*handle, 0, sizeof(struct occ_handle));
    (*handle)->magic = OCC_HANDLE_MAGIC;

    strncpy(rxpath, pipe_names, sizeof(rxpath));
    strncpy(txpath, "/missing/TX/pipe", sizeof(txpath));

    saveptr = strchr(pipe_names, ',');
    if (saveptr != NULL) {
        rxpath[saveptr-pipe_names] = '\0';
        saveptr++;
        strncpy(txpath, saveptr, sizeof(txpath));
    }

fprintf(stderr, "RX pipe: %s\n", rxpath);
fprintf(stderr, "TX pipe: %s\n", txpath);

    ret = open_pipe(rxpath, O_RDONLY, 0666);
    if (ret < 0) {
        free(*handle);
        *handle = NULL;
        return ret;
    }
    (*handle)->fd_rx = ret;

    ret = open_pipe(txpath, O_WRONLY, 0644);
    if (ret < 0) {
        close((*handle)->fd_rx);
        free(*handle);
        *handle = NULL;
        return ret;
    }
    (*handle)->fd_tx = ret;

    return 0;
}

int occ_close(struct occ_handle *handle) {

    if (handle != NULL && handle->magic == OCC_HANDLE_MAGIC) {
        (void)close(handle->fd_tx);
        (void)close(handle->fd_rx);

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

    status->dma_size = 0;
    status->board = OCC_BOARD_SIMULATOR;
    status->interface = OCC_INTERFACE_OPTICAL;
    status->firmware_ver = 0x000F0001;
    status->optical_signal = OCC_OPT_CONNECTED;
    status->rx_enabled = handle->rx_enabled;

    return 0;
}

int occ_reset(struct occ_handle *handle) {

    if (handle == NULL || handle->magic != OCC_HANDLE_MAGIC)
        return -EINVAL;

    handle->rx_enabled = false;

    return 0;
}

static size_t _occ_data_align(size_t size) {
    return (size + 3) & ~3;
}

int occ_send(struct occ_handle *handle, const void *data, size_t count) {
    if (handle == NULL || handle->magic != OCC_HANDLE_MAGIC || _occ_data_align(count) != count)
        return -EINVAL;

    int ret = write(handle->fd_tx, data, count);
    if (ret == -1)
        ret = -errno;
    return ret;
}

static int wait_for_ready_read(struct occ_handle *handle, uint32_t timeout) {

    struct pollfd pollfd;
    uint32_t timeout_remain = timeout;
    int ret;

    while (!handle->rx_enabled) {
        if (timeout > 0 && timeout_remain-- == 0)
            return -ETIME;
        usleep(1000);
    }

    pollfd.fd = handle->fd_rx;
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

    ret = read(handle->fd_rx, &handle->buffer[handle->buffer_len], sizeof(handle->buffer) - handle->buffer_len);
    if (ret == -1)
        return -errno;

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
    memmove(handle->buffer, &handle->buffer[count], handle->buffer_len - count);

    handle->buffer_len -= count;
    return 0;
}

int occ_read(struct occ_handle *handle, void *data, size_t count, uint32_t timeout) {
    int ret;

    ret = wait_for_ready_read(handle, timeout);
    if (ret != 0)
        return ret;

    ret = read(handle->fd_rx, data, count);
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
