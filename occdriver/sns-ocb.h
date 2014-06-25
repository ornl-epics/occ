#ifndef __SNS_DAS_H
#define __SNS_DAS_H

#if !defined(__KERNEL__)
typedef uint32_t u32;
#endif

/* The user should read an appropriate amount of data from the device for
 * the command being requested. Commands are indicated by the offset read.
 *
 * Reading 8 bytes at offset OCB_CMD_RX gives the current status and HW
 * producer index. The first 4 bytes give the index, the second 4 bytes are
 * used for status information. This call will normally block until data is
 * available in the ring buffer, and error occurs, or we reset the card. It
 * may also be interrupted by a signal or return early if using O_NONBLOCK.
 *
 * Reading sizeof(struct ocb_status) bytes at offset OCB_CMD_GET_STATUS
 * gives information about the driver and current status of the hardware.
 * Check the struct ocb_status for details.
 */
#define OCB_CMD_RX				1
#define OCB_CMD_GET_STATUS		2

/* Status flags returned in status member of ocb_status struct */
#define OCB_RX_ENABLED				(1 << 5)
#define OCB_RX_MSG				(1 << 5)
#define OCB_RX_STALLED			(1 << 4)
#define OCB_RESET_OCCURRED		(1 << 3)
#define OCB_MODE_OPTICAL		(1 << 2)
#define OCB_OPTICAL_PRESENT		(1 << 1)
#define OCB_OPTICAL_NOSIGNAL	(1 << 0)

/* Commands are passed to the driver by writing to the fd at a given
 * offset. Each command requires a certain amount of data.
 *
 * OCB_CMD_TX expects the packet data. OCB_CMD_ADVANCE_DQ expects a uint32_t
 * with the amount of the ring buffer consumed, and OCB_CMD_RESET expects a
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
 * Writing 4 bytes at offset OCB_CMD_RX_ENABLE with a non-zero value will
 * enable the RX. 0 will disable it.
 *
 * Writing sizeof(ocb_hw_pkt_sim) bytes at offset OCB_CMD_HW_PKT_SIM_ENABLE
 * will configure the HW Packet Simulator.
 */
#define OCB_CMD_TX				9
#define OCB_CMD_ADVANCE_DQ		10
#define OCB_CMD_RESET			11
#define 	OCB_SELECT_LVDS		0
#define 	OCB_SELECT_OPTICAL	1
#define OCB_CMD_RX_ENABLE		12
#define OCB_CMD_ERR_PKTS_ENABLE	13

/* Not a full 8k as we have to avoid prod_idx == cons_idx (empty) */
// TODO: PCIe queue size is 32*1024, it can't just yet roll-over properly at lower sizes
#define OCB_TX_FIFO_LEN			8192
#define OCB_MAX_TX_LEN			(OCB_TX_FIFO_LEN - 8)
#define OCB_VER					1

/* Offsets are passed to the driver by calling mmap() with the given offset.
 */
#define OCB_MMAP_BAR0           0
#define OCB_MMAP_BAR1           1
#define OCB_MMAP_BAR2           2
#define OCB_MMAP_RX_DMA         6

/* Boards supported by the driver.
 */
#define BOARD_SNS_PCIX	1
#define BOARD_SNS_PCIE	2
#define BOARD_GE_PCIE	3

struct ocb_status {
    u32 ocb_ver;			// Position of this one should not change. OCB version defines the rest of the protocol.
	u32 board_type;			// One of the BOARD_xxx values
    u32 firmware_ver;		// Code is 0xVVYYMMDD -- version, year, month, day (BCD)
    u32 status;
    u32 dq_size;			// Size of RX DMA data cyclic-queue in bytes
    u32 dq_used;			// Used space
    u32 bars[3];			// Sizes of BAR regions used for mmap
};

#endif /* __SNS_DAS_H */
