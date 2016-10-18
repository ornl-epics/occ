#include "LabPacket.h"

#include <cstddef>

#define SPECIAL_PIXEL_BIT   0x40000000  // pixel ids above this require special handling
#define EVENT_TOF_MAX       2000000 // <=1s/5Hz, in 100ns units

bool LabPacket::verify(LabPacket::Type &type, uint32_t &errorOffset) const
{
    if (isRtdl()) {
        type = TYPE_RTDL;
        return verifyRtdl(errorOffset);
    } else if (isCommand()) {
        if (cmdinfo.command == CMD_TSYNC) {
            type = TYPE_TSYNC;
            return verifyTsync(errorOffset);
        } else {
            type = TYPE_COMMAND;
            return verifyCmd(errorOffset);
        }
    } else if (isMetaData()) {
        type = TYPE_METADATA;
        return verifyMeta(errorOffset);
    } else if (isNeutronData()) {
        if ((info & 0xFC) == 0x2C) {
            type = TYPE_RAMP;
            return verifyRamp(errorOffset);
        } else {
            type = TYPE_NEUTRONS;
            return verifyNeutrons(errorOffset);
        }
    }
    type = TYPE_UNKNOWN;
    return true;
}

bool LabPacket::verifyRtdl(uint32_t &errorOffset) const
{
    // Frame numbers in upper 8 bits?
    static const uint32_t expected_payload[32] = {
        0, 0, 0, 0, 0, 0, 4, 5,
        6, 7, 8,15,17,24,25,26,
        28,29,30,31,32,33,34,35,
        36,37,38,39,40,41, 1, 2
    };

    if (getPayloadLength() != sizeof(expected_payload)) {
        errorOffset = offsetof(DasPacket, payload);
        return false;
    }

    for (size_t i=6; 4*i < sizeof(expected_payload); i++) {
        if (((payload[i] >> 24) & 0xFF) != expected_payload[i]) {
            errorOffset = 6+i;
            return false;
        }
    }

    return true;
}

bool LabPacket::verifyMeta(uint32_t &errorOffset) const
{
    uint32_t size;
    const DasPacket::Event *event = reinterpret_cast<const DasPacket::Event *>(getData(&size));
    uint32_t nevents = size / (sizeof(DasPacket::Event)/4);

    // Skip checking for RTDL if it exists, start checking at actual neutrons
    errorOffset = (sizeof(DasPacket) + (getPayload() - payload)) / 4;

    if (size % (sizeof(DasPacket::Event)/4)) {
        errorOffset += nevents * sizeof(DasPacket::Event)/4;
        return false;
    }

    while (--nevents) {
        if (event->pixelid & SPECIAL_PIXEL_BIT) {
            switch (event->pixelid & ~0x1) {
            case 0x40010000:
            case 0x40020000:
                if ((event->pixelid & 0x1) != 0x1) {
                    return false;
                }
                break;
            case 0x70010000:
            case 0x70020000:
            case 0x70030000:
                break;
            default:
                return false;
            }
        } else if (event->pixelid < SPECIAL_PIXEL_BIT &&
                   (event->tof > 0x32000 || ((event->pixelid & 0x3FFFFFFF) > 3023))) {
            return false;
        }

        errorOffset += sizeof(DasPacket::Event)/4;
        event++;
    }

    // No errors
    errorOffset = 0;

    return true;
}

bool LabPacket::verifyNeutrons(uint32_t &errorOffset) const
{
    uint32_t size;
    const DasPacket::Event *event = reinterpret_cast<const DasPacket::Event *>(getData(&size));
    uint32_t nevents = size / (sizeof(DasPacket::Event)/4);

    // Skip checking for RTDL if it exists, start checking at actual neutrons
    errorOffset = (sizeof(DasPacket) + (getPayload() - payload)) / 4;

    if (size % (sizeof(DasPacket::Event)/4)) {
        errorOffset += nevents * sizeof(DasPacket::Event)/4;
        return false;
    }

    while (--nevents) {
        if (event->tof > EVENT_TOF_MAX)
            return false;

        // Pixel ID is arbitrary

        errorOffset += sizeof(DasPacket::Event)/4;
        event++;
    }

    // No errors
    errorOffset = 0;

    return true;
}

bool LabPacket::verifyRamp(uint32_t &errorOffset) const
{
    uint32_t size;
    const DasPacket::Event *event = reinterpret_cast<const DasPacket::Event *>(getData(&size));
    uint32_t nevents = size / (sizeof(DasPacket::Event)/4);
    static uint32_t lastValue = (uint32_t)-1;

    if (lastValue == (uint32_t)-1)
        lastValue = event->tof;

    for (uint32_t i = 0; i < nevents; i++, event++) {
        if (event->tof != lastValue) {
            errorOffset = i;
            lastValue = -1;
            return false;
        } else {
            lastValue++;
            lastValue &= 0xFFFFFFF;
        }

        if (event->pixelid != lastValue) {
            errorOffset = i;
            lastValue = -1;
            return false;
        } else {
            lastValue++;
            lastValue &= 0xFFFFFFF;
        }
    }

    return true;
}

bool LabPacket::verifyTsync(uint32_t &errorOffset) const
{
    return (getPayloadLength() == 2*sizeof(uint32_t));
}

bool LabPacket::verifyCmd(uint32_t &errorOffset) const
{
    return true;
}
