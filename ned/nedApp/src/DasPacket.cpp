#include "DasPacket.h"

#include <stdexcept>
#include <string.h>

// Static members initilization
const uint32_t DasPacket::MinLength = sizeof(DasPacket);
const uint32_t DasPacket::MaxLength = 1800*8 + MinLength;

DasPacket::DasPacket(uint32_t source_, uint32_t destination_, CommandInfo cmdinfo_, uint32_t payload_length_, uint32_t *payload_)
    : destination(destination_)
    , source(source_)
    , cmdinfo(cmdinfo_)
    , payload_length(payload_length_)
    , reserved1(0)
    , reserved2(0)
{
    if (payload_) {
        memcpy(payload, payload_, payload_length);
    } else {
        memset(payload, 0, payload_length);
    }
}

DasPacket *DasPacket::createOcc(uint32_t source, uint32_t destination, CommandType command, uint32_t payload_length, uint32_t *payload)
{
    DasPacket *packet = 0;
    CommandInfo cmdinfo;

    memset(&cmdinfo, 0, sizeof(cmdinfo));

    payload_length = (payload_length + 3) & ~3;
    cmdinfo.is_command = true;
    cmdinfo.command = command;

    void *addr = malloc(sizeof(DasPacket) + payload_length);
    if (addr)
        packet = new (addr) DasPacket(source, destination, cmdinfo, payload_length, payload);
    return packet;
}

DasPacket *DasPacket::createLvds(uint32_t source, uint32_t destination, CommandType command, uint32_t payload_length, uint32_t *payload)
{
    DasPacket *packet = 0;
    CommandInfo cmdinfo;

    memset(&cmdinfo, 0, sizeof(cmdinfo));

    uint32_t aligned_length = (payload_length + 3) & ~3; // 4-byte aligned data required
    uint32_t real_payload_length;

    real_payload_length = (payload_length + 3) & ~3; // 4-byte aligned data
    real_payload_length *= 2; // 32 bits are devided into two dwords
    real_payload_length += (destination != HWID_BROADCAST ? 2*sizeof(uint32_t) : 0); // First 2 dwords of the payload represent LVDS address for non-global commands

    cmdinfo.is_command = true;
    cmdinfo.is_passthru = true;
    cmdinfo.command = command;

    void *addr = malloc(sizeof(DasPacket) + real_payload_length);
    if (addr) {
        uint32_t offset = 0;

        // Cmdinfo from the OCC header is the first dword DSP passes thru
        // It must have all the LVDS flags
        cmdinfo.lvds_cmd = true;
        cmdinfo.lvds_start = true;
        cmdinfo.lvds_parity = lvdsParity(*reinterpret_cast<uint32_t *>(&cmdinfo) & 0xFFFFFF);

        // Destination in OCC header is always 0 for LVDS packets
        // Don't copy the payload
        packet = new (addr) DasPacket(source, 0, cmdinfo, real_payload_length, 0);

        // Add 2 dwords for destination address, both LVDS pass-thru
        if (destination != HWID_BROADCAST) {
            packet->payload[offset] = destination & 0xFFFF;
            packet->payload[offset] |= (lvdsParity(packet->payload[offset]) << 16);
            offset++;
            packet->payload[offset] = (destination >> 16 ) & 0xFFFF;
            packet->payload[offset] |= (lvdsParity(packet->payload[offset]) << 16);
            offset++;
        }

        // Split each 32 bits from the payload into two 16 bit parts and put them into separate dwords;
        // lower 16 bits into first dword, upper 16 bits into second dword
        // Add LVDS parity check to each word
        for (uint32_t i=0; i<aligned_length/4; i++) {
            packet->payload[offset] = payload[i] & 0xFFFF;
            packet->payload[offset] |= lvdsParity(packet->payload[offset]) << 16;
            offset++;
            packet->payload[offset] = (payload[i] >> 16) & 0xFFFF;
            packet->payload[offset] |= lvdsParity(packet->payload[offset]) << 16;
            offset++;
        }

        // Finalize LVDS packet - last dword must have the stop flag
        if (offset > 0) {
            // When message is not word-aligned, the last word is not valid
            // and must be taken out.
            if (payload_length < aligned_length)
                packet->payload[--offset] = 0;

            offset--;
            packet->payload[offset] |= (0x1 << 17); // Last word flag...
            packet->payload[offset] ^= (0x1 << 16); // ... which flips parity
        } else {
            packet->cmdinfo.lvds_stop = true;       // Same here
            packet->cmdinfo.lvds_parity ^= 0x1;
        }
    }
    return packet;
}

bool DasPacket::isValid() const
{
    return (length() > MinLength);
}

uint32_t DasPacket::length() const
{
    uint32_t packet_length = sizeof(DasPacket) + payload_length;
    if (packet_length != ((packet_length + 3 ) & ~3))
        packet_length = 0;
    return packet_length;
}

bool DasPacket::isCommand() const
{
    return (cmdinfo.is_command);
}

bool DasPacket::isResponse() const
{
    return (cmdinfo.is_command && cmdinfo.is_response);
}

bool DasPacket::isData() const
{
    return (!cmdinfo.is_command);
}

bool DasPacket::isBadPacket() const
{
    return (cmdinfo.command == BAD_PACKET);
}

bool DasPacket::isNeutronData() const
{
    // info == 0x0C
    return (!datainfo.is_command && datainfo.only_neutron_data && datainfo.rtdl_present && datainfo.format_code == 0x0);
}

bool DasPacket::isMetaData() const
{
    // info == 0x08
    return (!datainfo.is_command && !datainfo.only_neutron_data && datainfo.rtdl_present && datainfo.format_code == 0x0);
}

bool DasPacket::isRtdl() const
{
    if (cmdinfo.is_command) {
        return (cmdinfo.command == DasPacket::CommandType::CMD_RTDL);
    } else {
        // Data version of the RTDL command (0x85)
        return (info == 0x200000FF);
    }
}

const DasPacket::RtdlHeader *DasPacket::getRtdlHeader() const
{
    // RTDL packets always come from DSP only, so the RtdlHeader is at the start of payload
    if (cmdinfo.is_command && cmdinfo.command == DasPacket::CommandType::CMD_RTDL)
        return (RtdlHeader *)payload;
    if (!datainfo.is_command && datainfo.rtdl_present && payload_length >= sizeof(RtdlHeader))
        return (RtdlHeader *)payload;
    return 0;
};

const DasPacket::Event *DasPacket::getEventData(uint32_t *count) const
{
    // DSP aggregates detectors data into data packets and the data is always at the start of payload
    const uint8_t *start = 0;
    *count = 0;
    if (!datainfo.is_command) {
        start = reinterpret_cast<const uint8_t *>(payload);
        if (datainfo.rtdl_present)
            start += sizeof(RtdlHeader);
        *count = (payload_length - (start - reinterpret_cast<const uint8_t*>(payload))) / 8;
    }
    return reinterpret_cast<const Event *>(start);
}

DasPacket::CommandType DasPacket::getResponseType() const
{
    CommandType command = static_cast<CommandType>(0);
    if (cmdinfo.is_command && cmdinfo.is_response) {
        if (cmdinfo.command == RSP_ACK || cmdinfo.command == RSP_NACK) {
            if (cmdinfo.is_passthru) {
                command = reinterpret_cast<const CommandInfo *>(&payload[1])->command;
            } else {
                command = reinterpret_cast<const CommandInfo *>(&payload[0])->command;
            }
        } else {
            command = cmdinfo.command;
        }
    }
    return command;
}

uint32_t DasPacket::getSourceAddress() const
{
    if (cmdinfo.is_command && cmdinfo.is_passthru)
        return payload[0];
    else
        return source;
}

uint32_t DasPacket::getRouterAddress() const
{
    if (cmdinfo.is_command && cmdinfo.is_passthru)
        return source;
    else
        return 0;
}

const uint32_t *DasPacket::getPayload() const
{
    if (cmdinfo.is_command && cmdinfo.is_passthru) {
        if (cmdinfo.command == RSP_ACK || cmdinfo.command == RSP_NACK) {
            return &payload[2];
        } else {
            return &payload[1];
        }
    } else {
        return payload;
    }
}

uint32_t DasPacket::getPayloadLength() const
{
    if (cmdinfo.is_command && cmdinfo.is_passthru) {
        if (cmdinfo.command == RSP_ACK || cmdinfo.command == RSP_NACK) {
            return payload_length - 8;
        } else {
            return payload_length - 4;
        }
    } else {
        return payload_length;
    }
}

bool DasPacket::lvdsParity(int number)
{
    // even parity
    int temp = 0;
    while (number != 0) {
        temp = temp ^ (number & 0x1);
        number >>= 1;
    }
    return temp;
}
