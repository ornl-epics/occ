/*
 * Copyright (c) 2018 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 *
 * sns-occ -- a Linux driver for the SNS PCI-X, PCIe, and GE OCC cards
 *
 * Originally by David Dillow, December 2013
 * Maintained by Klemen Vodopivec <vodopiveck@ornl.gov>
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
#include <linux/ratelimit.h>
#include <linux/poll.h>
#include <linux/delay.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,11,0)
#include <linux/sched/signal.h>
#else
#include <linux/sched.h>
#endif

#include "sns-occ.h"

#define OCC_VER_STR __stringify(OCC_VER_MAJ) "." __stringify(OCC_VER_MIN) "." __stringify(OCC_VER_BUILD)

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
#define SNSOCC_DEVICE_ATTR(_name, _mode, _show, _store)			\
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
#define OCC_MAX_DEVS		8

/* How much room do we allocate for the ring buffer -- it is difficult to
 * get contiguous memory once the machine has been up for a while, but the
 * expected use case for this driver is to be loaded at boot. We can handle
 * up to 4 MB, but we'll start with 2 MB for now.
 */
#define OCC_DQ_SIZE			(2 * 1024 * 1024)

/* For cards that split the Optical data into three queues -- we have
 * an inbound message queue of 64 entries, each 32 bytes and a command
 * queue, which we'll ask for 64 KB.
 */
#define OCC_IMQ_ENTRIES		64
#define OCC_IMQ_SIZE		(OCC_IMQ_ENTRIES * 8 * sizeof(u32))
#define OCC_CQ_SIZE		(64 * 1024)

#define OCC_MMIO_BAR		0
#define OCC_TXFIFO_BAR		1
#define OCC_DDR_BAR		2

#define REG_VERSION		0x0000

/* How many interrupt latency records to keep. The size affects allocation
 * of 2 u32 arrays which are printed through sysfs interface. Since sysfs
 * limits text size, not all entries may be displayed. Depending
 * on the PAGE_SIZE value and the actual delay numbers lengths as string,
 * about 200-500 entries will be printed.
 */
#define OCC_IRQ_LAT_BUF_SIZE	500

/* Old versions of the firmware had more configuration options, but
 * these are all that are used in the version we support for this driver.
 */
#define REG_CONFIG					0x0004
#define		OCC_CONF_TX_ENABLE			0x00000001
#define		OCC_CONF_RX_ENABLE			0x00000002
#define		OCC_CONF_SELECT_OPTICAL		0x00000008
#define		OCC_CONF_OPTICAL_ENABLE		0x00000010
#define		OCC_CONF_ERR_PKTS_ENABLE	0x00040000 // Turn detected errors into error packets
#define		OCC_CONF_OLD_PKTS_DISABLE	0x00080000 // Turn off support for old SNS DAS packets, added in firmware 12-18-2017
#define		OCC_CONF_ERRORS_RESET		0x04000000 // Clear detected error counters
#define		OCC_CONF_RESET				0x80000000
#define REG_STATUS						0x0008
#define		OCC_STATUS_TX_DONE			0x00000001
#define		OCC_STATUS_RX_LVDS			0x00000002
#define		OCC_STATUS_BUFFER_FULL		0x00000004
#define		OCC_STATUS_OPTICAL_PRESENT	0x00000008
#define		OCC_STATUS_OPTICAL_NOSIGNAL	0x00000010
#define		OCC_STATUS_OPTICAL_FAULT	0x00000020
#define		OCC_STATUS_TX_IDLE			0x00000040
#define		OCC_STATUS_RX_OPTICAL		0x00000100
#define REG_MODULE_MASK					0x0010
#define REG_MODULE_ID					0x0014
#define REG_SERNO_LO 					0x0018
#define REG_SERNO_HI 					0x001C
#define REG_IRQ_STATUS					0x00c0
#define		OCC_IRQ_DMA_STALL			0x00000002
#define		OCC_IRQ_FIFO_OVERFLOW		0x00000004
#define		OCC_IRQ_RX_DONE				0x00000010
#define		OCC_IRQ_ENABLE				0x80000000
#define REG_IRQ_ENABLE					0x00c4
#define REG_IRQ_CNTL					0x00c8
#define		OCC_COALESCING_ENABLE		0x80000000
#define		OCC_EOP_ENABLE				0x40000000
#define REG_FIRMWARE_DATE				0x0100
#define REG_ERROR_CRC_COUNTER			0x0180 // CRC errors counter (PCIe only)
#define REG_ERROR_LENGTH_COUNTER		0x0184 // Frame length errors counter (PCIe only)
#define REG_ERROR_FRAME_COUNTER			0x0188 // Frame errors counter (PCIe only)
#define REG_RX_RATE     				0x018C     // Receive rate calculated every second
#define REG_COMM_ERR					0x0240
#define REG_LOST_PACKETS				0x0244		/* 16 bit register */
#define REG_FPGA_TEMP					0x0310 // FPGA temperature, PCIe specific
#define REG_FPGA_CORE_VOLT				0x0314 // FPGA core voltage, PCIe specific
#define REG_FPGA_AUX_VOLT				0x0318 // FPGA aux voltage, PCIe specific

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

/* Timer counter registers */
#define REG_TIME_COUNTER	0x0cc
#define REG_TIME_IRQ_DQ		0x504
#define REG_TIME_IRQ_DMA_STALL	0x508
#define REG_TIME_IRQ_FIF_OVRFLW	0x50c

/* For the split RX handling, we need to copy the IMQ out of the queue
 * quickly, as there are only 63 hardware entries available. We'll also
 * need to check the packet type to find the rest of the data.
 */
#define SW_IMQ_RING_SIZE	4096
#define IMQ_TYPE_COMMAND	0x80000000

/* Interrupt types supported
 * Matches PCI_IRQ_(LEGACY|MSI|MSIX) macros in kernel 4.8.
 */
#define OCC_IRQ_LEGACY		(1 << 0) /* allow legacy interrupts */
#define OCC_IRQ_MSI		(1 << 1) /* allow MSI interrupts */
#define OCC_IRQ_MSIX		(1 << 2) /* allow MSI-X interrupts */

/* Forward declaration of functions used in the structs */
static ssize_t snsocc_sysfs_show_irq_coallesce(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t snsocc_sysfs_store_irq_coallesce(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
static ssize_t snsocc_sysfs_show_dma_big_mem(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t snsocc_sysfs_store_dma_big_mem(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
static ssize_t snsocc_sysfs_show_serial_number(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t snsocc_sysfs_show_firmware_date(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t snsocc_sysfs_show_irq_latency(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t snsocc_sysfs_store_irq_latency(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);

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
struct occ_board_desc {
	u32 type;
	u32 *version;
	u32 tx_fifo_len;
	u32 unified_que;
	u32 reset_errcnt;	// Does board have support for resetting error counters?
	u8 late_rx_enable;  // Does board support late RX enable?
	u8 interrupts;      // Supported interrupt types, see OCC_IRQ_*
	struct attribute_group sysfs;
};

struct irq_latency {
	u32 *isr_delay;
	u32 *isr_proctime;
	u32 size;
	u32 end;
	u32 abs_min;
	u32 abs_max;
	u8 capture;
};

struct occ {
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
	int stalled; // valid values are 0, OCC_DMA_STALLED and OCC_FIFO_OVERFLOW

	u32 version;
	u32 firmware_date;
	u64 fpga_serial;
	resource_size_t bars[3];

	wait_queue_head_t tx_wq;
	wait_queue_head_t rx_wq;
	struct mutex tx_lock;
	void *tx_buffer;
	u32 tx_prod;
	struct irq_latency irq_latency;

	struct tasklet_struct rxtask;
	struct device dev;
	struct cdev cdev;

	bool in_use;
	bool use_optical;
	int minor;
	unsigned int msi_enabled;

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
	struct occ_board_desc *board;
};

/**
 * Context for single file open operations, from when the file is opened
 * until it's closed.
 */
struct file_ctx {
	struct occ *occ;
	bool debug_mode;
};

static const char *snsocc_name[] = {
	[BOARD_SNS_PCIX] = "SNS PCI-X",
	[BOARD_SNS_PCIE] = "SNS PCIe",
	[BOARD_GE_PCIE] = "GE PCIe",
};

static struct occ_board_desc boards[] = {
	{
		.type = BOARD_SNS_PCIX,
		.version = (u32 []){ 0x31121106, 0x31130603, 0 },
		.tx_fifo_len = 8192,
		.unified_que = 1,
		.reset_errcnt = 0,
		.late_rx_enable = 0,
		.interrupts = OCC_IRQ_LEGACY,
	},
	{
		.type = BOARD_SNS_PCIX,
		.version = (u32 []){ 0x22100817, 0 },
		.tx_fifo_len = 8192,
		.unified_que = 0,
		.reset_errcnt = 0,
		.late_rx_enable = 0,
		.interrupts = OCC_IRQ_LEGACY,
	},
	{
		.type = BOARD_SNS_PCIE,
		.version = (u32 []){ 0x000a0001, 0 },
		.tx_fifo_len = 32768,
		.unified_que = 1,
		.reset_errcnt = 1,
		.late_rx_enable = 1,
		.interrupts = OCC_IRQ_LEGACY,
	},
	{
		.type = BOARD_SNS_PCIE,
		.version = (u32 []){ 0x000b0001, 0 },
		.tx_fifo_len = 32768,
		.unified_que = 1,
		.reset_errcnt = 1,
		.late_rx_enable = 1,
		.interrupts = OCC_IRQ_LEGACY,
		.sysfs.attrs = (struct attribute **) (struct device_attribute *[]){
			SNSOCC_DEVICE_ATTR("irq_coalescing", 0644, snsocc_sysfs_show_irq_coallesce, snsocc_sysfs_store_irq_coallesce),
			SNSOCC_DEVICE_ATTR("dma_big_mem", 0644, snsocc_sysfs_show_dma_big_mem, snsocc_sysfs_store_dma_big_mem),
			SNSOCC_DEVICE_ATTR("serial_number", 0444, snsocc_sysfs_show_serial_number, NULL),
			SNSOCC_DEVICE_ATTR("firmware_date", 0444, snsocc_sysfs_show_firmware_date, NULL),
			NULL,
		},
	},
	{
		.type = BOARD_SNS_PCIE,
		.version = (u32 []){ 0x000b0002, 0 },
		.tx_fifo_len = 32768,
		.unified_que = 1,
		.reset_errcnt = 1,
		.late_rx_enable = 1,
		.interrupts = OCC_IRQ_LEGACY | OCC_IRQ_MSI,
		.sysfs.attrs = (struct attribute **) (struct device_attribute *[]){
			SNSOCC_DEVICE_ATTR("irq_coalescing", 0644, snsocc_sysfs_show_irq_coallesce, snsocc_sysfs_store_irq_coallesce),
			SNSOCC_DEVICE_ATTR("dma_big_mem", 0644, snsocc_sysfs_show_dma_big_mem, snsocc_sysfs_store_dma_big_mem),
			SNSOCC_DEVICE_ATTR("serial_number", 0444, snsocc_sysfs_show_serial_number, NULL),
			SNSOCC_DEVICE_ATTR("firmware_date", 0444, snsocc_sysfs_show_firmware_date, NULL),
			SNSOCC_DEVICE_ATTR("irq_latency", 0644, snsocc_sysfs_show_irq_latency, snsocc_sysfs_store_irq_latency),
			NULL,
		},
	},
	{ 0 }
};

static DEFINE_MUTEX(snsocc_devlock);
static struct class *snsocc_class;
static dev_t snsocc_basedev;
static struct occ *snsocc_devs[OCC_MAX_DEVS];

static void __snsocc_stalled(struct occ *occ, int type)
{
	/* We've stalled for some reason; disable RX, as it appears the GE
	 * card gets unhappy and nuke interrupts on the machine.
	 *
	 * Caller must hold occ->lock.
	 */
	occ->stalled = type;
	// Must change occ->conf otherwise RX might get re-enabled automatically in TX thread
	if (occ->board->late_rx_enable)
		occ->conf &= ~OCC_CONF_RX_ENABLE;
	iowrite32(occ->conf, occ->ioaddr + REG_CONFIG);
	wake_up(&occ->rx_wq);
}

static void snsocc_stalled(struct occ *occ, int type)
{
	unsigned long flags;

	spin_lock_irqsave(&occ->lock, flags);
	__snsocc_stalled(occ, type);
	spin_unlock_irqrestore(&occ->lock, flags);
}

static u32 __snsocc_status(struct occ *occ)
{
	/* Caller must hold occ->lock */
	u32 hw_status, status = 0;

	status |= occ->stalled;
	if (occ->use_optical)
		status |= OCC_MODE_OPTICAL;
	if (occ->reset_occurred)
		status |= OCC_RESET_OCCURRED;
	if (occ->conf & OCC_CONF_RX_ENABLE)
		status |= OCC_RX_ENABLED;
	if (occ->conf & OCC_CONF_ERR_PKTS_ENABLE)
		status |= OCC_RX_ERR_PKTS_ENABLED;

	hw_status = ioread32(occ->ioaddr + REG_STATUS);
	if (hw_status & OCC_STATUS_OPTICAL_PRESENT) {
		status |= OCC_OPTICAL_PRESENT;
		if (hw_status & OCC_STATUS_OPTICAL_NOSIGNAL)
			status |= OCC_OPTICAL_NOSIGNAL;
		if (hw_status & OCC_STATUS_OPTICAL_FAULT)
			status |= OCC_OPTICAL_FAULT;
	}

	return status;
}

static u32 __snsocc_rxrate(struct occ *occ)
{
	if (occ->board->type == BOARD_SNS_PCIE)
		return ioread32(occ->ioaddr + REG_RX_RATE);
	return 0;
}

static void __snsocc_errcounters(struct occ *occ, u32 *err_crc, u32 *err_length, u32 *err_frame)
{
	if (occ->board->type == BOARD_SNS_PCIE) {
		*err_crc = ioread32(occ->ioaddr + REG_ERROR_CRC_COUNTER);
		*err_length = ioread32(occ->ioaddr + REG_ERROR_LENGTH_COUNTER);
		*err_frame = ioread32(occ->ioaddr + REG_ERROR_FRAME_COUNTER);
	} else {
		*err_crc = *err_length = *err_frame = 0;
	}
}

static void __snsocc_fpgainfo(struct occ *occ, u32 *fpga_temp, u32 *fpga_core_volt, u32 *fpga_aux_volt)
{
	if (occ->board->type == BOARD_SNS_PCIE) {
		*fpga_temp = ioread32(occ->ioaddr + REG_FPGA_TEMP);
		*fpga_core_volt = ioread32(occ->ioaddr + REG_FPGA_CORE_VOLT);
		*fpga_aux_volt = ioread32(occ->ioaddr + REG_FPGA_AUX_VOLT);
	} else {
		*fpga_temp = *fpga_core_volt = *fpga_aux_volt = 0;
	}
}

static u32 __snsocc_tx_room(struct occ *occ)
{
	/* Caller must hold occ->tx_lock
	 *
	 * We want to return bytes available, but the indexes are kept in
	 * double words, so we need to convert
	 */
	u32 prod = occ->tx_prod << 3;
	u32 cons = ioread32(occ->ioaddr + REG_TX_CONS_INDEX) << 3;
	u32 room = occ->board->tx_fifo_len + cons - prod - 8;
	return room % occ->board->tx_fifo_len;
}

static ssize_t snsocc_tx(struct file *file, struct occ *occ,
			 const char __user *buf, size_t count)
{
	u32 dwords, head, tail, conf;
	DEFINE_WAIT(wait);
	int timeout, ret = 0;

	if (count < 1 || count > occ->board->tx_fifo_len)
		return -EINVAL;

	mutex_lock(&occ->tx_lock);
	for (;;) {
		prepare_to_wait(&occ->tx_wq, &wait, TASK_INTERRUPTIBLE);
		if (occ->reset_in_progress) {
			ret = -ECONNRESET;
			break;
		}
		conf = ioread32(occ->ioaddr + REG_CONFIG);
		if (!(conf & OCC_CONF_TX_ENABLE) &&
					count < __snsocc_tx_room(occ))
			break;
		if (file->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			break;
		}
		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}
		mutex_unlock(&occ->tx_lock);
		schedule();
		mutex_lock(&occ->tx_lock);
	}
	finish_wait(&occ->tx_wq, &wait);

	if (ret)
		goto out;

	/* We don't have a nice function to copy from user space to
	 * the MMIO/FIFO, so we bounce through a buffer. This isn't a
	 * terrible problem, as the TX side is not performance critical.
	 */
	ret = -EFAULT;
	if (copy_from_user(occ->tx_buffer, buf, count))
		goto out;

	if (count % 8)
		memset(occ->tx_buffer + count, 0, 8 - (count % 8));

	dwords = count + 7;
	dwords /= 8;

	/* TX launched, adjust our return code to match the original count */
	ret = count;

	/* Calculate how many dwords of this message can fit before we wrap
	 * around the MMIO space (head).
	 */
	head = (occ->board->tx_fifo_len / 8) - occ->tx_prod;
	if (dwords < head) {
		head = dwords;
		tail = 0;
	} else
		tail = dwords - head;

	__iowrite32_copy(occ->txfifo + (occ->tx_prod * 8),
			 occ->tx_buffer, head * 2);
	__iowrite32_copy(occ->txfifo, occ->tx_buffer + head * 8, tail * 2);

	occ->tx_prod += dwords;
	occ->tx_prod %= occ->board->tx_fifo_len / 8;

	iowrite32(count, occ->ioaddr + REG_TX_LENGTH);
	iowrite32(occ->tx_prod, occ->ioaddr + REG_TX_PROD_INDEX);

	/* Force the settings to post */
	ioread32(occ->ioaddr + REG_TX_PROD_INDEX);

	/* Kick off the TX */
	iowrite32(occ->conf | OCC_CONF_TX_ENABLE, occ->ioaddr + REG_CONFIG);

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
		conf = ioread32(occ->ioaddr + REG_CONFIG);
	} while ((conf & OCC_CONF_TX_ENABLE) && --timeout);

	if (!timeout) {
		timeout = 5000;
		do {
			msleep(1);
			conf = ioread32(occ->ioaddr + REG_CONFIG);
		} while ((conf & OCC_CONF_TX_ENABLE) && --timeout);
	}

	if (!timeout) {
		dev_err(&occ->dev, "TX timeout\n");
		ret = -EIO;
	}

out:
	mutex_unlock(&occ->tx_lock);

	/* Wake up anyone else trying to send */
	wake_up(&occ->tx_wq);
	return ret;
}

static ssize_t snsocc_rx(struct file *file, char __user *buf, size_t count)
{
	struct file_ctx *file_ctx = file->private_data;
	struct occ *occ = file_ctx->occ;
	DEFINE_WAIT(wait);
	int ret = 0;
	u32 info[2];

	if (count != sizeof(info))
		return -EINVAL;

	spin_lock_irq(&occ->lock);
	for (;;) {
		prepare_to_wait(&occ->rx_wq, &wait, TASK_INTERRUPTIBLE);
		if (occ->reset_in_progress) {
			ret = -ECONNRESET;
			break;
		}
		if (occ->stalled)
			break;
		if (occ->dq_prod != occ->dq_cons)
			break;

		if (file->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			break;
		}
		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}
		spin_unlock_irq(&occ->lock);
		schedule();
		spin_lock_irq(&occ->lock);
	}
	finish_wait(&occ->rx_wq, &wait);

	info[0] = occ->dq_prod;
	info[1] = __snsocc_status(occ);
	if (occ->dq_prod != occ->dq_cons)
		info[1] |= OCC_RX_MSG;

	spin_unlock_irq(&occ->lock);

	if (ret)
		goto out;

	ret = -EFAULT;
	if (copy_to_user(buf, info, sizeof(info)))
		goto out;

	ret = sizeof(info);

out:
	return ret;
}

static u32 __snsocc_rxroom(struct occ *occ)
{
	/* Caller must hold occ->lock */
	u32 used = occ->dq_prod - occ->dq_cons + OCC_DQ_SIZE;
	used %= OCC_DQ_SIZE;
	return OCC_DQ_SIZE - used - 1;
}

static u32 snsocc_rxcopy(struct occ *occ, u32 prod, void *src, u32 len)
{
	if (len > 0) {
		/* __snsocc_rxroom() must indicate there is enough room
		 * for the entire packet before calling here.
		 */
		void *dst = page_address(occ->dq_page);
		u32 head, tail;

		/* We know there is room for the packet, so just
		 * check for any wrapping on the ring buffer.
		 */
		head = OCC_DQ_SIZE - prod;
		head = min(head, len);
		tail = len - head;

		memcpy(dst + prod, src, head);
		memcpy(dst, src + head, tail);
	}

	return (prod + len) % OCC_DQ_SIZE;
}

static int snsocc_rxone(struct occ *occ)
{
	/* Called by the RX interrupt to implement the software
	 * emulated data ring.
	 */
	u32 room_needed, length, head, tail, size;
	u32 creg, prod, dq_prod, cons, imq_cons, hwcq_cons, hwdq_cons, used_room;
	struct sw_imq *imq;
	void *src;

	spin_lock_irq(&occ->lock);
	imq_cons = occ->imq_cons;
	hwcq_cons = occ->hwcq_cons;
	hwdq_cons = occ->hwdq_cons;
	dq_prod = occ->dq_prod;
	used_room = __snsocc_rxroom(occ);

	if (occ->imq_prod == occ->imq_cons || occ->stalled) {
		spin_unlock_irq(&occ->lock);
		return 1;
	}
	spin_unlock_irq(&occ->lock);

	/* We need room for the split header, and the payload. */
	imq = &occ->imq[imq_cons];
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
		src = page_address(occ->hwcq_page);
		prod = ioread32(occ->ioaddr + REG_CQ_PROD_INDEX);
		cons = hwcq_cons;
		creg = REG_CQ_CONS_INDEX;
		size = OCC_CQ_SIZE;
	} else {
		src = page_address(occ->hwdq_page);
		prod = ioread32(occ->ioaddr + REG_DQ_PROD_INDEX);
		cons = hwdq_cons;
		creg = REG_DQ_CONS_INDEX;
		size = OCC_DQ_SIZE;
	}

	/* Make sure we have enough data in the source queue */
	if (length > ((prod - cons + size) % size)) {
		dev_err_ratelimited(&occ->dev,
				    "IMQ too-long length (reg %x)\n", creg);
		spin_lock_irq(&occ->lock);
		__snsocc_stalled(occ, OCC_DMA_STALLED);
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
		dev_warn_ratelimited(&occ->dev, "userspace stalled RX\n");
		spin_lock_irq(&occ->lock);
		__snsocc_stalled(occ, OCC_DMA_STALLED);
		goto consume_queue;
	}

	/* We do our updates against a local copy of the userspace DQ
	 * producer index; this drops the lock during the copies, and
	 * ensures users only see a complete packet at the end.
	 */
	dq_prod = snsocc_rxcopy(occ, dq_prod, imq, sizeof(struct sw_imq));
	dq_prod = snsocc_rxcopy(occ, dq_prod, src + cons, head);
	dq_prod = snsocc_rxcopy(occ, dq_prod, src, tail);

	/* The OCC card consumes the queues in 4 byte incremenets */
	cons += ALIGN(length, 4);
	cons %= size;

	spin_lock_irq(&occ->lock);
	occ->dq_prod = dq_prod;

consume_queue:
	if (imq->type & IMQ_TYPE_COMMAND)
		occ->hwcq_cons = cons;
	else
		occ->hwdq_cons = cons;
	iowrite32(cons, occ->ioaddr + creg);
	occ->imq_cons++;
	occ->imq_cons %= SW_IMQ_RING_SIZE;
	wake_up(&occ->rx_wq);

	spin_unlock_irq(&occ->lock);

	return 0;
}

static void snsocc_rxtask(unsigned long data)
{
	struct occ *occ = (void *)data;
	int budget = 32;

	while (--budget) {
		if (snsocc_rxone(occ))
			return;
	}

	tasklet_hi_schedule(&occ->rxtask);
}

static int snsocc_saveimqs(struct occ *occ)
{
	/* Copy packet headers from the limited hardware queue to our larger
	 * one to cover the latency of tasklet packet copy.
	 * New packet headers become avaialbe only when the function completes,
	 * but the device is not locked during copying.
	 */
	u32 hwprod = ioread32(occ->ioaddr + REG_IMQ_PROD_INDEX);
	struct hw_imq *hwimq = page_address(occ->hwimq_page);
	u32 new_prod, hwimq_cons, imq_prod, imq_cons;
	int rc = 0;

	spin_lock(&occ->lock);
	hwimq_cons = occ->hwimq_cons;
	imq_prod = occ->imq_prod;
	imq_cons = occ->imq_cons;
	spin_unlock(&occ->lock);

	while (hwimq_cons != hwprod) {
		/* Copy in the new descriptors we know about before
		 * checking for more.
		 */
		do {
			new_prod = (imq_prod + 1) % SW_IMQ_RING_SIZE;
			if (new_prod == imq_cons) {
				// XXX: If seeing this case a lot, consider refreshing imq_cons to make sure consumer did not advance
				rc = -ENOSPC;
				dev_warn_ratelimited(&occ->dev,
						     "no IMQ space\n");
				goto out_update;
			}

			memcpy(occ->imq + imq_prod, hwimq + hwimq_cons, sizeof(*occ->imq));
			imq_prod = new_prod;

			/* Go ahead and consume the HW entry */
			hwimq_cons++;
			hwimq_cons %= OCC_IMQ_ENTRIES;
		} while (hwimq_cons != hwprod);

		hwprod = ioread32(occ->ioaddr + REG_IMQ_PROD_INDEX);
	}

out_update:
	spin_lock(&occ->lock);
	occ->hwimq_cons = hwimq_cons;
	occ->imq_prod = imq_prod;
	spin_unlock(&occ->lock);

	/* snsocc_reset() is the only place where this register is overwritten
	 * but the snsocc_reset() makes sure to disable and synchronize interrupts
	 * we're safe to do the write without the lock.
	 */
	iowrite32(hwimq_cons, occ->ioaddr + REG_IMQ_CONS_INDEX);

	return rc;
}

static irqreturn_t snsocc_interrupt(int irq, void *data)
{
	struct occ *occ = data;
	u32 intr_status;

	u32 scheduled = 0;
	u32 start = 0;
	u8 irq_latency_capture = occ->irq_latency.capture;

	if (irq_latency_capture)
		start = ioread32(occ->ioaddr + REG_TIME_COUNTER);

	intr_status = ioread32(occ->ioaddr + REG_IRQ_STATUS);
	if (!intr_status) {
#ifdef CONFIG_PCI_MSI
		if (occ->msi_enabled) {
			/* With MSI, there is no sharing of interrupts, so this is our
			* interrupt. Why the device did not announce it through register?
			* Anyway, we must let kernel ack it or spurious interrupts will
			* occur.
			*/
			dev_err_ratelimited(&occ->dev, "Firmware did not assert interrupt");
			return IRQ_HANDLED;
		}
#endif
		return IRQ_NONE;
	}

	if (irq_latency_capture) {
		if (intr_status & OCC_IRQ_RX_DONE)
			scheduled = ioread32(occ->ioaddr + REG_TIME_IRQ_DQ);
		else if (intr_status & OCC_IRQ_DMA_STALL)
			scheduled = ioread32(occ->ioaddr + REG_TIME_IRQ_DMA_STALL);
		else if (intr_status & OCC_IRQ_FIFO_OVERFLOW)
			scheduled = ioread32(occ->ioaddr + REG_TIME_IRQ_FIF_OVRFLW);
	}

	/* Clearing interrupt to minimize propagation time to OCC. With non-MSI
         * interrupts, there's a potential race condition when OCC asserts interrupt. We tell the
	 * kernel we've handled the first interrupt but by the time kernel responds
	 * the line might be asserted again and the kernel will silently ignore it.
	 * Consider putting a while() loop around this code.
	 */
	intr_status &= ~OCC_IRQ_ENABLE;
	iowrite32(intr_status, occ->ioaddr + REG_IRQ_STATUS);
	ioread32(occ->ioaddr + REG_IRQ_STATUS); // iowrite32() is posted, make sure register gets to the board

	if (likely(intr_status & OCC_IRQ_RX_DONE)) {
		if (unlikely(occ->emulate_dq)) {
			if (snsocc_saveimqs(occ))
				snsocc_stalled(occ, OCC_DMA_STALLED);
			else
				tasklet_hi_schedule(&occ->rxtask);
		} else {
			spin_lock(&occ->lock);
			occ->dq_prod = ioread32(occ->ioaddr + REG_DQ_PROD_INDEX);
			wake_up(&occ->rx_wq);
			spin_unlock(&occ->lock);
		}
	}
	if (unlikely(intr_status & OCC_IRQ_DMA_STALL)) {
		snsocc_stalled(occ, OCC_DMA_STALLED);
		dev_err_ratelimited(&occ->dev, "Detected DMA stall flag");
	}
	if (unlikely(intr_status & OCC_IRQ_FIFO_OVERFLOW)) {
		if (!(intr_status & OCC_IRQ_DMA_STALL))
			snsocc_stalled(occ, OCC_FIFO_OVERFLOW);
		dev_err_ratelimited(&occ->dev, "Detected FIFO overflow flag");
	}

	if (scheduled != 0) {
		u32 latency = start - scheduled;
		u32 end = ioread32(occ->ioaddr + REG_TIME_COUNTER);

		spin_lock(&occ->lock);
		occ->irq_latency.abs_min = min(occ->irq_latency.abs_min, latency);
		occ->irq_latency.abs_max = max(occ->irq_latency.abs_max, latency);

		occ->irq_latency.end = (occ->irq_latency.end + 1) % occ->irq_latency.size;
		occ->irq_latency.isr_delay[occ->irq_latency.end] = latency;
		occ->irq_latency.isr_proctime[occ->irq_latency.end] = end - start;
		spin_unlock(&occ->lock);
	}

	return IRQ_HANDLED;
}

static void snsocc_reset(struct occ *occ)
{
	void __iomem *ioaddr = occ->ioaddr;

	/* Kick out anybody blocked in read() or trying to send data */
	mutex_lock(&occ->tx_lock);
	occ->reset_in_progress = true;
	wake_up_all(&occ->tx_wq);
	mutex_unlock(&occ->tx_lock);

	spin_lock_irq(&occ->lock);
	occ->reset_occurred = true;
	wake_up_all(&occ->rx_wq);
	spin_unlock_irq(&occ->lock);

	/* XXX should wait for everyone to leave */

	/* The GE cards don't like being reset while an interrupt is being
	 * processed; they go into a screaming match.
	 */
	if (ioread32(ioaddr + REG_IRQ_ENABLE)) {
		iowrite32(0, ioaddr + REG_IRQ_ENABLE);
		synchronize_irq(occ->pdev->irq);

		/* Make sure we aren't still accessing the card for DQ
		 * emulation.
		 */
		tasklet_kill(&occ->rxtask);
	}

	/* Disable the DMA first and give it some time to settle down.
	 * PCIe cards are not happy being reset while DMA is in progress.
	 * Especially with high throughputs the likelyhood of hitting it
	 * just right is high.
	 */
	iowrite32(0, ioaddr + REG_CONFIG);
	msleep(1); // no busy waiting here

	if (occ->board->reset_errcnt)
		iowrite32(OCC_CONF_RESET | OCC_CONF_ERRORS_RESET, ioaddr + REG_CONFIG);
	else
		iowrite32(OCC_CONF_RESET, ioaddr + REG_CONFIG);

	/* Post our writes; RESET will self-clear on the next PCI cycle. */
	ioread32(ioaddr + REG_CONFIG);

	if (occ->use_optical && occ->hwdq_page) {
		/* We're using a board/firmware that splits the optical
		 * RX path into three queues, so we need to point the
		 * hardware at a different DQ than the unified one we
		 * emulate to the user.
		 */
		occ->emulate_dq = 1;

		iowrite32(occ->hwimq_dma, ioaddr + REG_IMQ_ADDR);
		iowrite32(occ->hwimq_dma >> 32, ioaddr + REG_IMQ_ADDRHI);

		iowrite32(occ->hwcq_dma, ioaddr + REG_CQ_ADDR);
		iowrite32(occ->hwcq_dma >> 32, ioaddr + REG_CQ_ADDRHI);
		iowrite32(OCC_CQ_SIZE - 1, ioaddr + REG_CQ_MAX_OFFSET);

		iowrite32(occ->hwdq_dma, ioaddr + REG_DQ_ADDR);
		iowrite32(occ->hwdq_dma >> 32, ioaddr + REG_DQ_ADDRHI);
		iowrite32(OCC_DQ_SIZE - 1, ioaddr + REG_DQ_MAX_OFFSET);
	} else {
		/* This board uses an unified DQ, or we're using the LVDS
		 * so directly map it onto the buffer the user maps.
		 */
		u64 addr = (occ->dq_big_addr ? virt_to_bus(phys_to_virt(occ->dq_big_addr)) : occ->dq_dma);
		occ->emulate_dq = 0;
		iowrite32(addr & 0xFFFFFFFF, ioaddr + REG_DQ_ADDR);
		iowrite32((addr >> 32) & 0xFFFFFFFF, ioaddr + REG_DQ_ADDRHI);
		iowrite32(occ->dq_size - 1, ioaddr + REG_DQ_MAX_OFFSET);
	}

	/* Clear queue offset indexes */
	iowrite32(0x0, ioaddr + REG_TX_PROD_INDEX);
	if (occ->emulate_dq)
		iowrite32(0x0, ioaddr + REG_RX_CONS_INDEX);
	else
		iowrite32(0x0, ioaddr + REG_DQ_CONS_INDEX);

	/* Legacy PCI-X does not like to get spontaneously enabled */
	if (occ->board->late_rx_enable)
		occ->conf &= ~OCC_CONF_RX_ENABLE;
	else
		occ->conf |= OCC_CONF_RX_ENABLE;

	iowrite32(occ->conf, occ->ioaddr + REG_CONFIG);
	if (occ->emulate_dq) {
		occ->dq_prod = occ->dq_cons = 0;
		occ->imq_cons = occ->imq_prod = 0;
		occ->hwdq_cons = ioread32(occ->ioaddr + REG_DQ_CONS_INDEX);
		occ->hwdq_prod = ioread32(occ->ioaddr + REG_DQ_PROD_INDEX);
		occ->hwimq_cons = ioread32(occ->ioaddr + REG_IMQ_CONS_INDEX);
		occ->hwcq_cons = ioread32(occ->ioaddr + REG_CQ_CONS_INDEX);
	} else {
		occ->dq_cons = ioread32(occ->ioaddr + REG_DQ_CONS_INDEX);
		occ->dq_prod = ioread32(occ->ioaddr + REG_DQ_PROD_INDEX);
	}
	occ->tx_prod = ioread32(occ->ioaddr + REG_TX_PROD_INDEX);
	iowrite32(occ->irqs, occ->ioaddr + REG_IRQ_ENABLE);

	/* Give the optical module some time to bring up the TX laser, and
	 * lock on to any RX signal to prevent spurious reports of lost
	 * signal.
	 */
	if (occ->use_optical)
		msleep(100);

	spin_lock_irq(&occ->lock);
	occ->irq_latency.capture = 0;
	occ->irq_latency.abs_min = 0xFFFFFFFF;
	occ->irq_latency.abs_max = 0;
	occ->irq_latency.size = OCC_IRQ_LAT_BUF_SIZE;
	occ->irq_latency.end = 0;
	memset(occ->irq_latency.isr_delay, 0, sizeof(u32) * OCC_IRQ_LAT_BUF_SIZE);
	memset(occ->irq_latency.isr_proctime, 0, sizeof(u32) * OCC_IRQ_LAT_BUF_SIZE);

	occ->reset_in_progress = false;
	occ->stalled = false;
	spin_unlock_irq(&occ->lock);
}

static int snsocc_alloc_queue(struct device *dev, struct page **page,
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
		get_page(compound_head((*page) + i));

	/* Prevent information leaks to user-space */
	memset(page_address(*page), 0, PAGE_SIZE * (1 << order));

	return 0;
}

static void snsocc_free_queue(struct device *dev, struct page *page,
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
		put_page(compound_head((page) + i));

	__free_pages(page, order);
}

static void snsocc_alloc_big_queue(struct occ *occ, unsigned long phys_addr, unsigned long size)
{
	occ->dq_size = size & PAGE_MASK;
	occ->dq_big_addr = phys_addr;
}

static void snsocc_free_big_queue(struct occ *occ)
{
	if (occ->dq_size > OCC_DQ_SIZE) {
		occ->dq_big_addr = 0;
		// Revert to the kmalloc-ed page memory
		occ->dq_size = OCC_DQ_SIZE;
	}
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,11,0)
static int snsocc_vm_fault(struct vm_fault *vmf)
#else
static int snsocc_vm_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
#endif
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,11,0)
	struct file_ctx *file_ctx = vmf->vma->vm_private_data;
#else
	struct file_ctx *file_ctx = vma->vm_private_data;
#endif
	struct occ *occ = file_ctx->occ;
	unsigned long offset;
	struct page *page;

	if (!occ)
		return VM_FAULT_SIGBUS;

	page = occ->dq_page;
	if (vmf->pgoff >= (1 << get_order(occ->dq_size)))
		return VM_FAULT_SIGBUS;

	offset >>= PAGE_SHIFT;
	page += offset;
	get_page(page);
	vmf->page = page;

	return 0;
}

static const struct vm_operations_struct snsocc_vm_ops = {
	.fault = snsocc_vm_fault,
};

static int snsocc_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct file_ctx *file_ctx = file->private_data;
	struct occ *occ = file_ctx->occ;
	unsigned long size = vma->vm_end - vma->vm_start;
	unsigned long pfn;

	switch (vma->vm_pgoff) {
	case OCC_MMAP_BAR0:
	case OCC_MMAP_BAR1:
	case OCC_MMAP_BAR2:
		if (size != occ->bars[vma->vm_pgoff])
			return -EINVAL;
		pfn = pci_resource_start(occ->pdev, vma->vm_pgoff) >> PAGE_SHIFT;
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
		vma->vm_flags |= VM_IO;
		break;
	case OCC_MMAP_RX_DMA:
		if (size != occ->dq_size)
			return -EINVAL;
		if (occ->dq_big_addr)
			pfn = virt_to_phys(bus_to_virt(occ->dq_big_addr)) >> PAGE_SHIFT;
		else
			pfn = page_to_pfn(occ->dq_page);
		vma->vm_flags |= VM_IO | VM_DONTEXPAND;
		break;
	default:
		return -EINVAL;
	}

	return remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot);
}

static unsigned int snsocc_poll(struct file *file,
				struct poll_table_struct *wait)
{
	struct file_ctx *file_ctx = file->private_data;
	struct occ *occ = file_ctx->occ;
	unsigned int mask = 0;
	unsigned long flags;

	poll_wait(file, &occ->rx_wq, wait);
	poll_wait(file, &occ->tx_wq, wait);

	mutex_lock(&occ->tx_lock);
	if (!(ioread32(occ->ioaddr + REG_CONFIG) & OCC_CONF_TX_ENABLE))
		mask |= POLLOUT | POLLWRNORM;
	mutex_unlock(&occ->tx_lock);

	spin_lock_irqsave(&occ->lock, flags);
	if (occ->dq_prod != occ->dq_cons)
		mask |= POLLIN | POLLRDNORM;
 	if (occ->reset_occurred || occ->reset_in_progress)
		mask |= POLLERR;
	if (occ->stalled)
		mask |= POLLHUP;
	spin_unlock_irqrestore(&occ->lock, flags);

	return mask;
}

static ssize_t snsocc_read(struct file *file, char __user *buf,
			   size_t count, loff_t *pos)
{
	struct file_ctx *file_ctx = file->private_data;
	struct occ *occ = file_ctx->occ;
	struct occ_status info;
	struct occ_version ver;
	ssize_t ret = 0;

	switch (*pos) {
	case OCC_CMD_GET_STATUS:
		if (count != sizeof(struct occ_status))
			return -EINVAL;

		spin_lock_irq(&occ->lock);
		if (occ->reset_in_progress) {
			ret = -ECONNRESET;
		} else {
			info.occ_ver = OCC_VER;
			info.board_type = occ->board->type;
			info.hardware_ver = (occ->board->type == BOARD_SNS_PCIE ? ((occ->version >> 16) & 0xFFFF) : 0);
			info.firmware_ver = (occ->board->type == BOARD_SNS_PCIE ? (occ->version & 0xFFFF) : occ->version);
			info.firmware_date = occ->firmware_date;
			info.fpga_serial = occ->fpga_serial;
			info.dq_used = (occ->dq_size + occ->dq_prod - occ->dq_cons) % occ->dq_size;
			info.dq_size = occ->dq_size;
			info.bars[0] = occ->bars[0];
			info.bars[1] = occ->bars[1];
			info.bars[2] = occ->bars[2];
			occ->reset_occurred = occ->reset_in_progress;
			info.status = __snsocc_status(occ);
			info.rx_rate = __snsocc_rxrate(occ);
			__snsocc_errcounters(occ, &info.err_crc, &info.err_length, &info.err_frame);
			__snsocc_fpgainfo(occ, &info.fpga_temp, &info.fpga_core_volt, &info.fpga_aux_volt);
		}
		spin_unlock_irq(&occ->lock);

		if (ret != 0)
			return ret;

		if (copy_to_user(buf, &info, sizeof(info)))
			return -EFAULT;
		break;
	case OCC_CMD_RX:
		count = snsocc_rx(file, buf, count);
		break;
	case OCC_CMD_VERSION:
		ver.major = OCC_VER_MAJ;
		ver.minor = OCC_VER_MIN;
		if (count != sizeof(ver))
			return -EINVAL;
		if (copy_to_user(buf, &ver, sizeof(ver)))
			return -EFAULT;
		break;
	default:
		return -EINVAL;
	}

	return count;
}

static ssize_t snsocc_write(struct file *file, const char __user *buf,
			    size_t count, loff_t *pos)
{
	struct file_ctx *file_ctx = file->private_data;
	struct occ *occ = file_ctx->occ;
	u32 val, prod;
	ssize_t ret = 0;

	/* Debug connection is limited to reset only */
	if (file_ctx->debug_mode && *pos != OCC_CMD_RESET)
		return -EINVAL;

	switch (*pos) {
	case OCC_CMD_ADVANCE_DQ:
		if (count != sizeof(u32))
			return -EINVAL;

		if (copy_from_user(&val, buf, sizeof(u32)))
			return -EFAULT;

		if (val == 0)
			break;

		/* We only deal with packets that are multiples of 4 bytes */
		val = ALIGN(val, 4);

		if (val >= occ->dq_size)
			return -EOVERFLOW;

		/* Validate that the new consumer index is within the range
		 * of valid data.
		 */
		spin_lock_irq(&occ->lock);
		if (occ->reset_in_progress) {
			ret = -ECONNRESET;
		} else {
			val += occ->dq_cons;
			prod = occ->dq_prod;
			if (occ->dq_prod < occ->dq_cons)
				prod += occ->dq_size;
			if (val > occ->dq_cons && val <= prod) {
				occ->dq_cons = val % occ->dq_size;
				if (!occ->emulate_dq) {
					iowrite32(occ->dq_cons,
						  occ->ioaddr + REG_DQ_CONS_INDEX);
				}
			} else {
				ret = -EOVERFLOW;
			}
		}
		spin_unlock_irq(&occ->lock);

		if (ret != 0)
			return ret;
		break;
	case OCC_CMD_RESET:
		if (count != sizeof(u32))
			return -EINVAL;

		if (copy_from_user(&val, buf, sizeof(u32)))
			return -EFAULT;

		if (val > OCC_SELECT_OPTICAL)
			return -EINVAL;

		spin_lock_irq(&occ->lock);
		if (occ->reset_in_progress) {
			ret = -ECONNRESET;
		} else {
			occ->use_optical = !!val;
			if (occ->use_optical) {
				occ->conf |= OCC_CONF_SELECT_OPTICAL;
				occ->conf |= OCC_CONF_OPTICAL_ENABLE;
			} else {
				occ->conf &= ~OCC_CONF_SELECT_OPTICAL;
				occ->conf &= ~OCC_CONF_OPTICAL_ENABLE;
			}
		}
		spin_unlock_irq(&occ->lock);

		if (ret != 0)
			return ret;

		snsocc_reset(occ);
		break;
	case OCC_CMD_TX:
		count = snsocc_tx(file, occ, buf, count);
		break;
	case OCC_CMD_RX_ENABLE:
		if (count != sizeof(u32))
			return -EINVAL;

		if (copy_from_user(&val, buf, sizeof(u32)))
			return -EFAULT;
		if (!occ->board->late_rx_enable) {
			/* When board does not support enabling/disabling RX,
			   it's always enabled. Skip re-enabling but scream if
			   tried to disable. */
			if (!val)
				return -EINVAL;
			break;
		}


		if (!occ->board->late_rx_enable) {
			/* When board does not support enabling/disabling RX,
			   it always enabled. Allow enabling but scream if
			   tried to disable. */
			if (!val)
				return -EINVAL;
			break;
		}

		spin_lock_irq(&occ->lock);
		if (occ->reset_in_progress) {
			ret = -ECONNRESET;
		} else {
			if (val)
				occ->conf |= OCC_CONF_RX_ENABLE;
			else
				occ->conf &= ~OCC_CONF_RX_ENABLE;
			val = occ->conf;
		}
		spin_unlock_irq(&occ->lock);

		if (ret != 0)
			return ret;

		iowrite32(val, occ->ioaddr + REG_CONFIG);
		ioread32(occ->ioaddr + REG_CONFIG); // post write

		break;
	case OCC_CMD_ERR_PKTS_ENABLE:
		if (count != sizeof(u32))
			return -EINVAL;

		if (copy_from_user(&val, buf, sizeof(u32)))
			return -EFAULT;

		spin_lock_irq(&occ->lock);
		if (occ->reset_in_progress) {
			ret = -ECONNRESET;
		} else {
			if (val)
				occ->conf |= OCC_CONF_ERR_PKTS_ENABLE;
			else
				occ->conf &= ~OCC_CONF_ERR_PKTS_ENABLE;
			val = occ->conf;
		}
		spin_unlock_irq(&occ->lock);

		if (ret != 0)
			return ret;

		iowrite32(val, occ->ioaddr + REG_CONFIG);
		ioread32(occ->ioaddr + REG_CONFIG); // post write

		break;
	case OCC_CMD_OLD_PKTS_EN:
		if (count != sizeof(u32))
			return -EINVAL;

		if (copy_from_user(&val, buf, sizeof(u32)))
			return -EFAULT;

		spin_lock_irq(&occ->lock);
		if (occ->reset_in_progress) {
			ret = -ECONNRESET;
		} else {
			if (val)
				occ->conf &= ~OCC_CONF_OLD_PKTS_DISABLE;
			else
				occ->conf |= OCC_CONF_OLD_PKTS_DISABLE;
			val = occ->conf;
		}
		spin_unlock_irq(&occ->lock);

		if (ret != 0)
			return ret;

		iowrite32(val, occ->ioaddr + REG_CONFIG);
		ioread32(occ->ioaddr + REG_CONFIG); // post write

		break;
	default:
		return -EINVAL;
	}

	return count;
}

static int snsocc_open(struct inode *inode, struct file *file)
{
	struct occ *occ = container_of(inode->i_cdev, struct occ, cdev);
	int err = 0;
	struct file_ctx *file_ctx;

	file->private_data = NULL;

	file_ctx = kmalloc(sizeof(struct file_ctx), GFP_KERNEL);
	if (!file_ctx)
		return -ENOMEM;

	memset(file_ctx, 0, sizeof(struct file_ctx));
	file_ctx->occ = occ;
	file_ctx->debug_mode = ((file->f_flags & O_EXCL) ? false : true);

	file->private_data = file_ctx;

	/* Debug connection is limited but not exclusive */
	if (file_ctx->debug_mode)
		return 0;

	/* We only allow one process at a time to have us open. */
	spin_lock_irq(&occ->lock);
	if (occ->in_use)
		err = -EBUSY;
	else
		occ->in_use = true;
	spin_unlock_irq(&occ->lock);

	if (err)
		return err;

	occ->conf = 0;
	if (occ->use_optical) {
		occ->conf |= OCC_CONF_SELECT_OPTICAL;
		occ->conf |= OCC_CONF_OPTICAL_ENABLE;
		// TODO: Leave old packets enabled by default for now.
		//occ->conf |= OCC_CONF_OLD_PKTS_DISABLE;
	}
	occ->irqs = OCC_IRQ_ENABLE | OCC_IRQ_RX_DONE | OCC_IRQ_DMA_STALL | OCC_IRQ_FIFO_OVERFLOW;
	snsocc_reset(occ);

	return err;
}

static int snsocc_release(struct inode *inode, struct file *file)
{
	struct file_ctx *file_ctx = file->private_data;

	if (file_ctx) {
		if (!file_ctx->debug_mode) {
			struct occ *occ = file_ctx->occ;
			void __iomem *ioaddr = occ->ioaddr;

			/* Disable DMA only, no need to send more data since noone is listening */
			iowrite32(0, ioaddr + REG_CONFIG);

			spin_lock_irq(&occ->lock);
			occ->in_use = false;
			spin_unlock_irq(&occ->lock);
		}

		file_ctx->occ = NULL;
		kfree(file_ctx);
	}

	return 0;
}

static void snsocc_free(struct device *dev)
{
	struct occ *occ = container_of(dev, struct occ, dev);

	kfree(occ);
}

static ssize_t snsocc_sysfs_show_irq_coallesce(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct occ *occ = dev_get_drvdata(dev);
	u32 val = ioread32(occ->ioaddr + REG_IRQ_CNTL) & 0xFFFF;
	return scnprintf(buf, PAGE_SIZE, "%u\n", val);
}

static ssize_t snsocc_sysfs_store_irq_coallesce(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct occ *occ = dev_get_drvdata(dev);
	u32 val;
	if (sscanf(buf, "%u", &val) == 1) {
		val &= 0xFFFF;
		if (val > 0) val |= OCC_COALESCING_ENABLE;
		iowrite32(val, occ->ioaddr + REG_IRQ_CNTL);
		return count;
	}
	return -EINVAL;
}

static ssize_t snsocc_sysfs_show_dma_big_mem(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct occ *occ = dev_get_drvdata(dev);
	int ret = 0;

	spin_lock_irq(&occ->lock);
	ret = scnprintf(buf, PAGE_SIZE, "%s\n", occ->dq_big_cnf);
	spin_unlock_irq(&occ->lock);

	return ret;
}

static ssize_t snsocc_sysfs_store_dma_big_mem(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct occ *occ = dev_get_drvdata(dev);
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
	if (size <= OCC_DQ_SIZE)
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

	spin_lock_irq(&occ->lock);
	if (occ->in_use) {
		err = -EBUSY;
	} else {
		occ->in_use = true;
	}
	spin_unlock_irq(&occ->lock);

	if (err)
		return err;

	snsocc_free_big_queue(occ);
	snsocc_alloc_big_queue(occ, offset, size);

	spin_lock_irq(&occ->lock);
	strncpy(occ->dq_big_cnf, buf, sizeof(occ->dq_big_cnf));
	occ->in_use = false;
	spin_unlock_irq(&occ->lock);

	return count;
}

static ssize_t snsocc_sysfs_show_serial_number(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct occ *occ = dev_get_drvdata(dev);
	int ret = 0;

	spin_lock_irq(&occ->lock);
	ret = scnprintf(buf, PAGE_SIZE, "%016llX\n", occ->fpga_serial);
	spin_unlock_irq(&occ->lock);

	return ret;
}

static ssize_t snsocc_sysfs_show_firmware_date(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct occ *occ = dev_get_drvdata(dev);
	int ret = 0;

	spin_lock_irq(&occ->lock);
	ret = scnprintf(buf, PAGE_SIZE, "%02X/%02X/%04X\n", occ->firmware_date >> 24, occ->firmware_date >> 16 & 0xFF, occ->firmware_date & 0xFFFF);
	spin_unlock_irq(&occ->lock);

	return ret;
}

static ssize_t snsocc_sysfs_show_irq_latency(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct occ *occ = dev_get_drvdata(dev);
	ssize_t len = 0;

	spin_lock_irq(&occ->lock);
	if (occ->irq_latency.capture) {
		u32 i;
		char line[32];

		len = scnprintf(buf, PAGE_SIZE, "abs min=%u max=%u\n"
	                                	"delay proctime\n",
	                	occ->irq_latency.abs_min, occ->irq_latency.abs_max);
		if (len < 0)
			return len;

		for (i=0; i<occ->irq_latency.size; i++) {
			u32 offset = (i > occ->irq_latency.end ? occ->irq_latency.size : 0) + occ->irq_latency.end - i;
			ssize_t r = scnprintf(line, sizeof(line), "%u %u\n",
		                      	occ->irq_latency.isr_delay[offset],
		                      	occ->irq_latency.isr_proctime[offset]);
			if (r < 0)
				return r;
			if (r > (PAGE_SIZE - len))
				break;
			strncpy(&buf[len], line, r);
			len += r;
		}
	}
	spin_unlock_irq(&occ->lock);

	return len;
}

static ssize_t snsocc_sysfs_store_irq_latency(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct occ *occ = dev_get_drvdata(dev);
	unsigned long enable;

	if (sscanf(buf, "%lu", &enable) < 1)
		return -EINVAL;

	spin_lock_irq(&occ->lock);
	occ->irq_latency.capture = (enable != 0);
	spin_unlock_irq(&occ->lock);

	return count;
}

static struct file_operations snsocc_fops = {
	.owner	 = THIS_MODULE,
	.open	 = snsocc_open,
	.release = snsocc_release,
	.mmap	 = snsocc_mmap,
	.poll	 = snsocc_poll,
	.write	 = snsocc_write,
	.read	 = snsocc_read,
};

static int __devexit snsocc_probe(struct pci_dev *pdev,
				  const struct pci_device_id *ent)
{
	int board_id = ent->driver_data;
	struct device *dev = &pdev->dev;
	struct occ *occ = NULL;
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
				 (1 << OCC_MMIO_BAR) | (1 << OCC_TXFIFO_BAR),
				 KBUILD_MODNAME);
	if (err) {
		dev_err(dev, "unable to request and map MMIO, aborting");
		goto error;
	}

	err = -ENOMEM;
	occ = kzalloc(sizeof(*occ), GFP_KERNEL);
	if (!occ) {
		dev_err(dev, "Unable to allocate private memory, aborting");
		goto error;
	}

	err = -ENODEV;
	mutex_lock(&snsocc_devlock);
	for (minor = 0; minor < OCC_MAX_DEVS; minor++) {
		if (!snsocc_devs[minor]) {
			snsocc_devs[minor] = occ;
			break;
		}
	}
	mutex_unlock(&snsocc_devlock);

	if (minor == OCC_MAX_DEVS) {
		/* We've not associated it with a device yet, so clean up. */
		kfree(occ);
		dev_err(dev, "too many OCC cards in system, aborting");
		goto error;
	}

	cdev_init(&occ->cdev, &snsocc_fops);
	spin_lock_init(&occ->lock);
	mutex_init(&occ->tx_lock);
	init_waitqueue_head(&occ->tx_wq);
	init_waitqueue_head(&occ->rx_wq);
	tasklet_init(&occ->rxtask, snsocc_rxtask, (unsigned long) occ);
	occ->cdev.owner = THIS_MODULE;
	occ->pdev = pdev;
	occ->minor = minor;
	strncpy(occ->dq_big_cnf, "0$0", sizeof(occ->dq_big_cnf));
	occ->bars[0] = pci_resource_len(pdev, 0);
	occ->bars[1] = pci_resource_len(pdev, 1);
	occ->bars[2] = pci_resource_len(pdev, 2);

	dev_set_name(&occ->dev, "snsocc%d", minor);
	occ->dev.devt = snsocc_basedev + minor;
	occ->dev.class = snsocc_class;
	occ->dev.parent = dev;
	occ->dev.release = snsocc_free;
	device_initialize(&occ->dev);

	memset(&occ->irq_latency, 0, sizeof(struct irq_latency));
	occ->irq_latency.isr_delay = kmalloc(sizeof(u32) * OCC_IRQ_LAT_BUF_SIZE, GFP_KERNEL);
	occ->irq_latency.isr_proctime = kmalloc(sizeof(u32) * OCC_IRQ_LAT_BUF_SIZE, GFP_KERNEL);
	if (!occ->irq_latency.isr_delay || !occ->irq_latency.isr_proctime) {
		dev_err(dev, "unable to allocate interrupt latency queues, aborting");
		goto error_stat;
	}

	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if (err) {
		err = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (err) {
			dev_err(dev, "no usable DMA config, aborting");
			goto error_dev;
		}

		dev_info(dev, "using 32-bit DMA mask");
	}

	occ->ioaddr = pcim_iomap_table(pdev)[OCC_MMIO_BAR];
	occ->txfifo = pcim_iomap_table(pdev)[OCC_TXFIFO_BAR];

	/* Verify that we support the firmware loaded on the card.
	 * Code is 0xVVYYMMDD -- version, year, month, day (BCD)
	 */
	err = -ENODEV;
	occ->version          = ioread32(occ->ioaddr + REG_VERSION);
	occ->firmware_date    = ioread32(occ->ioaddr + REG_FIRMWARE_DATE);
	occ->fpga_serial      = ioread32(occ->ioaddr + REG_SERNO_LO);
	occ->fpga_serial     |= (u64)ioread32(occ->ioaddr + REG_SERNO_HI) << 32;
	occ->board = &boards[0];
	while (occ->board && occ->board->type != 0) {
		if (occ->board->type == board_id) {
			u32 *version = occ->board->version;
			while (*version != 0) {
				if (*version == occ->version)
					break;
				version++;
			}
			if (*version == occ->version)
				break;
		}
		++occ->board;
	}
	if (!occ->board || occ->board->type == 0) {
		dev_err(dev, "unsupported %s firmware 0x%08x\n", snsocc_name[board_id], occ->version);
		err = -ENODEV;
		goto error_dev;
	}

	if (occ->board->sysfs.attrs) {
		err = sysfs_create_group(&dev->kobj, &occ->board->sysfs);
		if (err) {
			dev_err(dev, "unable to create sysfs entries");
			goto error_dev;
		}
	}

	occ->msi_enabled = 0;
#ifdef CONFIG_PCI_MSI
	if (occ->board->interrupts & OCC_IRQ_MSI) {
		if (pci_find_capability(pdev, PCI_CAP_ID_MSI) == 0) {
			dev_err(dev, "device does not support MSI interrupts, falling back to legacy");
		} else if (pci_enable_msi(pdev) != 0) {
			dev_err(dev, "MSI init failed");
		} else {
			occ->msi_enabled = 1;
		}
	}
#endif

	err = request_irq(pdev->irq, snsocc_interrupt, IRQF_SHARED, KBUILD_MODNAME, occ);
	if (err) {
		dev_err(dev, "unable to request interrupt, aborting");
		goto error_dev;
	}

	// Start with small DMA buffer, change through sysfs later
	occ->dq_size = OCC_DQ_SIZE;
	if (snsocc_alloc_queue(dev, &occ->dq_page, &occ->dq_dma, OCC_DQ_SIZE)) {
		dev_err(dev, "unable to allocate data queue, aborting");
		goto error_dev;
	}

	if (!occ->board->unified_que) {
		/* Some of these could be done by dev_alloc_coherent(), but
		 * this works just as well and minimizes the different
		 * concepts in the driver.
		 */
		if (snsocc_alloc_queue(dev, &occ->hwimq_page, &occ->hwimq_dma,
							OCC_IMQ_SIZE)) {
			dev_err(dev, "unable to allocate msg queue, aborting");
			goto error_dq;
		}
		if (snsocc_alloc_queue(dev, &occ->hwcq_page, &occ->hwcq_dma,
							OCC_CQ_SIZE)) {
			dev_err(dev,
				"unable to allocate command queue, aborting");
			goto error_dq;
		}
		if (snsocc_alloc_queue(dev, &occ->hwdq_page, &occ->hwdq_dma,
							OCC_DQ_SIZE)) {
			dev_err(dev,
				"unable to allocate hw data queue, aborting");
			goto error_dq;
		}
		occ->imq = kmalloc(SW_IMQ_RING_SIZE * sizeof(*occ->imq),
				   GFP_KERNEL);
		if (!occ->imq) {
			dev_err(dev,
				"unable to allocate sw imq queue, aborting");
			goto error_dq;
		}
	}

	occ->tx_buffer = kmalloc(occ->board->tx_fifo_len, GFP_KERNEL);
	if (!occ->tx_buffer) {
		dev_err(dev, "unable to allocate TX buffer, aborting");
		goto error_dq;
	}

	occ->conf = OCC_CONF_RESET;
	occ->irqs = 0;
	snsocc_reset(occ);

	/* Now that we've reset the card, we can enable bus mastering and
	 * be fairly confident it won't scribble over random memory.
	 */
	pci_set_master(pdev);
	pci_set_drvdata(pdev, occ);

	err = cdev_add(&occ->cdev, occ->dev.devt, 1);
	if (err) {
		dev_err(dev, "unable to register device, aborting");
		goto error_dq;
	}

	err = device_add(&occ->dev);
	if (err) {
		dev_err(dev, "unable to add device, aborting");
		goto error_cdev;
	}

	dev_set_drvdata(dev, occ);

	dev_info(dev, "snsocc%d: %s OCC version %08x, datecode %08x (%s IRQ: %u)\n",
		 minor, snsocc_name[board_id],
		 occ->version,
		 occ->firmware_date,
		 occ->msi_enabled ? "MSI" : "legacy",
		 pdev->irq
	);

	return 0;

error_cdev:
	cdev_del(&occ->cdev);
error_dq:
	kfree(occ->tx_buffer);
	kfree(occ->imq);
	snsocc_free_queue(dev, occ->dq_page, occ->dq_dma, OCC_DQ_SIZE);
	snsocc_free_queue(dev, occ->hwcq_page, occ->hwcq_dma, OCC_CQ_SIZE);
	snsocc_free_queue(dev, occ->hwimq_page, occ->hwimq_dma, OCC_IMQ_SIZE);
	snsocc_free_queue(dev, occ->hwdq_page, occ->hwdq_dma, OCC_DQ_SIZE);
error_dev:
	if (occ && occ->board && occ->board->sysfs.attrs)
		sysfs_remove_group(&dev->kobj, &occ->board->sysfs);
	put_device(&occ->dev);
	mutex_lock(&snsocc_devlock);
	snsocc_devs[minor] = NULL;
	mutex_unlock(&snsocc_devlock);
error_stat:
	if (occ->irq_latency.isr_delay)
		kfree(occ->irq_latency.isr_delay);
	if (occ->irq_latency.isr_proctime)
		kfree(occ->irq_latency.isr_proctime);
error:
	return err;
}

static void __devexit snsocc_remove(struct pci_dev *pdev)
{
	/* Very little to do here, since most resources are managed for us. */
	struct occ *occ = pci_get_drvdata(pdev);
	struct device *dev = &pdev->dev;

	if (occ && occ->board && occ->board->sysfs.attrs)
		sysfs_remove_group(&dev->kobj, &occ->board->sysfs);

	iowrite32(0, occ->ioaddr + REG_IRQ_ENABLE);
	iowrite32(OCC_CONF_RESET, occ->ioaddr + REG_CONFIG);
	ioread32(occ->ioaddr + REG_IRQ_ENABLE); // Make sure device got the iowrite32()

	free_irq(pdev->irq, occ);
#ifdef CONFIG_PCI_MSI
	if (occ->msi_enabled != 0)
		pci_disable_msi(pdev);
#endif

	device_del(&occ->dev);
	cdev_del(&occ->cdev);

	snsocc_free_big_queue(occ);
	snsocc_free_queue(dev, occ->dq_page, occ->dq_dma, OCC_DQ_SIZE);
	snsocc_free_queue(dev, occ->hwcq_page, occ->hwcq_dma, OCC_CQ_SIZE);
	snsocc_free_queue(dev, occ->hwimq_page, occ->hwimq_dma, OCC_IMQ_SIZE);
	snsocc_free_queue(dev, occ->hwdq_page, occ->hwdq_dma, OCC_DQ_SIZE);
	kfree(occ->tx_buffer);
	kfree(occ->imq);

	kfree(occ->irq_latency.isr_delay);
	kfree(occ->irq_latency.isr_proctime);

	mutex_lock(&snsocc_devlock);
	snsocc_devs[occ->minor] = NULL;
	mutex_unlock(&snsocc_devlock);

	put_device(&occ->dev);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,8,0)
static const struct pci_device_id snsocc_pci_table[] = {
#else
DEFINE_PCI_DEVICE_TABLE(snsocc_pci_table) = {
#endif
	{ PCI_DEVICE(PCI_VENDOR_ID_XILINX, 0x1002), .driver_data = BOARD_SNS_PCIX },
	{ PCI_DEVICE(0x1775,               0x1000), .driver_data = BOARD_GE_PCIE },
	{ PCI_DEVICE(PCI_VENDOR_ID_XILINX, 0x7014), .driver_data = BOARD_SNS_PCIE },
	{ 0, }
};

static struct pci_driver __refdata snsocc_driver = {
	.name = "snsocc",
	.id_table = snsocc_pci_table,
	.probe = snsocc_probe,
	.remove = snsocc_remove,
};

static int __init snsocc_init(void)
{
	int err;

	snsocc_class = class_create(THIS_MODULE, "snsocc");
	err = PTR_ERR(snsocc_class);
	if (IS_ERR(snsocc_class))
		goto error;

	err = alloc_chrdev_region(&snsocc_basedev, 0, OCC_MAX_DEVS, "snsocc");
	if (err)
		goto error_class;

	err = pci_register_driver(&snsocc_driver);
	if (err)
		goto error_chrdev;

	printk("SNS OCC driver ver %s loaded\n", OCC_VER_STR);

	return 0;

error_chrdev:
	unregister_chrdev_region(snsocc_basedev, OCC_MAX_DEVS);
error_class:
	class_destroy(snsocc_class);
error:
	return err;
}

static void __exit snsocc_exit(void)
{
	pci_unregister_driver(&snsocc_driver);
	unregister_chrdev_region(snsocc_basedev, OCC_MAX_DEVS);
	class_destroy(snsocc_class);
}

module_init(snsocc_init);
module_exit(snsocc_exit);

MODULE_AUTHOR("David Dillow <dillowda@ornl.gov>");
MODULE_VERSION(OCC_VER_STR);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SNS Optical Communication Board for Neutron Detectors");
