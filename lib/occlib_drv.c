/*
 * Copyright (c) 2018 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 *
 * @author Klemen Vodopivec <vodopiveck@ornl.gov>
 */

#include "occlib_hw.h"
#include "occlib_drv.h"
#include "i2c.h"
#include <sns-occ.h>
#include <stdio.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define OCC_HANDLE_MAGIC        0x0cc0cc
#ifndef ROLLOVER_BUF_SIZE
#    define ROLLOVER_BUF_SIZE   8192    // Depends on maximum packet size, we need at most 2 times max packet size
#endif

#define OCC_PCIE_I2C_ADDR0              0xA0
#define OCC_PCIE_I2C_SFP_TYPE           8
#define OCC_PCIE_I2C_SFP_PARTNO_START   40
#define OCC_PCIE_I2C_SFP_PARTNO_END     59
#define OCC_PCIE_I2C_SFP_SERNO_START    68
#define OCC_PCIE_I2C_SFP_SERNO_END      83
#define OCC_PCIE_I2C_ADDR2              0xA2
#define OCC_PCIE_I2C_SFP_TEMP           96
#define OCC_PCIE_I2C_SFP_VCC_POWER      98
#define OCC_PCIE_I2C_SFP_TX_BIAS_CUR    100
#define OCC_PCIE_I2C_SFP_TX_POWER       102
#define OCC_PCIE_I2C_SFP_RX_POWER       104

#define xstr(s) str(s)
#define str(s) #s
#define MIN(a,b) ((a)>(b)?(b):(a))

struct occ_handle {
    uint32_t magic;
    int fd;
    void *dma_buf;
    struct {
        void *addr;
        uint32_t len;
    } bars[3];;
    uint32_t dma_buf_len;
    uint32_t dma_cons_off;
    uint8_t use_optic;
    uint8_t *last_addr;
    uint32_t last_count;                        //<! Number of bytes available returned by the last occ_data_wait()
    uint8_t *rollover_buf;
    uint32_t rollover_size;
    bool debug_mode;
    bool rx_enabled;

#ifdef TX_DUMP_PATH
    int tx_dump_fd;
#endif
#ifdef RX_DUMP_PATH
    int rx_dump_fd;
    uint32_t rx_dump_cons_off;
#endif
};

#pragma pack(push)
#pragma pack(4)
struct occ_packet_header {
    uint32_t destination;       //<! Destination id
    uint32_t source;            //<! Sender id
    uint32_t info;              //<! field describing packet type and other info
    uint32_t payload_length;    //<! payload length
    uint32_t reserved1;
    uint32_t reserved2;
};
#pragma pack(pop)

int _occdrv_open_common(const char *devfile, int flags, struct occ_handle **handle) {
    int ret;
    struct occ_version ver;

    *handle = malloc(sizeof(struct occ_handle));
    if (*handle == NULL)
        return -ENOMEM;

    memset(*handle, 0, sizeof(struct occ_handle));
    (*handle)->magic = OCC_HANDLE_MAGIC;
    (*handle)->dma_buf = MAP_FAILED;

    do {
        (*handle)->fd = open(devfile, flags);
        if ((*handle)->fd == -1) {
            ret = -errno;
            break;
        }

        ret = pread((*handle)->fd, &ver, sizeof(ver), OCC_CMD_VERSION);
        if (ret == -1 && errno != EINVAL) {
            ret = -errno;
            break;
        }

        if (ret != sizeof(ver) || ver.major != OCC_VER_MAJ || ver.minor != OCC_VER_MIN) {
            ret = -EPROTO;
            break;
        }

        if (flags & O_EXCL)
            (*handle)->debug_mode = true;

        (*handle)->rollover_buf = malloc(ROLLOVER_BUF_SIZE);
        if ((*handle)->rollover_buf == NULL) {
            ret = -ENOMEM;
            break;
        }
        (*handle)->rollover_size = ROLLOVER_BUF_SIZE;
        return 0;
    } while (0);

    free(*handle);
    *handle = NULL;
    return ret;
}

int occdrv_open(const char *devfile, occ_interface_type type, struct occ_handle **handle) {
    int ret = 0;
    struct occ_status info;

    do {
        if (type != OCC_INTERFACE_OPTICAL && type != OCC_INTERFACE_LVDS) {
            ret = -EINVAL;
            break;
        }

        ret = _occdrv_open_common(devfile, O_EXCL | O_RDWR, handle);
        if (ret != 0)
            break;

        ret = pread((*handle)->fd, &info, sizeof(info), OCC_CMD_GET_STATUS);
        if (ret != sizeof(info)) {
            if (ret < 0)
                ret = -errno;
            else
                ret = -ENODATA;
            break;
        }
        if (info.occ_ver != OCC_VER) {
            ret = -ENOMSG;
            break;
        }
        (*handle)->dma_buf_len = info.dq_size;
        (*handle)->use_optic = (type == OCC_INTERFACE_OPTICAL);

        (*handle)->dma_buf = (void *)mmap(NULL, (*handle)->dma_buf_len,
                                             PROT_READ|PROT_WRITE, MAP_SHARED|MAP_POPULATE,
                                             (*handle)->fd, 6*sysconf(_SC_PAGESIZE));
        if ((*handle)->dma_buf == MAP_FAILED) {
            ret = -errno;
            break;
        }
        (*handle)->last_addr = (*handle)->dma_buf;

        /* Reset the card to select our preferred interface */
        ret = occdrv_reset(*handle);

#ifdef TX_DUMP_PATH
        (*handle)->tx_dump_fd = open(xstr(TX_DUMP_PATH), O_WRONLY | O_CREAT | O_TRUNC | O_SYNC, (mode_t)0666);
        if ((*handle)->tx_dump_fd == -1) {
            ret = -ENODATA;
            break;
        }
#endif
#ifdef RX_DUMP_PATH
        (*handle)->rx_dump_cons_off = 0;
        (*handle)->rx_dump_fd = open(xstr(RX_DUMP_PATH), O_WRONLY | O_CREAT | O_TRUNC | O_SYNC, (mode_t)0666);
        if ((*handle)->rx_dump_fd == -1) {
            ret = -ENODATA;
            break;
        }
#endif

    } while (0);

    if (ret != 0 && *handle) {

        if ((*handle)->dma_buf != MAP_FAILED)
            munmap((void *)(*handle)->dma_buf, (*handle)->dma_buf_len);

        if ((*handle)->fd != -1)
            close((*handle)->fd);

        free(*handle);
        *handle = NULL;
    }

    return ret;
}

int occdrv_open_debug(const char *devfile, occ_interface_type type, struct occ_handle **handle) {
    return _occdrv_open_common(devfile, O_RDWR, handle);
}

int occdrv_close(struct occ_handle *handle) {
    int ret = 0;

    if (handle != NULL && handle->magic == OCC_HANDLE_MAGIC) {

        // XXX: call reset?

#ifdef TX_DUMP_PATH
        close(handle->tx_dump_fd);
#endif
#ifdef RX_DUMP_PATH
        close(handle->rx_dump_fd);
#endif

        if (munmap((void *)handle->dma_buf, handle->dma_buf_len) != 0)
            ret = -1 * errno;

        if (close(handle->fd) != 0)
            ret = -errno;

        if (handle->rollover_buf)
            free(handle->rollover_buf);

        free(handle);
    }

    return ret;
}

int occdrv_enable_rx(struct occ_handle *handle, bool enable) {
    uint32_t val = (enable ? 1 : 0);

    if (handle == NULL || handle->magic != OCC_HANDLE_MAGIC)
        return -EINVAL;

    if (enable != handle->rx_enabled) {

        if (enable) {
            int ret = occdrv_reset(handle);
            if (ret != 0)
                return ret;
        }

        if (pwrite(handle->fd, &val, sizeof(val), OCC_CMD_RX_ENABLE) < 0)
            return -errno;

        handle->rx_enabled = enable;
    }

    return 0;
}

int occdrv_enable_old_packets(struct occ_handle *handle, bool enable) {
    uint32_t val = (enable ? 1 : 0);
    bool rx_enabled = handle->rx_enabled;
    int ret;

    if (handle == NULL || handle->magic != OCC_HANDLE_MAGIC)
        return -EINVAL;

    if ((ret = occdrv_enable_rx(handle, false)) != 0)
        return ret;

    if (pwrite(handle->fd, &val, sizeof(val), OCC_CMD_OLD_PKTS_EN) < 0)
        return -errno;

    return occdrv_enable_rx(handle, rx_enabled);
}

int occdrv_enable_error_packets(struct occ_handle *handle, bool enable) {
    uint32_t val = (enable ? 1 : 0);

    if (handle == NULL || handle->magic != OCC_HANDLE_MAGIC)
        return -EINVAL;

    if (pwrite(handle->fd, &val, sizeof(val), OCC_CMD_ERR_PKTS_ENABLE) < 0)
        return -errno;

    return 0;
}

int occdrv_status(struct occ_handle *handle, occ_status_t *status, occ_status_type type) {
    struct occ_status info;
    int ret;

    if (handle == NULL || handle->magic != OCC_HANDLE_MAGIC || status == NULL)
        return -EINVAL;

    if (pread(handle->fd, &info, sizeof(info), OCC_CMD_GET_STATUS) < 0)
        return -errno;

    status->dma_size = info.dq_size;
    status->dma_used = info.dq_used;
    status->rx_rate = info.rx_rate;
    status->board = (info.board_type == BOARD_SNS_PCIE ? OCC_BOARD_PCIE : OCC_BOARD_PCIX);
    status->stalled = (info.status & OCC_DMA_STALLED);
    status->overflowed = (info.status & OCC_FIFO_OVERFLOW);
    status->interface = (handle->use_optic ? OCC_INTERFACE_OPTICAL : OCC_INTERFACE_LVDS);
    status->hardware_ver = info.hardware_ver;
    status->firmware_ver = info.firmware_ver;
    status->firmware_date = info.firmware_date;
    status->fpga_serial_number = info.fpga_serial;
    status->rx_enabled = (info.status & OCC_RX_ENABLED);
    status->err_packets_enabled = (info.status & OCC_RX_ERR_PKTS_ENABLED);
    status->err_crc = info.err_crc;
    status->err_frame = info.err_frame;
    status->err_length = info.err_length;
    status->fpga_temp = ( (503.975/65536.0) * info.fpga_temp ) - 273.15;
    status->fpga_core_volt = (3.0/65536.0) * info.fpga_core_volt;
    status->fpga_aux_volt = (3.0/65536.0) * info.fpga_aux_volt;
    if (!(info.status & OCC_OPTICAL_PRESENT))    status->optical_signal = OCC_OPT_NO_SFP;
    else if (info.status & OCC_OPTICAL_FAULT)    status->optical_signal = OCC_OPT_LASER_FAULT;
    else if (info.status & OCC_OPTICAL_NOSIGNAL) status->optical_signal = OCC_OPT_NO_CABLE;
    else                                         status->optical_signal = OCC_OPT_CONNECTED;

    ret = 0;
    while (status->board == BOARD_SNS_PCIE && type == OCC_STATUS_FULL && status->optical_signal != OCC_OPT_NO_SFP) {
        word valWord;
        int i;

        // Set global error, will override at end if all goes well
        ret = -EIO;

        // Get SFP serial number - multiple addresses with 1 ASCII char per address
        memset(status->sfp_serial_number, 0, sizeof(status->sfp_serial_number));
        for (i = OCC_PCIE_I2C_SFP_SERNO_START; i <= OCC_PCIE_I2C_SFP_SERNO_END; i += 2) {
            int j = i - OCC_PCIE_I2C_SFP_SERNO_START;
            if (Read_I2C_Bus(handle, OCC_PCIE_I2C_ADDR0, i, &valWord) == 1 &&
                j < (sizeof(status->sfp_serial_number) - 1)) {
                status->sfp_serial_number[j]   = (valWord & 0xFF00) >> 8;
                status->sfp_serial_number[j+1] = (valWord & 0xFF);
            }
        }

        // Get SFP part number - multiple addresses with 1 ASCII char per address
        memset(status->sfp_part_number, 0, sizeof(status->sfp_part_number));
        for (i = OCC_PCIE_I2C_SFP_PARTNO_START; i <= OCC_PCIE_I2C_SFP_PARTNO_END; i += 2) {
            int j = i - OCC_PCIE_I2C_SFP_PARTNO_START;
            if (Read_I2C_Bus(handle, OCC_PCIE_I2C_ADDR0, i, &valWord) == 1 &&
                j < (sizeof(status->sfp_part_number) - 1)) {
                status->sfp_part_number[j]   = (valWord & 0xFF00) >> 8;
                status->sfp_part_number[j+1] = (valWord & 0xFF);
            }
        }

        // Get SFP type
        if (Read_I2C_Bus(handle, OCC_PCIE_I2C_ADDR0, OCC_PCIE_I2C_SFP_TYPE, &valWord) != 1)
            break;
        if ((valWord & 0xF) == 0x1)
            status->sfp_type = OCC_SFP_MODE_SINGLE;
        else if ((valWord & 0xF) == 0xC)
            status->sfp_type = OCC_SFP_MODE_MULTI;
        else
            status->sfp_type = OCC_SFP_MODE_UNKNOWN;

        // Get SFP temperature
        if (Read_I2C_Bus(handle, OCC_PCIE_I2C_ADDR2, OCC_PCIE_I2C_SFP_TEMP, &valWord) != 1)
            break;
        status->sfp_temp = (float)valWord / 256.0;

        // Get SFP RX In Power
        if (Read_I2C_Bus(handle, OCC_PCIE_I2C_ADDR2, OCC_PCIE_I2C_SFP_RX_POWER, &valWord) != 1)
            break;
        status->sfp_rx_power = 0.1 * valWord;

        // Get SFP TX Power
        if (Read_I2C_Bus(handle, OCC_PCIE_I2C_ADDR2, OCC_PCIE_I2C_SFP_TX_POWER, &valWord) != 1)
            break;
        status->sfp_tx_power = 0.1 * valWord;

        // Get SFP Vcc Power
        if (Read_I2C_Bus(handle, OCC_PCIE_I2C_ADDR2, OCC_PCIE_I2C_SFP_VCC_POWER, &valWord) != 1)
            break;
        status->sfp_vcc_power = 0.0001 * valWord;

        // Get SFP Tx Bias Current
        if (Read_I2C_Bus(handle, OCC_PCIE_I2C_ADDR2, OCC_PCIE_I2C_SFP_TX_BIAS_CUR, &valWord) != 1)
            break;
        status->sfp_tx_bias_cur = 2.0 * valWord;

        ret = 0;
        break;
    }

    return ret;
}

int occdrv_reset(struct occ_handle *handle) {
    uint32_t interface;
    struct occ_status info;

    if (handle == NULL || handle->magic != OCC_HANDLE_MAGIC)
        return -EINVAL;

    interface = (handle->use_optic == 0) ? OCC_SELECT_LVDS : OCC_SELECT_OPTICAL;
    if (pwrite(handle->fd, &interface, sizeof(interface), OCC_CMD_RESET) != sizeof(interface))
        return -errno;

    // Read status to clear the reset-occurred flag
    if (pread(handle->fd, &info, sizeof(info), OCC_CMD_GET_STATUS) < 0)
        return -errno;
    // XXX verify the returned status?

    handle->dma_cons_off = 0;
    handle->rx_enabled = false;

    return 0;
}

static size_t _occdrv_data_align(size_t size) {
    return (size + 3) & ~3;
}

int occdrv_send(struct occ_handle *handle, const void *data, size_t count) {
    if (handle == NULL || handle->magic != OCC_HANDLE_MAGIC || _occdrv_data_align(count) != count)
        return -EINVAL;

    int ret = pwrite(handle->fd, (const void *)data, count, OCC_CMD_TX);
    if (ret < 0)
        ret = -errno;

#ifdef TX_DUMP_PATH
    write(handle->tx_dump_fd, data, count);
#endif

    return ret;
}

static bool _timeout_expired(uint32_t *timeout, struct timespec *t1, struct timespec *t2) {
    if (t2->tv_sec < t1->tv_sec)
        *timeout = 0;

    if (*timeout > 0) {
        if (t2->tv_nsec < t1->tv_nsec && t1->tv_sec == t2->tv_sec) {
            *timeout = 0;
        } else {
            uint32_t diff = (1000 * (t2->tv_sec - t1->tv_sec)) + (t2->tv_nsec - t1->tv_nsec)/1000000;
            if (diff < *timeout)
                *timeout -= diff;
            else
                *timeout = 0;
        }
    }
    return (*timeout == 0);
}

int occdrv_data_wait(struct occ_handle *handle, void **address, size_t *count, uint32_t timeout) {
    int ret;
    uint32_t info[2];
    struct timespec t1,t2;
    void *last_addr = NULL;

    if (handle == NULL || handle->magic != OCC_HANDLE_MAGIC)
        return -EINVAL;

    *address = handle->dma_buf;
    *count = 0;

    // Block until some data is available
    while (1) {
        if (timeout > 0) {
            struct pollfd pollfd;
            pollfd.fd = handle->fd;
            pollfd.events = POLLIN;
            clock_gettime(CLOCK_MONOTONIC, &t1);
            ret = poll(&pollfd, 1, timeout);
            if (ret < 0)
                return -errno;
            else if (ret == 0)
                return -ETIME;
            else if (pollfd.revents & POLLERR)
                return -ECONNRESET;
            else if ( !(pollfd.revents & POLLIN) )
                return -ETIME;
            // Ignore POLLHUP, instead do a read which will give us more
            // information about the error.
        }

        ret = pread(handle->fd, info, sizeof(info), OCC_CMD_RX);
        if (ret < 0)
            return -errno;

        if (!(info[1] & OCC_RX_MSG)) {
            if (info[1] & OCC_RESET_OCCURRED)
                return -ECONNRESET;
            if (info[1] & OCC_DMA_STALLED)
                return -ENOSPC;
            if (info[1] & OCC_FIFO_OVERFLOW)
                return -EOVERFLOW;
            if (timeout > 0) {
                clock_gettime(CLOCK_MONOTONIC, &t2);
                if (_timeout_expired(&timeout, &t1, &t2))
                    return -ETIME;
            }
            // No data in queue, wait for more
            continue;
        }

        // There are a couple of assumptions here that we take for granted.
        // * Producer index is always 4-byte aligned
        // * Producer index is always OCC packet aligned

        uint32_t dma_prod_off = info[0];
        if (dma_prod_off >= handle->dma_cons_off) {
            *address = handle->dma_buf + handle->dma_cons_off;
            *count = dma_prod_off - handle->dma_cons_off;
            last_addr = *address;
        } else {
            // This is the tricky part. Producer has rolled-over already and we need
            // to figure out what to do next. If there's enough data to process, let
            // application process it. But when we get closer to end of buffer and
            // the last packet is likely split, we may prefer to use roll-over
            // buffer instead. Too bad it's dynamic as the code gets more complicated.
            if (handle->dma_buf_len <= handle->dma_cons_off)
                return -ERANGE;

            *address = handle->dma_buf + handle->dma_cons_off;
            *count = handle->dma_buf_len - handle->dma_cons_off;
            last_addr = *address;

            // Client is telling us he can't process any data, probably packet is split.
            if (handle->last_addr == *address && *count < handle->rollover_size) {
                uint32_t headlen = *count;
                uint32_t taillen = MIN(handle->rollover_size - *count, dma_prod_off);

                memcpy(handle->rollover_buf, *address, headlen);
                memcpy(&handle->rollover_buf[headlen], handle->dma_buf, taillen);
                *address = handle->rollover_buf;
                *count = headlen + taillen;
            }
        }

        if (_occdrv_data_align(*count) != *count) {
            // Tough choice. We can't extend the count as the data might not be there yet.
            // So we must shrink the count to previous 4-byte boundary.
            *count = _occdrv_data_align(*count - 3);
        }

        if (*count != 0)
            break;

        if (timeout != 0) {
            clock_gettime(CLOCK_MONOTONIC, &t2);
            if (_timeout_expired(&timeout, &t1, &t2))
                return -ETIME;
        }
    }

    handle->last_count = *count;
    handle->last_addr = last_addr;

#ifdef RX_DUMP_PATH
    if (dma_prod_off >= handle->rx_dump_cons_off) {
        write(handle->rx_dump_fd, handle->dma_buf + handle->rx_dump_cons_off, dma_prod_off - handle->rx_dump_cons_off);
    } else {
        write(handle->rx_dump_fd, handle->dma_buf + handle->rx_dump_cons_off, handle->dma_buf_len - handle->rx_dump_cons_off);
        write(handle->rx_dump_fd, handle->dma_buf, dma_prod_off);
    }
    handle->rx_dump_cons_off = dma_prod_off;
#endif

    return 0;
}

int occdrv_data_ack(struct occ_handle *handle, size_t count) {

    if (handle == NULL || handle->magic != OCC_HANDLE_MAGIC || _occdrv_data_align(count) != count)
        return -EINVAL;

    if (count == 0)
        return 0;

    if (count > handle->last_count)
        count = handle->last_count;

    uint32_t length = count;
    if (pwrite(handle->fd, &length, sizeof(length), OCC_CMD_ADVANCE_DQ) < 0)
        return -errno;

    handle->dma_cons_off = (handle->dma_cons_off + count) % handle->dma_buf_len;
    return 0;
}

int occdrv_read(struct occ_handle *handle, void *data, size_t count, uint32_t timeout) {
    void *address;
    size_t avail;
    int ret;

    ret = occdrv_data_wait(handle, &address, &avail, timeout);
    if (ret != 0)
        return ret;

    if (count > avail)
        count = avail;
    memcpy(data, address, count);

    if (handle->debug_mode == false) {
        ret = occdrv_data_ack(handle, count);
        if (ret != 0)
            return ret;
    }

    return count;
}

static int _occdrv_map_bar(struct occ_handle *handle, uint8_t bar) {

    if (handle->bars[bar].addr == NULL) {
        struct occ_status info;

        if (bar >= (sizeof(info.bars)/sizeof(info.bars[0]))) {
            return -ENOSYS;
        }

        if (pread(handle->fd, &info, sizeof(info), OCC_CMD_GET_STATUS) != sizeof(info)) {
            return -errno;
        }

        if (info.bars[bar] == 0) {
            return -ENOSYS;
        }

        handle->bars[bar].len = info.bars[bar];
        handle->bars[bar].addr = (void *)mmap(NULL,
                                              handle->bars[bar].len,
                                              PROT_READ | PROT_WRITE,
                                              MAP_SHARED | MAP_POPULATE,
                                              handle->fd,
                                              bar * sysconf(_SC_PAGESIZE));
        if (handle->bars[bar].addr == MAP_FAILED) {
            handle->bars[bar].addr = NULL;
            return -errno;
        }
    }
    return 0;
}

int occdrv_io_read(struct occ_handle *handle, uint8_t bar, uint32_t offset, uint32_t *data, uint32_t count) {
    uint32_t *addr;
    int ret;
    size_t i;

    if (handle == NULL || handle->magic != OCC_HANDLE_MAGIC || offset % 4 != 0)
        return -EINVAL;

    ret = _occdrv_map_bar(handle, bar);
    if (ret != 0)
        return ret;
    if (offset >= handle->bars[bar].len)
        return -EOVERFLOW;

    addr = handle->bars[bar].addr + offset;
    for (i = 0; i < count; i++)
        *data++ = *addr++;

    return count;
}

int occdrv_io_write(struct occ_handle *handle, uint8_t bar, uint32_t offset, const uint32_t *data, uint32_t count) {
    uint32_t *addr;
    int ret;
    size_t i;

    if (handle == NULL || handle->magic != OCC_HANDLE_MAGIC || offset % 4 != 0)
        return -EINVAL;

    ret = _occdrv_map_bar(handle, bar);
    if (ret != 0)
        return ret;
    if (offset >= handle->bars[bar].len)
        return -EOVERFLOW;

    addr = handle->bars[bar].addr + offset;
    for (i = 0; i < count; i++)
        *addr++ = *data++;

    return count;
}

int occdrv_report(struct occ_handle *handle, FILE *outfile) {
    int ret = 0;
    unsigned last_reg = 0x318; // Last register address to print from BAR0, starts at 0
    unsigned i;
    do {
        // Macros are evil. Although defined locally, FILE_WRITE becomes available to all code after this line.
#       define FILE_WRITE(args...) if (fprintf(outfile, args) < 0) { ret = -errno; break; }

        FILE_WRITE("OCC registers:\n");
        for (i = 0; i <= last_reg; i += 4) {
            uint32_t reg;
            if (occdrv_io_read(handle, 0, i, &reg, 1) < 0) {
                FILE_WRITE("  0x%04X: read error\n", i);
            } else {
                FILE_WRITE("  0x%04X: 0x%08X\n", i, reg);
            }
        }
        FILE_WRITE("\n");
        FILE_WRITE("Last data processed:\n");
        if (handle->last_addr == handle->rollover_buf) {
            FILE_WRITE("  rollover buffer\n");
        } else {
            uint32_t offset = (void*)handle->last_addr - handle->dma_buf;
            FILE_WRITE("  DMA offset 0x%08X\n", offset);
        }
        FILE_WRITE("\n");
        FILE_WRITE("DMA buffer:\n");
        for (i = 0; i < handle->dma_buf_len; i+=4) {
            if ((i%16) == 0) {
                if (i > 0) {
                    FILE_WRITE("\n  0x%08X:", i);
                } else {
                    FILE_WRITE("  0x%08X:", i);
                }
            }
            FILE_WRITE(" 0x%08X", ((uint32_t*)handle->dma_buf)[i/4]);
        }
        FILE_WRITE("\n\n");
        FILE_WRITE("Rollover buffer:\n");
        for (i = 0; i < handle->rollover_size; i+=4) {
            if ((i%16) == 0) {
                if (i > 0) {
                    FILE_WRITE("\n  0x%08X:", i);
                } else {
                    FILE_WRITE("  0x%08X:", i);
                }
            }
            FILE_WRITE(" 0x%08X", ((uint32_t*)handle->rollover_buf)[i/4]);
        }
        FILE_WRITE("\n");
    } while (0);
    return ret;
}

