/*
 * Copyright (c) 2018 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 */

#ifndef __SNS_DAS_H
#define __SNS_DAS_H

#if !defined(__KERNEL__)
typedef uint32_t u32;
typedef uint64_t u64;
#endif

/**
 * OCC major version number, changed when new big new features or reworks.
 */
#define OCC_VER_MAJ 1

/**
 * OCC minor version, changed when interface changes.
 */
#define OCC_VER_MIN 9

/**
 * OCC build version, not enforced to the client.
 */
#define OCC_VER_BUILD 5

/* The user should read an appropriate amount of data from the device for
 * the command being requested. Commands are indicated by the offset read.
 *
 * Reading 8 bytes at offset OCC_CMD_RX gives the current status and HW
 * producer index. The first 4 bytes give the index, the second 4 bytes are
 * used for status information. This call will normally block until data is
 * available in the ring buffer, and error occurs, or we reset the card. It
 * may also be interrupted by a signal or return early if using O_NONBLOCK.
 *
 * Reading sizeof(struct occ_status) bytes at offset OCC_CMD_GET_STATUS
 * gives information about the driver and current status of the hardware.
 * Check the struct occ_status for details.
 */
#define OCC_CMD_RX                  1
#define OCC_CMD_VERSION             2
#define OCC_CMD_GET_STATUS          3
#define OCC_CMD_OLD_PKTS_EN         4

/* Status flags returned in status member of occ_status struct */
#define OCC_OPTICAL_FAULT			(1 << 9)
#define OCC_FIFO_OVERFLOW		(1 << 8)
#define OCC_RX_ERR_PKTS_ENABLED		(1 << 7)
#define OCC_RX_ENABLED			(1 << 6)
#define OCC_RX_MSG			(1 << 5)
#define OCC_DMA_STALLED			(1 << 4)
#define OCC_RESET_OCCURRED		(1 << 3)
#define OCC_MODE_OPTICAL		(1 << 2)
#define OCC_OPTICAL_PRESENT		(1 << 1)
#define OCC_OPTICAL_NOSIGNAL		(1 << 0)

/* Commands are passed to the driver by writing to the fd at a given
 * offset. Each command requires a certain amount of data.
 *
 * OCC_CMD_TX expects the packet data. OCC_CMD_ADVANCE_DQ expects a uint32_t
 * with the amount of the ring buffer consumed, and OCC_CMD_RESET expects a
 * uint32_t to select which interface should be active.
 *
 * The actual command number is selected to be distinct from the read()
 * commands, and should not be a multiple of 4. This will help catch
 * incorrect use of the driver interface.
 *
 * TX calls can be interrupted by a signal, in which case they may leave
 * EINTR in errno. If the card is reset while the call is queued to send a
 * packet, it may use ECONNRESET. EIO indicates a timeout during the TX
 *
 * Writing 4 bytes at offset OCC_CMD_RX_ENABLE with a non-zero value will
 * enable the RX. 0 will disable it.
 *
 * Writing 4 bytes at offset OCC_CMD_ERR_PKTS_ENABLE with a non-zero value will
 * enable receiving error packets. 0 will disable it.
 */
#define OCC_CMD_TX			9
#define OCC_CMD_ADVANCE_DQ		10
#define OCC_CMD_RESET			11
#define 	OCC_SELECT_LVDS		0
#define 	OCC_SELECT_OPTICAL	1
#define OCC_CMD_RX_ENABLE		12
#define OCC_CMD_ERR_PKTS_ENABLE		13

/* Not a full 8k as we have to avoid prod_idx == cons_idx (empty) */
// TODO: PCIe queue size is 32*1024, it can't just yet roll-over properly at lower sizes
#define OCC_TX_FIFO_LEN			8192
#define OCC_MAX_TX_LEN			(OCC_TX_FIFO_LEN - 8)
#define OCC_VER				1

/* Offsets are passed to the driver by calling mmap() with the given offset.
 */
#define OCC_MMAP_BAR0           	0
#define OCC_MMAP_BAR1           	1
#define OCC_MMAP_BAR2           	2
#define OCC_MMAP_RX_DMA         	6

/* Boards supported by the driver.
 */
#define BOARD_SNS_PCIX			1
#define BOARD_SNS_PCIE			2
#define BOARD_GE_PCIE			3

struct occ_status {
    u32 occ_ver;			// Position of this one should not change. OCC version defines the rest of the protocol.
    u32 board_type;			// One of the BOARD_xxx values
    u32 hardware_ver;			// Hardware version
    u32 firmware_ver;			// Firmware version
    u32 firmware_date;			// Code is 0xVVYYMMDD -- version, year, month, day (BCD)
    u64 fpga_serial;			// FPGA serial number
    u32 status;
    u32 dq_size;			// Size of RX DMA data cyclic-queue in bytes
    u32 dq_used;			// Used space
    u32 rx_rate;			// Receive (optic side) data rate in B/s calculated by hw
    u32 bars[3];			// Sizes of BAR regions used for mmap
    u32 err_crc;            // CRC error counter
    u32 err_length;         // Length error counter
    u32 err_frame;          // Frame error counter
    u32 fpga_temp;          // FPGA temperature raw value, conv: ((503.975/4096.0) * (X / 16.0) ) - 273.15 C
    u32 fpga_core_volt;     // FPGA core voltage raw value, conv: ((3.0/4096.0) * (X/16)) V
    u32 fpga_aux_volt;      // FPGA aug voltage raw value, conv: ((3.0/4096.0) * (X/16)) V
};

struct occ_version {
    u32 major;				// Major driver version
    u32 minor;				// Minor version
};

#endif /* __SNS_DAS_H */
