#ifndef LABPACKET_HPP
#define LABPACKET_HPP

#include "DasPacket.h"
#include "Packet.h"

class LabPacket
{
    public:
        typedef enum {
            TYPE_LEGACY     = 0x0,
            TYPE_ERROR      = 0x1,
            TYPE_DAS_RTDL   = 0x6,
            TYPE_DAS_DATA   = 0x7,
            TYPE_DAS_CMD    = 0x8,
            TYPE_ACC_TIME   = 0x10,
        } PacketType;

        /**
         * Constructor parses raw data.
         */
        LabPacket(const void *data);

        /**
         * Return total packet length in bytes.
         */
        uint32_t getLength();

        /**
         * Return packet type.
         */
        PacketType getType();

        /**
         * Check the validity of packet and its data.
         *
         * Depending on the packet type, different payload checks are performed.
         * @param[out] type Packet type, returned regarless whether packet is good or bad, unless it's too damaged to resolve the type
         * @param[out] errorOffset dword (4 byte) offset of the error, starting from packet address
         */
        bool verify(uint32_t &errorOffset);

        /**
         * Reset ramp counter.
         */
        static void resetRamp();

    private:
        static uint32_t lastRampValue;
        unsigned version;
        struct DasPacket *dasPacket;
        struct Packet *packet;

        bool verifyRtdl(Packet *packet, uint32_t &errorOffset);
        bool verifyRtdl(DasPacket *packet, uint32_t &errorOffset);
        bool verifyMeta(DasPacket *packet, uint32_t &errorOffset);
        bool verifyMeta(DasDataPacket *packet, uint32_t &errorOffset);
        bool verifyNeutrons(DasPacket *packet, uint32_t &errorOffset);
        bool verifyNeutrons(DasDataPacket *packet, uint32_t &errorOffset);
        bool verifyRamp(DasPacket *packet, uint32_t &errorOffset);
        bool verifyCmd(DasPacket *packet, uint32_t &errorOffset);
};

#endif // LABPACKET_HPP
