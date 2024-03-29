/*
 * Copyright (c) 2018 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 *
 * @author Klemen Vodopivec
 */

/*
 * Copyright (c) 2018 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 *
 * @author Klemen Vodopivec <vodopiveck@ornl.gov>
 */

#include "OccAdapter.h"
#include "Packet.h"
#include "DasPacket.h"

#include <errno.h>
#include <cstring> // strerror

#include <occlib_hw.h>
#include <stdexcept>

OccAdapter::OccAdapter(const std::string &devfile, bool oldpkts, const std::map<uint32_t, uint32_t> &initRegisters)
    : m_occ(NULL)
    , m_initRegisters(initRegisters)
    , m_oldPkts(oldpkts)
{
    int ret;
    occ_status_t status;

    if ((ret = occ_open(devfile.c_str(), OCC_INTERFACE_OPTICAL, &m_occ)) != 0)
        throw std::runtime_error("Failed to open OCC device - " + occErrorString(ret));

    if ((ret = occ_enable_old_packets(m_occ, oldpkts)) != 0) {
        std::string enable = (oldpkts?"enable":"disable");
        throw std::runtime_error("Failed to " + enable + " old DAS packets - " + occErrorString(ret));
    }

    if ((ret = occ_status(m_occ, &status, OCC_STATUS_FAST)) != 0)
        throw std::runtime_error("Failed to initialize OCC status");

    // OCC is reset when connection is opened, the first read of data should
    // give the address of DMA memory
    ret = occ_data_wait(m_occ, &m_dmaAddr, &m_dmaSize, 1);
    if (ret != 0 && ret != -ETIME)
        throw std::runtime_error("Failed to read DMA info");
    m_dmaSize = status.dma_size;
    m_isPcie = (status.board == OCC_BOARD_PCIE);

    setupRegisters();
}

OccAdapter::~OccAdapter()
{
    if (m_occ)
        occ_close(m_occ);
}

void OccAdapter::reset()
{
    if (occ_reset(m_occ) != 0)
        throw std::runtime_error("Failed to reset OCC board");

    setupRegisters();
}

void OccAdapter::setupRegisters()
{
    uint8_t bar0 = 0;
    for (auto it=m_initRegisters.begin(); it!=m_initRegisters.end(); it++) {
        if (occ_io_write(m_occ, bar0, it->first, &it->second, 1) != 1)
            throw std::runtime_error("Failed to write register");
    }
}

std::string OccAdapter::occErrorString(int error)
{
    if (error > 0)
        error = 0;
    return std::string(strerror(-error));
}

void OccAdapter::process(OccAdapter::AnalyzeStats &stats, bool throwOnBad, double timeout)
{
    int ret;
    void *data;
    size_t dataLen;
    uint32_t timeoutMsec = timeout * 1e3;
    uint32_t nPackets = 0;
    bool sameAddr;

    if ((ret = occ_data_wait(m_occ, &data, &dataLen, timeoutMsec)) != 0) {
        stats.lastLen = 0;
        if (ret == -ETIME)
            return;
        //mvwprintw(winstats, 0, 70, "ERR %d", ret);
        throw std::runtime_error("Can't receive data - " + occErrorString(ret));
    }

    sameAddr = (data == stats.lastAddr);
    stats.lastAddr = data;
    stats.lastLen = dataLen;
    stats.lastErrorAddr = 0;
    stats.lastPacketAddr = data;
    stats.lastPacketSize = 0;
    while (dataLen >= sizeof(Packet)) {
        bool good = false;
        uint32_t errorOffset;
        uint32_t packetLen = 0;
        Packet::Type type;

        try {
            if (!m_oldPkts) {
                const Packet *packet = Packet::cast(static_cast<uint8_t*>(data), dataLen);
                if (packet->version != 1) {
                    throw std::runtime_error("Not version 1");
                }
                good = packet->verify(errorOffset);
                packetLen = packet->length;
                type = packet->type;
            } else {
                const DasPacket *packet = DasPacket::cast(static_cast<uint8_t*>(data), dataLen);
                good = true; // No checks on legacy packets
                packetLen = sizeof(DasPacket) + packet->payload_length;
                type = Packet::TYPE_LEGACY;
            }
            nPackets++;
        } catch (std::runtime_error &e) {
            // Gracefully handle buffer roll-over case, but throw on real error
            if (sameAddr)
                throw;
            break;
        }

        stats.lastPacketAddr = data;
        stats.lastPacketSize = packetLen;

        if (good) {
            stats.good[type]++;
        } else {
            stats.bad[type]++;
            if (throwOnBad) {
                stats.lastErrorAddr = (char *)data + 4*errorOffset;
                throw std::bad_exception();
            }
        }
        stats.bytes[type] += packetLen;

        dataLen -= packetLen;
        data = ((char*)data + packetLen);
    }

    if ((ret = occ_data_ack(m_occ, stats.lastLen - dataLen)) != 0)
        throw std::runtime_error("Can't acknowlege data - " + occErrorString(ret));
}

void OccAdapter::toggleRx(bool enable)
{
    int ret = occ_enable_rx(m_occ, enable);
    if (ret != 0)
        throw std::runtime_error("Can't toggle RX - " + occErrorString(ret));

    // Packet generator must be enabled after RX
    if (m_initRegisters.find(0x380) != m_initRegisters.end() ||
        m_initRegisters.find(0x384) != m_initRegisters.end()) {

        uint8_t bar0 = 0;
        uint32_t val;
        uint32_t config_reg = 0x4;
        if (occ_io_read(m_occ, bar0, config_reg, &val, 1) != 1)
            throw std::runtime_error("Failed to read existing register configuration");
        val |= (0x1 << 7) | (0x1 << 8);
        if (occ_io_write(m_occ, bar0, config_reg, &val, 1) != 1)
            throw std::runtime_error("Failed to write register configuration");
    }
}

void OccAdapter::getDmaInfo(const void **addr, size_t &size)
{
    *addr = m_dmaAddr;
    size = m_dmaSize;
}

void OccAdapter::getOccStatus(size_t &used, bool &stalled, bool &overflowed)
{
    occ_status_t status;
    int ret;

    if ((ret = occ_status(m_occ, &status, OCC_STATUS_FAST)) != 0)
        throw std::runtime_error("Can't get OCC status - " + occErrorString(ret));

    used = status.dma_used;
    stalled = status.stalled;
    overflowed = status.overflowed;
}

std::map<uint32_t, uint32_t> OccAdapter::getRegisters()
{
    std::map<uint32_t, uint32_t> registers;

    static uint32_t pcieOffsets[] = {
        0x0, 0x4, 0x8, 0x14, 0x18, 0x1C, 0x70, 0x74, 0x80, 0x84, 0x88, 0x90,
        0x94, 0x98, 0xC0, 0xC4, 0xC8, 0x100, 0x120, 0x124, 0x180, 0x184, 0x188,
        0x300, 0x304, 0x308, 0x30C, 0x310, 0x314, 0x318, 0x320, 0x380, 0x384,
    };

    if (!m_isPcie)
        throw std::runtime_error("TODO: registers for PCI-X");

    uint32_t *offsets = pcieOffsets;
    uint32_t nOffsets = sizeof(pcieOffsets) / sizeof(uint32_t);

    for (uint32_t i=0; i<nOffsets; i++) {
        int ret;
        uint32_t value;
        if ((ret = occ_io_read(m_occ, 0, offsets[i], &value, 1)) != 1)
            throw std::runtime_error("Failed to read registers - " + occErrorString(ret));

        registers[offsets[i]] = value;
    }
    return registers;
}
