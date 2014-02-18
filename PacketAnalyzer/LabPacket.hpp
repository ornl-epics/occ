#ifndef LABPACKET_HPP
#define LABPACKET_HPP

#include "DasPacket.hpp"

struct LabPacket : public DasPacket
{
public:
    bool isDataRamp() const;
    bool verifyRtdl(uint32_t &errorOffset) const;
    bool verifyMeta(uint32_t &errorOffset) const;
    bool verifyEvent(uint32_t &errorOffset) const;
    bool verifyRamp(uint32_t &errorOffset, uint32_t &lastValue) const;
};

#endif // LABPACKET_HPP
