/* Packet.cpp
 *
 * Copyright (c) 2017 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 *
 * @author Klemen Vodopivec
 */

#include "Packet.h"

#include <string.h>
#include <assert.h>
#include <sstream>

#define ALIGN_UP(number, boundary)      (((number) + (boundary) - 1) & ~((boundary) - 1))

// Replacement for C's offsetof - C++ pre-11 doesn't like offsetof on non-PODs
template<typename T, typename U> size_t offsetOf(U T::*member)
{
    return (char*)&((T*)0->*member) - (char*)0;
}

const Packet *Packet::cast(const uint8_t *data, size_t size) throw(std::runtime_error)
{
    if (size < sizeof(Packet)) {
        throw std::runtime_error("Not enough data to describe packet header");
    }

    const Packet *packet = reinterpret_cast<const Packet *>(data);
    
    if (packet->length != ALIGN_UP(packet->length, 4)) {
        throw std::runtime_error("Invalid packet length");
    }

    if (packet->length > 0xFFFFFF) {
        throw std::runtime_error("Packet length out of range");
    }

    return packet;
}

std::string Packet::getTypeName(Packet::Type type)
{
    std::ostringstream ss;

    switch (type) {
    case TYPE_LEGACY:   return "DAS 1.0";
    case TYPE_TEST:     return "Test";
    case TYPE_ERROR:    return "Error";
    case TYPE_RTDL:     return "RTDL";
    case TYPE_DAS_CMD:  return "DAS cmd";
    case TYPE_DAS_DATA: return "DAS data";
    default:
        ss << "Pkt type " << type;
        return ss.str();
    }
}

bool Packet::verify(uint32_t &errorOffset) const
{
    uint32_t minSize;

    switch (this->type) {
    case TYPE_DAS_CMD:
        minSize = sizeof(DasCmdPacket);
        if (!static_cast<const DasCmdPacket *>(this)->verify(errorOffset))
            return false;
        break;
    case TYPE_DAS_DATA:
        minSize = sizeof(DasDataPacket);
        if (!static_cast<const DasDataPacket *>(this)->verify(errorOffset))
            return false;
        break;
    case TYPE_RTDL:
        minSize = sizeof(RtdlPacket);
        if (!static_cast<const RtdlPacket *>(this)->verify(errorOffset))
            return false;
        break;
    case TYPE_ERROR:
        minSize = sizeof(ErrorPacket);
        break;
    case TYPE_TEST:
        minSize = sizeof(TestPacket);
        if (!static_cast<const TestPacket *>(this)->verify(errorOffset))
            return false;
        break;
    default:
        minSize = sizeof(Packet);
        break;
    }
    
    if (this->length < minSize) {
        errorOffset = offsetOf(&Packet::length);
        return false;
    }
    
    if (this->version != 1) {
        errorOffset = 0;
        return false;
    }
    
    return true;
}

bool RtdlPacket::verify(uint32_t &errorOffset) const
{
    if (this->length != (sizeof(RtdlPacket) + this->num_frames*4)) {
        errorOffset = offsetOf(&RtdlPacket::num_frames)/4;
        return false;
    }
    return true;
}

bool DasCmdPacket::verify(uint32_t &errorOffset) const
{
    if (this->length > (sizeof(DasCmdPacket) + this->cmd_length - 6)) {
        errorOffset = offsetOf(&DasCmdPacket::module_id)/4 - 1;
        return false;
    }
    return true;
}

bool DasDataPacket::verify(uint32_t &errorOffset) const
{
    struct Event {
        uint32_t tof;
        uint32_t pixelid;
    };
    
    if (this->event_format == EVENT_FMT_META || this->event_format == EVENT_FMT_PIXEL) {
        const Event *event = reinterpret_cast<const Event *>(this->events);
        for (uint32_t i = 0; i < this->num_events; i++) {
            if (event->tof > 0xFFFFFF) {
                errorOffset = offsetOf(&DasDataPacket::events) + i*sizeof(Event);
                return false;
            }

            if (this->event_format == EVENT_FMT_META) {
                if (((event->pixelid >> 28) & 0xF) == 0) {
                    errorOffset = offsetOf(&DasDataPacket::events) + i*sizeof(Event);
                    return false;
                }
            } else {
                if (((event->pixelid >> 28) & 0xF) != 0) {
                    errorOffset = offsetOf(&DasDataPacket::events) + i*sizeof(Event);
                    return false;
                }
            }
            
            event++;
        }
    }
    return true;
}

static uint32_t g_lastRampValue = (uint32_t)-1;

bool TestPacket::verify(uint32_t &errorOffset) const
{
    struct Event {
        uint32_t tof;
        uint32_t pixelid;
    };
    
    const Event *event = reinterpret_cast<const Event *>(this->payload);
    uint32_t nevents = data_len / sizeof(Event);

    if (g_lastRampValue == (uint32_t)-1)
        g_lastRampValue = event->tof;

    for (uint32_t i = 0; i < nevents; i++) {
        if (event->tof != g_lastRampValue) {
            errorOffset = offsetOf(&TestPacket::payload) + i*sizeof(Event);
            resetRamp();
            return false;
        } else {
            g_lastRampValue++;
            g_lastRampValue &= 0xFFFFFFF;
        }

        if (event->pixelid != g_lastRampValue) {
            errorOffset = offsetOf(&TestPacket::payload) + i*sizeof(Event) + 4;
            resetRamp();
            return false;
        } else {
            g_lastRampValue++;
            g_lastRampValue &= 0xFFFFFFF;
        }

        event++;
    }
    
    return true;
}

void TestPacket::resetRamp()
{
    g_lastRampValue = (uint32_t)-1;
}
