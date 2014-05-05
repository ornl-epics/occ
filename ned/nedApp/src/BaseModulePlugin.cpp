#include "BaseModulePlugin.h"
#include "Log.h"

#include <osiSock.h>
#include <string.h>

#define NUM_BASEMODULEPLUGIN_PARAMS ((int)(&LAST_BASEMODULEPLUGIN_PARAM - &FIRST_BASEMODULEPLUGIN_PARAM + 1))

BaseModulePlugin::BaseModulePlugin(const char *portName, const char *dispatcherPortName, const char *hardwareId,
                                   ConnectionType conn, int blocking, int numParams)
    : BasePlugin(portName, dispatcherPortName, REASON_OCCDATA, blocking, NUM_BASEMODULEPLUGIN_PARAMS + numParams,
                 1, asynOctetMask | asynInt32Mask | asynDrvUserMask, asynOctetMask | asynInt32Mask)
    , m_hardwareId(0)
    , m_connType(conn)
{
    struct in_addr hwid;
    if (strncasecmp(hardwareId, "0x", 2) == 0) {
        char *endptr;
        m_hardwareId = strtoul(hardwareId, &endptr, 16);
        if (*endptr != 0)
            m_hardwareId = 0;
    } else if (hostToIPAddr(hardwareId, &hwid) == 0) {
            m_hardwareId = ntohl(hwid.s_addr);
    }
    if (m_hardwareId == 0)
        LOG_ERROR("Invalid hardware id '%s'", hardwareId);

    createParam("HwId",         asynParamInt32, &HardwareId);

    setIntegerParam(HardwareId, m_hardwareId);

    callParamCallbacks();
}

void BaseModulePlugin::sendToDispatcher(DasPacket::CommandType command, uint32_t *payload, uint32_t length)
{
    DasPacket *packet;
    if (m_connType == CONN_TYPE_LVDS)
        packet = createLvdsPacket(m_hardwareId, command, payload, length);
    else
        packet = createOpticalPacket(m_hardwareId, command, payload, length);

    if (packet) {
        BasePlugin::sendToDispatcher(packet);
        delete packet;
    }
}

void BaseModulePlugin::createStatusParam(const char *name, uint32_t offset, uint32_t nBits, uint32_t shift)
{
    if ((shift + nBits) > 32) {
        LOG_ERROR("Module status parameter '%s' cannot shift over 32 bits, %d bits shifted for %d requested", name, nBits, shift);
        return;
    }

    int index;
    if (createParam(name, asynParamInt32, &index) != asynSuccess) {
        LOG_ERROR("Module status parameter '%s' cannot be created (already exist?)", name);
        return;
    }
    StatusParamDesc desc;
    desc.offset = offset;
    desc.shift = shift;
    desc.width = nBits;
    m_statusParams[index] = desc;
}

DasPacket *BaseModulePlugin::createOpticalPacket(uint32_t destination, DasPacket::CommandType command, uint32_t *payload, uint32_t length)
{
    DasPacket *packet = DasPacket::create(length*sizeof(uint32_t), reinterpret_cast<uint8_t *>(payload));
    if (packet) {
        packet->source = DasPacket::HWID_SELF;
        packet->destination = destination;
        packet->cmdinfo.is_command = true;
        packet->cmdinfo.command = command;
    }
    return packet;
}

DasPacket *BaseModulePlugin::createLvdsPacket(uint32_t destination, DasPacket::CommandType command, uint32_t *payload, uint32_t length)
{
    DasPacket *packet = DasPacket::create((2+length)*sizeof(uint32_t), reinterpret_cast<uint8_t *>(payload));
    if (packet) {
        packet->source = DasPacket::HWID_SELF;
        packet->destination = 0;
        packet->cmdinfo.is_command = true;
        packet->cmdinfo.is_passthru = true;
        packet->cmdinfo.lvds_all_one = 0x3;
        packet->cmdinfo.lvds_global = (destination == DasPacket::HWID_BROADCAST);
        packet->cmdinfo.lvds_parity = evenParity(command);
        packet->cmdinfo.command = command;
        if (destination != DasPacket::HWID_BROADCAST) {
            packet->payload[0] = destination & 0xFFFF;
            packet->payload[0] |= (evenParity(packet->payload[0]) << 16);
            packet->payload[1] = (destination >> 16 ) & 0xFFFF;
            packet->payload[1] |= (evenParity(packet->payload[1]) << 16); // last bit changes parity
            packet->payload[1] |= (0x1 << 17);
        } else {
            packet->payload_length = 0;
        }
    }
    return packet;
}

bool BaseModulePlugin::evenParity(int number)
{
    int temp = 0;
    while (number != 0) {
        temp = temp ^ (number & 0x1);
        number >>= 1;
    }
    return temp;
}

void BaseModulePlugin::rspReadStatus(const DasPacket *packet)
{
    const uint32_t *payload = packet->getPayload();
    for (std::map<int, StatusParamDesc>::iterator it=m_statusParams.begin(); it != m_statusParams.end(); it++) {
        int value = (payload[it->second.offset] >> it->second.shift) & ((0x1 << it->second.width) - 1);
        setIntegerParam(it->first, value);
    }
    callParamCallbacks();
}
