#include "RocPlugin.h"
#include "Log.h"

#define NUM_ROCPLUGIN_PARAMS    0 // ((int)(&LAST_ROCPLUGIN_PARAM - &FIRST_ROCPLUGIN_PARAM + 1))
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
    : BaseModulePlugin(portName, dispatcherPortName, hardwareId, true,
                       blocking, NUM_ROCPLUGIN_PARAMS + NUM_ROCPLUGIN_DYNPARAMS)
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

// createStatusParams_v* and createConfigParams_v* functions are implemented in custom files for two
// reasons:
// * easy parsing through scripts in tools/ directory
// * easily compare PVs between ROC versions
