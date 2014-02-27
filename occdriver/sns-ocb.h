#ifndef __SNS_DAS_H
#define __SNS_DAS_H

/* The user should read an appropriate amount of data from the device for
 * the command being requested. Commands are indicated by the offset read.
 *
 * Reading 8 bytes at offset OCB_CMD_RX gives the current status and HW
 * producer index. The first 4 bytes give the index, the second 4 bytes are
 * used for status information. This call will normally block until data is
 * available in the ring buffer, and error occurs, or we reset the card. It
 * may also be interrupted by a signal or return early if using O_NONBLOCK.
 *
 * Reading 12 bytes at offset OCB_CMD_GET_STATUS gives information about
 * the driver and current status of the hardware. The first four bytes is
 * the firmware version of the card. The second four bytes is the length
 * of the RX ring buffer, which should be passed to mmap() to get access.
 * The last four bytes are status information, as returned in the second
 * word for OCB_CMD_RX.
 */
#define OCB_CMD_RX		1
#define OCB_CMD_GET_STATUS	2

/* Status flags returned in second word from read() calls */
#define OCB_RX_MSG		(1 << 5)
#define OCB_RX_STALLED		(1 << 4)
#define OCB_RESET_OCCURRED	(1 << 3)
#define OCB_MODE_OPTICAL	(1 << 2)
#define OCB_OPTICAL_PRESENT	(1 << 1)
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
 */
#define OCB_CMD_TX		9
#define OCB_CMD_ADVANCE_DQ	10
#define OCB_CMD_RESET		11
#define 	OCB_SELECT_LVDS		0
#define 	OCB_SELECT_OPTICAL	1

/* Not a full 8k as we have to avoid prod_idx == cons_idx (empty) */
// TODO: PCIe queue size is 32*1024, it can't just yet roll-over properly at lower sizes
#define OCB_TX_FIFO_LEN		8192
#define OCB_MAX_TX_LEN		(OCB_TX_FIFO_LEN - 8)

#endif /* __SNS_DAS_H */
