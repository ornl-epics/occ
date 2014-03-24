#include "occlib_hw.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#define OCC_HANDLE_MAGIC        0x0cc0cc

struct occ_handle {
    uint32_t magic;
    bool rx_enabled;
};

#pragma pack(push)
#pragma pack(4)
struct occ_packet_header {
    uint32_t destination;       //<! Destination id
    uint32_t source;            //<! Sender id
    uint32_t info;              //<! field describing packet type and other info
    uint32_t payload_length;    //<! payload length
    uint32_t reserved1;
    uint32_t reserved2;
};
#pragma pack(pop)

int occ_open(const char *devfile, occ_interface_type type, struct occ_handle **handle) {

    *handle = malloc(sizeof(struct occ_handle));
    if (*handle == NULL) {
        return -ENOMEM;
    }

    memset(*handle, 0, sizeof(struct occ_handle));
    (*handle)->magic = OCC_HANDLE_MAGIC;

    return 0;
}

int occ_close(struct occ_handle *handle) {

    if (handle != NULL && handle->magic == OCC_HANDLE_MAGIC) {
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

int occ_status(struct occ_handle *handle, occ_status_t *status) {

    if (handle == NULL || handle->magic != OCC_HANDLE_MAGIC || status == NULL)
        return -EINVAL;

    status->dma_size = 0;
    status->board = OCC_BOARD_SIMULATOR;
    status->interface = OCC_INTERFACE_OPTICAL;
    status->firmware_ver = 0x000F0001;
    status->optical_signal = true;
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
    return (size + 7) & ~7;
}

int occ_send(struct occ_handle *handle, const void *data, size_t count) {
    if (handle == NULL || handle->magic != OCC_HANDLE_MAGIC || _occ_data_align(count) != count)
        return -EINVAL;

    return -ENOSYS;
}

int occ_data_wait(struct occ_handle *handle, void **address, size_t *count, uint32_t timeout) {

    if (handle == NULL || handle->magic != OCC_HANDLE_MAGIC)
        return -EINVAL;

    return -ENOSYS;
}

int occ_data_ack(struct occ_handle *handle, size_t count) {

    if (handle == NULL || handle->magic != OCC_HANDLE_MAGIC || _occ_data_align(count) != count)
        return -EINVAL;

    return -ENOSYS;
}

int occ_read(struct occ_handle *handle, void *data, size_t count, uint32_t timeout) {
    void *address;
    size_t avail;
    int ret;

    ret = occ_data_wait(handle, &address, &avail, timeout);
    if (ret != 0)
        return ret;

    if (count > avail)
        count = avail;
    memcpy(data, address, count);

    ret = occ_data_ack(handle, count);
    if (ret != 0)
        return ret;

    return count;
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
