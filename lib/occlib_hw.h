/*
 * Copyright (c) 2018 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 *
 * @author Klemen Vodopivec <vodopiveck@ornl.gov>
 *
 * OCC lib interfaces for direct hardware access.
 *
 * \warning interfaces provided by this file should be used with care.
 * \file occlib_hw.h
 */

#ifndef OCCLIB_HW_H_INCLUDED
#define OCCLIB_HW_H_INCLUDED

#include "occlib.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Write data directly to PCI I/O.
 *
 * \warning This function is for device debugging and diagnostics only. Use with care.
 *
 * \param[in] handle Valid OCC API handle.
 * \param[in] bar Base Address Registers number, 0-6.
 * \param[in] offset Offset within selected BAR.
 * \param[in] data Buffer to be written starting at BAR+offset.
 * \param[in] count Number of dwords to be written.
 * \return 0 on success, negative errno on error.
 * \retval -ENOSYS Selected BAR is not available in current device.
 */
int occ_io_write(struct occ_handle *handle, uint8_t bar, uint32_t offset, const uint32_t *data, uint32_t count);

/**
 * Read data directly from PCI I/O.
 *
 * \warning This function is for device debugging and diagnostics only. Use with care.
 *
 * \param[in] handle Valid OCC API handle.
 * \param[in] bar Base Address Registers number, 0-6.
 * \param[in] offset Offset within selected BAR.
 * \param[in] data Buffer where to put dword data from the address starting at BAR+offset.
 * \param[in] count Number of dwords to be read.
 * \return 0 on success, negative errno on error.
 * \retval -ENOSYS Selected BAR is not available in current device.
 */
int occ_io_read(struct occ_handle *handle, uint8_t bar, uint32_t offset, uint32_t *data, uint32_t count);

#ifdef __cplusplus
}
#endif

#endif // OCCLIB_HW_H_INCLUDED
