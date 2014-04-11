#include "DasPacket.h"

#include <stdexcept>
#include <string.h>

// Get rid of these defines gradually as we re-implement into structured way
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

DasPacket *DasPacket::create(uint32_t payloadLen, const uint8_t *payload)
{
    DasPacket *packet = 0;
    void *addr = malloc(sizeof(DasPacket) + payloadLen);
    if (addr)
        packet = new (addr) DasPacket(payloadLen, payload);
    return packet;
}

DasPacket::DasPacket(uint32_t payloadLen, const uint8_t *payload)
    : destination(0)
    , source(0)
    , info(0)
    , payload_length((payloadLen + 7 ) & ~7)
    , reserved1(0)
    , reserved2(0)
{
    if (payload) {
        memcpy(data, payload, payloadLen);
        if (payload_length != payloadLen) {
            memset(&data[payload_length], 0, payload_length - payloadLen);
        }
    } else {
        memset(data, 0, payload_length);
    }
}

bool DasPacket::valid() const
{
    return (length() > 0);
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
    return (cmdinfo.is_command);
}

bool DasPacket::isData() const
{
    return (!cmdinfo.is_command);
}

bool DasPacket::isNeutronData() const
{
    // info == 0x0C
    return (!datainfo.is_command && datainfo.only_neutron_data && datainfo.rtdl_present && datainfo.unused4_7 == 0x0);
}

bool DasPacket::isMetaData() const
{
    // info == 0x08
    return (!datainfo.is_command && !datainfo.only_neutron_data && datainfo.rtdl_present && datainfo.unused4_7 == 0x0);
}

bool DasPacket::isRtdl() const
{
    if (cmdinfo.is_command) {
        return (cmdinfo.command == DasPacket::CommandType::CMD_RTDL);
    } else {
        // Data version of the RTDL command (0x85)
        return (info == 0xFC);
    }
}

const DasPacket::RtdlHeader *DasPacket::getRtdlHeader() const
{
    if (!datainfo.is_command && datainfo.rtdl_present && payload_length >= sizeof(RtdlHeader))
        return (RtdlHeader *)data;
    return 0;
};

const DasPacket::NeutronEvent *DasPacket::getNeutronData(uint32_t *count) const
{
    const uint8_t *start = 0;
    *count = 0;
    if (!datainfo.is_command) {
        start = reinterpret_cast<const uint8_t *>(data);
        if (datainfo.rtdl_present)
            start += sizeof(RtdlHeader);
        *count = (payload_length - (start - reinterpret_cast<const uint8_t*>(data))) / 8;
    }
    return reinterpret_cast<const NeutronEvent *>(start);
}
