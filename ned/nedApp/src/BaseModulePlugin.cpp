#include "BaseModulePlugin.h"
#include "Log.h"

#include <osiSock.h>
#include <string.h>

#define NUM_BASEMODULEPLUGIN_PARAMS ((int)(&LAST_BASEMODULEPLUGIN_PARAM - &FIRST_BASEMODULEPLUGIN_PARAM + 1))

BaseModulePlugin::BaseModulePlugin(const char *portName, const char *dispatcherPortName, const char *hardwareId,
                                   bool behindDsp, int blocking, int numParams)
    : BasePlugin(portName, dispatcherPortName, REASON_OCCDATA, blocking, NUM_BASEMODULEPLUGIN_PARAMS + numParams,
                 1, asynOctetMask | asynInt32Mask | asynDrvUserMask, asynOctetMask | asynInt32Mask)
    , m_hardwareId(parseHardwareId(hardwareId))
    , m_stateMachine(STAT_NOT_INITIALIZED)
    , m_behindDsp(behindDsp)
{
    if (m_hardwareId == 0) {
        LOG_ERROR("Invalid hardware id '%s'", hardwareId);
        return;
    }

    m_stateMachine.addState(STAT_NOT_INITIALIZED,   DISCOVER_OK,                STAT_TYPE_VERIFIED);
    m_stateMachine.addState(STAT_NOT_INITIALIZED,   DISCOVER_MISMATCH,          STAT_TYPE_MISMATCH);
    m_stateMachine.addState(STAT_NOT_INITIALIZED,   VERSION_READ_OK,            STAT_VERSION_VERIFIED);
    m_stateMachine.addState(STAT_NOT_INITIALIZED,   VERSION_READ_MISMATCH,      STAT_VERSION_MISMATCH);
    m_stateMachine.addState(STAT_TYPE_VERIFIED,     VERSION_READ_OK,            STAT_READY);
    m_stateMachine.addState(STAT_TYPE_VERIFIED,     VERSION_READ_MISMATCH,      STAT_VERSION_MISMATCH);
    m_stateMachine.addState(STAT_TYPE_VERIFIED,     TIMEOUT,                    STAT_TIMEOUT);
    m_stateMachine.addState(STAT_VERSION_VERIFIED,  DISCOVER_OK,                STAT_READY);
    m_stateMachine.addState(STAT_VERSION_VERIFIED,  DISCOVER_MISMATCH,          STAT_VERSION_MISMATCH);
    m_stateMachine.addState(STAT_VERSION_VERIFIED,  TIMEOUT,                    STAT_TIMEOUT);

    createParam("HwId",         asynParamInt32, &HardwareId);
    createParam("Status",       asynParamInt32, &Status);
    createParam("Command",      asynParamInt32, &Command);
    setIntegerParam(HardwareId, m_hardwareId);
    setIntegerParam(Status, STAT_NOT_INITIALIZED);
    callParamCallbacks();
}

BaseModulePlugin::~BaseModulePlugin()
{}


asynStatus BaseModulePlugin::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
    if (pasynUser->reason == Command) {
        switch (value) {
        case BaseModulePlugin::CMD_INITIALIZE:
            sendToDispatcher(DasPacket::CMD_DISCOVER);
            sendToDispatcher(DasPacket::CMD_READ_VERSION);
            return asynSuccess;
        case BaseModulePlugin::CMD_READ_STATUS:
            sendToDispatcher(DasPacket::CMD_READ_STATUS);
            return asynSuccess;
        case BaseModulePlugin::CMD_READ_CONFIG:
            sendToDispatcher(DasPacket::CMD_READ_CONFIG);
            return asynSuccess;
        case BaseModulePlugin::CMD_WRITE_CONFIG:
            reqConfigWrite();
            return asynSuccess;
        case BaseModulePlugin::CMD_NONE:
            return asynSuccess;
        default:
            LOG_ERROR("Unrecognized command '%d'", value);
            return asynError;
        }
    }
    std::map<int, struct ConfigParamDesc>::iterator it = m_configParams.find(pasynUser->reason);
    if (it != m_configParams.end()) {
        int mask = (0x1 << it->second.width) - 1;
        if ((value & mask) != value) {
            LOG_ERROR("Parameter %s value %d out of bounds", getParamName(it->first), value);
            return asynError;
        } else {
            setIntegerParam(it->first, value);
            callParamCallbacks();
            return asynSuccess;
        }
    }
    return BasePlugin::writeInt32(pasynUser, value);
}

void BaseModulePlugin::sendToDispatcher(DasPacket::CommandType command, uint32_t *payload, uint32_t length)
{
    DasPacket *packet;
    if (m_behindDsp)
        packet = createLvdsPacket(m_hardwareId, command, payload, length);
    else
        packet = createOpticalPacket(m_hardwareId, command, payload, length);

    if (packet) {
        BasePlugin::sendToDispatcher(packet);
        delete packet;
    }
}

void BaseModulePlugin::reqConfigWrite()
{
    if (m_configPayloadLength == 0)
        recalculateConfigParams();

    uint32_t data[m_configPayloadLength];
    for (uint32_t i=0; i<m_configPayloadLength; i++)
        data[i] = 0;

    for (std::map<int, struct ConfigParamDesc>::iterator it=m_configParams.begin(); it != m_configParams.end(); it++) {
        int offset = m_configSectionOffsets[it->second.section] + it->second.offset;
        int mask = (0x1 << it->second.width) - 1;
        int value = 0;

        if (getIntegerParam(it->first, &value) != asynSuccess) {
            // This should not happen. It's certainly error when creating and parameters.
            LOG_ERROR("Failed to get parameter %s value", getParamName(it->first));
            return;
        }
        if ((value & mask) != value) {
            // This should not happen. It's certainly error when setting new value for parameter
            LOG_WARN("Parameter %s value out of range", getParamName(it->first));
        }

        data[offset] |= ((value & mask) << it->second.shift);
    }

    sendToDispatcher(DasPacket::CMD_WRITE_CONFIG, data, m_configPayloadLength);
}

void BaseModulePlugin::processData(const DasPacketList * const packetList)
{
    int nReceived = 0;
    int nProcessed = 0;
    getIntegerParam(RxCount,    &nReceived);
    getIntegerParam(ProcCount,  &nProcessed);

    for (const DasPacket *packet = packetList->first(); packet != 0; packet = packetList->next(packet)) {
        nReceived++;

        if (!packet->isResponse())
            continue;

        // Silently skip packets we're not interested in
        if (!packet->isResponse() || packet->getSourceAddress() != m_hardwareId)
            continue;

        if (processResponse(packet))
            nProcessed++;
    }

    setIntegerParam(RxCount,    nReceived);
    setIntegerParam(ProcCount,  nProcessed);
    callParamCallbacks();
}

bool BaseModulePlugin::processResponse(const DasPacket *packet)
{
    int status;
    getIntegerParam(Status,     &status);

    // Parse only responses valid in pre-initialization state
    switch (packet->cmdinfo.command) {
    case DasPacket::CMD_DISCOVER:
        if (rspDiscover(packet))
            status = m_stateMachine.transition(DISCOVER_OK);
        else
            status = m_stateMachine.transition(DISCOVER_MISMATCH);
        setIntegerParam(Status, status);
        callParamCallbacks();
        return true;
    case DasPacket::CMD_READ_VERSION:
        if (rspReadVersion(packet))
            status = m_stateMachine.transition(VERSION_READ_OK);
        else
            status = m_stateMachine.transition(VERSION_READ_MISMATCH);
        setIntegerParam(Status, status);
        callParamCallbacks();
        return true;
    default:
        break;
    }

    if (status != STAT_READY) {
        LOG_WARN("Module type and versions not yet verified, ignoring packet");
        return false;
    }

    switch (packet->cmdinfo.command) {
    case DasPacket::CMD_READ_CONFIG:
        return rspReadConfig(packet);
    case DasPacket::CMD_READ_STATUS:
        return rspReadStatus(packet);
    case DasPacket::RSP_ACK:
        switch (packet->getPayload()[0]) {
        case DasPacket::CMD_WRITE_CONFIG:
            // TODO:
        default:
            break;
        }
        return true;
    default:
        LOG_WARN("Received unhandled response 0x%02X", packet->cmdinfo.command);
        return false;
    }
}

bool BaseModulePlugin::rspReadConfig(const DasPacket *packet)
{
    if (m_configPayloadLength == 0)
        recalculateConfigParams();

    if (packet->getPayloadLength() != m_configPayloadLength) {
        LOG_ERROR("Received wrong READ_CONFIG response based on length; received %u, expected %u", packet->getPayloadLength(), m_configPayloadLength);
        return false;
    }

    const uint32_t *payload = packet->getPayload();
    for (std::map<int, ConfigParamDesc>::iterator it=m_configParams.begin(); it != m_configParams.end(); it++) {
        int offset = m_configSectionOffsets[it->second.section] + it->second.offset;
        int mask = (0x1 << it->second.width) - 1;
        int value = (payload[offset] >> it->second.shift) & mask;
        setIntegerParam(it->first, value);
    }
    callParamCallbacks();
    return true;
}

bool BaseModulePlugin::rspReadStatus(const DasPacket *packet)
{
    if (packet->getPayloadLength() != m_statusPayloadLength) {
        LOG_ERROR("Received wrong READ_STATUS response based on length; received %u, expected %u", packet->getPayloadLength(), m_statusPayloadLength);
        return false;
    }

    const uint32_t *payload = packet->getPayload();
    for (std::map<int, StatusParamDesc>::iterator it=m_statusParams.begin(); it != m_statusParams.end(); it++) {
        int value = payload[it->second.offset] >> it->second.shift;
        if ((it->second.shift + it->second.width) > 32) {
            value |= payload[it->second.offset + 1] << (32 - it->second.shift);
        }
        value &= (0x1 << it->second.width) - 1;
        setIntegerParam(it->first, value);
    }
    callParamCallbacks();
    return true;
}

void BaseModulePlugin::createStatusParam(const char *name, uint32_t offset, uint32_t nBits, uint32_t shift)
{
    int index;
    if (createParam(name, asynParamInt32, &index) != asynSuccess) {
        LOG_ERROR("Module status parameter '%s' cannot be created (already exist?)", name);
        return;
    }

    if (m_behindDsp) {
        shift += (offset % 2 == 0 ? 0 : 16);
        offset /= 2;
    }

    StatusParamDesc desc;
    desc.offset = offset;
    desc.shift = shift;
    desc.width = nBits;
    m_statusParams[index] = desc;

    m_statusPayloadLength = std::max(m_statusPayloadLength, (offset+1)*4);
}

void BaseModulePlugin::createConfigParam(const char *name, char section, uint32_t offset, uint32_t nBits, uint32_t shift, int value)
{
    int index;
    if (createParam(name, asynParamInt32, &index) != asynSuccess) {
        LOG_ERROR("Module config parameter '%s' cannot be created (already exist?)", name);
        return;
    }
    setIntegerParam(index, value);

    if (m_behindDsp) {
        shift += (offset % 2 == 0 ? 0 : 16);
        offset /= 2;
    }

    ConfigParamDesc desc;
    desc.section = section;
    desc.offset  = offset;
    desc.shift   = shift;
    desc.width   = nBits;
    desc.initVal = value;
    m_configParams[index] = desc;

    m_configSectionSizes[section] = std::max(m_configSectionSizes[section], offset+1);
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
        uint32_t offset = 0;
        packet->source = DasPacket::HWID_SELF;
        packet->destination = 0;
        packet->cmdinfo.is_command = true;
        packet->cmdinfo.is_passthru = true;
        packet->cmdinfo.lvds_cmd = true;
        packet->cmdinfo.lvds_start = true;
        packet->cmdinfo.lvds_parity = evenParity(packet->info & 0xFFFFFF);
        packet->cmdinfo.command = command;
        if (destination != DasPacket::HWID_BROADCAST) {
            packet->payload[offset] = destination & 0xFFFF;
            packet->payload[offset] |= (evenParity(packet->payload[offset]) << 16);
            offset++;
            packet->payload[offset] = (destination >> 16 ) & 0xFFFF;
            packet->payload[offset] |= (evenParity(packet->payload[offset]) << 16);
            offset++;
        } else {
            packet->payload_length -= 2;
        }
        for (uint32_t i=0; i<length; i++) {
            packet->payload[offset] = payload[i] & 0xFFFF;
            packet->payload[offset] |= evenParity(packet->payload[offset]) << 16;
            offset++;
            packet->payload[offset] = (payload[i] >> 16) & 0xFFFF;
            packet->payload[offset] |= evenParity(packet->payload[offset]) << 16;
            offset++;
        }
        if (offset > 0) {
            offset--;
            packet->payload[offset] |= (0x1 << 17); // Last word bit...
            packet->payload[offset] ^= (0x1 << 16); // ... also flips parity
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

uint32_t BaseModulePlugin::parseHardwareId(const std::string &text)
{
    uint32_t id = 0;

    if (text.substr(2) == "0x") {
        char *endptr;
        id = strtoul(text.c_str(), &endptr, 16);
        if (*endptr != 0)
            id = 0;
    } else {
        struct in_addr hwid;
        if (hostToIPAddr(text.c_str(), &hwid) == 0)
            id = ntohl(hwid.s_addr);
    }
    return id;
}

void BaseModulePlugin::recalculateConfigParams()
{
    // Section sizes have already been calculated in createConfigParams()

    // Calculate section offsets
    m_configSectionOffsets['1'] = 0x0;
    for (char i='1'; i<='9'; i++) {
        m_configSectionOffsets[i] = m_configSectionOffsets[i-1] + m_configSectionSizes[i-1];
        LOG_WARN("Section '%c' size=%u offset=%u", i, m_configSectionSizes[i], m_configSectionOffsets[i]);
    }
    m_configSectionOffsets['A'] = m_configSectionOffsets['9'] + m_configSectionSizes['9'];
    LOG_WARN("Section '%c' size=%u offset=%u", 'A', m_configSectionSizes['A'], m_configSectionOffsets['A']);
    for (char i='B'; i<='F'; i++) {
        m_configSectionOffsets[i] = m_configSectionOffsets[i-1] + m_configSectionSizes[i-1];
        LOG_WARN("Section '%c' size=%u offset=%u", i, m_configSectionSizes[i], m_configSectionOffsets[i]);
    }

    // Calculate total required payload size in bytes
    m_configPayloadLength = 4 * (m_configSectionOffsets['F'] + m_configSectionSizes['F']);
}
