#include "occlib_hw.h"
#include "sns-ocb.h"

#include <assert.h>
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
#define ROLLOVER_BUFFER_SIZE    1800      // Size of temporary buffer when DMA buffer rollover occurs

struct occ_handle {
    uint32_t magic;
    int fd;
    void *dma_buf;
    uint32_t dma_buf_len;
    uint32_t dma_cons_off;
    uint8_t use_optic;
    uint32_t firmware_ver;
    uint32_t last_count;                        //<! Number of bytes available returned by the last occ_data_wait()
    uint8_t rollover_buf[ROLLOVER_BUFFER_SIZE];
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
    int ret = 0;
    uint32_t info[3];

    do {
        *handle = malloc(sizeof(struct occ_handle));
        if (*handle == NULL) {
            ret = -ENOMEM;
            break;
        }

        memset(*handle, 0, sizeof(struct occ_handle));
        (*handle)->magic = OCC_HANDLE_MAGIC;
        (*handle)->dma_buf = MAP_FAILED;

        (*handle)->fd = open(devfile, O_RDWR);
        if ((*handle)->fd == -1) {
            ret = -errno;
            break;
        }

        if (pread((*handle)->fd, info, sizeof(info), OCB_CMD_GET_STATUS) != sizeof(info)) {
            ret = -errno;
            break;
        }
        (*handle)->dma_buf_len = info[1];
        if ((info[2] & OCB_OPTICAL_PRESENT) && type == OCC_INTERFACE_OPTICAL)
            (*handle)->use_optic = 1;

        (*handle)->dma_buf = (void *)mmap(NULL, (*handle)->dma_buf_len,
                                             PROT_READ|PROT_WRITE, MAP_SHARED|MAP_POPULATE,
                                             (*handle)->fd, 0);
        if ((*handle)->dma_buf == MAP_FAILED) {
            ret = -errno;
            break;
        }

        /* Reset the card to select our preferred interface */
        ret = occ_reset(*handle);

    } while (0);

    if (ret != 0 && *handle) {

        if ((*handle)->dma_buf != MAP_FAILED)
            munmap((void *)(*handle)->dma_buf, (*handle)->dma_buf_len);

        if ((*handle)->fd != -1)
            close((*handle)->fd);

        free(*handle);
        *handle = NULL;
    }

    return ret;
}

int occ_close(struct occ_handle *handle) {
    int ret = 0;

    if (handle != NULL && handle->magic == OCC_HANDLE_MAGIC) {

        // XXX: call reset?

        if (munmap((void *)handle->dma_buf, handle->dma_buf_len) != 0)
            ret = -1 * errno;

        if (close(handle->fd) != 0)
            ret = -errno;

        free(handle);
    }

    return ret;
}

int occ_status(struct occ_handle *handle, occ_status_t *status) {
    uint32_t info[3];

    assert(handle != NULL && handle->magic == OCC_HANDLE_MAGIC);
    assert(status != NULL);

    if (pread(handle->fd, info, sizeof(info), OCB_CMD_GET_STATUS) < 0)
        return -errno;

    status->dma_size = handle->dma_buf_len;
    status->interface = (handle->use_optic ? OCC_INTERFACE_OPTICAL : OCC_INTERFACE_LVDS);
    status->firmware_ver = info[0];
    status->optical_signal = (info[2] & OCB_OPTICAL_PRESENT);

    return 0;
}

int occ_reset(struct occ_handle *handle) {
    uint32_t interface;
    uint32_t info[3];

    assert(handle != NULL && handle->magic == OCC_HANDLE_MAGIC);

    interface = (handle->use_optic == 0) ? OCB_SELECT_LVDS : OCB_SELECT_OPTICAL;
    if (pwrite(handle->fd, &interface, sizeof(interface), OCB_CMD_RESET) != sizeof(interface))
        return -errno;

    // Read status to clear the reset-occurred flag
    if (pread(handle->fd, info, sizeof(info), OCB_CMD_GET_STATUS) < 0)
        return -errno;
    // XXX verify the returned status?

    handle->dma_cons_off = 0;

    return 0;
}

static size_t _occ_data_align(size_t size) {
    return (size + 7) & ~7;
}

int occ_send(struct occ_handle *handle, const void *data, size_t count) {
    assert(handle != NULL && handle->magic == OCC_HANDLE_MAGIC);
    assert(_occ_data_align(count) == count);

    int ret = pwrite(handle->fd, (const void *)data, count, OCB_CMD_TX);
    if (ret < 0)
        ret = -errno;
    return 0;
}

int occ_data_wait(struct occ_handle *handle, void **address, size_t *count, uint32_t timeout) {
    int ret;
    uint32_t info[2];

    assert(handle != NULL && handle->magic == OCC_HANDLE_MAGIC);

    // Block until some data is available
    do {
        if (timeout > 0) {
            struct pollfd pollfd;
            pollfd.fd = handle->fd;
            pollfd.events = POLLIN;
            ret = poll(&pollfd, 1, timeout);
            if (ret < 0)
                return -errno;
            else if (ret == 0)
                return -ETIME;
            else if (pollfd.revents & POLLERR)
                return -ECONNRESET;
        }

        ret = pread(handle->fd, info, sizeof(info), OCB_CMD_RX);
        if (ret < 0)
            return -errno;

        /* XXX need to deal with OCB_RX_STALLED, and perhaps status
         * changes such as the optical module losing signal or being
         * removed.
         */
        if (!(info[1] & OCB_RX_MSG)) {
            if (info[1] & OCB_RESET_OCCURRED)
                return -ECONNRESET;
            if (info[1] & OCB_RX_STALLED)
                return -ENOBUFS;
            continue;
        }
    } while (0);

    // There are a couple of assumptions here that we take for granted.
    // * Producer index is always 8-byte aligned
    // * Producer index is always OCC packet aligned

    uint32_t dma_prod_off = info[0];
    if (dma_prod_off >= handle->dma_cons_off) {
        *address = handle->dma_buf + handle->dma_cons_off;
        *count = dma_prod_off - handle->dma_cons_off;
    } else {
        uint32_t headlen = handle->dma_buf_len - handle->dma_cons_off;
        assert(handle->dma_buf_len > handle->dma_cons_off);
        if (headlen > sizeof(handle->rollover_buf)) {
            assert(handle->dma_buf_len > handle->dma_cons_off);
            *address = handle->dma_buf + handle->dma_cons_off;
            *count = handle->dma_buf_len - handle->dma_cons_off;
        } else {
            // Overflow occured and there's little data at the end of the buffer.
            // Since we're not parsing the data, we don't know whether there's more
            // complete packets in this tail. If it is the case, application will
            // most probably process all complete packets and acknowledge what it
            // processed, leaving some data unacknowledged. Which means this block is
            // being called twice at the end of a buffer - not once as one might
            // assume.
            uint32_t taillen = sizeof(handle->rollover_buf) - headlen;
            if (taillen > dma_prod_off)
                taillen = dma_prod_off;
            memcpy(handle->rollover_buf, handle->dma_buf + handle->dma_cons_off, headlen);
            memcpy(&handle->rollover_buf[headlen], handle->dma_buf, taillen);
            *address = handle->rollover_buf;
            *count = headlen + taillen;
        }
    }

    if (_occ_data_align(*count) != *count) {
        // Tough choice. We can't extend the count as the data might not be there yet.
        // So we must shrink the count to previous 8-byte boundary.
        *count = _occ_packet_align(*count - 7);
    }
    handle->last_count = *count;

    return 0;
}

int occ_data_ack(struct occ_handle *handle, size_t count) {
    assert(handle != NULL && handle->magic == OCC_HANDLE_MAGIC);
    assert(_occ_data_align(count) == count); // Driver is aligning the data silently anyway

    if (count > handle->last_count)
        count = handle->last_count;

    uint32_t length = count;
    if (pwrite(handle->fd, &length, sizeof(length), OCB_CMD_ADVANCE_DQ) < 0)
        return -errno;

    handle->dma_cons_off = (handle->dma_cons_off + count) % handle->dma_buf_len;
    return 0;
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
