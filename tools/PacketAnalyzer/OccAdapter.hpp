#ifndef OCCADAPTER_HPP
#define OCCADAPTER_HPP

#include <stdint.h>
#include <string>

// Forward declarations
struct occ_handle;

class OccAdapter {
    public:
        OccAdapter(const std::string &devfile);
        ~OccAdapter();

        void reset(bool rx_enable);
        bool isPcie();
        void enablePcieGenerator(uint32_t rate, uint16_t pkt_size);
        uint32_t getDmaSize();
    protected:
        struct occ_handle *m_occ;

        std::string occErrorString(int error);
    private:
        uint32_t m_pcie_generator_rate;
        uint16_t m_pcie_generator_pkt_size;
};

#endif // OCCADAPTER_HPP
