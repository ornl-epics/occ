/**
 * OCC library implementation that talks to driver directly.
 *
 * All the functions herein are implementation specifics of the OCC
 * library with a different prefix to their names but the same
 * semantics. See occlib.h for API description.
 *
 * \file occlib_drv.h
 */

#include "occlib_hw.h"

int occdrv_open(const char *devfile, occ_interface_type type, struct occ_handle **handle);
int occdrv_open_debug(const char *devfile, occ_interface_type type, struct occ_handle **handle);
int occdrv_close(struct occ_handle *handle);
int occdrv_enable_rx(struct occ_handle *handle, bool enable);
int occdrv_enable_error_packets(struct occ_handle *handle, bool enable);
int occdrv_status(struct occ_handle *handle, occ_status_t *status, occ_status_type type);
int occdrv_reset(struct occ_handle *handle);
int occdrv_send(struct occ_handle *handle, const void *data, size_t count);
int occdrv_data_wait(struct occ_handle *handle, void **address, size_t *count, uint32_t timeout);
int occdrv_data_ack(struct occ_handle *handle, size_t count);
int occdrv_read(struct occ_handle *handle, void *data, size_t count, uint32_t timeout);
int occdrv_io_read(struct occ_handle *handle, uint8_t bar, uint32_t offset, uint32_t *data, uint32_t count);
int occdrv_io_write(struct occ_handle *handle, uint8_t bar, uint32_t offset, const uint32_t *data, uint32_t count);
