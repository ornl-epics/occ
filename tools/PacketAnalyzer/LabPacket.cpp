#include "LabPacket.hpp"

#define DAS_DATA_MASK                       0xFC
#define DAS_DATA_RAMP                       0x2C
#define MAX_EVENTS                          500         // logical "limit" for badpacket tests
#define SPECIAL_PIXEL_BIT                   0x40000000  // pixel ids above this require special handling
#define EVENT_PIXELID_MAX       3023
#define EVENT_PIXELID_MIN       0
#define EVENT_TOF_MAX           0x32000
#define MAX_EVENTS_PER_PACKET               1800

LabPacket::LabPacket(uint32_t datalen) :
    DasPacket(datalen)
{
}

bool LabPacket::isDataRamp() const
{
    return (info & DAS_DATA_MASK) == DAS_DATA_RAMP;
}

bool LabPacket::verifyRtdl(uint32_t &errorOffset) const
{
    static uint32_t expected_payload[32] = {
        0, 0, 0, 0, 0, 0, 4, 5,
        6, 7, 8,15,17,24,25,26,
        28,29,30,31,32,33,34,35,
        36,37,38,39,40,41, 1, 2
    };

    if ((payload_length >> 2) != 32) {
        errorOffset = (reinterpret_cast<const char *>(&(this->payload_length)) - reinterpret_cast<const char *>(this));
        return false;
    }

    const uint32_t *p = data + 6;
    uint32_t *r = expected_payload + 6;
    for (int i=6; i < 32; i++, p++, r++) {
        if (((*p >> 24) & 0xFF) != *r) {
            errorOffset = i;
            return false;
        }
    }

    return true;
}

bool LabPacket::verifyMeta(uint32_t &errorOffset) const
{
    const struct DasNeutronEvent *event = reinterpret_cast<const struct DasNeutronEvent *>(data);
    uint32_t nevents = payload_length / sizeof(struct DasNeutronEvent);

    if (nevents > MAX_EVENTS) {
        errorOffset = (reinterpret_cast<const char *>(&(this->payload_length)) - reinterpret_cast<const char *>(this));
        return false;
    }

    // DSP-T uses 6 words for RTDL at start
    event += (6 * sizeof(uint32_t) / sizeof(struct DasNeutronEvent));
    nevents -= (6 * sizeof(uint32_t) / sizeof(struct DasNeutronEvent));

    for (uint32_t i=0; i<nevents; i++) {
        if (event->pixelid & SPECIAL_PIXEL_BIT) {
            switch (event->pixelid & ~0x1) {
            case 0x40010000:
            case 0x40020000:
                if ((event->pixelid & 0x1) != 0x1) {
                    errorOffset = i;
                    return false;
                }
                break;
            case 0x70010000:
            case 0x70020000:
            case 0x70030000:
                break;
            default:
                errorOffset = i;
                return false;
            }
        } else if (event->pixelid < SPECIAL_PIXEL_BIT &&
                   (event->tof > 0x32000 || ((event->pixelid & 0x3FFFFFFF) > 3023))) {
            errorOffset = i;
            return false;
        }
    }

    return true;
}

bool LabPacket::verifyEvent(uint32_t &errorOffset) const
{
    const struct DasNeutronEvent *event = reinterpret_cast<const struct DasNeutronEvent *>(data);
    uint32_t nevents = payload_length / sizeof(struct DasNeutronEvent);

    if (nevents > MAX_EVENTS_PER_PACKET)
        return false;

    // DSP-T uses 6 words at start
    event += 3;
    for (uint32_t i = 3; i < nevents; i++, event++) {
        if (event->tof > EVENT_TOF_MAX) {
            errorOffset = i;
            return false;
        }

        if ((event->pixelid & 0x3FFFFFFF) > EVENT_PIXELID_MAX ||
            (event->pixelid & 0x3FFFFFFF) < EVENT_PIXELID_MIN) {
            errorOffset = i;
            return false;
        }
    }

    return true;
}

bool LabPacket::verifyRamp(uint32_t &errorOffset, uint32_t &lastValue) const
{
    const struct DasNeutronEvent *event = reinterpret_cast<const struct DasNeutronEvent *>(data);
    uint32_t nevents = payload_length / sizeof(struct DasNeutronEvent);

    if (nevents > MAX_EVENTS_PER_PACKET) {
        event += nevents - 1;
        lastValue = event->pixelid + 1;
        return false;
    }

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
