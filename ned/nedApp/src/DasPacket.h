#ifndef DASPACKET_HPP
#define DASPACKET_HPP

#include <stdint.h>

struct DasNeutronEvent {
	uint32_t tof;
	uint32_t pixelid;
};

/**
 * Class representing a DAS packet.
 *
 * Don't introduce any virtual functions here as this will break allocating
 * objects from OCC buffer.
 */
struct DasPacket
{
    public:
        static const uint32_t MinLength = 6*4;
        static const uint32_t MaxLength = 1800*8;

        uint32_t destination;       //<! Destination id
        uint32_t source;            //<! Sender id
        uint32_t info;              //<! field describing packet type and other info
        uint32_t payload_length;    //<! payload length
        uint32_t reserved1;
        uint32_t reserved2;
        uint32_t data[0];

        DasPacket(uint32_t datalen);

        uint32_t length() const;
        uint32_t payloadLength() const;

        bool isCommand() const;
        bool isData() const;
        bool isDataRtdl() const;
        bool isDataMeta() const;
        bool isDataEvent() const;
};

#endif // DASPACKET_HPP
