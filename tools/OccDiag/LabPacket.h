#ifndef LABPACKET_HPP
#define LABPACKET_HPP

#include "DasPacket.h"

class LabPacket : public DasPacket
{
    public:
        typedef enum {
            TYPE_UNKNOWN,
            TYPE_COMMAND,
            TYPE_METADATA,
            TYPE_NEUTRONS,
            TYPE_RAMP,
            TYPE_RTDL,
            TYPE_TSYNC,
        } Type;

        /**
         * Check the validity of packet and its data.
         *
         * Depending on the packet type, different payload checks are performed.
         * @param[out] type Packet type, returned regarless whether packet is good or bad, unless it's too damaged to resolve the type
         * @param[out] errorOffset dword (4 byte) offset of the error, starting from packet address
         */
        bool verify(Type &type, uint32_t &errorOffset) const;

        /**
         * Reset ramp counter.
         */
        static void resetRamp();

    private:
        static uint32_t lastRampValue;

        bool verifyRtdl(uint32_t &errorOffset) const;
        bool verifyMeta(uint32_t &errorOffset) const;
        bool verifyNeutrons(uint32_t &errorOffset) const;
        bool verifyRamp(uint32_t &errorOffset) const;
        bool verifyTsync(uint32_t &errorOffset) const;
        bool verifyCmd(uint32_t &errorOffset) const;
};

#endif // LABPACKET_HPP
