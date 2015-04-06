/* sns-ocb -- a Linux driver for the SNS PCI-X, PCIe, and GE OCC cards
 *
 * Originally by David Dillow, December 2013
 */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/ratelimit.h>
#include <linux/poll.h>
#include <linux/delay.h>
#include <linux/version.h>
#include "sns-ocb.h"

/* Newer kernels provide these, but not the RHEL6 kernels...
 */
#ifndef dev_err_ratelimited
#define dev_level_ratelimited(dev_level, dev, fmt, ...)			\
do {									\
	static DEFINE_RATELIMIT_STATE(_rs,				\
				      DEFAULT_RATELIMIT_INTERVAL,	\
				      DEFAULT_RATELIMIT_BURST);		\
	if (__ratelimit(&_rs))						\
		dev_level(dev, fmt, ##__VA_ARGS__);			\
} while (0)

#define dev_warn_ratelimited(dev, fmt, ...)				\
	dev_level_ratelimited(dev_warn, dev, fmt, ##__VA_ARGS__)
#define dev_err_ratelimited(dev, fmt, ...)				\
	dev_level_ratelimited(dev_err, dev, fmt, ##__VA_ARGS__)
#endif

/* Helper macro for defining sysfs device attributes and hiding
 * compound literals.
 */
#define SNSOCB_DEVICE_ATTR(_name, _mode, _show, _store)			\
	&(struct device_attribute) {					\
		.attr = {						\
			.name = _name,					\
			.mode = _mode,					\
		},							\
		.show = _show,						\
		.store = _store,					\
	}


#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,8,0)
#define __devexit
#endif // LINUX_VERSION_CODE

/* Only really need one while on PCI-X, but hope to support multiple
 * cards easily on PCIe with the same driver.
 */
#define OCB_MAX_DEVS		8

/* How much room do we allocate for the ring buffer -- it is difficult to
 * get contiguous memory once the machine has been up for a while, but the
 * expected use case for this driver is to be loaded at boot. We can handle
 * up to 4 MB, but we'll start with 2 MB for now.
 */
#define OCB_DQ_SIZE			(2 * 1024 * 1024)

/* For cards that split the Optical data into three queues -- we have
 * an inbound message queue of 64 entries, each 32 bytes and a command
 * queue, which we'll ask for 64 KB.
 */
#define OCB_IMQ_ENTRIES		64
#define OCB_IMQ_SIZE		(OCB_IMQ_ENTRIES * 8 * sizeof(u32))
#define OCB_CQ_SIZE		(64 * 1024)

#define OCB_MMIO_BAR		0
#define OCB_TXFIFO_BAR		1
#define OCB_DDR_BAR		2

#define REG_VERSION		0x0000

/* Old versions of the firmware had more configuration options, but
 * these are all that are used in the version we support for this driver.
 */
#define REG_CONFIG					0x0004
#define		OCB_CONF_TX_ENABLE			0x00000001
#define		OCB_CONF_RX_ENABLE			0x00000002
#define		OCB_CONF_SELECT_OPTICAL			0x00000008
#define		OCB_CONF_OPTICAL_ENABLE			0x00000010
#define		OCB_CONF_ERR_PKTS_ENABLE		0x00040000 // Turn detected errors into error packets
#define		OCB_CONF_ERRORS_RESET			0x04000000 // Clear detected error counters
#define		OCB_CONF_RESET				0x80000000
#define REG_STATUS					0x0008
#define		OCB_STATUS_TX_DONE			0x00000001
#define		OCB_STATUS_RX_LVDS			0x00000002
#define		OCB_STATUS_BUFFER_FULL			0x00000004
#define		OCB_STATUS_OPTICAL_PRESENT		0x00000008
#define		OCB_STATUS_OPTICAL_NOSIGNAL		0x00000010
#define		OCB_STATUS_OPTICAL_FAULT		0x00000020
#define		OCB_STATUS_TX_IDLE			0x00000040
#define		OCB_STATUS_RX_OPTICAL			0x00000100
#define REG_MODULE_MASK					0x0010
#define REG_MODULE_ID					0x0014
#define REG_SERNO_LO 					0x0018
#define REG_SERNO_HI 					0x001C
#define REG_IRQ_STATUS					0x00c0
#define		OCB_IRQ_DMA_STALL			0x00000002
#define		OCB_IRQ_FIFO_OVERFLOW			0x00000004
#define		OCB_IRQ_RX_DONE				0x00000010
#define		OCB_IRQ_ENABLE				0x80000000
#define REG_IRQ_ENABLE					0x00c4
#define REG_IRQ_CNTL					0x00c8
#define		OCB_COALESCING_ENABLE			0x80000000
#define		OCB_EOP_ENABLE				0x40000000
#define REG_FIRMWARE_DATE				0x0100
#define REG_ERROR_CRC_COUNTER				0x0180 // CRC errors counter (PCIe only)
#define REG_ERROR_LENGTH_COUNTER			0x0184 // Frame length errors counter (PCIe only)
#define REG_ERROR_FRAME_COUNTER				0x0188 // Frame errors counter (PCIe only)
#define REG_RX_RATE     				0x018C     // Receive rate calculated every second
#define REG_COMM_ERR					0x0240
#define REG_LOST_PACKETS				0x0244		/* 16 bit register */

/* Inbound message queue (RX, split RX from older firmware, GE card) */
#define REG_IMQ_ADDR		0x0058
#define REG_IMQ_ADDRHI		0x005c
#define REG_IMQ_PROD_ADDR	0x0060
#define REG_IMQ_PROD_ADDRHI	0x0064
#define REG_IMQ_CONS_INDEX	0x0068
#define REG_IMQ_PROD_INDEX	0x006c

/* Command queue (split RX from older firmware, GE card) */
#define REG_CQ_MAX_OFFSET	0x003c
#define REG_CQ_ADDR		0x0040
#define REG_CQ_ADDRHI		0x0044
#define REG_CQ_PROD_ADDR	0x0048
#define REG_CQ_PROD_ADDRHI	0x004c
#define REG_CQ_CONS_INDEX	0x0050
#define REG_CQ_PROD_INDEX	0x0054

/* Data queue */
#define REG_DQ_MAX_OFFSET	0x0088
#define REG_DQ_ADDR		0x0070
#define REG_DQ_ADDRHI		0x0074
#define REG_DQ_PROD_ADDR	0x0078
#define REG_DQ_PROD_ADDRHI	0x007c
#define REG_DQ_CONS_INDEX	0x0080
#define REG_DQ_PROD_INDEX	0x0084

/* Inbound DMA (IDMA) from the point of view of the card, TX from host */
#define REG_TX_CONS_INDEX	0x090
#define REG_TX_PROD_INDEX	0x094
#define REG_TX_LENGTH		0x098

/* Outbound DMA (ODMA) from the point of view of the card, RX from host */
#define REG_RX_CONS_INDEX	0x09c
#define REG_RX_PROD_INDEX	0x0a0
#define REG_RX_LENGTH		0x0a4

/* For the split RX handling, we need to copy the IMQ out of the queue
 * quickly, as there are only 63 hardware entries available. We'll also
 * need to check the packet type to find the rest of the data.
 */
#define SW_IMQ_RING_SIZE	4096
#define IMQ_TYPE_COMMAND	0x80000000

/* Forward declaration of functions used in the structs */
static ssize_t snsocb_sysfs_show_irq_coallesce(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t snsocb_sysfs_store_irq_coallesce(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
static ssize_t snsocb_sysfs_show_dma_big_mem(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t snsocb_sysfs_store_dma_big_mem(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);

/* Layout of the hardware Incoming Message Queue */
struct hw_imq {
	u32	dest;
	u32	src;
	u32	type;
	u32	length;
	u32	info[2];
	u32	unused[2];
};

/* Layout of the actual used data in the IMQ */
struct sw_imq {
	u32	dest;
	u32	src;
	u32	type;
	u32	length;
	u32	info[2];
};

/* Board capabilities description structure */
struct ocb_board_desc {
	u32 type;
	u32 *version;
	u32 tx_fifo_len;
	u32 unified_que;
	u32 bars[3];
	u32 reset_errcnt;	// Does board have support for resetting error counters?
	struct attribute_group sysfs;
};

struct ocb {
	void __iomem *ioaddr;
	void __iomem *txfifo;

	spinlock_t lock;

	/* These indexes are always used for the buffer shared with the
	 * user, whether the OCC DMA's directly or if we're emulating
	 * a unified ring.
	 */
	u32 dq_cons;
	u32 dq_prod;
	u32 conf;
	u32 irqs;

	bool emulate_dq;
	bool reset_in_progress;
	bool reset_occurred;
	int stalled; // valid values are 0, OCB_DMA_STALLED and OCB_FIFO_OVERFLOW

	u32 version;
	u32 firmware_date;
	u64 fpga_serial;

	wait_queue_head_t tx_wq;
	wait_queue_head_t rx_wq;
	struct mutex tx_lock;
	void *tx_buffer;
	u32 tx_prod;

	struct tasklet_struct rxtask;
	struct device dev;
	struct cdev cdev;

	bool in_use;
	bool use_optical;
	int minor;

	/* The user DQ ring; For LVDS or unified DQ ring firmware, we point
	 * the OCC's DMA engine directly at the memory, otherwise, we'll
	 * point it at a separate ring and copy.
	 */
	struct page *dq_page;
	dma_addr_t dq_dma;
	unsigned long dq_size;
	unsigned long dq_big_addr;
	char dq_big_cnf[64];

	/* These are used to emulate the combined DQ from later firmware
	 * on the SNS PCI-X card and SNS PCIe card.
	 */
	struct page *hwdq_page;
	struct page *hwimq_page;
	struct page *hwcq_page;
	dma_addr_t hwdq_dma;
	dma_addr_t hwimq_dma;
	dma_addr_t hwcq_dma;
	u32 hwdq_cons;
	u32 hwdq_prod;
	u32 hwimq_cons;
	u32 hwcq_cons;

	struct sw_imq *imq;
	u32 imq_cons;
	u32 imq_prod;

	/* Needed to synchronize interrupts before card reset */
	struct pci_dev *pdev;

	/* Pointer to a statically allocated description for this board */
	struct ocb_board_desc *board;
};

/**
 * Context for single file open operations, from when the file is opened
 * until it's closed.
 */
struct file_ctx {
	struct ocb *ocb;
	bool debug_mode;
};

static const char *snsocb_name[] = {
	[BOARD_SNS_PCIX] = "SNS PCI-X",
	[BOARD_SNS_PCIE] = "SNS PCIe",
	[BOARD_GE_PCIE] = "GE PCIe",
};

static struct ocb_board_desc boards[] = {
	{
		.type = BOARD_SNS_PCIX,
		.version = (u32 []){ 0x31121106, 0x31130603, 0 },
		.tx_fifo_len = 8192,
		.unified_que = 1,
		.bars = { 1048576, 1048576 },
		.reset_errcnt = 0,
	},
	{
		.type = BOARD_SNS_PCIX,
		.version = (u32 []){ 0x22100817, 0 },
		.tx_fifo_len = 8192,
		.unified_que = 0,
		.bars = { 1048576, 1048576 },
		.reset_errcnt = 0,
	},
	{
		.type = BOARD_SNS_PCIE,
		.version = (u32 []){ 0x000a0001, 0 },
		.tx_fifo_len = 32768,
		.unified_que = 1,
		.bars = { 4096, 32768, 16777216 },
		.reset_errcnt = 1,
	},
	{
		.type = BOARD_SNS_PCIE,
		.version = (u32 []){ 0x000b0001, 0 },
		.tx_fifo_len = 32768,
		.unified_que = 1,
		.bars = { 4096, 32768, 16777216 },
		.reset_errcnt = 1,
		.sysfs.attrs = (struct attribute **) (struct device_attribute *[]){
			SNSOCB_DEVICE_ATTR("irq_coalescing", 0644, snsocb_sysfs_show_irq_coallesce, snsocb_sysfs_store_irq_coallesce),
			SNSOCB_DEVICE_ATTR("dma_big_mem", 0644, snsocb_sysfs_show_dma_big_mem, snsocb_sysfs_store_dma_big_mem),
			NULL,
		},
	},
	{ 0 }
};

static DEFINE_MUTEX(snsocb_devlock);
static struct class *snsocb_class;
static dev_t snsocb_basedev;
static struct ocb *snsocb_devs[OCB_MAX_DEVS];

static void __snsocb_stalled(struct ocb *ocb, int type)
{
	/* We've stalled for some reason; disable RX, as it appears the GE
	 * card gets unhappy and nuke interrupts on the machine.
	 *
	 * Caller must hold ocb->lock.
	 */
	ocb->stalled = type;
	iowrite32(ocb->conf & ~OCB_CONF_RX_ENABLE, ocb->ioaddr + REG_CONFIG);
	wake_up(&ocb->rx_wq);
}

static void snsocb_stalled(struct ocb *ocb, int type)
{
	unsigned long flags;

	spin_lock_irqsave(&ocb->lock, flags);
	__snsocb_stalled(ocb, type);
	spin_unlock_irqrestore(&ocb->lock, flags);
}

static u32 __snsocb_status(struct ocb *ocb)
{
	/* Caller must hold ocb->lock */
	u32 hw_status, status = 0;

	status |= ocb->stalled;
	if (ocb->use_optical)
		status |= OCB_MODE_OPTICAL;
	if (ocb->reset_occurred)
		status |= OCB_RESET_OCCURRED;
	if (ocb->conf & OCB_CONF_RX_ENABLE)
		status |= OCB_RX_ENABLED;
	if (ocb->conf & OCB_CONF_ERR_PKTS_ENABLE)
		status |= OCB_RX_ERR_PKTS_ENABLED;

	hw_status = ioread32(ocb->ioaddr + REG_STATUS);
	if (hw_status & OCB_STATUS_OPTICAL_PRESENT) {
		status |= OCB_OPTICAL_PRESENT;
		if (hw_status & OCB_STATUS_OPTICAL_NOSIGNAL)
			status |= OCB_OPTICAL_NOSIGNAL;
		if (hw_status & OCB_STATUS_OPTICAL_FAULT)
			status |= OCB_OPTICAL_FAULT;
	}

	return status;
}

static u32 __snsocb_rxrate(struct ocb *ocb)
{
	if (ocb->board->type == BOARD_SNS_PCIE)
		return ioread32(ocb->ioaddr + REG_RX_RATE);
	return 0;
}

static u32 __snsocb_tx_room(struct ocb *ocb)
{
	/* Caller must hold ocb->tx_lock
	 *
	 * We want to return bytes available, but the indexes are kept in
	 * double words, so we need to convert
	 */
	u32 prod = ocb->tx_prod << 3;
	u32 cons = ioread32(ocb->ioaddr + REG_TX_CONS_INDEX) << 3;
	u32 room = ocb->board->tx_fifo_len + cons - prod - 8;
	return room % ocb->board->tx_fifo_len;
}

static ssize_t snsocb_tx(struct file *file, struct ocb *ocb,
			 const char __user *buf, size_t count)
{
	u32 dwords, head, tail, conf;
	DEFINE_WAIT(wait);
	int timeout, ret = 0;

	if (count < 1 || count > ocb->board->tx_fifo_len)
		return -EINVAL;

	mutex_lock(&ocb->tx_lock);
	for (;;) {
		prepare_to_wait(&ocb->tx_wq, &wait, TASK_INTERRUPTIBLE);
		if (ocb->reset_in_progress) {
			ret = -ECONNRESET;
			break;
		}
		conf = ioread32(ocb->ioaddr + REG_CONFIG);
		if (!(conf & OCB_CONF_TX_ENABLE) &&
					count < __snsocb_tx_room(ocb))
			break;
		if (file->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			break;
		}
		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}
		mutex_unlock(&ocb->tx_lock);
		schedule();
		mutex_lock(&ocb->tx_lock);
	}
	finish_wait(&ocb->tx_wq, &wait);

	if (ret)
		goto out;

	/* We don't have a nice function to copy from user space to
	 * the MMIO/FIFO, so we bounce through a buffer. This isn't a
	 * terrible problem, as the TX side is not performance critical.
	 */
	ret = -EFAULT;
	if (copy_from_user(ocb->tx_buffer, buf, count))
		goto out;

	if (count % 8)
		memset(ocb->tx_buffer + count, 0, 8 - (count % 8));

	dwords = count + 7;
	dwords /= 8;

	/* TX launched, adjust our return code to match the original count */
	ret = count;

	/* Calculate how many dwords of this message can fit before we wrap
	 * around the MMIO space (head).
	 */
	head = (ocb->board->tx_fifo_len / 8) - ocb->tx_prod;
	if (dwords < head) {
		head = dwords;
		tail = 0;
	} else
		tail = dwords - head;

	__iowrite32_copy(ocb->txfifo + (ocb->tx_prod * 8),
			 ocb->tx_buffer, head * 2);
	__iowrite32_copy(ocb->txfifo, ocb->tx_buffer + head * 8, tail * 2);

	ocb->tx_prod += dwords;
	ocb->tx_prod %= ocb->board->tx_fifo_len / 8;

	iowrite32(count, ocb->ioaddr + REG_TX_LENGTH);
	iowrite32(ocb->tx_prod, ocb->ioaddr + REG_TX_PROD_INDEX);

	/* Force the settings to post */
	ioread32(ocb->ioaddr + REG_TX_PROD_INDEX);

	/* Kick off the TX */
	iowrite32(ocb->conf | OCB_CONF_TX_ENABLE, ocb->ioaddr + REG_CONFIG);

	/* There is no completion interrupt, so now we wait...
	 *
	 * It takes 100ns or so to transmit the minimum 24 byte packet over
	 * the 2 Gbps optical link. We'll busy wait much longer to handle
	 * the common probe packets, but not spin the CPU waiting for the
	 * larger config packets.
	 *
	 * Note that we hold the tx_lock for this entire time; this is how
	 * we serialize multiple threads that wish to send a message.
	 */
	timeout = 20;
	do {
		udelay(1);
		conf = ioread32(ocb->ioaddr + REG_CONFIG);
	} while ((conf & OCB_CONF_TX_ENABLE) && --timeout);

	if (!timeout) {
		timeout = 5000;
		do {
			msleep(1);
			conf = ioread32(ocb->ioaddr + REG_CONFIG);
		} while ((conf & OCB_CONF_TX_ENABLE) && --timeout);
	}

	if (!timeout) {
		dev_err(&ocb->dev, "TX timeout\n");
		ret = -EIO;
	}

out:
	mutex_unlock(&ocb->tx_lock);

	/* Wake up anyone else trying to send */
	wake_up(&ocb->tx_wq);
	return ret;
}

static ssize_t snsocb_rx(struct file *file, char __user *buf, size_t count)
{
	struct file_ctx *file_ctx = file->private_data;
	struct ocb *ocb = file_ctx->ocb;
	DEFINE_WAIT(wait);
	int ret = 0;
	u32 info[2];

	if (count != sizeof(info))
		return -EINVAL;

	for (;;) {
		prepare_to_wait(&ocb->rx_wq, &wait, TASK_INTERRUPTIBLE);
		spin_lock_irq(&ocb->lock);
		if (ocb->reset_in_progress) {
			ret = -ECONNRESET;
			break;
		}
		if (ocb->stalled)
			break;
		if (ocb->dq_prod != ocb->dq_cons)
			break;
		spin_unlock_irq(&ocb->lock);

		if (file->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			spin_lock_irq(&ocb->lock);
			break;
		}
		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			spin_lock_irq(&ocb->lock);
			break;
		}
		schedule();
	}
	finish_wait(&ocb->rx_wq, &wait);

	info[0] = ocb->dq_prod;
	info[1] = __snsocb_status(ocb);
	if (ocb->dq_prod != ocb->dq_cons)
		info[1] |= OCB_RX_MSG;

	spin_unlock_irq(&ocb->lock);

	if (ret)
		goto out;

	ret = -EFAULT;
	if (copy_to_user(buf, info, sizeof(info)))
		goto out;

	ret = sizeof(info);

out:
	return ret;
}

static u32 __snsocb_rxroom(struct ocb *ocb)
{
	/* Caller must hold ocb->lock */
	u32 used = ocb->dq_prod - ocb->dq_cons + OCB_DQ_SIZE;
	used %= OCB_DQ_SIZE;
	return OCB_DQ_SIZE - used - 1;
}

static u32 snsocb_rxcopy(struct ocb *ocb, u32 prod, void *src, u32 len)
{
	if (len > 0) {
		/* __snsocb_rxroom() must indicate there is enough room
		 * for the entire packet before calling here.
		 */
		void *dst = page_address(ocb->dq_page);
		u32 head, tail;

		/* We know there is room for the packet, so just
		 * check for any wrapping on the ring buffer.
		 */
		head = OCB_DQ_SIZE - prod;
		head = min(head, len);
		tail = len - head;

		memcpy(dst + prod, src, head);
		memcpy(dst, src + head, tail);
	}

	return (prod + len) % OCB_DQ_SIZE;
}

static int snsocb_rxone(struct ocb *ocb)
{
	/* Called by the RX interrupt to implement the software
	 * emulated data ring.
	 */
	u32 room_needed, length, head, tail, size;
	u32 creg, prod, dq_prod, cons, imq_cons, hwcq_cons, hwdq_cons, used_room;
	struct sw_imq *imq;
	void *src;

	spin_lock_irq(&ocb->lock);
	imq_cons = ocb->imq_cons;
	hwcq_cons = ocb->hwcq_cons;
	hwdq_cons = ocb->hwdq_cons;
	dq_prod = ocb->dq_prod;
	used_room = __snsocb_rxroom(ocb);

	if (ocb->imq_prod == ocb->imq_cons || ocb->stalled) {
		spin_unlock_irq(&ocb->lock);
		return 1;
	}
	spin_unlock_irq(&ocb->lock);

	/* We need room for the split header, and the payload. */
	imq = &ocb->imq[imq_cons];
	length = ALIGN(imq->length, 4);
	room_needed = sizeof(struct sw_imq);
	room_needed += length;

	/* TODO given the glitches on the index updates in the PCI-X unified
	 * firmware versions, we may wish to handle the index update and
	 * validation in the interrupt handler for the CQ/DQ rings. This does
	 * not seem to be a problem on the GE card, and should not be on issue
	 * on the SNS PCIe card, so we'll hold off on the change for now.
	 */
	if (imq->type & IMQ_TYPE_COMMAND) {
		src = page_address(ocb->hwcq_page);
		prod = ioread32(ocb->ioaddr + REG_CQ_PROD_INDEX);
		cons = hwcq_cons;
		creg = REG_CQ_CONS_INDEX;
		size = OCB_CQ_SIZE;
	} else {
		src = page_address(ocb->hwdq_page);
		prod = ioread32(ocb->ioaddr + REG_DQ_PROD_INDEX);
		cons = hwdq_cons;
		creg = REG_DQ_CONS_INDEX;
		size = OCB_DQ_SIZE;
	}

	/* Make sure we have enough data in the source queue */
	if (length > ((prod - cons + size) % size)) {
		dev_err_ratelimited(&ocb->dev,
				    "IMQ too-long length (reg %x)\n", creg);
		spin_lock_irq(&ocb->lock);
		__snsocb_stalled(ocb, OCB_DMA_STALLED);
		goto consume_queue;
	}

	/* How much can we copy from the source before we wrap? */
	if (cons > prod) {
		head = size - cons;
		head = min(head, length);
		tail = length - head;
	} else {
		head = length;
		tail = 0;
	}

	if (room_needed > used_room) {
		dev_warn_ratelimited(&ocb->dev, "userspace stalled RX\n");
		spin_lock_irq(&ocb->lock);
		__snsocb_stalled(ocb, OCB_DMA_STALLED);
		goto consume_queue;
	}

	/* We do our updates against a local copy of the userspace DQ
	 * producer index; this drops the lock during the copies, and
	 * ensures users only see a complete packet at the end.
	 */
	dq_prod = snsocb_rxcopy(ocb, dq_prod, imq, sizeof(struct sw_imq));
	dq_prod = snsocb_rxcopy(ocb, dq_prod, src + cons, head);
	dq_prod = snsocb_rxcopy(ocb, dq_prod, src, tail);

	/* The OCC card consumes the queues in 4 byte incremenets */
	cons += ALIGN(length, 4);
	cons %= size;

	spin_lock_irq(&ocb->lock);
	ocb->dq_prod = dq_prod;

consume_queue:
	if (imq->type & IMQ_TYPE_COMMAND)
		ocb->hwcq_cons = cons;
	else
		ocb->hwdq_cons = cons;
	iowrite32(cons, ocb->ioaddr + creg);
	ocb->imq_cons++;
	ocb->imq_cons %= SW_IMQ_RING_SIZE;
	wake_up(&ocb->rx_wq);

	spin_unlock_irq(&ocb->lock);

	return 0;
}

static void snsocb_rxtask(unsigned long data)
{
	struct ocb *ocb = (void *)data;
	int budget = 32;

	while (--budget) {
		if (snsocb_rxone(ocb))
			return;
	}

	tasklet_hi_schedule(&ocb->rxtask);
}

static int snsocb_saveimqs(struct ocb *ocb)
{
	/* Copy packet headers from the limited hardware queue to our larger
	 * one to cover the latency of tasklet packet copy.
	 * New packet headers become avaialbe only when the function completes,
	 * but the device is not locked during copying.
	 */
	u32 hwprod = ioread32(ocb->ioaddr + REG_IMQ_PROD_INDEX);
	struct hw_imq *hwimq = page_address(ocb->hwimq_page);
	u32 new_prod, hwimq_cons, imq_prod, imq_cons;
	int rc = 0;

	spin_lock(&ocb->lock);
	hwimq_cons = ocb->hwimq_cons;
	imq_prod = ocb->imq_prod;
	imq_cons = ocb->imq_cons;
	spin_unlock(&ocb->lock);

	while (hwimq_cons != hwprod) {
		/* Copy in the new descriptors we know about before
		 * checking for more.
		 */
		do {
			new_prod = (imq_prod + 1) % SW_IMQ_RING_SIZE;
			if (new_prod == imq_cons) {
				// XXX: If seeing this case a lot, consider refreshing imq_cons to make sure consumer did not advance
				rc = -ENOSPC;
				dev_warn_ratelimited(&ocb->dev,
						     "no IMQ space\n");
				goto out_update;
			}

			memcpy(ocb->imq + imq_prod, hwimq + hwimq_cons, sizeof(*ocb->imq));
			imq_prod = new_prod;

			/* Go ahead and consume the HW entry */
			hwimq_cons++;
			hwimq_cons %= OCB_IMQ_ENTRIES;
		} while (hwimq_cons != hwprod);

		hwprod = ioread32(ocb->ioaddr + REG_IMQ_PROD_INDEX);
	}

out_update:
	spin_lock(&ocb->lock);
	ocb->hwimq_cons = hwimq_cons;
	ocb->imq_prod = imq_prod;
	spin_unlock(&ocb->lock);

	/* snsocb_reset() is the only place where this register is overwritten
	 * but the snsocb_reset() makes sure to disable and synchronize interrupts
	 * we're safe to do the write without the lock.
	 */
	iowrite32(hwimq_cons, ocb->ioaddr + REG_IMQ_CONS_INDEX);

	return rc;
}

static irqreturn_t snsocb_interrupt(int irq, void *data)
{
	struct ocb *ocb = data;
	u32 intr_status;

	intr_status = ioread32(ocb->ioaddr + REG_IRQ_STATUS);
	if (!intr_status)
		return IRQ_NONE;

	if (intr_status & OCB_IRQ_DMA_STALL) {
		snsocb_stalled(ocb, OCB_DMA_STALLED);
		dev_err_ratelimited(&ocb->dev, "Detected DMA stall flag");
		goto out;
	} else if (intr_status & OCB_IRQ_FIFO_OVERFLOW) {
		snsocb_stalled(ocb, OCB_FIFO_OVERFLOW);
		dev_err_ratelimited(&ocb->dev, "Detected FIFO overflow flag");
		goto out;
	}

	if (ocb->emulate_dq) {
		if (snsocb_saveimqs(ocb))
			snsocb_stalled(ocb, OCB_DMA_STALLED);
		else
			tasklet_hi_schedule(&ocb->rxtask);
	} else {
		spin_lock(&ocb->lock);
		ocb->dq_prod = ioread32(ocb->ioaddr + REG_DQ_PROD_INDEX);
		wake_up(&ocb->rx_wq);
		spin_unlock(&ocb->lock);
	}

out:
	intr_status &= ~OCB_IRQ_ENABLE;
	iowrite32(intr_status, ocb->ioaddr + REG_IRQ_STATUS);
	return IRQ_HANDLED;
}

static void snsocb_reset(struct ocb *ocb)
{
	void __iomem *ioaddr = ocb->ioaddr;

	/* Kick out anybody blocked in read() or trying to send data */
	mutex_lock(&ocb->tx_lock);
	ocb->reset_in_progress = true;
	wake_up_all(&ocb->tx_wq);
	mutex_unlock(&ocb->tx_lock);

	spin_lock_irq(&ocb->lock);
	ocb->reset_occurred = true;
	wake_up_all(&ocb->rx_wq);
	spin_unlock_irq(&ocb->lock);

	/* XXX should wait for everyone to leave */

	/* The GE cards don't like being reset while an interrupt is being
	 * processed; they go into a screaming match.
	 */
	if (ioread32(ioaddr + REG_IRQ_ENABLE)) {
		iowrite32(0, ioaddr + REG_IRQ_ENABLE);
		synchronize_irq(ocb->pdev->irq);

		/* Make sure we aren't still accessing the card for DQ
		 * emulation.
		 */
		tasklet_kill(&ocb->rxtask);
	}

	/* Disable the DMA first and give it some time to settle down.
	 * PCIe cards are not happy being reset while DMA is in progress.
	 * Especially with high throughputs the likelyhood of hitting it
	 * just right is high.
	 */
	iowrite32(0, ioaddr + REG_CONFIG);
	msleep(1); // no busy waiting here

	if (ocb->board->reset_errcnt)
		iowrite32(OCB_CONF_RESET | OCB_CONF_ERRORS_RESET, ioaddr + REG_CONFIG);
	else
		iowrite32(OCB_CONF_RESET, ioaddr + REG_CONFIG);

	/* Post our writes; RESET will self-clear on the next PCI cycle. */
	ioread32(ioaddr + REG_CONFIG);

	if (ocb->use_optical && ocb->hwdq_page) {
		/* We're using a board/firmware that splits the optical
		 * RX path into three queues, so we need to point the
		 * hardware at a different DQ than the unified one we
		 * emulate to the user.
		 */
		ocb->emulate_dq = 1;

		iowrite32(ocb->hwimq_dma, ioaddr + REG_IMQ_ADDR);
		iowrite32(ocb->hwimq_dma >> 32, ioaddr + REG_IMQ_ADDRHI);

		iowrite32(ocb->hwcq_dma, ioaddr + REG_CQ_ADDR);
		iowrite32(ocb->hwcq_dma >> 32, ioaddr + REG_CQ_ADDRHI);
		iowrite32(OCB_CQ_SIZE - 1, ioaddr + REG_CQ_MAX_OFFSET);

		iowrite32(ocb->hwdq_dma, ioaddr + REG_DQ_ADDR);
		iowrite32(ocb->hwdq_dma >> 32, ioaddr + REG_DQ_ADDRHI);
		iowrite32(OCB_DQ_SIZE - 1, ioaddr + REG_DQ_MAX_OFFSET);
	} else {
		/* This board uses an unified DQ, or we're using the LVDS
		 * so directly map it onto the buffer the user maps.
		 */
		u64 addr = (ocb->dq_big_addr ? virt_to_bus(phys_to_virt(ocb->dq_big_addr)) : ocb->dq_dma);
		ocb->emulate_dq = 0;
		iowrite32(addr & 0xFFFFFFFF, ioaddr + REG_DQ_ADDR);
		iowrite32((addr >> 32) & 0xFFFFFFFF, ioaddr + REG_DQ_ADDRHI);
		iowrite32(ocb->dq_size - 1, ioaddr + REG_DQ_MAX_OFFSET);
	}

	/* Clear queue offset indexes */
	iowrite32(0x0, ioaddr + REG_TX_PROD_INDEX);
	if (ocb->emulate_dq)
		iowrite32(0x0, ioaddr + REG_RX_CONS_INDEX);
	else
		iowrite32(0x0, ioaddr + REG_DQ_CONS_INDEX);

	ocb->conf &= ~OCB_CONF_RX_ENABLE;
	iowrite32(ocb->conf, ocb->ioaddr + REG_CONFIG);
	if (ocb->emulate_dq) {
		ocb->dq_prod = ocb->dq_cons = 0;
		ocb->imq_cons = ocb->imq_prod = 0;
		ocb->hwdq_cons = ioread32(ocb->ioaddr + REG_DQ_CONS_INDEX);
		ocb->hwdq_prod = ioread32(ocb->ioaddr + REG_DQ_PROD_INDEX);
		ocb->hwimq_cons = ioread32(ocb->ioaddr + REG_IMQ_CONS_INDEX);
		ocb->hwcq_cons = ioread32(ocb->ioaddr + REG_CQ_CONS_INDEX);
	} else {
		ocb->dq_cons = ioread32(ocb->ioaddr + REG_DQ_CONS_INDEX);
		ocb->dq_prod = ioread32(ocb->ioaddr + REG_DQ_PROD_INDEX);
	}
	ocb->tx_prod = ioread32(ocb->ioaddr + REG_TX_PROD_INDEX);
	iowrite32(ocb->irqs, ocb->ioaddr + REG_IRQ_ENABLE);

	/* Give the optical module some time to bring up the TX laser, and
	 * lock on to any RX signal to prevent spurious reports of lost
	 * signal.
	 */
	if (ocb->use_optical)
		msleep(100);

	spin_lock_irq(&ocb->lock);
	ocb->reset_in_progress = false;
	ocb->stalled = false;
	spin_unlock_irq(&ocb->lock);
}

static int snsocb_alloc_queue(struct device *dev, struct page **page,
			      dma_addr_t *dma, unsigned long size)
{
	/* We allocate the data queue as individual pages, because we will
	 * map them into the user's address space.
	 */
	unsigned int order = get_order(size);
	int i;

	*page = alloc_pages(GFP_KERNEL, order);
	if (!*page)
		return -ENOMEM;

	*dma = dma_map_page(dev, *page, 0, size, DMA_FROM_DEVICE);
	if (dma_mapping_error(dev, *dma)) {
		__free_pages(*page, order);
		return -EIO;
	}

	/* We'll be mapping these pages into user space, so manually add a
	 * reference to the tail pages so they don't go away on unmap and
	 * corrupt the page lists.
	 */
        for (i = 1; i < (1 << order); i++)
                atomic_inc(&compound_head((*page) + i)->_count);

	/* Prevent information leaks to user-space */
	memset(page_address(*page), 0, PAGE_SIZE * (1 << order));

	return 0;
}

static void snsocb_free_queue(struct device *dev, struct page *page,
			      dma_addr_t dma, unsigned long size)
{
	/* We allocated the data queue as individual pages, because we
	 * will map them into the user's address space.
	 */
	unsigned int order = get_order(size);
	int i;

	if (!page)
		return;

	dma_unmap_page(dev, dma, size, DMA_FROM_DEVICE);

	/* Remove our inflated reference counts */
        for (i = 1; i < (1 << order); i++)
                atomic_dec(&compound_head((page) + i)->_count);

	__free_pages(page, order);
}

static void snsocb_alloc_big_queue(struct ocb *ocb, unsigned long phys_addr, unsigned long size)
{
	ocb->dq_size = size & PAGE_MASK;
	ocb->dq_big_addr = phys_addr;
}

static void snsocb_free_big_queue(struct ocb *ocb)
{
	if (ocb->dq_size > OCB_DQ_SIZE) {
		ocb->dq_big_addr = 0;
		// Revert to the kmalloc-ed page memory
		ocb->dq_size = OCB_DQ_SIZE;
	}
}

static int snsocb_vm_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct file_ctx *file_ctx = vma->vm_private_data;
	struct ocb *ocb = file_ctx->ocb;
	unsigned long offset;
	struct page *page;

	if (!ocb)
		return VM_FAULT_SIGBUS;

	page = ocb->dq_page;
	offset = (unsigned long) vmf->virtual_address - vma->vm_start;
	if (offset >= ocb->dq_size)
		return VM_FAULT_SIGBUS;

	offset >>= PAGE_SHIFT;
	page += offset;
	get_page(page);
	vmf->page = page;

	return 0;
}

static const struct vm_operations_struct snsocb_vm_ops = {
	.fault = snsocb_vm_fault,
};

static int snsocb_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct file_ctx *file_ctx = file->private_data;
	struct ocb *ocb = file_ctx->ocb;
	unsigned long size = vma->vm_end - vma->vm_start;
	unsigned long pfn;

	switch (vma->vm_pgoff) {
	case OCB_MMAP_BAR0:
	case OCB_MMAP_BAR1:
	case OCB_MMAP_BAR2:
		if (ocb->board->bars[vma->vm_pgoff] == 0)
			return -ENOSYS;
		if (size != ocb->board->bars[vma->vm_pgoff])
			return -EINVAL;
		pfn = pci_resource_start(ocb->pdev, vma->vm_pgoff) >> PAGE_SHIFT;
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
		vma->vm_flags |= VM_IO;
		break;
	case OCB_MMAP_RX_DMA:
		if (size != ocb->dq_size)
			return -EINVAL;
		if (ocb->dq_big_addr)
			pfn = virt_to_phys(bus_to_virt(ocb->dq_big_addr)) >> PAGE_SHIFT;
		else
			pfn = page_to_pfn(ocb->dq_page);
		vma->vm_flags |= VM_IO | VM_DONTEXPAND;
		break;
	default:
		return -EINVAL;
	}

	return remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot);
}

static unsigned int snsocb_poll(struct file *file,
				struct poll_table_struct *wait)
{
	struct file_ctx *file_ctx = file->private_data;
	struct ocb *ocb = file_ctx->ocb;
	unsigned int mask = 0;
	unsigned long flags;

	/* Debug connection doesn't support poll */
	if (file_ctx->debug_mode)
		return -EINVAL;

	poll_wait(file, &ocb->rx_wq, wait);
	poll_wait(file, &ocb->tx_wq, wait);

	mutex_lock(&ocb->tx_lock);
	if (!(ioread32(ocb->ioaddr + REG_CONFIG) & OCB_CONF_TX_ENABLE))
		mask |= POLLOUT | POLLWRNORM;
	mutex_unlock(&ocb->tx_lock);

	spin_lock_irqsave(&ocb->lock, flags);
	if (ocb->dq_prod != ocb->dq_cons)
		mask |= POLLIN | POLLRDNORM;
 	if (ocb->reset_occurred || ocb->reset_in_progress)
		mask |= POLLERR;
	if (ocb->stalled)
		mask |= POLLHUP;
	spin_unlock_irqrestore(&ocb->lock, flags);

	return mask;
}

static ssize_t snsocb_read(struct file *file, char __user *buf,
			   size_t count, loff_t *pos)
{
	struct file_ctx *file_ctx = file->private_data;
	struct ocb *ocb = file_ctx->ocb;
	struct ocb_status info;
	ssize_t ret = 0;

	/* Debug connection is limited to reset only when in read-write mode */
	if (file_ctx->debug_mode && *pos != OCB_CMD_GET_STATUS)
		return -EPERM;

	switch (*pos) {
	case OCB_CMD_GET_STATUS:
		if (count != sizeof(struct ocb_status))
			return -EINVAL;

		spin_lock_irq(&ocb->lock);
		if (ocb->reset_in_progress) {
			ret = -ECONNRESET;
		} else {
			info.ocb_ver = OCB_VER;
			info.board_type = ocb->board->type;
			info.hardware_ver = (ocb->board->type == BOARD_SNS_PCIE ? ((ocb->version >> 16) & 0xFFFF) : 0);
			info.firmware_ver = (ocb->board->type == BOARD_SNS_PCIE ? (ocb->version & 0xFFFF) : ocb->version);
			info.firmware_date = ocb->firmware_date;
			info.fpga_serial = ocb->fpga_serial;
			info.status = __snsocb_status(ocb);
			info.dq_used = (ocb->dq_size + ocb->dq_prod - ocb->dq_cons) % ocb->dq_size;
			info.dq_size = ocb->dq_size;
			info.rx_rate = __snsocb_rxrate(ocb);
			info.bars[0] = ocb->board->bars[0];
			info.bars[1] = ocb->board->bars[1];
			info.bars[2] = ocb->board->bars[2];
			if (!ocb->reset_in_progress)
				ocb->reset_occurred = false;
		}
		spin_unlock_irq(&ocb->lock);

		if (ret != 0)
			return ret;

		if (copy_to_user(buf, &info, sizeof(info)))
			return -EFAULT;
		break;
	case OCB_CMD_RX:
		count = snsocb_rx(file, buf, count);
		break;
	default:
		return -EINVAL;
	}

	return count;
}

static ssize_t snsocb_write(struct file *file, const char __user *buf,
			    size_t count, loff_t *pos)
{
	struct file_ctx *file_ctx = file->private_data;
	struct ocb *ocb = file_ctx->ocb;
	u32 val, prod;
	ssize_t ret = 0;

	/* Debug connection is limited to reset only */
	if (file_ctx->debug_mode && *pos != OCB_CMD_RESET)
		return -EINVAL;

	switch (*pos) {
	case OCB_CMD_ADVANCE_DQ:
		if (count != sizeof(u32))
			return -EINVAL;

		if (copy_from_user(&val, buf, sizeof(u32)))
			return -EFAULT;

		/* We only deal with packets that are multiples of 4 bytes */
		val = ALIGN(val, 4);

		if (!val || val >= ocb->dq_size)
			return -EOVERFLOW;

		/* Validate that the new consumer index is within the range
		 * of valid data.
		 */
		spin_lock_irq(&ocb->lock);
		if (ocb->reset_in_progress) {
			ret = -ECONNRESET;
		} else {
			val += ocb->dq_cons;
			prod = ocb->dq_prod;
			if (ocb->dq_prod < ocb->dq_cons)
				prod += ocb->dq_size;
			if (val > ocb->dq_cons && val <= prod) {
				ocb->dq_cons = val % ocb->dq_size;
				if (!ocb->emulate_dq) {
					iowrite32(ocb->dq_cons,
						  ocb->ioaddr + REG_DQ_CONS_INDEX);
				}
			} else {
				ret = -EOVERFLOW;
			}
		}
		spin_unlock_irq(&ocb->lock);

		if (ret != 0)
			return ret;
		break;
	case OCB_CMD_RESET:
		if (count != sizeof(u32))
			return -EINVAL;

		if (copy_from_user(&val, buf, sizeof(u32)))
			return -EFAULT;

		if (val > OCB_SELECT_OPTICAL)
			return -EINVAL;

		spin_lock_irq(&ocb->lock);
		if (ocb->reset_in_progress) {
			ret = -ECONNRESET;
		} else {
			ocb->use_optical = !!val;
			if (ocb->use_optical) {
				ocb->conf |= OCB_CONF_SELECT_OPTICAL;
				ocb->conf |= OCB_CONF_OPTICAL_ENABLE;
			} else {
				ocb->conf &= ~OCB_CONF_SELECT_OPTICAL;
				ocb->conf &= ~OCB_CONF_OPTICAL_ENABLE;
			}
		}
		spin_unlock_irq(&ocb->lock);

		if (ret != 0)
			return ret;

		snsocb_reset(ocb);
		break;
	case OCB_CMD_TX:
		count = snsocb_tx(file, ocb, buf, count);
		break;
	case OCB_CMD_RX_ENABLE:
		if (count != sizeof(u32))
			return -EINVAL;

		if (copy_from_user(&val, buf, sizeof(u32)))
			return -EFAULT;

		spin_lock_irq(&ocb->lock);
		if (ocb->reset_in_progress) {
			ret = -ECONNRESET;
		} else {
			if (val)
				ocb->conf |= OCB_CONF_RX_ENABLE;
			else
				ocb->conf &= ~OCB_CONF_RX_ENABLE;
			val = ocb->conf;
		}
		spin_unlock_irq(&ocb->lock);

		if (ret != 0)
			return ret;

		iowrite32(val, ocb->ioaddr + REG_CONFIG);
		ioread32(ocb->ioaddr + REG_CONFIG); // post write

		break;
	case OCB_CMD_ERR_PKTS_ENABLE:
		if (count != sizeof(u32))
			return -EINVAL;

		if (copy_from_user(&val, buf, sizeof(u32)))
			return -EFAULT;

		spin_lock_irq(&ocb->lock);
		if (ocb->reset_in_progress) {
			ret = -ECONNRESET;
		} else {
			if (val)
				ocb->conf |= OCB_CONF_ERR_PKTS_ENABLE;
			else
				ocb->conf &= ~OCB_CONF_ERR_PKTS_ENABLE;
			val = ocb->conf;
		}
		spin_unlock_irq(&ocb->lock);

		if (ret != 0)
			return ret;

		iowrite32(val, ocb->ioaddr + REG_CONFIG);
		ioread32(ocb->ioaddr + REG_CONFIG); // post write

		break;
	default:
		return -EINVAL;
	}

	return count;
}

static int snsocb_open(struct inode *inode, struct file *file)
{
	struct ocb *ocb = container_of(inode->i_cdev, struct ocb, cdev);
	int err = 0;
	struct file_ctx *file_ctx;

	file->private_data = NULL;

	file_ctx = kmalloc(sizeof(struct file_ctx), GFP_KERNEL);
	if (!file_ctx)
		return -ENOMEM;

	memset(file_ctx, 0, sizeof(struct file_ctx));
	file_ctx->ocb = ocb;
	file_ctx->debug_mode = ((file->f_flags & O_EXCL) ? false : true);

	file->private_data = file_ctx;

	/* Debug connection is limited but not exclusive */
	if (file_ctx->debug_mode)
		return 0;

	/* We only allow one process at a time to have us open. */
	spin_lock_irq(&ocb->lock);
	if (ocb->in_use)
		err = -EBUSY;
	else
		ocb->in_use = true;
	spin_unlock_irq(&ocb->lock);

	if (err)
		return err;

	ocb->conf = 0;
	if (ocb->use_optical) {
		ocb->conf |= OCB_CONF_SELECT_OPTICAL;
		ocb->conf |= OCB_CONF_OPTICAL_ENABLE;
	}
	ocb->irqs = OCB_IRQ_ENABLE | OCB_IRQ_RX_DONE | OCB_IRQ_DMA_STALL | OCB_IRQ_FIFO_OVERFLOW;
	snsocb_reset(ocb);

	return err;
}

static int snsocb_release(struct inode *inode, struct file *file)
{
	struct file_ctx *file_ctx = file->private_data;

	if (file_ctx) {
		if (!file_ctx->debug_mode) {
			struct ocb *ocb = file_ctx->ocb;
			void __iomem *ioaddr = ocb->ioaddr;

			/* Disable DMA only, no need to send more data since noone is listening */
			iowrite32(0, ioaddr + REG_CONFIG);

			spin_lock_irq(&ocb->lock);
			ocb->in_use = false;
			spin_unlock_irq(&ocb->lock);
		}

		file_ctx->ocb = NULL;
		kfree(file_ctx);
	}

	return 0;
}

static void snsocb_free(struct device *dev)
{
	struct ocb *ocb = container_of(dev, struct ocb, dev);

	kfree(ocb);
}

static ssize_t snsocb_sysfs_show_irq_coallesce(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ocb *ocb = dev_get_drvdata(dev);
	u32 val = ioread32(ocb->ioaddr + REG_IRQ_CNTL) & 0xFFFF;
	return scnprintf(buf, PAGE_SIZE, "%u\n", val);
}

static ssize_t snsocb_sysfs_store_irq_coallesce(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct ocb *ocb = dev_get_drvdata(dev);
	u32 val;
	if (sscanf(buf, "%u", &val) == 1) {
		val &= 0xFFFF;
		if (val > 0) val |= OCB_COALESCING_ENABLE;
		iowrite32(val, ocb->ioaddr + REG_IRQ_CNTL);
		return count;
	}
	return -EINVAL;
}

static ssize_t snsocb_sysfs_show_dma_big_mem(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ocb *ocb = dev_get_drvdata(dev);
	int ret = 0;

	spin_lock_irq(&ocb->lock);
	ret = scnprintf(buf, PAGE_SIZE, "%s\n", ocb->dq_big_cnf);
	spin_unlock_irq(&ocb->lock);

	return ret;
}

static ssize_t snsocb_sysfs_store_dma_big_mem(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct ocb *ocb = dev_get_drvdata(dev);
	int err = 0;
	unsigned long size, offset;
	char size_mod, offset_mod;

	if (sscanf(buf, "%lu%c$%lu%c", &size, &size_mod, &offset, &offset_mod) < 3)
		return -EINVAL;
	// Verify size parameter
	if (size_mod != 'M')
		return -EINVAL;
	size *= 1024*1024; // size_mod == 'M'
	if (size & (size - 1)) // must be power of two
		return -EFAULT;
	if (size <= OCB_DQ_SIZE)
		return -ENOMEM;
	// Verify offset parameter
	if (offset_mod != 'M' && offset_mod != 'G') {
		if (offset_mod != 0)
			return -EINVAL;
		if (offset != (offset & PAGE_MASK))
			return -EFAULT;
	}
        if (offset_mod == 'M')
                offset *= 1024*1024;
        else if (offset_mod == 'G')
                offset *= 1024*1024*1024;
	// size and offset are now page aligned

	spin_lock_irq(&ocb->lock);
	if (ocb->in_use) {
		err = -EBUSY;
	} else {
		ocb->in_use = true;
	}
	spin_unlock_irq(&ocb->lock);

	if (err)
		return err;

	snsocb_free_big_queue(ocb);
	snsocb_alloc_big_queue(ocb, offset, size);

	spin_lock_irq(&ocb->lock);
	strncpy(ocb->dq_big_cnf, buf, sizeof(ocb->dq_big_cnf));
	ocb->in_use = false;
	spin_unlock_irq(&ocb->lock);

	return count;
}

static struct file_operations snsocb_fops = {
	.owner	 = THIS_MODULE,
	.open	 = snsocb_open,
	.release = snsocb_release,
	.mmap	 = snsocb_mmap,
	.poll	 = snsocb_poll,
	.write	 = snsocb_write,
	.read	 = snsocb_read,
};

static int __devexit snsocb_probe(struct pci_dev *pdev,
				  const struct pci_device_id *ent)
{
	int board_id = ent->driver_data;
	struct device *dev = &pdev->dev;
	struct ocb *ocb = NULL;
	int minor, err;

	err = pcim_enable_device(pdev);
	if (err) {
		dev_err(dev, "Unable to enable device, aborting");
		goto error;
	}

	/* Sanity check the BARs for this device */
	err = -ENODEV;
	if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM)) {
		dev_err(dev, "region #0 is not a PCI MMIO region, aborting");
		goto error;
	}

	if (pci_resource_len(pdev, 0) < 0x400) {
		dev_err(dev, "region #0 has invalid size, aborting");
		goto error;
	}

	if (!(pci_resource_flags(pdev, 1) & IORESOURCE_MEM)) {
		dev_err(dev, "region #1 is not a PCI MMIO region, aborting");
		goto error;
	}

	if (pci_resource_len(pdev, 1) < 8192) {
		dev_err(dev, "region #1 has invalid size, aborting");
		goto error;
	}

	err = pcim_iomap_regions(pdev,
				 (1 << OCB_MMIO_BAR) | (1 << OCB_TXFIFO_BAR),
				 KBUILD_MODNAME);
	if (err) {
		dev_err(dev, "unable to request and map MMIO, aborting");
		goto error;
	}

	err = -ENOMEM;
	ocb = kzalloc(sizeof(*ocb), GFP_KERNEL);
	if (!ocb) {
		dev_err(dev, "Unable to allocate private memory, aborting");
		goto error;
	}

	err = -ENODEV;
	mutex_lock(&snsocb_devlock);
	for (minor = 0; minor < OCB_MAX_DEVS; minor++) {
		if (!snsocb_devs[minor]) {
			snsocb_devs[minor] = ocb;
			break;
		}
	}
	mutex_unlock(&snsocb_devlock);

	if (minor == OCB_MAX_DEVS) {
		/* We've not associated it with a device yet, so clean up. */
		kfree(ocb);
		dev_err(dev, "too many OCB cards in system, aborting");
		goto error;
	}

	cdev_init(&ocb->cdev, &snsocb_fops);
	spin_lock_init(&ocb->lock);
	mutex_init(&ocb->tx_lock);
	init_waitqueue_head(&ocb->tx_wq);
	init_waitqueue_head(&ocb->rx_wq);
	tasklet_init(&ocb->rxtask, snsocb_rxtask, (unsigned long) ocb);
	ocb->cdev.owner = THIS_MODULE;
	ocb->pdev = pdev;
	ocb->minor = minor;
	strncpy(ocb->dq_big_cnf, "0$0", sizeof(ocb->dq_big_cnf));

	dev_set_name(&ocb->dev, "snsocb%d", minor);
	ocb->dev.devt = snsocb_basedev + minor;
	ocb->dev.class = snsocb_class;
	ocb->dev.parent = dev;
	ocb->dev.release = snsocb_free;
	device_initialize(&ocb->dev);

	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if (err) {
		err = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (err) {
			dev_err(dev, "no usable DMA config, aborting");
			goto error_dev;
		}

		dev_info(dev, "using 32-bit DMA mask");
	}

	ocb->ioaddr = pcim_iomap_table(pdev)[OCB_MMIO_BAR];
	ocb->txfifo = pcim_iomap_table(pdev)[OCB_TXFIFO_BAR];

	/* Verify that we support the firmware loaded on the card.
	 * Code is 0xVVYYMMDD -- version, year, month, day (BCD)
	 */
	err = -ENODEV;
	ocb->version          = ioread32(ocb->ioaddr + REG_VERSION);
	ocb->firmware_date    = ioread32(ocb->ioaddr + REG_FIRMWARE_DATE);
	ocb->fpga_serial      = ioread32(ocb->ioaddr + REG_SERNO_LO);
	ocb->fpga_serial     |= (u64)ioread32(ocb->ioaddr + REG_SERNO_HI) << 32;
	ocb->board = &boards[0];
	while (ocb->board && ocb->board->type != 0) {
		if (ocb->board->type == board_id) {
			u32 *version = ocb->board->version;
			while (*version != 0) {
				if (*version == ocb->version)
					break;
				version++;
			}
			if (*version == ocb->version)
				break;
		}
		++ocb->board;
	}
	if (!ocb->board || ocb->board->type == 0) {
		dev_err(dev, "unsupported %s firmware 0x%08x\n", snsocb_name[board_id], ocb->version);
		err = -ENODEV;
		goto error_dev;
	}

	if (ocb->board->sysfs.attrs) {
		err = sysfs_create_group(&dev->kobj, &ocb->board->sysfs);
		if (err) {
			dev_err(dev, "unable to create sysfs entries");
			goto error_dev;
		}
        }

	err = devm_request_irq(dev, pdev->irq, snsocb_interrupt, IRQF_SHARED,
			       KBUILD_MODNAME, ocb);
	if (err) {
		dev_err(dev, "unable to request interrupt, aborting");
		goto error_dev;
	}

	// Start with small DMA buffer, change through sysfs later
	ocb->dq_size = OCB_DQ_SIZE;
	if (snsocb_alloc_queue(dev, &ocb->dq_page, &ocb->dq_dma, OCB_DQ_SIZE)) {
		dev_err(dev, "unable to allocate data queue, aborting");
		goto error_dev;
	}

	if (!ocb->board->unified_que) {
		/* Some of these could be done by dev_alloc_coherent(), but
		 * this works just as well and minimizes the different
		 * concepts in the driver.
		 */
		if (snsocb_alloc_queue(dev, &ocb->hwimq_page, &ocb->hwimq_dma,
							OCB_IMQ_SIZE)) {
			dev_err(dev, "unable to allocate msg queue, aborting");
			goto error_dq;
		}
		if (snsocb_alloc_queue(dev, &ocb->hwcq_page, &ocb->hwcq_dma,
							OCB_CQ_SIZE)) {
			dev_err(dev,
				"unable to allocate command queue, aborting");
			goto error_dq;
		}
		if (snsocb_alloc_queue(dev, &ocb->hwdq_page, &ocb->hwdq_dma,
							OCB_DQ_SIZE)) {
			dev_err(dev,
				"unable to allocate hw data queue, aborting");
			goto error_dq;
		}
		ocb->imq = kmalloc(SW_IMQ_RING_SIZE * sizeof(*ocb->imq),
				   GFP_KERNEL);
		if (!ocb->imq) {
			dev_err(dev,
				"unable to allocate sw imq queue, aborting");
			goto error_dq;
		}
	}

	ocb->tx_buffer = kmalloc(ocb->board->tx_fifo_len, GFP_KERNEL);
	if (!ocb->tx_buffer) {
		dev_err(dev, "unable to allocate TX buffer, aborting");
		goto error_dq;
	}

	ocb->conf = OCB_CONF_RESET;
	ocb->irqs = 0;
	snsocb_reset(ocb);

	/* Now that we've reset the card, we can enable bus mastering and
	 * be fairly confident it won't scribble over random memory.
	 */
	pci_set_master(pdev);
	pci_set_drvdata(pdev, ocb);

	err = cdev_add(&ocb->cdev, ocb->dev.devt, 1);
	if (err) {
		dev_err(dev, "unable to register device, aborting");
		goto error_dq;
	}

	err = device_add(&ocb->dev);
	if (err) {
		dev_err(dev, "unable to add device, aborting");
		goto error_cdev;
	}

	dev_set_drvdata(dev, ocb);

	dev_info(dev, "snsocb%d: %s OCB version %08x, datecode %08x\n",
		 minor, snsocb_name[board_id],
		 ocb->version,
		 ocb->firmware_date);

	return 0;

error_cdev:
	cdev_del(&ocb->cdev);
error_dq:
	kfree(ocb->tx_buffer);
	kfree(ocb->imq);
	snsocb_free_queue(dev, ocb->dq_page, ocb->dq_dma, OCB_DQ_SIZE);
	snsocb_free_queue(dev, ocb->hwcq_page, ocb->hwcq_dma, OCB_CQ_SIZE);
	snsocb_free_queue(dev, ocb->hwimq_page, ocb->hwimq_dma, OCB_IMQ_SIZE);
	snsocb_free_queue(dev, ocb->hwdq_page, ocb->hwdq_dma, OCB_DQ_SIZE);
error_dev:
	if (ocb && ocb->board && ocb->board->sysfs.attrs)
		sysfs_remove_group(&dev->kobj, &ocb->board->sysfs);
	put_device(&ocb->dev);
	mutex_lock(&snsocb_devlock);
	snsocb_devs[minor] = NULL;
	mutex_unlock(&snsocb_devlock);
error:
	return err;
}

static void __devexit snsocb_remove(struct pci_dev *pdev)
{
	/* Very little to do here, since most resources are managed for us. */
	struct ocb *ocb = pci_get_drvdata(pdev);
	struct device *dev = &pdev->dev;

	if (ocb && ocb->board && ocb->board->sysfs.attrs)
		sysfs_remove_group(&dev->kobj, &ocb->board->sysfs);

	device_del(&ocb->dev);
	cdev_del(&ocb->cdev);

	iowrite32(0, ocb->ioaddr + REG_IRQ_ENABLE);
	iowrite32(OCB_CONF_RESET, ocb->ioaddr + REG_CONFIG);

	snsocb_free_big_queue(ocb);
	snsocb_free_queue(dev, ocb->dq_page, ocb->dq_dma, OCB_DQ_SIZE);
	snsocb_free_queue(dev, ocb->hwcq_page, ocb->hwcq_dma, OCB_CQ_SIZE);
	snsocb_free_queue(dev, ocb->hwimq_page, ocb->hwimq_dma, OCB_IMQ_SIZE);
	snsocb_free_queue(dev, ocb->hwdq_page, ocb->hwdq_dma, OCB_DQ_SIZE);
	kfree(ocb->tx_buffer);
	kfree(ocb->imq);

	mutex_lock(&snsocb_devlock);
	snsocb_devs[ocb->minor] = NULL;
	mutex_unlock(&snsocb_devlock);

	put_device(&ocb->dev);
}

DEFINE_PCI_DEVICE_TABLE(snsocb_pci_table) = {
	{ PCI_VDEVICE(XILINX, 0x1002), .driver_data = BOARD_SNS_PCIX },
	{ PCI_DEVICE(0x1775, 0x1000), .driver_data = BOARD_GE_PCIE },
	{ PCI_VDEVICE(XILINX, 0x7014), .driver_data = BOARD_SNS_PCIE },
	{ 0, }
};

static struct pci_driver __refdata snsocb_driver = {
	.name = "snsocb",
	.id_table = snsocb_pci_table,
	.probe = snsocb_probe,
	.remove = snsocb_remove,
};

static int __init snsocb_init(void)
{
	int err;

	snsocb_class = class_create(THIS_MODULE, "snsocb");
	err = PTR_ERR(snsocb_class);
	if (IS_ERR(snsocb_class))
		goto error;

	err = alloc_chrdev_region(&snsocb_basedev, 0, OCB_MAX_DEVS, "snsocb");
	if (err)
		goto error_class;

	err = pci_register_driver(&snsocb_driver);
	if (err)
		goto error_chrdev;

	return 0;

error_chrdev:
	unregister_chrdev_region(snsocb_basedev, OCB_MAX_DEVS);
error_class:
	class_destroy(snsocb_class);
error:
	return err;
}

static void __exit snsocb_exit(void)
{
	pci_unregister_driver(&snsocb_driver);
	unregister_chrdev_region(snsocb_basedev, OCB_MAX_DEVS);
	class_destroy(snsocb_class);
}

module_init(snsocb_init);
module_exit(snsocb_exit);

MODULE_AUTHOR("David Dillow <dillowda@ornl.gov>");
MODULE_VERSION("0.01");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SNS Optical Communication Board for Neutron Detectors");
