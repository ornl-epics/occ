#include "RocPlugin.h"
#include "Log.h"

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
    } else if (m_version == "v52" || m_version == "v54") {
        createStatusParams_v52();
        createConfigParams_v52();
    } else {
        LOG_ERROR("Unsupported ROC version '%s'", version);
    }

    LOG_DEBUG("Number of configured dynamic parameters: %zu", m_statusParams.size() + m_configParams.size());

    callParamCallbacks();
    setCallbacks(true);
}

bool RocPlugin::processResponse(const DasPacket *packet)
{
    DasPacket::CommandType command = packet->getResponseType();

    switch (command) {
    case DasPacket::CMD_HV_SEND:
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
        reqHvCmd(value, nChars);
        m_lastHvRsp.clear();
        *nActual = nChars;
        return asynSuccess;
    }

    return BaseModulePlugin::writeOctet(pasynUser, value, nChars, nActual);
}

asynStatus RocPlugin::readOctet(asynUser *pasynUser, char *value, size_t nChars, size_t *nActual, int *eomReason)
{
    // Only serving StreamDevice - puts reason as -1
    if (pasynUser->reason == -1) {
        // StreamDevice may not request entire response at once.
        // Thus we only copy what was requested and leave the rest for later.
        // Easiest way to do char FIFO is with std::list

        *nActual = m_lastHvRsp.size();
        if (*nActual > nChars)
            *nActual = nChars;
        for (size_t i = 0; i < *nActual; i++) {
            value[i] = m_lastHvRsp.front();
            m_lastHvRsp.pop_front();
        }
        if (eomReason && m_lastHvRsp.size() == 0)
            *eomReason = ASYN_EOM_END;

        return asynSuccess;
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

    if (!BaseModulePlugin::rspReadVersion(packet))
        return false;

    BaseModulePlugin::Version version;
    if (!parseVersionRsp(packet, version)) {
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
    }

    LOG_WARN("Unsupported ROC version");
    return false;
}

bool RocPlugin::parseVersionRsp(const DasPacket *packet, BaseModulePlugin::Version &version)
{
    const RspReadVersion_v5x *response;
    if (packet->getPayloadLength() == sizeof(RspReadVersion_v5x)) {
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

void RocPlugin::reqHvCmd(const char *data, uint32_t length)
{
    uint32_t buffer[32];
    uint32_t bufferLen = length / 2;
    if (length % 2 != 0)
        bufferLen++;

    for (uint32_t i = 0; i < length; i++) {
        if ((i % 2) == 0)
            buffer[bufferLen - 1 - i/2] = 0;
        buffer[bufferLen - 1 - i/2] |= data[i] << (16 - 16*(i%2));
    }
    sendToDispatcher(DasPacket::CMD_HV_SEND, buffer, bufferLen);
}

bool RocPlugin::rspHvCmd(const DasPacket *packet)
{
    const uint32_t *payload = packet->getPayload();

    for (uint32_t i = 0; i < packet->getPayloadLength(); i++) {
        for (int j = 0; j < 32; j+=8) {
            char byte = (payload[i] << j) & 0xFF;
            if (byte == 0x0)
                break;
            m_lastHvRsp.push_back(byte);
        }
    }

    return true;
}

// createStatusParams_v* and createConfigParams_v* functions are implemented in custom files for two
// reasons:
// * easy parsing through scripts in tools/ directory
// * easily compare PVs between ROC versions
