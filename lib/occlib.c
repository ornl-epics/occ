/*
 * Copyright (c) 2018 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 *
 * @author Klemen Vodopivec <vodopiveck@ornl.gov>
 *
 * This is a wrapper for abstracting the implementation details into specific
 * files. It sets up C function pointers when the connection is opened and the
 * type is known. Rest of the functions are simply invoke the selected
 * implementation function.
 * Such design allows a program to use any implementation at run time
 * without needing to link to different shared libraries.
 */

#include "occlib_hw.h"
#include "occlib_drv.h"
#include "occlib_sock.h"

#include <sns-occ.h> // For OCC_VER_* only

#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#define OCC_HANDLE_MAGIC        0x0cc0cc

struct occ_handle {
    uint32_t magic;
    struct {
        int (*open)(const char *, occ_interface_type, struct occ_handle **);
        int (*open_debug)(const char *, occ_interface_type, struct occ_handle **);
        int (*close)(struct occ_handle *handle);
        int (*enable_rx)(struct occ_handle *handle, bool enable);
        int (*enable_old_packets)(struct occ_handle *handle, bool enable);
        int (*enable_error_packets)(struct occ_handle *handle, bool enable);
        int (*status)(struct occ_handle *handle, occ_status_t *status, occ_status_type type);
        int (*reset)(struct occ_handle *handle);
        int (*send)(struct occ_handle *handle, const void *data, size_t count);
        int (*data_wait)(struct occ_handle *handle, void **address, size_t *count, uint32_t timeout);
        int (*data_ack)(struct occ_handle *handle, size_t count);
        int (*read)(struct occ_handle *handle, void *data, size_t count, uint32_t timeout);
        int (*io_read)(struct occ_handle *handle, uint8_t bar, uint32_t offset, uint32_t *data, uint32_t count);
        int (*io_write)(struct occ_handle *handle, uint8_t bar, uint32_t offset, const uint32_t *data, uint32_t count);
    } ops;
    void *impl_ctx;
};

void occ_version(unsigned *major, unsigned *minor) {
    *major = OCC_VER_MAJ;
    *minor = OCC_VER_MIN;
}

static int _occ_open_common(const char *devfile, occ_interface_type type, struct occ_handle **handle) {
    *handle = malloc(sizeof(struct occ_handle));
    if (!(*handle)) {
        return -ENOMEM;
    }
    (*handle)->magic = OCC_HANDLE_MAGIC;

    if (type == OCC_INTERFACE_LVDS || type == OCC_INTERFACE_OPTICAL) {
        (*handle)->ops.open                 = occdrv_open;
        (*handle)->ops.open_debug           = occdrv_open_debug;
        (*handle)->ops.close                = occdrv_close;
        (*handle)->ops.enable_rx            = occdrv_enable_rx;
        (*handle)->ops.enable_old_packets   = occdrv_enable_old_packets;
        (*handle)->ops.enable_error_packets = occdrv_enable_error_packets;
        (*handle)->ops.status               = occdrv_status;
        (*handle)->ops.reset                = occdrv_reset;
        (*handle)->ops.send                 = occdrv_send;
        (*handle)->ops.data_wait            = occdrv_data_wait;
        (*handle)->ops.data_ack             = occdrv_data_ack;
        (*handle)->ops.read                 = occdrv_read;
        (*handle)->ops.io_read              = occdrv_io_read;
        (*handle)->ops.io_write             = occdrv_io_write;
    } else if (type == OCC_INTERFACE_SOCKET) {
        (*handle)->ops.open                 = occsock_open;
        (*handle)->ops.open_debug           = occsock_open_debug;
        (*handle)->ops.close                = occsock_close;
        (*handle)->ops.enable_rx            = occsock_enable_rx;
        (*handle)->ops.enable_old_packets   = occsock_enable_old_packets;
        (*handle)->ops.enable_error_packets = occsock_enable_error_packets;
        (*handle)->ops.status               = occsock_status;
        (*handle)->ops.reset                = occsock_reset;
        (*handle)->ops.send                 = occsock_send;
        (*handle)->ops.data_wait            = occsock_data_wait;
        (*handle)->ops.data_ack             = occsock_data_ack;
        (*handle)->ops.read                 = occsock_read;
        (*handle)->ops.io_read              = occsock_io_read;
        (*handle)->ops.io_write             = occsock_io_write;
    } else {
        free(*handle);
        *handle = NULL;
        return -EINVAL;
    }

    return 0;
}

int occ_open(const char *devfile, occ_interface_type type, struct occ_handle **handle) {
    int ret = _occ_open_common(devfile, type, handle);

    if (ret == 0) {
        ret = (*handle)->ops.open(devfile, type, (struct occ_handle **)&(*handle)->impl_ctx);
        if (ret != 0) {
            free(*handle);
            *handle = NULL;
        }
    }

    return ret;
}

int occ_open_debug(const char *devfile, occ_interface_type type, struct occ_handle **handle) {
    int ret = _occ_open_common(devfile, type, handle);

    if (ret == 0) {
        ret = (*handle)->ops.open_debug(devfile, type, (struct occ_handle **)&(*handle)->impl_ctx);
        if (ret != 0) {
            free(*handle);
            *handle = NULL;
        }
    }

    return ret;
}

int occ_close(struct occ_handle *handle) {
    int ret = -EINVAL;
    if (handle != NULL && handle->magic == OCC_HANDLE_MAGIC) {
        ret = handle->ops.close(handle->impl_ctx);
        free(handle);
    }
    return ret;
}

int occ_enable_rx(struct occ_handle *handle, bool enable) {
    if (handle == NULL || handle->magic != OCC_HANDLE_MAGIC)
        return -EINVAL;

    return handle->ops.enable_rx(handle->impl_ctx, enable);
}

int occ_enable_old_packets(struct occ_handle *handle, bool enable) {
    if (handle == NULL || handle->magic != OCC_HANDLE_MAGIC)
        return -EINVAL;

    return handle->ops.enable_old_packets(handle->impl_ctx, enable);
}

int occ_enable_error_packets(struct occ_handle *handle, bool enable) {
    if (handle == NULL || handle->magic != OCC_HANDLE_MAGIC)
        return -EINVAL;

    return handle->ops.enable_error_packets(handle->impl_ctx, enable);
}

int occ_status(struct occ_handle *handle, occ_status_t *status, occ_status_type type) {
    if (handle == NULL || handle->magic != OCC_HANDLE_MAGIC || status == NULL)
        return -EINVAL;

    return handle->ops.status(handle->impl_ctx, status, type);
}

int occ_reset(struct occ_handle *handle) {
    if (handle == NULL || handle->magic != OCC_HANDLE_MAGIC)
        return -EINVAL;

    return handle->ops.reset(handle->impl_ctx);
}

int occ_send(struct occ_handle *handle, const void *data, size_t count) {
    if (handle == NULL || handle->magic != OCC_HANDLE_MAGIC)
        return -EINVAL;

    return handle->ops.send(handle->impl_ctx, data, count);
}

int occ_data_wait(struct occ_handle *handle, void **address, size_t *count, uint32_t timeout) {
    if (handle == NULL || handle->magic != OCC_HANDLE_MAGIC)
        return -EINVAL;

    return handle->ops.data_wait(handle->impl_ctx, address, count, timeout);
}

int occ_data_ack(struct occ_handle *handle, size_t count) {
    if (handle == NULL || handle->magic != OCC_HANDLE_MAGIC)
        return -EINVAL;

    return handle->ops.data_ack(handle->impl_ctx, count);
}

int occ_read(struct occ_handle *handle, void *data, size_t count, uint32_t timeout) {
    if (handle == NULL || handle->magic != OCC_HANDLE_MAGIC)
        return -EINVAL;

    return handle->ops.read(handle->impl_ctx, data, count, timeout);
}

int occ_io_read(struct occ_handle *handle, uint8_t bar, uint32_t offset, uint32_t *data, uint32_t count) {
    if (handle == NULL || handle->magic != OCC_HANDLE_MAGIC)
        return -EINVAL;

    return handle->ops.io_read(handle->impl_ctx, bar, offset, data, count);
}

int occ_io_write(struct occ_handle *handle, uint8_t bar, uint32_t offset, const uint32_t *data, uint32_t count) {
    if (handle == NULL || handle->magic != OCC_HANDLE_MAGIC)
        return -EINVAL;

    return handle->ops.io_write(handle->impl_ctx, bar, offset, data, count);
}
