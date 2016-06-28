/**
 * OCC library implementation that communicates over network socket.
 *
 * All the functions herein are implementation specifics of the OCC
 * library with a different prefix to their names but the same
 * semantics. See occlib.h for API description.
 *
 * \file occlib_drv.h
 */

#include "occlib_hw.h"

int occsock_open(const char *devfile, occ_interface_type type, struct occ_handle **handle);
int occsock_open_debug(const char *devfile, occ_interface_type type, struct occ_handle **handle);
int occsock_close(struct occ_handle *handle);
int occsock_enable_rx(struct occ_handle *handle, bool enable);
int occsock_enable_error_packets(struct occ_handle *handle, bool enable);
int occsock_status(struct occ_handle *handle, occ_status_t *status, bool fast_status);
int occsock_reset(struct occ_handle *handle);
int occsock_send(struct occ_handle *handle, const void *data, size_t count);
int occsock_data_wait(struct occ_handle *handle, void **address, size_t *count, uint32_t timeout);
int occsock_data_ack(struct occ_handle *handle, size_t count);
int occsock_read(struct occ_handle *handle, void *data, size_t count, uint32_t timeout);
int occsock_io_read(struct occ_handle *handle, uint8_t bar, uint32_t offset, uint32_t *data, uint32_t count);
int occsock_io_write(struct occ_handle *handle, uint8_t bar, uint32_t offset, const uint32_t *data, uint32_t count);
