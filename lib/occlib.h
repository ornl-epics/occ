/*
 * Copyright (c) 2018 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 *
 * @author Klemen Vodopivec <vodopiveck@ornl.gov>
 *
 * OCC lib is an interface for the OCC boards.
 *
 * A Linux driver handles OCC boards and allows applications to communicate
 * with them. The driver detects and initializes the OCC boards automatically
 * and leaves the configuration and data exchange to the application. This API
 * provides some abstraction of the low-level communication protocol.
 *
 * \file occlib.h
 */

#ifndef OCCLIB_H_INCLUDED
#define OCCLIB_H_INCLUDED

#include <stdbool.h>
#include <stddef.h> // size_t
#include <stdint.h> // uintX_t
#include <stdio.h>  // FILE

#ifdef __cplusplus
extern "C" {
#endif

/**
 * OCC handle represents a single connection to the OCC driver.
  */
struct occ_handle;

/**
 * OCC link interface types.
 */
typedef enum {
    OCC_INTERFACE_LVDS,
    OCC_INTERFACE_OPTICAL,
    OCC_INTERFACE_SOCKET,
} occ_interface_type;

/**
 * OCC board types.
 */
typedef enum {
    OCC_BOARD_NONE = 0,
    OCC_BOARD_PCIX = 1,
    OCC_BOARD_PCIE = 2,
} occ_board_type;

/**
 * Optical signal status
 */
typedef enum {
    OCC_OPT_CONNECTED   = 0,
    OCC_OPT_NO_SFP      = 1,
    OCC_OPT_NO_CABLE    = 2,
    OCC_OPT_LASER_FAULT = 3,
} occ_optical_signal_type;


/**
 * SFP module type
 */
typedef enum {
    OCC_SFP_MODE_UNKNOWN = 0,
    OCC_SFP_MODE_SINGLE  = 1,
    OCC_SFP_MODE_MULTI   = 2,
} occ_sfp_type;

/**
 * Amount of status information obtained.
 */
typedef enum {
    OCC_STATUS_FULL     = 0, //!< All fields in occ_status_t populated
    OCC_STATUS_FAST     = 1, //!< Skip I2C fields
} occ_status_type;

/**
 * Structure describing OCC board and driver information.
 */
typedef struct {
    occ_board_type board;           //!< Board type used by this handle.
    occ_interface_type interface;   //!< Interface type used by this handle.
    uint32_t hardware_ver;          //!< Version of the hardware.
    uint32_t firmware_ver;          //!< Version of the FPGA firmware.
    uint32_t firmware_date;         //!< Build date of the FPGA firmware.
    uint32_t dma_size;              //!< Size of the DMA memory in bytes.
    uint32_t dma_used;              //!< DMA memory used space in bytes.
    uint32_t rx_rate;               //!< Receive (optical side) data rate in B/s
    bool stalled;                   //!< True if DMA memory for incoming data is full and device stopped processing incoming data.
    bool overflowed;                //!< True if internal FIFO overflow was detected and device stopped processing incoming data.
    occ_optical_signal_type optical_signal; //!< Optical signal status
    bool rx_enabled;                //!< True when receiving of data is enabled.
    bool err_packets_enabled;       //!< True when error packets are enabled.
    uint64_t fpga_serial_number;    //!< FPGA serial number
    float fpga_temp;
    float fpga_core_volt;
    float fpga_aux_volt;
    occ_sfp_type sfp_type;          //!< SFP type, single or multi mode
    char sfp_part_number[21];       //!< SFP part number ASCII string
    char sfp_serial_number[17];     //!< SFP serial number ASCII string
    float sfp_temp;
    float sfp_rx_power;
    float sfp_tx_power;
    float sfp_vcc_power;
    float sfp_tx_bias_cur;
    uint32_t err_crc;
    uint32_t err_length;
    uint32_t err_frame;
} occ_status_t;

/**
 * Return OCC library version.
 *
 * Returns version of the library, not the kernel driver. But the library
 * version must match driver's version in order to work together. The check is
 * enforced by occ_open().
 *
 * \param[out] major OCC library number.
 * \param[out] minor OCC library number.
 */
void occ_version(unsigned *major, unsigned *minor);

/**
 * Open a connection to OCC driver and return a handle for it.
 *
 * An opened connection is required for communication with the driver. It
 * creates a channel for sending and receiving data. Sending data to OCC
 * board goes through PCI IO where data is copied from application buffer to
 * the transmit PCI BAR.
 *
 * Receiving data is implemented as DMA from the OCC board into kernel buffer
 * and the application can read data directly from there. OCC board keeps track
 * of producer and consumer indexes and ensures that producer will not overflow
 * the consumer. It will stall otherwise. This puts burden to the application
 * to keep up with incoming data rate.
 *
 * When connection to the OCC driver is opened, OCC board is automatically
 * reset. This ensures clean operation every time a connection is made but
 * it also enforces single connection per device at any give time. Driver will
 * refuse multiple connections for the same device anyway.
 *
 * Function returns a valid handle only if connection has been established
 * and verified. Handle should be used for all OCC driver communication and
 * occ_close() should be used when the communication is no longer required.
 *
 * \param[in] devfile Full path to the device file for selected OCC board.
 * \param[in] type Device type, either LVDS or optical.
 * \param[out] handle Handle to be used with the rest of the API interfaces.
 * \note When occlib_sim is used, devfile should be a pair of RX and TX pipe
 *       names separated by single comma, ie. /tmp/occ.rx,/tmp/occ.tx
 * \retval 0 on success
 * \retval -ENOENT No such device.
 * \retval -ENOMSG Driver/library version mismatch.
 * \retval -ENODATA Could not verify connection with driver.
 * \retval -ENOMEM Not enough memory.
 * \retval -EPROTO Driver version mismatch
 * \retval -X Other POSIX errno values.
 */
int occ_open(const char *devfile, occ_interface_type type, struct occ_handle **handle);

/**
 * Open a debug connection to OCC driver and return a handle for it.
 *
 * Debug connection is limited to following functions:
 * - occ_status()
 * - occ_reset()
 * - occ_io_read()
 * - occ_io_write()
 * - occ_read() - limited
 * - occ_data_wait()
 * - occ_close()
 * Calling any other function of this API with debug connection will return
 * -EINVAL.
 *
 * Debug connection can be used alongside regular connection and is meant for
 * debugging OCC board. As writing register values through debug connection
 * is allowed, it may impact sending and receiving data on the regular connection
 * if one present.
 *
 * Beside the limited set of functions that debug connection supports, there's
 * another important difference. When opened, regular connection automatically
 * resets OCC board and internal variables. This will impact any connection
 * that is opened at a time. Debug connection doesn't do that when opened.
 * This is needed for transparent opening of debug connection alongside
 * regular connection for monitoring purposes. It also allows to inspect OCC
 * board registers after a regular connection has closed.
 *
 * \param[in] devfile Full path to the device file for selected OCC board.
 * \param[in] type Device type, either LVDS or optical.
 * \param[out] handle Handle to be used with the rest of the API interfaces.
 * \note occlib_sim doesn't support this function and always returns -EINVAL.
 * \retval 0 on success
 * \retval -ENOENT No such device.
 * \retval -ENOMSG Driver/library version mismatch.
 * \retval -ENODATA Could not verify connection with driver.
 * \retval -ENOMEM Not enough memory.
 * \retval -X Other POSIX errno values.
 */
int occ_open_debug(const char *devfile, occ_interface_type type, struct occ_handle **handle);

/**
 * Close the connection to OCC driver and release handle.
 *
 * After this function returns the handle is no longer valid and can no
 * longer be used for communication with the driver. Function may not return
 * success but will still release the handle. DMA gets disabled but OCC board
 * is not reset.
 *
 * \param[in] handle OCC API handle to be released.
 * \return 0 on success, negative errno on error.
 */
int occ_close(struct occ_handle *handle);

/**
 * Reset OCC card.
 *
 * Reset will establish initial state of the board when the driver was loaded.
 * This includes setting registers to initial values, establishing DMA for
 * receive queue(s), clearing DMA buffer and sending reset command to the FPGA
 * for internal cleanup.
 *
 * Resetting OCC card might be required sometimes. For instance, OCC card
 * may get stalled to prevent buffer overflow. When that happens no data
 * can be received or sent and driver state as well as board need to be recycled.
 *
 * \param[in] handle Valid OCC API handle.
 * \retval 0 on success
 * \retval -x Return negative errno value.
 */
int occ_reset(struct occ_handle *handle);

/**
 * Enable receiving of data.
 *
 * When powered up or reset, the board will not be receiving any data until
 * instructed to do so. This function allows the application to enable as well
 * as disable receiving of data at any point in time.
 * When high data-rate is expected, the application may want to setup the
 * receive data handler first and only then enable data reception to prevent
 * exhausting the limited DMA buffer before the thread could be even started.
 *
 * Disabling RX can potentially interrupt currently transmitted packet resulting
 * in half of packet being processed. It could also leave some data in internal
 * buffer waiting to be transmitteed to DMA the next time RX gets enabled.
 * To avoid corruption the next time RX is enabled, OCC library will issue
 * OCC reset prior to enabling RX. Application should not expect the continuation of
 * DMA addresses when RX gets enabled.
 *
 * \param[in] handle Valid OCC API handle.
 * \param[in] enable Enable the RX when non-zero, disable otherwise.
 * \retval 0 on success
 * \retval -x Return negative errno value.
 */
int occ_enable_rx(struct occ_handle *handle, bool enable);

/**
 * Enable old style SNS DAS packet.
 *
 * Changing this parameter requires RX to be disabled. Library will
 * automatically enforce this requirement.
 *
 * \param[in] handle Valid OCC API handle.
 * \param[in] enable Enable flag.
 * \retval 0 on success
 * \retval -x Return negative errno value.
 */
int occ_enable_old_packets(struct occ_handle *handle, bool enable);

/**
 * Enable outputing error packets
 *
 * OCC FPGA detects communication errors. It recognizes three groups of
 * errors:
 * - CRC errors are detected when the data integrity for a packet fails
 * - Frame length errors are the ones when the packet length doesn't match actual data
 * - Frame errors are other out-of-sync data errors
 * FPGA provides counter for each group separately. But it can also
 * transform corrupted packet into error packet which application can
 * detect. Error packets can be enabled with this function.
 *
 * \param[in] handle Valid OCC API handle.
 * \param[in] enable Enable or disable error packets.
 * \retval 0 on success
 * \retval -x Return negative errno value.
 */
int occ_enable_error_packets(struct occ_handle *handle, bool enable);

/**
 * Retrieve the OCC board and driver status.
 *
 * \param[in] handle Valid OCC API handle.
 * \param[out] status Pointer to a structure where status information is put.
 * \param[in] occ_status_type Select the amount information to obtain, drives
 *            time needed to return.
 * \return 0 on success, negative errno on error.
 */
int occ_status(struct occ_handle *handle, occ_status_t *status, occ_status_type type);

/**
 * Send arbitrary data to OCC link.
 *
 * This interface does not enforce data format. In most cases only the OCC
 * commands data should be sent from this end. OCC data must be aligned to
 * 4 bytes and this function will make sure to align the non-aligned data
 * automatically.
 *
 * OCC boards do not provide DMA for sending data. Transfer rate of this
 * function is limited by PCI bus I/O.
 *
 * \param[in] handle Valid OCC API handle.
 * \param[in] data Buffer to be transmitted through OCC.
 * \param[in] count Number of bytes to be transmitted.
 * \return Negative errno on error, number of bytes transmitted otherwise.
 */
int occ_send(struct occ_handle *handle, const void *data, size_t count);

/**
 * Wait until some data available and return DMA buffer address.
 *
 * This function will block calling thread until there's some incoming data
 * available. Incoming data is transfered by the OCC board directly to the
 * kernel buffer using DMA. Using this function, the application will receive
 * starting address of the incoming data as well as number of bytes available.
 * Immediately after processing the data, application should use occ_data_ack()
 * function to acknowledge reception of the data. This will effectively allow
 * the OCC board to reuse that part of the buffer for new data.
 *
 * Since the kernel buffer size is limited, the application should process
 * the data quickly. For long computations data should be copied to application
 * memory and processed there.
 *
 * Number of bytes available in the buffer are guaranteed to be 8-byte aligned.
 * Valid data represented by start address and number of bytes contains 1 or more
 * OCC packets. The OCC protocol and OCC board implementations guarantee each
 * OCC packet is also aligned to 8-byte boundary. It's an invalid packet if the
 * length in the OCC header says otherwise. The last packet in the returned
 * buffer might not be complete and application must accomodate for that; either
 * not process the incomplete packet at the end or make an effort to merge with
 * the rest of the data when available.
 *
 * \param[in] handle Valid OCC API handle.
 * \param[out] address Pointer to buffer where incoming data is.
 * \param[out] count On success, the value is updated to the number of bytes available in the buffer.
 * \param[in] timeout Number of millisecond to wait for some data, 0 for infinity.
 * \retval 0 on success
 * \retval -ECONNRESET Device has been reset.
 * \retval -EOVERFLOW DMA buffer is full, device has stalled.
 * \retval -ENODATA No data is available, try again.
 * \retval -ETIME Timeout occured before any data was available.
 */
int occ_data_wait(struct occ_handle *handle, void **address, size_t *count, uint32_t timeout);

/**
 * Acknowledge reception (and processing) of incoming data.
 *
 * Note: Calling this function is equally important as the occ_data_wait().
 *       Each call to occ_data_wait() must have occ_data_ack() counterpart.
 *
 * After application processes the data, it must notify the OCC board. The
 * OCC board will not write over the DMA buffer until this function is called.
 * When buffer is exhausted, OCC board will stall and will need to be reset.
 * It's thus important to release part of the DMA buffer returned by
 * occ_data_wait() as quickly as possible. How much time that is, it depends
 * on the incoming data rate and the DMA buffer size. It's generally a good
 * idea to copy data into potentially much bigger buffer in application
 * address space (virtual memory, can be swapped out) and schedule late
 * processing.
 *
 * \param[in] handle Valid OCC API handle.
 * \param[in] count Number of consumed bytes aligned to 8 bytes.
 * \return 0 on success, negative errno on error.
 */
int occ_data_ack(struct occ_handle *handle, size_t count);

/**
 * Copy incoming data from DMA buffer into application buffer.
 *
 * A wrapper function around occ_data_wait(), memcpy() and occ_data_ack().
 * Its syntax is similar to POSIX read() function. At a cost of copying
 * data, it's a convinience function which most developers will be more
 * familiar with rather than occ_data_wait()/occ_data_ack().
 *
 * In debug mode memory can be read but it will not advance consumer index.
 * It means that same starting address is returned every time unless there's
 * another connection that is able to consume data.
 *
 * \param[in] handle Valid OCC API handle.
 * \param[out] data Copy data from DMA buffer into this buffer.
 * \param[in] count Size of the data buffer.
 * \param[in] timeout Number of millisecond to wait for some data, 0 for infinity.
 * \return Negative errno on error, number of bytes read otherwise.
 */
int occ_read(struct occ_handle *handle, void *data, size_t count, uint32_t timeout);

/**
 * Print available OCC information to file.
 *
 * Mostly used for debugging DMA data, can be called anytime.
 * Information printed includes entire DMA buffer in hex, rollover buffer in hex,
 * registers and more.
 *
 * \param[in] handle Valid OCC API handle.
 * \param[in] outfile Opened file handler where to print all data.
 * \return 0 on success, negative errno on error.
 */
int occ_report(struct occ_handle *handle, FILE *outfile);

#ifdef __cplusplus
}
#endif

#endif // OCCLIB_H_INCLUDED
