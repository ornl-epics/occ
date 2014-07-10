#include "OccAdapter.hpp"

#include <errno.h>
#include <cstring> // strerror

#include <occlib_hw.h>
#include <stdexcept>

using namespace std;

OccAdapter::OccAdapter(const string &devfile) :
    m_occ(NULL),
    m_pcie_generator_rate(0)
{
    int ret = occ_open(devfile.c_str(), OCC_INTERFACE_OPTICAL, &m_occ);
    if (ret != 0)
        throw runtime_error("Failed to open OCC device - " + occErrorString(ret));
}

OccAdapter::~OccAdapter()
{
    if (m_occ)
        occ_close(m_occ);
}

bool OccAdapter::isPcie()
{
    occ_status_t status;
    if (occ_status(m_occ, &status, false) != 0)
        throw runtime_error("Failed to read OCC status");

    return (status.board == OCC_BOARD_PCIE);
}

void OccAdapter::reset(bool rx_enable)
{
    if (occ_reset(m_occ) != 0)
        throw runtime_error("Failed to reset OCC board");

    if (rx_enable && occ_enable_rx(m_occ, true) != 0)
        throw runtime_error("Failed to enable data reception");

    if (m_pcie_generator_rate != 0)
        enablePcieGenerator(m_pcie_generator_rate);

}

void OccAdapter::enablePcieGenerator(uint32_t rate)
{
    const uint8_t bar0             = 0;
    const uint32_t config_reg      = 0x4;
    const uint32_t hw_pktsim_reg1  = 0x380;
    const uint32_t hw_pktsim_reg2  = 0x384;
    const uint32_t sim_enable_bits = (0x1 << 7) | (0x1 << 8);
    uint32_t val;

    if (!isPcie())
        throw logic_error("PCI-E data generator not available on this board");

    if (rate != 0.0) {
        val = 0;
        val |= (0x400 << 0);    // Starting packet size is 1024 dwords (=4K)
        val |= (0x400 << 16);   // Create fixed-sized packets
        val |= (0x3 << 28);     // Force neutron-data
        if (occ_io_write(m_occ, bar0, hw_pktsim_reg1, &val, 1) != 1)
            throw runtime_error("Failed to write generated packet configuration");

        val = (uint32_t)(125000000 / rate) & 0x7FF;
        if (occ_io_write(m_occ, bar0, hw_pktsim_reg2, &val, 1) != 1)
            throw runtime_error("Failed to write generated packet rate");
    }

    if (occ_io_read(m_occ, bar0, config_reg, &val, 1) != 1)
        throw runtime_error("Failed to read existing configuration");

    if (rate == 0.0)
        val &= ~sim_enable_bits;
    else
        val |= sim_enable_bits;
    if (occ_io_write(m_occ, bar0, config_reg, &val, 1) != 1)
        throw runtime_error("Failed to write packet generation configuration");

    m_pcie_generator_rate = rate;
}

string OccAdapter::occErrorString(int error)
{
    if (error > 0)
        error = 0;
    return string(strerror(-error));
}

uint32_t OccAdapter::getDmaSize()
{
    occ_status_t status;
    if (occ_status(m_occ, &status, false) == 0)
        return status.dma_size;
    return 0;
}
