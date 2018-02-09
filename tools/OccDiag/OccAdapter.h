#ifndef OCCADAPTER_HPP
#define OCCADAPTER_HPP

#include "LabPacket.h"

#include <stdint.h>
#include <string>
#include <map>
#include <vector>

// Forward declarations
struct occ_handle;

class OccAdapter {
    public:
        struct AnalyzeStats {
            std::map<LabPacket::PacketType, uint64_t> good;
            std::map<LabPacket::PacketType, uint64_t> bad;
            std::map<LabPacket::PacketType, uint64_t> bytes;
            const void *lastAddr;
            const void *lastErrorAddr;
            const void *lastPacketAddr;
            size_t lastLen;
            void clear()
            {
                good.clear();
                bad.clear();
                bytes.clear();
            }
        };

        OccAdapter(const std::string &devfile, bool oldpkts, const std::map<uint32_t, uint32_t> &initRegisters);
        ~OccAdapter();

        void reset();

        /**
         * Enable or disable RX in OCC FPGA.
         */
        void toggleRx(bool enable);

        /**
         * Return address and length of DMA memory.
         *
         * Is fast, using pre-cached values.
         */
        void getDmaInfo(const void **addr, size_t &size);

        /**
         * Retrieves OCC status and returns, throws on error.
         */
        void getOccStatus(size_t &dmaUsed, bool &stalled, bool &overflowed);

        /**
         * Return all available register offset,value pairs.
         */
        std::map<uint32_t, uint32_t> getRegisters();

        /**
         * Take as much data as available in DMA and process it.
         *
         * @param[out] stats Received and analyzed packet statistics
         * @param[in] throwOnBad Stops processing on bad packet and throws bad_exception
         * @param[in] timeout in seconds to wait for data if not available immediately
         */
        void process(AnalyzeStats &stats, bool throwOnBad, double timeout=1.0);
    protected:
        struct occ_handle *m_occ;

        /**
         * Return string describing OCC error.
         */
        std::string occErrorString(int error);

        /**
         * Apply register configuration as specified in constructor.
         */
        void setupRegisters();
    private:
        std::map<uint32_t, uint32_t> m_initRegisters;
        bool m_isPcie;

        void *m_dmaAddr;
        size_t m_dmaSize;
};

#endif // OCCADAPTER_HPP
