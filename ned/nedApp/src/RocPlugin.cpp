#include "RocPlugin.h"
#include "Log.h"

#include <cstring>

#define HEX_BYTE_TO_DEC(a)      ((((a)&0xFF)/16)*10 + ((a)&0xFF)%16)

EPICS_REGISTER_PLUGIN(RocPlugin, 5, "Port name", string, "Dispatcher port name", string, "Hardware ID", string, "Hw & SW version", string, "Blocking", int);

const unsigned RocPlugin::NUM_ROCPLUGIN_DYNPARAMS       = 500;  //!< Since supporting multiple versions with different number of PVs, this is just a maximum value
const float    RocPlugin::NO_RESPONSE_TIMEOUT           = 1.0;

/**
 * ROC V5 version response format
 */
struct RspReadVersion_v5x {
#ifdef BITFIELD_LSB_FIRST
    unsigned hw_revision:8;     // Board revision number
    unsigned hw_version:8;      // Board version number
    unsigned fw_revision:8;     // Firmware revision number
    unsigned fw_version:8;      // Firmware version number
    unsigned year:16;           // Year
    unsigned day:8;             // Day
    unsigned month:8;           // Month
#else
#error Missing RspReadVersionV5 declaration
#endif // BITFIELD_LSB_FIRST
};

/**
 * ROC V5 fw 5.4 adds vendor field.
 */
struct RspReadVersion_v54 : public RspReadVersion_v5x {
    uint32_t vendor_id;
};

RocPlugin::RocPlugin(const char *portName, const char *dispatcherPortName, const char *hardwareId, const char *version, int blocking)
    : BaseModulePlugin(portName, dispatcherPortName, hardwareId, true, blocking,
                       NUM_ROCPLUGIN_DYNPARAMS, defaultInterfaceMask, defaultInterruptMask)
    , m_version(version)
{
    if (m_version == "v51") {
        createStatusParams_v51();
        createConfigParams_v51();
    } else if (m_version == "v52" || m_version == "v54" || m_version == "v55") {
        createStatusParams_v52();
        createConfigParams_v52();
    } else {
        LOG_ERROR("Unsupported ROC version '%s'", version);
    }

    LOG_DEBUG("Number of configured dynamic parameters: %zu", m_statusParams.size() + m_configParams.size());

    callParamCallbacks();
}

bool RocPlugin::processResponse(const DasPacket *packet)
{
    DasPacket::CommandType command = packet->getResponseType();

    // Once HV command is initiated with CMD_HV_SEND, ROC board first ACKs the CMD_HV_SEND.
    // Train of CMD_HV_RECV packets follow, one character from response per packet.
    // Number ov CMD_HV_RECV packets is dynamic, depending on the response length.
    switch (command) {
    case DasPacket::CMD_HV_SEND:
        return asynSuccess;
    case DasPacket::CMD_HV_RECV:
        rspHvCmd(packet);
        return asynSuccess;
    default:
        return BaseModulePlugin::processResponse(packet);
    }
}

asynStatus RocPlugin::writeOctet(asynUser *pasynUser, const char *value, size_t nChars, size_t *nActual)
{
    // Only serving StreamDevice - puts reason as -1
    if (pasynUser->reason == -1) {
        // StreamDevice is sending entire string => no need to buffer the request.
        reqHvCmd(value, nChars);
        *nActual = nChars;
        return asynSuccess;
    }

    return BaseModulePlugin::writeOctet(pasynUser, value, nChars, nActual);
}

asynStatus RocPlugin::readOctet(asynUser *pasynUser, char *value, size_t nChars, size_t *nActual, int *eomReason)
{
    // Only serving StreamDevice - puts reason as -1
    if (pasynUser->reason == -1) {
        asynStatus status = asynSuccess;

        // Temporarily unlock the device or processResponse() will not run
        this->unlock();

        *nActual = getHvResponse(value, nChars, pasynUser->timeout);

        if (*nActual == 0) {
            status = asynTimeout;
        } else if (eomReason) {
            if (value[*nActual - 1] == '\r') *eomReason |= ASYN_EOM_EOS;
            else if (*nActual == nChars)     *eomReason |= ASYN_EOM_CNT;
        }

        this->lock();

        return status;
    }
    return BaseModulePlugin::readOctet(pasynUser, value, nChars, nActual, eomReason);
}

bool RocPlugin::rspDiscover(const DasPacket *packet)
{
    return (BaseModulePlugin::rspDiscover(packet) &&
            packet->cmdinfo.module_type == DasPacket::MOD_TYPE_ROC);
}

bool RocPlugin::rspReadVersion(const DasPacket *packet)
{
    char date[20];
    size_t len = (m_version == "v54" ? sizeof(RspReadVersion_v54) : sizeof(RspReadVersion_v5x));

    if (!BaseModulePlugin::rspReadVersion(packet))
        return false;

    BaseModulePlugin::Version version;
    if (!parseVersionRsp(packet, version, len)) {
        LOG_WARN("Bad READ_VERSION response");
        return false;
    }

    setIntegerParam(HardwareVer, version.hw_version);
    setIntegerParam(HardwareRev, version.hw_revision);
    setStringParam(HardwareDate, "");
    setIntegerParam(FirmwareVer, version.fw_version);
    setIntegerParam(FirmwareRev, version.fw_revision);
    snprintf(date, sizeof(date), "%04d/%02d/%02d", version.fw_year, version.fw_month, version.fw_day);
    setStringParam(FirmwareDate, date);

    callParamCallbacks();

    if (version.hw_version == 5 && version.fw_version == 5) {
        if (m_version == "v51" && version.fw_revision == 1)
            return true;
        if (m_version == "v52" && version.fw_revision == 2)
            return true;
        if (m_version == "v54" && version.fw_revision == 4)
            return true;
        if (m_version == "v55" && version.fw_revision == 5)
            return true;
    }

    LOG_WARN("Unsupported ROC version");
    return false;
}

bool RocPlugin::parseVersionRsp(const DasPacket *packet, BaseModulePlugin::Version &version, size_t expectedLen)
{
    const RspReadVersion_v5x *response;
    if (expectedLen != 0 && expectedLen != packet->getPayloadLength()) {
        return false;
    } else if (packet->getPayloadLength() == sizeof(RspReadVersion_v5x)) {
        response = reinterpret_cast<const RspReadVersion_v5x*>(packet->getPayload());
    } else if (packet->getPayloadLength() == sizeof(RspReadVersion_v54)) {
        response = reinterpret_cast<const RspReadVersion_v5x*>(packet->getPayload());
    } else {
        return false;
    }
    version.hw_version  = response->hw_version;
    version.hw_revision = response->hw_revision;
    version.hw_year     = 0;
    version.hw_month    = 0;
    version.hw_day      = 0;
    version.fw_version  = response->fw_version;
    version.fw_revision = response->fw_revision;
    version.fw_year     = HEX_BYTE_TO_DEC(response->year >> 8) * 100 + HEX_BYTE_TO_DEC(response->year);
    version.fw_month    = HEX_BYTE_TO_DEC(response->month);
    version.fw_day      = HEX_BYTE_TO_DEC(response->day);

    return true;
}

bool RocPlugin::rspReadConfig(const DasPacket *packet)
{
    if (m_version == "v54") {
        uint8_t buffer[480]; // actual size of the READ_CONFIG v5.4 packet

        if (packet->length() > sizeof(buffer)) {
            LOG_ERROR("Received v5.4 READ_CONFIG response bigger than expected");
            return asynError;
        }

        // Packet in shared queue must not be modified. So we make a copy.
        memcpy(buffer, packet, packet->length());
        packet = reinterpret_cast<const DasPacket *>(buffer);
        const_cast<DasPacket *>(packet)->payload_length -= 4; // This is the only reason we're doing all the buffering
    }

    return BaseModulePlugin::rspReadConfig(packet);
}

void RocPlugin::reqHvCmd(const char *data, uint32_t length)
{
    uint32_t buffer[32] = { 0 }; // Initialize all to 0
    uint32_t bufferLen = length * 2;

    // Every character in protocol needs to be prefixed with a zero byte when sent over OCC
    for (uint32_t i = 0; i < length; i++) {
        buffer[i/2] |= data[i] << (16*(i%2));
    }
    sendToDispatcher(DasPacket::CMD_HV_SEND, buffer, bufferLen);
}

bool RocPlugin::rspHvCmd(const DasPacket *packet)
{
    const uint32_t *payload = packet->getPayload();

    // According to dcomserver, one character per OCC packet is expected.
    // But accept more if present.
    m_hvRecvMutex.lock();
    for (uint32_t i = 0; i < (packet->getPayloadLength()/4); i++) {
        m_hvRecvBuffer.push_back(payload[i] & 0xFF);
    }
    m_hvRecvMutex.unlock();

    return true;
}

size_t RocPlugin::getHvResponse(char *response, size_t size, double timeout)
{
    size_t len = 0;
    bool eom = false;
    epicsTimeStamp expire, now;
    epicsTimeGetCurrent(&expire);
    epicsTimeAddSeconds(&expire, timeout);

    do {
        m_hvRecvMutex.lock();
        for (size_t i = 0; i < std::min(size, m_hvRecvBuffer.size()); i++) {
            char byte = m_hvRecvBuffer.front();
            m_hvRecvBuffer.pop_front();
            *(response++) = byte;
            len++;
            size--;
            if (byte == '\r') {
                if (size > 0)
                    *response = '\0';
                eom = true;
                break;
            }
        }
        m_hvRecvMutex.unlock();

        epicsTimeGetCurrent(&now);
    } while (size > 0 && !eom && epicsTimeLessThan(&now, &expire) != 0);

    return len;
}

// createStatusParams_v* and createConfigParams_v* functions are implemented in custom files for two
// reasons:
// * easy parsing through scripts in tools/ directory
// * easily compare PVs between ROC versions
