#include "LabPacket.h"

#include <cstddef>

#define EVENT_TOF_MAX       2000000 // <=1s/5Hz, in 100ns units

uint32_t LabPacket::lastRampValue = (uint32_t)-1;

// Replacement for C's offsetof - C++ pre-11 doesn't like offsetof on non-PODs
template<typename T, typename U> size_t offsetOf(U T::*member)
{
    return (char*)&((T*)0->*member) - (char*)0;
}

LabPacket::LabPacket(const void *data)
: version(0)
{
    struct Packet *p = reinterpret_cast<struct Packet *>(const_cast<void*>(data));
    if (p->version == 1) {
        version = 1;
        packet = p;
        dasPacket = 0;
    } else {
        version = 0;
        dasPacket = reinterpret_cast<struct DasPacket *>(const_cast<void*>(data));
        packet = 0;
    }
}

uint32_t LabPacket::getLength()
{
    if (packet)
        return packet->length;
    if (dasPacket)
        return dasPacket->length();
    return 0;
}

LabPacket::PacketType LabPacket::getType()
{
    if (packet)
        return static_cast<LabPacket::PacketType>(packet->type);

    if (dasPacket) {
        if (dasPacket->isRtdl())
            return LabPacket::TYPE_DAS_RTDL;
        if (dasPacket->isCommand())
            return LabPacket::TYPE_DAS_CMD;
        if (dasPacket->isData())
            return LabPacket::TYPE_DAS_DATA;
    }
    return LabPacket::TYPE_LEGACY;
}

bool LabPacket::verify(uint32_t &errorOffset)
{
    if (packet) {
        if (packet->type == Packet::TYPE_DAS_RTDL)
            return verifyRtdl(packet, errorOffset);
    }
    if (dasPacket) {
        if (dasPacket->isRtdl()) {
            return verifyRtdl(dasPacket, errorOffset);
        } else if (dasPacket->isCommand()) {
            return verifyCmd(dasPacket, errorOffset);
        } else if (dasPacket->isMetaData()) {
            return verifyMeta(dasPacket, errorOffset);
        } else if (dasPacket->isNeutronData()) {
            return verifyNeutrons(dasPacket, errorOffset);
        } else if ((dasPacket->info & 0xFC) == 0x2C) {
            return verifyRamp(dasPacket, errorOffset);
        }
    }
    return true;
}

bool LabPacket::verifyRtdl(Packet *packet_, uint32_t &errorOffset)
{
    DasRtdlPacket *packet = reinterpret_cast<DasRtdlPacket *>(packet_);
    if (packet->length != (sizeof(DasRtdlPacket) + packet->num_frames*4)) {
        errorOffset = offsetof(Packet, length)/4;
        return false;
    }

    return true;
}

bool LabPacket::verifyRtdl(DasPacket *packet, uint32_t &errorOffset)
{
    // Frame numbers in upper 8 bits?
    static const uint32_t expected_payload[32] = {
        0, 0, 0, 0, 0, 0, 4, 5,
        6, 7, 8,15,17,24,25,26,
        28,29,30,31,32,33,34,35,
        36,37,38,39,40,41, 1, 2
    };

    if (packet->getPayloadLength() != sizeof(expected_payload)) {
        errorOffset = offsetOf(&DasPacket::payload_length)/4;
        return false;
    }

    for (size_t i=6; 4*i < sizeof(expected_payload); i++) {
        if (((packet->payload[i] >> 24) & 0xFF) != expected_payload[i]) {
            errorOffset = 6+i;
            return false;
        }
    }

    return true;
}

bool LabPacket::verifyMeta(DasDataPacket *packet, uint32_t &errorOffset)
{
    struct Event {
        uint32_t tof;
        uint32_t pixelid;
    };

    uint32_t nevents;
    // The next line doesn't compile on RHEL6 with g++ -std=c++0x
    //const Event *event = packet->getEvents<Event>(nevents);
    nevents = (packet->length - sizeof(DasDataPacket)) / sizeof(Event);
    const Event *event = reinterpret_cast<Event *>(packet->events);

    errorOffset = sizeof(DasDataPacket) / 4; // Ugly I know!

    while (--nevents) {
        switch (event->pixelid >> 28) {
            case 0x6:
            case 0x5:
                break;
            case 0x7:
            case 0x4:
                if ((event->pixelid & 0xFFFF) > 3)
                    return false;
                break;
            case 0x3:
            case 0x2:
            case 0x1:
            case 0x0:
            default:
                // Neutrons?
                return false;
        }

        errorOffset += sizeof(Event)/4;
        event++;
    }

    // No errors
    errorOffset = 0;
    return true;
}

bool LabPacket::verifyMeta(DasPacket *packet, uint32_t &errorOffset)
{
    uint32_t size;
    const DasPacket::Event *event = reinterpret_cast<const DasPacket::Event *>(packet->getData(&size));
    uint32_t nevents = size / (sizeof(DasPacket::Event)/4);

    // Skip checking for RTDL if it exists, start checking at actual neutrons
    errorOffset = (sizeof(DasPacket) + (packet->getPayload() - packet->payload)) / 4;

    if (size % (sizeof(DasPacket::Event)/4)) {
        errorOffset += nevents * sizeof(DasPacket::Event)/4;
        return false;
    }

    while (--nevents) {
        switch (event->pixelid >> 28) {
            case 0x6:
            case 0x5:
                break;
            case 0x7:
            case 0x4:
                if ((event->pixelid & 0xFFFF) > 3)
                    return false;
                break;
            case 0x3:
            case 0x2:
            case 0x1:
            case 0x0:
            default:
                // Neutrons?
                return false;
        }

        errorOffset += sizeof(DasPacket::Event)/4;
        event++;
    }

    // No errors
    errorOffset = 0;

    return true;
}

bool LabPacket::verifyNeutrons(DasDataPacket *packet, uint32_t &errorOffset)
{
    struct Event {
        uint32_t tof;
        uint32_t pixelid;
    };

    uint32_t nevents;
    // The next line doesn't compile on RHEL6 with g++ -std=c++0x
    //const Event *event = packet->getEvents<Event>(nevents);
    nevents = (packet->length - sizeof(DasDataPacket)) / sizeof(Event);
    const Event *event = reinterpret_cast<Event *>(packet->events);

    errorOffset = sizeof(DasDataPacket)/4;

    while (--nevents) {
        if (event->tof > EVENT_TOF_MAX)
            return false;

        if ((event->pixelid >> 28) > 0x3)
            return false;

        errorOffset += sizeof(Event)/4;
        event++;
    }

    // No errors
    errorOffset = 0;
    return true;
}

bool LabPacket::verifyNeutrons(DasPacket *packet, uint32_t &errorOffset)
{
    uint32_t size;
    const DasPacket::Event *event = reinterpret_cast<const DasPacket::Event *>(packet->getData(&size));
    uint32_t nevents = size / (sizeof(DasPacket::Event)/4);

    // Skip checking for RTDL if it exists, start checking at actual neutrons
    errorOffset = (sizeof(DasPacket) + (packet->getPayload() - packet->payload)) / 4;

    if (size % (sizeof(DasPacket::Event)/4)) {
        errorOffset += nevents * sizeof(DasPacket::Event)/4;
        return false;
    }

    while (nevents-- > 0) {
        if (event->tof > EVENT_TOF_MAX)
            return false;

        if ((event->pixelid >> 28) > 0x3)
            return false;

        errorOffset += sizeof(DasPacket::Event)/4;
        event++;
    }

    // No errors
    errorOffset = 0;

    return true;
}

bool LabPacket::verifyRamp(DasPacket *packet, uint32_t &errorOffset)
{
    const DasPacket::Event *event = reinterpret_cast<const DasPacket::Event *>(packet->payload);
    uint32_t nevents = packet->payload_length / sizeof(DasPacket::Event);

    if ((packet->payload_length % sizeof(DasPacket::Event)) != 0) {
        errorOffset = offsetOf(&DasPacket::payload_length)/4;
        return false;
    }

    if (lastRampValue == (uint32_t)-1)
        lastRampValue = event->tof;

    errorOffset = offsetOf(&DasPacket::payload)/4;
    for (uint32_t i = 0; i < nevents; i++) {
        if (event->tof != lastRampValue) {
            errorOffset += 2*i;
            lastRampValue = -1;
            return false;
        } else {
            lastRampValue++;
            lastRampValue &= 0xFFFFFFF;
        }

        if (event->pixelid != lastRampValue) {
            errorOffset += 2*i+1;
            lastRampValue = -1;
            return false;
        } else {
            lastRampValue++;
            lastRampValue &= 0xFFFFFFF;
        }

        event++;
    }

    return true;
}

bool LabPacket::verifyCmd(DasPacket *packet, uint32_t &errorOffset)
{
    return true;
}

void LabPacket::resetRamp()
{
    LabPacket::lastRampValue = (uint32_t)-1;
}
