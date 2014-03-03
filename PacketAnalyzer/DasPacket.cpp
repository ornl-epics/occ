#include "DasPacket.hpp"

#include <stddef.h> // offsetof

#include <stdexcept>

#define DAS_ROOT_ID                         0x000f10cc
#define DAS_BROADCAST                       0x00000000

#define DAS_PASSTHROUGH_LVDS                0x40000000
#define DAS_COMMAND                         0x80000000
#define DAS_RESPONSE                        (DAS_COMMAND | 0x20000000)

#define DAS_LVDS_PARITY                     (1 << 16)
#define DAS_LVDS_LAST                       (1 << 17)
#define DAS_LVDS_FIRST                      (1 << 18)
#define DAS_LVDS_CMD                        (1 << 19)

#define DAS_CMD_READ_VERSION                (DAS_COMMAND | 0x20)
#define DAS_CMD_READ_CONFIG                 (DAS_COMMAND | 0x21)
#define DAS_CMD_READ_STATUS                 (DAS_COMMAND | 0x22)
#define DAS_CMD_WRITE_CONF                  (DAS_COMMAND | 0x30)
#define DAS_CMD_SERIAL_TX                   (DAS_COMMAND | 0x50)
#define DAS_CMD_DISCOVER                    (DAS_COMMAND | 0x80)
#define DAS_CMD_RESET                       (DAS_COMMAND | 0x81)
#define DAS_CMD_START                       (DAS_COMMAND | 0x82)
#define DAS_CMD_STOP                        (DAS_COMMAND | 0x83)
#define DAS_CMD_READ_CHAN_VERSION           (DAS_CMD_READ_VERSION | 0x1000)
#define DAS_CMD_READ_CHAN_CONFIG            (DAS_CMD_READ_CONFIG | 0x1000)
#define DAS_CMD_READ_CHAN_STATUS            (DAS_CMD_READ_STATUS | 0x1000)
#define DAS_CMD_WRITE_CHAN_CONF             (DAS_CMD_WRITE_CONF | 0x1000)

#define DAS_CMD_TSYNC                       (DAS_COMMAND | 0x84)
#define DAS_CMD_RTDL                        (DAS_COMMAND | 0x85)

#define DAS_RSP_MASK                        (DAS_RESPONSE | 0xff)
#define DAS_RSP_VERSION                     (DAS_RESPONSE | 0x20)
#define DAS_RSP_CONFIG                      (DAS_RESPONSE | 0x21)
#define DAS_RSP_STATUS                      (DAS_RESPONSE | 0x22)
#define DAS_RSP_ERROR                       (DAS_RESPONSE | 0x40)
#define DAS_RSP_ACK                         (DAS_RESPONSE | 0x41)
#define DAS_RSP_SERIAL_RX                   (DAS_RESPONSE | 0x51)
#define DAS_RSP_DEVLIST                     (DAS_RESPONSE | 0x7e)
#define DAS_RSP_DISCOVER                    (DAS_RESPONSE | 0x80)
#define DAS_RSP_RESET                       (DAS_RESPONSE | 0x81)
#define DAS_RSP_START                       (DAS_RESPONSE | 0x82)
#define DAS_RSP_STOP                        (DAS_RESPONSE | 0x83)
#define DAS_RSP_CHAN                        (DAS_RESPONSE | 0xf8ff)
#define DAS_RSP_CHAN_VERSION                (DAS_RSP_VERSION | 0x1000)
#define DAS_RSP_CHAN_CONFIG                 (DAS_RSP_CONFIG | 0x1000)
#define DAS_RSP_CHAN_STATUS                 (DAS_RSP_STATUS | 0x1000)
#define DAS_RSP_CHAN_MASK                   (7 << 8)

#define DAS_DATA_MASK                       0xFC
#define DAS_DATA_RTDL_FROM_DSP              0xFC
#define DAS_DATA_METADATA                   0x08
#define DAS_DATA_EVENT                      0x0C

#define MAX_EVENTS_PER_PACKET               1800

DasPacket::DasPacket(uint32_t datalen)
{
    uint32_t len = length();
    if (len == 0)
        throw std::overflow_error("Packet size is not right, either not aligned packet or zero size");
    if (len > datalen)
        throw std::overflow_error("Packet spans over the provided buffer boundaries");
}

uint32_t DasPacket::length() const
{
    uint32_t packet_length = sizeof(DasPacket) + payload_length;
    if (packet_length != ((packet_length + 7 ) & ~7))
        packet_length = 0;
    return packet_length;
}

bool DasPacket::isCommand() const
{
    return (info & DAS_COMMAND);
}

bool DasPacket::isData() const
{
    return !((info & DAS_COMMAND) && (info & DAS_PASSTHROUGH_LVDS));
}

bool DasPacket::isDataRtdl() const
{
    return (info & DAS_DATA_MASK) == DAS_DATA_RTDL_FROM_DSP;
}

bool DasPacket::isDataMeta() const
{
    return (info & DAS_DATA_MASK) == DAS_DATA_METADATA;
}

bool DasPacket::isDataEvent() const
{
    return (info & DAS_DATA_MASK) == DAS_DATA_EVENT;
}

