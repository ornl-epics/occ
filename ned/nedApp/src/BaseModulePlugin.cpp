#include "BaseModulePlugin.h"
#include "Log.h"

#include <osiSock.h>
#include <string.h>

#define NUM_BASEMODULEPLUGIN_PARAMS ((int)(&LAST_BASEMODULEPLUGIN_PARAM - &FIRST_BASEMODULEPLUGIN_PARAM + 1))

const float BaseModulePlugin::NO_RESPONSE_TIMEOUT = 2.0;

BaseModulePlugin::BaseModulePlugin(const char *portName, const char *dispatcherPortName, const char *hardwareId,
                                   bool behindDsp, int blocking, int numParams)
    : BasePlugin(portName, dispatcherPortName, REASON_OCCDATA, blocking, NUM_BASEMODULEPLUGIN_PARAMS + numParams,
                 1, asynOctetMask | asynInt32Mask | asynDrvUserMask, asynOctetMask | asynInt32Mask)
    , m_hardwareId(parseHardwareId(hardwareId))
    , m_stateMachine(ST_NOT_INITIALIZED)
    , m_behindDsp(behindDsp)
{
    if (m_hardwareId == 0) {
        LOG_ERROR("Invalid hardware id '%s'", hardwareId);
        return;
    }

    m_stateMachine.addState(ST_NOT_INITIALIZED,     SM_ACTION_CMD(DasPacket::CMD_DISCOVER),         ST_WAITING_TYPE_RSP);
    m_stateMachine.addState(ST_WAITING_TYPE_RSP,    SM_ACTION_ACK(DasPacket::CMD_DISCOVER),         ST_TYPE_VERIFIED);
    m_stateMachine.addState(ST_WAITING_TYPE_RSP,    SM_ACTION_ERR(DasPacket::CMD_DISCOVER),         ST_ERROR);
    m_stateMachine.addState(ST_WAITING_TYPE_RSP,    SM_ACTION_TIMEOUT(DasPacket::CMD_DISCOVER),     ST_TIMEOUT);
    m_stateMachine.addState(ST_TYPE_VERIFIED,       SM_ACTION_CMD(DasPacket::CMD_READ_VERSION),     ST_WAITING_VER_RSP);
    m_stateMachine.addState(ST_WAITING_VER_RSP,     SM_ACTION_ACK(DasPacket::CMD_READ_VERSION),     ST_READY);
    m_stateMachine.addState(ST_WAITING_VER_RSP,     SM_ACTION_ERR(DasPacket::CMD_READ_VERSION),     ST_ERROR);
    m_stateMachine.addState(ST_WAITING_VER_RSP,     SM_ACTION_TIMEOUT(DasPacket::CMD_READ_VERSION), ST_ERROR);

    m_stateMachine.addState(ST_READY,               SM_ACTION_CMD(DasPacket::CMD_READ_STATUS),      ST_WAITING);
    m_stateMachine.addState(ST_WAITING,             SM_ACTION_ACK(DasPacket::CMD_READ_STATUS),      ST_READY);
    m_stateMachine.addState(ST_WAITING,             SM_ACTION_ERR(DasPacket::CMD_READ_STATUS),      ST_ERROR);
    m_stateMachine.addState(ST_WAITING,             SM_ACTION_TIMEOUT(DasPacket::CMD_READ_STATUS),  ST_TIMEOUT);
    m_stateMachine.addState(ST_READY,               SM_ACTION_CMD(DasPacket::CMD_READ_CONFIG),      ST_WAITING);
    m_stateMachine.addState(ST_WAITING,             SM_ACTION_ACK(DasPacket::CMD_READ_CONFIG),      ST_READY);
    m_stateMachine.addState(ST_WAITING,             SM_ACTION_ERR(DasPacket::CMD_READ_CONFIG),      ST_ERROR);
    m_stateMachine.addState(ST_WAITING,             SM_ACTION_TIMEOUT(DasPacket::CMD_READ_CONFIG),  ST_TIMEOUT);
    m_stateMachine.addState(ST_READY,               SM_ACTION_CMD(DasPacket::CMD_WRITE_CONFIG),     ST_WAITING);
    m_stateMachine.addState(ST_WAITING,             SM_ACTION_ACK(DasPacket::CMD_WRITE_CONFIG),     ST_READY);
    m_stateMachine.addState(ST_WAITING,             SM_ACTION_ERR(DasPacket::CMD_WRITE_CONFIG),     ST_ERROR);
    m_stateMachine.addState(ST_WAITING,             SM_ACTION_TIMEOUT(DasPacket::CMD_WRITE_CONFIG), ST_TIMEOUT);
    m_stateMachine.addState(ST_READY,               SM_ACTION_CMD(DasPacket::CMD_START),            ST_WAITING);
    m_stateMachine.addState(ST_WAITING,             SM_ACTION_ACK(DasPacket::CMD_START),            ST_READY);
    m_stateMachine.addState(ST_WAITING,             SM_ACTION_ERR(DasPacket::CMD_START),            ST_ERROR);
    m_stateMachine.addState(ST_WAITING,             SM_ACTION_TIMEOUT(DasPacket::CMD_START),        ST_TIMEOUT);
    m_stateMachine.addState(ST_READY,               SM_ACTION_CMD(DasPacket::CMD_STOP),             ST_WAITING);
    m_stateMachine.addState(ST_WAITING,             SM_ACTION_ACK(DasPacket::CMD_STOP),             ST_READY);
    m_stateMachine.addState(ST_WAITING,             SM_ACTION_ERR(DasPacket::CMD_STOP),             ST_ERROR);
    m_stateMachine.addState(ST_WAITING,             SM_ACTION_TIMEOUT(DasPacket::CMD_STOP),         ST_TIMEOUT);

    // Temporary fix to get us from error state
    m_stateMachine.addState(ST_ERROR,               0,                                              ST_NOT_INITIALIZED);
    m_stateMachine.addState(ST_TIMEOUT,             0,                                              ST_NOT_INITIALIZED);

    createParam("HwId",         asynParamInt32, &HardwareId);
    createParam("Status",       asynParamInt32, &Status);
    createParam("Command",      asynParamInt32, &Command);
    setIntegerParam(HardwareId, m_hardwareId);
    setIntegerParam(Status, ST_NOT_INITIALIZED);
    callParamCallbacks();
}

BaseModulePlugin::~BaseModulePlugin()
{}


asynStatus BaseModulePlugin::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
    if (!m_stateMachine.checkTransition(SM_ACTION_CMD(value))) {
        LOG_WARN("Command '%d' not allowed at this time", value);
        return asynError;
    }

    if (pasynUser->reason == Command) {
        switch (value) {
        case DasPacket::CMD_DISCOVER:
            reqDiscover();
            break;
        case DasPacket::CMD_READ_VERSION:
            reqReadVersion();
            break;
        case DasPacket::CMD_READ_STATUS:
            reqReadStatus();
            break;
        case DasPacket::CMD_READ_CONFIG:
            reqReadConfig();
            break;
        case DasPacket::CMD_WRITE_CONFIG:
            reqWriteConfig();
            break;
        case DasPacket::CMD_START:
            reqStart();
            break;
        case DasPacket::CMD_STOP:
            reqStop();
            break;
        case 0:
            break;
        default:
            LOG_WARN("Unrecognized '%d' command", SM_ACTION_CMD(value));
            return asynError;
        }
        m_stateMachine.transition(SM_ACTION_CMD(value));
        setIntegerParam(Status, m_stateMachine.getCurrentState());
        callParamCallbacks();
        return asynSuccess;
    }
    std::map<int, struct ConfigParamDesc>::iterator it = m_configParams.find(pasynUser->reason);
    if (it != m_configParams.end()) {
        uint32_t mask = (0x1ULL << it->second.width) - 1;
        if (static_cast<int>(value & mask) != value) {
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
    bool ack = false;
    DasPacket::CommandType command = packet->getResponseType();

    if (!m_stateMachine.checkTransition(SM_ACTION_ACK(command))) {
        LOG_WARN("Response '0x%X' not allowed in this state, ignoring", command);
        return false;
    }

    switch (command) {
    case DasPacket::CMD_DISCOVER:
        ack = rspDiscover(packet);
        break;
    case DasPacket::CMD_READ_VERSION:
        ack = rspReadVersion(packet);
        break;
    case DasPacket::CMD_READ_CONFIG:
        ack = rspReadConfig(packet);
        break;
    case DasPacket::CMD_READ_STATUS:
        ack = rspReadStatus(packet);
        break;
    case DasPacket::CMD_WRITE_CONFIG:
        ack = rspWriteConfig(packet);
        break;
    case DasPacket::CMD_START:
        ack = rspStart(packet);
        break;
    case DasPacket::CMD_STOP:
        ack = rspStop(packet);
        break;
    default:
        LOG_WARN("Received unhandled response 0x%02X", command);
        return false;
    }

    int action = (ack ? SM_ACTION_ACK(command) : SM_ACTION_ERR(command));
    m_stateMachine.transition(action);
    setIntegerParam(Status, m_stateMachine.getCurrentState());
    callParamCallbacks();
    return true;
}

void BaseModulePlugin::reqDiscover()
{
    sendToDispatcher(DasPacket::CMD_DISCOVER);
    scheduleTimeoutCallback(DasPacket::CMD_DISCOVER, NO_RESPONSE_TIMEOUT);
}

bool BaseModulePlugin::rspDiscover(const DasPacket *packet)
{
    if (!cancelTimeoutCallback()) {
        LOG_WARN("Received DISCOVER response after timeout");
        return false;
    }
    return true;
}

void BaseModulePlugin::reqReadVersion()
{
    sendToDispatcher(DasPacket::CMD_READ_VERSION);
    scheduleTimeoutCallback(DasPacket::CMD_READ_VERSION, NO_RESPONSE_TIMEOUT);
}

bool BaseModulePlugin::rspReadVersion(const DasPacket *packet)
{
    if (!cancelTimeoutCallback()) {
        LOG_WARN("Received READ_VERSION response after timeout");
        return false;
    }
    return true;
}

void BaseModulePlugin::reqReadStatus()
{
    sendToDispatcher(DasPacket::CMD_READ_STATUS);
    scheduleTimeoutCallback(DasPacket::CMD_READ_STATUS, NO_RESPONSE_TIMEOUT);
}

bool BaseModulePlugin::rspReadStatus(const DasPacket *packet)
{
    if (!cancelTimeoutCallback()) {
        LOG_WARN("Received READ_STATUS response after timeout");
        return false;
    }

    if (packet->getPayloadLength() != m_statusPayloadLength) {
        LOG_ERROR("Received wrong READ_STATUS response based on length; received %u, expected %u", packet->getPayloadLength(), m_statusPayloadLength);
        return false;
    }

    const uint32_t *payload = packet->getPayload();
    for (std::map<int, StatusParamDesc>::iterator it=m_statusParams.begin(); it != m_statusParams.end(); it++) {
        int offset = it->second.offset;
        int shift = it->second.shift;
        if (m_behindDsp) {
            shift += (offset % 2 == 0 ? 0 : 16);
            offset /= 2;
        }
        int value = payload[offset] >> shift;
        if ((shift + it->second.width) > 32) {
            value |= payload[offset + 1] << (32 - shift);
        }
        value &= (0x1ULL << it->second.width) - 1;
        setIntegerParam(it->first, value);
    }
    callParamCallbacks();
    return true;
}

void BaseModulePlugin::reqReadConfig()
{
    sendToDispatcher(DasPacket::CMD_READ_CONFIG);
    scheduleTimeoutCallback(DasPacket::CMD_READ_CONFIG, NO_RESPONSE_TIMEOUT);
}

bool BaseModulePlugin::rspReadConfig(const DasPacket *packet)
{
    if (!cancelTimeoutCallback()) {
        LOG_WARN("Received READ_CONFIG response after timeout");
        return false;
    }

    if (m_configPayloadLength == 0)
        recalculateConfigParams();

    if (packet->getPayloadLength() != m_configPayloadLength) {
        LOG_ERROR("Received wrong READ_CONFIG response based on length; received %u, expected %u", packet->getPayloadLength(), m_configPayloadLength);
        return false;
    }

    const uint32_t *payload = packet->getPayload();
    for (std::map<int, ConfigParamDesc>::iterator it=m_configParams.begin(); it != m_configParams.end(); it++) {
        int offset = m_configSectionOffsets[it->second.section] + it->second.offset;
        int shift = it->second.shift;
        if (m_behindDsp) {
            shift += (offset % 2 == 0 ? 0 : 16);
            offset /= 2;
        }
        int value = payload[offset] >> shift;
        if ((shift + it->second.width) > 32) {
            value |= payload[offset + 1] << (32 - shift);
        }
        value &= (0x1ULL << it->second.width) - 1;
        setIntegerParam(it->first, value);
    }
    callParamCallbacks();
    return true;
}

void BaseModulePlugin::reqWriteConfig()
{
    if (m_configPayloadLength == 0)
        recalculateConfigParams();

    uint32_t length = ((m_configPayloadLength + 3) & ~3) / 4;
    uint32_t data[length];
    for (uint32_t i=0; i<length; i++)
        data[i] = 0;

    for (std::map<int, struct ConfigParamDesc>::iterator it=m_configParams.begin(); it != m_configParams.end(); it++) {
        uint32_t offset = m_configSectionOffsets[it->second.section] + it->second.offset;
        uint32_t mask = (0x1ULL << it->second.width) - 1;
        int shift = it->second.shift;
        int value = 0;

        if (getIntegerParam(it->first, &value) != asynSuccess) {
            // This should not happen. It's certainly error when creating and parameters.
            LOG_ERROR("Failed to get parameter %s value", getParamName(it->first));
            return;
        }
        if (static_cast<int>(value & mask) != value) {
            // This should not happen. It's certainly error when setting new value for parameter
            LOG_WARN("Parameter %s value out of range", getParamName(it->first));
        }
        value &= mask;

        if (m_behindDsp) {
            shift += (offset % 2 == 0 ? 0 : 16);
            offset /= 2;
        }

        if (offset >= length) {
            // Unlikely, but rather sure than sorry
            LOG_ERROR("Parameter %s offset out of range", getParamName(it->first));
            continue;
        }

        data[offset] |= value << shift;
        if ((it->second.width + shift) > 32) {
            data[offset+1] |= value >> (it->second.width -(32 - shift + 1));
        }
    }

    sendToDispatcher(DasPacket::CMD_WRITE_CONFIG, data, length);
    scheduleTimeoutCallback(DasPacket::CMD_WRITE_CONFIG, NO_RESPONSE_TIMEOUT);
}

bool BaseModulePlugin::rspWriteConfig(const DasPacket *packet)
{
    if (!cancelTimeoutCallback()) {
        LOG_WARN("Received READ_STATUS response after timeout");
        return false;
    }
    return true;
}

void BaseModulePlugin::reqStart()
{
    sendToDispatcher(DasPacket::CMD_START);
    scheduleTimeoutCallback(DasPacket::CMD_START, NO_RESPONSE_TIMEOUT);
}

bool BaseModulePlugin::rspStart(const DasPacket *packet)
{
    if (!cancelTimeoutCallback()) {
        LOG_WARN("Received CMD_START response after timeout");
        return false;
    }
    return (packet->cmdinfo.command == DasPacket::RSP_ACK);
}

void BaseModulePlugin::reqStop()
{
    sendToDispatcher(DasPacket::CMD_STOP);
    scheduleTimeoutCallback(DasPacket::CMD_STOP, NO_RESPONSE_TIMEOUT);
}

bool BaseModulePlugin::rspStop(const DasPacket *packet)
{
    if (!cancelTimeoutCallback()) {
        LOG_WARN("Received CMD_STPO response after timeout");
        return false;
    }
    return (packet->cmdinfo.command == DasPacket::RSP_ACK);
}

void BaseModulePlugin::createStatusParam(const char *name, uint32_t offset, uint32_t nBits, uint32_t shift)
{
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

    uint32_t length = offset +1;
    if (m_behindDsp && nBits > 16)
        length++;
    uint32_t wordsize = (m_behindDsp ? 2 : 4);
    m_statusPayloadLength = std::max(m_statusPayloadLength, length*wordsize);
}

void BaseModulePlugin::createConfigParam(const char *name, char section, uint32_t offset, uint32_t nBits, uint32_t shift, int value)
{
    int index;
    if (createParam(name, asynParamInt32, &index) != asynSuccess) {
        LOG_ERROR("Module config parameter '%s' cannot be created (already exist?)", name);
        return;
    }
    setIntegerParam(index, value);

    ConfigParamDesc desc;
    desc.section = section;
    desc.offset  = offset;
    desc.shift   = shift;
    desc.width   = nBits;
    desc.initVal = value;
    m_configParams[index] = desc;

    uint32_t length = offset + 1;
    if (m_behindDsp && nBits > 16)
        length++;
    m_configSectionSizes[section] = std::max(m_configSectionSizes[section], length);
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
    DasPacket *packet = DasPacket::create((2+2*length)*sizeof(uint32_t));
    if (packet) {
        uint32_t offset = 0;
        packet->source = DasPacket::HWID_SELF;
        packet->destination = 0;
        packet->cmdinfo.command = command;
        packet->cmdinfo.is_command = true;
        packet->cmdinfo.is_passthru = true;
        packet->cmdinfo.lvds_cmd = true;
        packet->cmdinfo.lvds_start = true;
        packet->cmdinfo.lvds_parity = evenParity(packet->info & 0xFFFFFF);
        if (destination != DasPacket::HWID_BROADCAST) {
            packet->payload[offset] = destination & 0xFFFF;
            packet->payload[offset] |= (evenParity(packet->payload[offset]) << 16);
            offset++;
            packet->payload[offset] = (destination >> 16 ) & 0xFFFF;
            packet->payload[offset] |= (evenParity(packet->payload[offset]) << 16);
            offset++;
        } else {
            packet->payload_length -= 2*sizeof(uint32_t);
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
        } else {
            packet->cmdinfo.lvds_stop = true;
            packet->cmdinfo.lvds_parity ^= 0x1;
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

    if (text.substr(0, 2) == "0x") {
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

float BaseModulePlugin::noResponseCleanup(DasPacket::CommandType command)
{
    m_stateMachine.transition(SM_ACTION_TIMEOUT(command));
    setIntegerParam(Status, m_stateMachine.getCurrentState());
    callParamCallbacks();
    return 0;
}

bool BaseModulePlugin::scheduleTimeoutCallback(DasPacket::CommandType command, double delay)
{
    std::function<float(void)> timeoutCb = std::bind(&BaseModulePlugin::noResponseCleanup, this, command);
    m_timeoutTimer = scheduleCallback(timeoutCb, NO_RESPONSE_TIMEOUT);
    return (m_timeoutTimer);
}

bool BaseModulePlugin::cancelTimeoutCallback()
{
    bool canceled = false;
    if (m_timeoutTimer) {
        canceled = m_timeoutTimer->cancel();
        m_timeoutTimer.reset();
    }
    return canceled;
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
    m_configPayloadLength = m_configSectionOffsets['F'] + m_configSectionSizes['F'];
    int wordsize = (m_behindDsp ? 2 : 4);
    m_configPayloadLength *= wordsize;
}
