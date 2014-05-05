#include "DspPlugin.h"
#include "Log.h"

#include <epicsAlgorithm.h>
#include <osiSock.h>
#include <string.h>

#include <functional>
#include <string>

#define NUM_DSPPLUGIN_PARAMS ((int)(&LAST_DSPPLUGIN_PARAM - &FIRST_DSPPLUGIN_PARAM + 1))

EPICS_REGISTER_PLUGIN(DspPlugin, 4, "Port name", string, "Dispatcher port name", string, "Hardware ID", string, "Blocking", int);

const unsigned DspPlugin::NUM_DSPPLUGIN_CONFIGPARAMS    = 263;
const unsigned DspPlugin::NUM_DSPPLUGIN_STATUSPARAMS    = 100;
const double DspPlugin::DSP_RESPONSE_TIMEOUT            = 1.0;

DspPlugin::DspPlugin(const char *portName, const char *dispatcherPortName, const char *hardwareId, int blocking)
    : BaseModulePlugin(portName, dispatcherPortName, hardwareId, BaseModulePlugin::CONN_TYPE_OPTICAL,
                       blocking, NUM_DSPPLUGIN_PARAMS + NUM_DSPPLUGIN_CONFIGPARAMS + NUM_DSPPLUGIN_CONFIGPARAMS)
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

    createParam("HwDate",       asynParamOctet, &FirmwareDate);
    createParam("HwVer",        asynParamInt32, &HardwareVer);
    createParam("HwRev",        asynParamInt32, &HardwareRev);
    createParam("FwVer",        asynParamInt32, &FirmwareVer);
    createParam("FwRev",        asynParamInt32, &FirmwareRev);
    createParam("FwDate",       asynParamOctet, &FirmwareDate);
    createParam("Command",      asynParamInt32, &Command);
    createParam("Status",       asynParamInt32, &Status);


    createConfigParams();
//    createStatusParams();

    assert(m_configParams.size() == NUM_DSPPLUGIN_CONFIGPARAMS);

    setIntegerParam(Status, STAT_NOT_INITIALIZED);

    callParamCallbacks();
}

void DspPlugin::reqVersionRead()
{
    sendToDispatcher(DasPacket::CMD_READ_VERSION);
}

void DspPlugin::rspVersionRead(const DasPacket *packet)
{
    char date[20];
    const RspVersion *payload = reinterpret_cast<const RspVersion*>(packet->payload);

    setIntegerParam(HardwareVer, payload->hardware.version);
    setIntegerParam(HardwareRev, payload->hardware.revision);
    snprintf(date, sizeof(date), "20%d/%d/%d", (payload->hardware.year/16)*10 + payload->hardware.year%16,
                                               (payload->hardware.month/16)*10 + payload->hardware.month%16,
                                               (payload->hardware.day/16)*10 + payload->hardware.day%16);
    setStringParam(HardwareDate, date);

    setIntegerParam(FirmwareVer, payload->firmware.version);
    setIntegerParam(FirmwareRev, payload->firmware.revision);
    snprintf(date, sizeof(date), "20%d/%d/%d", (payload->firmware.year/16)*10 + payload->firmware.year%16,
                                               (payload->firmware.month/16)*10 + payload->firmware.month%16,
                                               (payload->firmware.day/16)*10 + payload->firmware.day%16);

    setStringParam(FirmwareDate, date);

    callParamCallbacks();
}

void DspPlugin::reqCfgWrite()
{
    uint32_t size = 0;
    for (int i='A'; i<='F'; i++)
        size += getCfgSectionSize(i);

    uint32_t data[size];
    for (uint32_t i=0; i<size; i++)
        data[i] = 0;

    int offset = 0;
    for (int i='B'; i<='F'; i++) {
        offset += configureSection(i, &data[offset], size - offset);
    }
    sendToDispatcher(DasPacket::CMD_WRITE_CONFIG, data, size*sizeof(uint32_t));
}

void DspPlugin::reqCfgRead()
{
    sendToDispatcher(DasPacket::CMD_READ_CONFIG);
}

void DspPlugin::rspCfgRead(const DasPacket *packet)
{
    uint32_t configSize = 0;
    for (int i='A'; i<='F'; i++)
        configSize += getCfgSectionSize(i);
    if (packet->payload_length < (configSize*sizeof(int32_t))) {
        LOG_ERROR("Configuration response packet too short (%d b, expecting %d b)", packet->payload_length, configSize);
        return;
    }

    for (std::map<int, struct ParamDesc>::iterator it=m_configParams.begin(); it != m_configParams.end(); it++) {
        uint32_t offset = getCfgSectionOffset(it->second.section) + it->second.offset;
        int value = *(packet->payload + offset);
        setIntegerParam(it->first, value);
    }
    callParamCallbacks();
}

void DspPlugin::cfgReset()
{
    for (std::map<int, struct ParamDesc>::iterator it=m_configParams.begin(); it != m_configParams.end(); it++) {
        setIntegerParam(it->first, it->second.initVal);
    }
    callParamCallbacks();
}

asynStatus DspPlugin::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
    if (pasynUser->reason == Command) {
        switch (value) {
        case DSP_CMD_INITIALIZE:
            reqVersionRead();
            break;
        case DSP_CMD_CONFIG_WRITE:
            reqCfgWrite();
            break;
        case DSP_CMD_CONFIG_READ:
            reqCfgRead();
            break;
        case DSP_CMD_CONFIG_RESET:
            cfgReset();
            break;
        case DSP_CMD_NONE:
            break;
        default:
            LOG_ERROR("Unrecognized command '%d'", value);
            return asynError;
        }
        return asynSuccess;
    }
    for (std::map<int, struct ParamDesc>::iterator it=m_configParams.begin(); it != m_configParams.end(); it++) {
        if (it->first == pasynUser->reason) {
            int multiplier = it->second.mask & ~(it->second.mask << 1);
            if ((value * multiplier) & ~(it->second.mask)) {
                LOG_ERROR("Parameter %s value %d out of bounds", getParamName(it->first), value);
                return asynError;
            } else {
                setIntegerParam(it->first, value);
                callParamCallbacks();
                return asynSuccess;
            }
        }
    }
    return BasePlugin::writeInt32(pasynUser, value);
}

asynStatus DspPlugin::readInt32(asynUser *pasynUser, epicsInt32 *value)
{
    for (std::map<int, struct ParamDesc>::iterator it=m_configParams.begin(); it != m_configParams.end(); it++) {
        if (it->first == pasynUser->reason) {
            return getIntegerParam(it->first, value);
        }
    }
    return BasePlugin::readInt32(pasynUser, value);
}

void DspPlugin::processData(const DasPacketList * const packetList)
{
    int nReceived = 0;
    int nProcessed = 0;
    getIntegerParam(RxCount,    &nReceived);
    getIntegerParam(ProcCount,  &nProcessed);

    for (const DasPacket *packet = packetList->first(); packet != 0; packet = packetList->next(packet)) {
        nReceived++;
        // Silently skip packets we're not interested in
        if (!packet->isResponse() || packet->source != m_hardwareId)
            continue;

        switch (packet->cmdinfo.command) {
        case DasPacket::CMD_READ_VERSION:
            rspVersionRead(packet);
            nProcessed++;
            break;
        case DasPacket::CMD_READ_CONFIG:
            rspCfgRead(packet);
            nProcessed++;
            break;
        case DasPacket::CMD_READ_STATUS:
            rspReadStatus(packet);
            nProcessed++;
            break;
        }

        nProcessed++;
    }

    setIntegerParam(RxCount,    nReceived);
    setIntegerParam(ProcCount,  nProcessed);
    callParamCallbacks();
}

uint32_t DspPlugin::configureSection(char section, uint32_t *data, uint32_t count)
{
    uint32_t sectionSize = getCfgSectionSize(section);

    if (count < sectionSize) {
        // TODO: error, exception?
        return 0;
    }

    for (uint32_t i=0; i<sectionSize; i++)
        data[i] = 0;

    for (std::map<int, struct ParamDesc>::iterator it=m_configParams.begin(); it != m_configParams.end(); it++) {
        if (it->second.section == section) {
            if (it->second.offset >= sectionSize) {
                // This should not happen. It's certainly error when creating parameters.
                LOG_ERROR("Parameter %s offset %d is beyond section boundary %d", getParamName(it->first), it->second.offset, sectionSize);
                return 0;
            }
            int multiplier = it->second.mask & ~(it->second.mask << 1);
            int value = 0;
            if (getIntegerParam(it->first, &value) != asynSuccess) {
                // This should not happen. It's certainly error when creating and parameters.
                LOG_ERROR("Failed to get parameter %s value", getParamName(it->first));
                return 0;
            }
            data[it->second.offset] |= (value * multiplier) & it->second.mask;
        }
    }
    return sectionSize;
}

uint32_t DspPlugin::getCfgSectionSize(char section)
{
    switch (section) {
    case 'B':
        return 1;
    case 'C':
        return 0x13 + 1;
    case 'D':
        return 0x47 + 1;
    case 'E':
        return 0x9 + 1;
    case 'F':
        return 1;
    default:
        return 0;
    }
}

uint32_t DspPlugin::getCfgSectionOffset(char section)
{
    uint32_t offset = 0;
    switch (section) {
    case 'F':
        offset += getCfgSectionSize('E');
    case 'E':
        offset += getCfgSectionSize('D');
    case 'D':
        offset += getCfgSectionSize('C');
    case 'C':
        offset += getCfgSectionSize('B');
    case 'B':
        offset += getCfgSectionSize('A');
    default:
        return offset;
    }
}

void DspPlugin::expectResponse(DasPacket::CommandType cmd, std::function<void(const DasPacket *)> &cb, double timeout)
{
    // TODO: introduce state-machine or other response tracking
    std::function<void(void)> timeoutCb = std::bind(&DspPlugin::noResponseCleanup, this, DasPacket::CMD_READ_CONFIG);
    scheduleCallback(timeoutCb, timeout);
}

void DspPlugin::noResponseCleanup(DasPacket::CommandType cmd)
{
    // Since called, no response was obtained in the given time window.
    switch (cmd) {
        case DasPacket::CMD_READ_VERSION:
            LOG_ERROR("Timeout waiting for DSP version response");
            break;
        default:
            break;
    }
}

void DspPlugin::createConfigParam(const char *name, char section, uint32_t offset, uint32_t nBits, uint32_t shift, int value) {

    if ((shift + nBits) > 32) {
        LOG_ERROR("DSP config parameters cannot shift over 32 bits, %d bits shifted for %d requested", nBits, shift);
        return;
    }

    uint32_t mask = ((1ULL << nBits) - 1) << shift;
    int multiplier = mask & ~(mask << 1);
    if ((value * multiplier) & ~mask) {
        LOG_ERROR("DSP config parameter %s value %d out of bounds", name, value);
        return;
    }
    if (offset >= getCfgSectionSize(section)) {
        LOG_ERROR("DSP config parameter %s offset %d out of section bounds", name, offset);
        return;
    }

    int index;
    createParam(name, asynParamInt32, &index);
    setIntegerParam(index, value);
    ParamDesc desc;
    desc.section = section;
    desc.offset  = offset;
    desc.mask    = mask;
    desc.initVal = value;
    m_configParams[index] = desc;
}

void DspPlugin::createConfigParams() {
//      BLXXX:Det:DspX:| sig name  |                           | EPICS record description  | (bi and mbbi description)
    createConfigParam("PixIdOffset", 'B', 0x0,  32,  0, 0); // Pixel id offset by the Event Processor to the position index

    // Chopper parameters
    createConfigParam("Chop0Delay",  'C', 0x0,  32,  0, 0); // Chopper 0 number of 9.4ns cycles to delay the copper reference signal.
    createConfigParam("Chop1Delay",  'C', 0x1,  32,  0, 0); // Chopper 1 number of 9.4ns cycles to delay the copper reference signal.
    createConfigParam("Chop2Delay",  'C', 0x2,  32,  0, 0); // Chopper 2 number of 9.4ns cycles to delay the copper reference signal.
    createConfigParam("Chop3Delay",  'C', 0x3,  32,  0, 0); // Chopper 3 number of 9.4ns cycles to delay the copper reference signal.
    createConfigParam("Chop4Delay",  'C', 0x4,  32,  0, 0); // Chopper 4 number of 9.4ns cycles to delay the copper reference signal.
    createConfigParam("Chop5Delay",  'C', 0x5,  32,  0, 0); // Chopper 5 number of 9.4ns cycles to delay the copper reference signal.
    createConfigParam("Chop6Delay",  'C', 0x6,  32,  0, 0); // Chopper 6 number of 9.4ns cycles to delay the copper reference signal.
    createConfigParam("Chop7Delay",  'C', 0x7,  32,  0, 0); // Chopper 7 number of 9.4ns cycles to delay the copper reference signal.

    createConfigParam("Chop0Freq",   'C', 0x8,   4,  0, 0); // Chopper 0 frequency selector (0=60Hz,1=30Hz,2=20Hz,3=15Hz,4=12.5Hz,5=10Hz,6=7.5Hz,7=6Hz,8=5Hz,9=4Hz,10=3Hz,11=2.4Hz,12=2Hz,13=1.5Hz,14=1.25Hz,15=1Hz)
    createConfigParam("Chop0Freq",   'C', 0x8,   4,  4, 0); // Chopper 1 frequency selector (0=60Hz,1=30Hz,2=20Hz,3=15Hz,4=12.5Hz,5=10Hz,6=7.5Hz,7=6Hz,8=5Hz,9=4Hz,10=3Hz,11=2.4Hz,12=2Hz,13=1.5Hz,14=1.25Hz,15=1Hz)
    createConfigParam("Chop0Freq",   'C', 0x8,   4,  8, 0); // Chopper 2 frequency selector (0=60Hz,1=30Hz,2=20Hz,3=15Hz,4=12.5Hz,5=10Hz,6=7.5Hz,7=6Hz,8=5Hz,9=4Hz,10=3Hz,11=2.4Hz,12=2Hz,13=1.5Hz,14=1.25Hz,15=1Hz)
    createConfigParam("Chop0Freq",   'C', 0x8,   4, 12, 0); // Chopper 3 frequency selector (0=60Hz,1=30Hz,2=20Hz,3=15Hz,4=12.5Hz,5=10Hz,6=7.5Hz,7=6Hz,8=5Hz,9=4Hz,10=3Hz,11=2.4Hz,12=2Hz,13=1.5Hz,14=1.25Hz,15=1Hz)
    createConfigParam("Chop0Freq",   'C', 0x8,   4, 16, 0); // Chopper 4 frequency selector (0=60Hz,1=30Hz,2=20Hz,3=15Hz,4=12.5Hz,5=10Hz,6=7.5Hz,7=6Hz,8=5Hz,9=4Hz,10=3Hz,11=2.4Hz,12=2Hz,13=1.5Hz,14=1.25Hz,15=1Hz)
    createConfigParam("Chop0Freq",   'C', 0x8,   4, 20, 0); // Chopper 5 frequency selector (0=60Hz,1=30Hz,2=20Hz,3=15Hz,4=12.5Hz,5=10Hz,6=7.5Hz,7=6Hz,8=5Hz,9=4Hz,10=3Hz,11=2.4Hz,12=2Hz,13=1.5Hz,14=1.25Hz,15=1Hz)
    createConfigParam("Chop0Freq",   'C', 0x8,   4, 24, 0); // Chopper 6 frequency selector (0=60Hz,1=30Hz,2=20Hz,3=15Hz,4=12.5Hz,5=10Hz,6=7.5Hz,7=6Hz,8=5Hz,9=4Hz,10=3Hz,11=2.4Hz,12=2Hz,13=1.5Hz,14=1.25Hz,15=1Hz)
    createConfigParam("Chop0Freq",   'C', 0x8,   4, 28, 0); // Chopper 7 frequency selector (0=60Hz,1=30Hz,2=20Hz,3=15Hz,4=12.5Hz,5=10Hz,6=7.5Hz,7=6Hz,8=5Hz,9=4Hz,10=3Hz,11=2.4Hz,12=2Hz,13=1.5Hz,14=1.25Hz,15=1Hz)

    createConfigParam("ChopDutyCycl",'C', 0x9,  32,  0, 83400); // Number of 100ns cycles to hold reference pulse in logic '1' state.
    createConfigParam("ChopMaxPerio",'C', 0xA,  32,  0, 166800); // Number of 100ns cycles expected between master timing reference pulses.
    createConfigParam("ChopFixedOff",'C', 0xB,  32,  0, 0); // Chopper TOF fixed offset - todo: make proper record links to chopper ioc

    createConfigParam("ChopRtdlFr6", 'C', 0xC,   8,  8, 5); // RTDL Frame to load into RTDL Data RAM Address 6
    createConfigParam("ChopRtdlFr7", 'C', 0xC,   8, 16, 6); // RTDL Frame to load into RTDL Data RAM Address 7
    createConfigParam("ChopRtdlFr8", 'C', 0xC,   8, 24, 7); // RTDL Frame to load into RTDL Data RAM Address 8
    createConfigParam("ChopRtdlFr9", 'C', 0xD,   8,  0, 8); // RTDL Frame to load into RTDL Data RAM Address 9
    createConfigParam("ChopRtdlFr10",'C', 0xD,   8,  8, 15); // RTDL Frame to load into RTDL Data RAM Address 10
    createConfigParam("ChopRtdlFr11",'C', 0xD,   8, 16, 17); // RTDL Frame to load into RTDL Data RAM Address 11
    createConfigParam("ChopRtdlFr12",'C', 0xD,   8, 24, 24); // RTDL Frame to load into RTDL Data RAM Address 12
    createConfigParam("ChopRtdlFr13",'C', 0xE,   8,  0, 25); // RTDL Frame to load into RTDL Data RAM Address 13
    createConfigParam("ChopRtdlFr14",'C', 0xE,   8,  8, 26); // RTDL Frame to load into RTDL Data RAM Address 14
    createConfigParam("ChopRtdlFr15",'C', 0xE,   8, 16, 28); // RTDL Frame to load into RTDL Data RAM Address 15
    createConfigParam("ChopRtdlFr16",'C', 0xC,   8, 24, 29); // RTDL Frame to load into RTDL Data RAM Address 16
    createConfigParam("ChopRtdlFr17",'C', 0xC,   8,  0, 30); // RTDL Frame to load into RTDL Data RAM Address 17
    createConfigParam("ChopRtdlFr18",'C', 0xC,   8,  8, 31); // RTDL Frame to load into RTDL Data RAM Address 18
    createConfigParam("ChopRtdlFr19",'C', 0xC,   8, 16, 32); // RTDL Frame to load into RTDL Data RAM Address 19
    createConfigParam("ChopRtdlFr20",'C', 0xC,   8, 24, 33); // RTDL Frame to load into RTDL Data RAM Address 20
    createConfigParam("ChopRtdlFr21",'C', 0xC,   8,  0, 34); // RTDL Frame to load into RTDL Data RAM Address 21
    createConfigParam("ChopRtdlFr22",'C', 0xC,   8,  8, 35); // RTDL Frame to load into RTDL Data RAM Address 22
    createConfigParam("ChopRtdlFr23",'C', 0xC,   8, 16, 36); // RTDL Frame to load into RTDL Data RAM Address 23
    createConfigParam("ChopRtdlFr24",'C', 0xC,   8, 24, 37); // RTDL Frame to load into RTDL Data RAM Address 24
    createConfigParam("ChopRtdlFr25",'C', 0xC,   8,  0, 38); // RTDL Frame to load into RTDL Data RAM Address 25
    createConfigParam("ChopRtdlFr26",'C', 0xC,   8,  8, 39); // RTDL Frame to load into RTDL Data RAM Address 26
    createConfigParam("ChopRtdlFr27",'C', 0xC,   8, 16, 40); // RTDL Frame to load into RTDL Data RAM Address 27
    createConfigParam("ChopRtdlFr28",'C', 0xC,   8, 24, 41); // RTDL Frame to load into RTDL Data RAM Address 28
    createConfigParam("ChopRtdlFr29",'C', 0xC,   8,  0, 1); // RTDL Frame to load into RTDL Data RAM Address 29
    createConfigParam("ChopRtdlFr30",'C', 0xC,   8,  8, 2); // RTDL Frame to load into RTDL Data RAM Address 30
    createConfigParam("ChopRtdlFr31",'C', 0xC,   8, 16, 3); // RTDL Frame to load into RTDL Data RAM Address 31

    createConfigParam("ChopTrefTrig",'C', 0x13,  2,  0, 1); // Chopper TREF trigger select (0=Extract,1=Cycle Start,2=Beam On, 3=Event equals TREF event)
    createConfigParam("ChopTrefFreq",'C', 0x13,  4,  2, 0); // TREF frequency select (0=60Hz,1=30Hz,2=20Hz,3=15Hz,4=12.5Hz,5=10Hz,6=7.5Hz,7=6Hz,8=5Hz,9=4Hz,10=3Hz,11=2.4Hz,12=2Hz,13=1.5Hz,14=1.25Hz,15=1Hz)
    createConfigParam("ChopRtdlOff", 'C', 0x13,  4,  8, 0); // Chopper RTDL frame offset
    createConfigParam("ChopTrefEvNo",'C', 0x13,  8, 12, 39); // Chopper TREF event number to trigger RTEF RTDL strobe (range 0-255)
    createConfigParam("ChopHystMinL",'C', 0x13,  4, 20, 4); // Chopper HYST minimum low (range 0-7)
    createConfigParam("ChopHystMinH",'C', 0x13,  4, 24, 4); // Chopper HYST minimum high (range 0-7)
    createConfigParam("ChopFreqCnt", 'C', 0x13,  2, 28, 1); // Chopper frequency count control (0=enable strobe at cycle X, 1=at cycle X-1, 2=at cycle X-2)
    createConfigParam("ChopFreqCyc", 'C', 0x13,  1, 30, 1); // Chopper frequency cycle select (0=Present cycle number, 1=Next cycle number)
    createConfigParam("ChopSweepEn", 'C', 0x13,  1, 31, 0); // Chopper sweep enable (0=TOF fixed offset & Strobe input, 1=TOF fractional offset & Strobe synthesized)

    // Meta parameters
    createConfigParam("EdgeDetMod0", 'D', 0x0,   2,  0, 0); // Edge Detection mode for chanel 0 (0=disable channel,1=detect rising edge,2=detect falling edge,3=detect both edges)
    createConfigParam("EdgeDetMod1", 'D', 0x0,   2,  2, 0); // Edge Detection mode for chanel 1 (0=disable channel,1=detect rising edge,2=detect falling edge,3=detect both edges)
    createConfigParam("EdgeDetMod2", 'D', 0x0,   2,  4, 0); // Edge Detection mode for chanel 2 (0=disable channel,1=detect rising edge,2=detect falling edge,3=detect both edges)
    createConfigParam("EdgeDetMod3", 'D', 0x0,   2,  6, 0); // Edge Detection mode for chanel 3 (0=disable channel,1=detect rising edge,2=detect falling edge,3=detect both edges)
    createConfigParam("EdgeDetMod4", 'D', 0x0,   2,  8, 0); // Edge Detection mode for chanel 4 (0=disable channel,1=detect rising edge,2=detect falling edge,3=detect both edges)
    createConfigParam("EdgeDetMod5", 'D', 0x0,   2, 10, 0); // Edge Detection mode for chanel 5 (0=disable channel,1=detect rising edge,2=detect falling edge,3=detect both edges)
    createConfigParam("EdgeDetMod6", 'D', 0x0,   2, 12, 0); // Edge Detection mode for chanel 6 (0=disable channel,1=detect rising edge,2=detect falling edge,3=detect both edges)
    createConfigParam("EdgeDetMod7", 'D', 0x0,   2, 14, 0); // Edge Detection mode for chanel 7 (0=disable channel,1=detect rising edge,2=detect falling edge,3=detect both edges)
    createConfigParam("EdgeDetMod8", 'D', 0x0,   2, 16, 0); // Edge Detection mode for chanel 8 (0=disable channel,1=detect rising edge,2=detect falling edge,3=detect both edges)
    createConfigParam("EdgeDetMod9", 'D', 0x0,   2, 18, 0); // Edge Detection mode for chanel 9 (0=disable channel,1=detect rising edge,2=detect falling edge,3=detect both edges)
    createConfigParam("EdgeDetMod10",'D', 0x0,   2, 20, 0); // Edge Detection mode for chanel 10 (0=disable channel,1=detect rising edge,2=detect falling edge,3=detect both edges)
    createConfigParam("EdgeDetMod11",'D', 0x0,   2, 22, 0); // Edge Detection mode for chanel 11 (0=disable channel,1=detect rising edge,2=detect falling edge,3=detect both edges)
    createConfigParam("EdgeDetMod12",'D', 0x0,   2, 24, 0); // Edge Detection mode for chanel 12 (0=disable channel,1=detect rising edge,2=detect falling edge,3=detect both edges)
    createConfigParam("EdgeDetMod13",'D', 0x0,   2, 26, 0); // Edge Detection mode for chanel 13 (0=disable channel,1=detect rising edge,2=detect falling edge,3=detect both edges)
    createConfigParam("EdgeDetMod14",'D', 0x0,   2, 28, 0); // Edge Detection mode for chanel 14 (0=disable channel,1=detect rising edge,2=detect falling edge,3=detect both edges)
    createConfigParam("EdgeDetMod15",'D', 0x0,   2, 30, 0); // Edge Detection mode for chanel 15 (0=disable channel,1=detect rising edge,2=detect falling edge,3=detect both edges)
    createConfigParam("EdgeDetMod16",'D', 0x1,   2,  0, 0); // Edge Detection mode for chanel 16 (0=disable channel,1=detect rising edge,2=detect falling edge,3=detect both edges)
    createConfigParam("EdgeDetMod17",'D', 0x1,   2,  2, 0); // Edge Detection mode for chanel 17 (0=disable channel,1=detect rising edge,2=detect falling edge,3=detect both edges)
    createConfigParam("EdgeDetMod18",'D', 0x1,   2,  4, 0); // Edge Detection mode for chanel 18 (0=disable channel,1=detect rising edge,2=detect falling edge,3=detect both edges)
    createConfigParam("EdgeDetMod19",'D', 0x1,   2,  6, 0); // Edge Detection mode for chanel 19 (0=disable channel,1=detect rising edge,2=detect falling edge,3=detect both edges)
    createConfigParam("EdgeDetMod20",'D', 0x1,   2,  8, 0); // Edge Detection mode for chanel 20 (0=disable channel,1=detect rising edge,2=detect falling edge,3=detect both edges)
    createConfigParam("EdgeDetMod21",'D', 0x1,   2, 10, 0); // Edge Detection mode for chanel 21 (0=disable channel,1=detect rising edge,2=detect falling edge,3=detect both edges)
    createConfigParam("EdgeDetMod22",'D', 0x1,   2, 12, 0); // Edge Detection mode for chanel 22 (0=disable channel,1=detect rising edge,2=detect falling edge,3=detect both edges)
    createConfigParam("EdgeDetMod23",'D', 0x1,   2, 14, 0); // Edge Detection mode for chanel 23 (0=disable channel,1=detect rising edge,2=detect falling edge,3=detect both edges)
    createConfigParam("EdgeDetMod24",'D', 0x1,   2, 16, 0); // Edge Detection mode for chanel 24 (0=disable channel,1=detect rising edge,2=detect falling edge,3=detect both edges)
    createConfigParam("EdgeDetMod25",'D', 0x1,   2, 18, 0); // Edge Detection mode for chanel 25 (0=disable channel,1=detect rising edge,2=detect falling edge,3=detect both edges)
    createConfigParam("EdgeDetMod26",'D', 0x1,   2, 20, 0); // Edge Detection mode for chanel 26 (0=disable channel,1=detect rising edge,2=detect falling edge,3=detect both edges)
    createConfigParam("EdgeDetMod27",'D', 0x1,   2, 22, 0); // Edge Detection mode for chanel 27 (0=disable channel,1=detect rising edge,2=detect falling edge,3=detect both edges)
    createConfigParam("EdgeDetMod28",'D', 0x1,   2, 24, 0); // Edge Detection mode for chanel 28 (0=disable channel,1=detect rising edge,2=detect falling edge,3=detect both edges)
    createConfigParam("EdgeDetMod29",'D', 0x1,   2, 26, 0); // Edge Detection mode for chanel 29 (0=disable channel,1=detect rising edge,2=detect falling edge,3=detect both edges)
    createConfigParam("EdgeDetMod30",'D', 0x1,   2, 28, 0); // Edge Detection mode for chanel 30 (0=disable channel,1=detect rising edge,2=detect falling edge,3=detect both edges)
    createConfigParam("EdgeDetMod31",'D', 0x1,   2, 30, 0); // Edge Detection mode for chanel 31 (0=disable channel,1=detect rising edge,2=detect falling edge,3=detect both edges)

    createConfigParam("EdgePixId0",  'D', 0x2,  32,  0, 0x50000000); // Edge Pixel id 0 assigned by metadata formatter
    createConfigParam("EdgePixId1",  'D', 0x3,  32,  0, 0x50000002); // Edge Pixel id 1 assigned by metadata formatter
    createConfigParam("EdgePixId2",  'D', 0x4,  32,  0, 0x50000004); // Edge Pixel id 2 assigned by metadata formatter
    createConfigParam("EdgePixId3",  'D', 0x5,  32,  0, 0x50000008); // Edge Pixel id 3 assigned by metadata formatter
    createConfigParam("EdgePixId4",  'D', 0x6,  32,  0, 0x5000000a); // Edge Pixel id 4 assigned by metadata formatter
    createConfigParam("EdgePixId5",  'D', 0x7,  32,  0, 0x5000000c); // Edge Pixel id 5 assigned by metadata formatter
    createConfigParam("EdgePixId6",  'D', 0x8,  32,  0, 0x5000000e); // Edge Pixel id 6 assigned by metadata formatter
    createConfigParam("EdgePixId7",  'D', 0x9,  32,  0, 0x50000010); // Edge Pixel id 7 assigned by metadata formatter
    createConfigParam("EdgePixId8",  'D', 0xA,  32,  0, 0x50000012); // Edge Pixel id 8 assigned by metadata formatter
    createConfigParam("EdgePixId9",  'D', 0xB,  32,  0, 0x50000014); // Edge Pixel id 9 assigned by metadata formatter
    createConfigParam("EdgePixId10", 'D', 0xC,  32,  0, 0x50000016); // Edge Pixel id 10 assigned by metadata formatter
    createConfigParam("EdgePixId11", 'D', 0xD,  32,  0, 0x50000018); // Edge Pixel id 11 assigned by metadata formatter
    createConfigParam("EdgePixId12", 'D', 0xE,  32,  0, 0x5000001a); // Edge Pixel id 12 assigned by metadata formatter
    createConfigParam("EdgePixId13", 'D', 0xF,  32,  0, 0x5000001c); // Edge Pixel id 13 assigned by metadata formatter
    createConfigParam("EdgePixId14", 'D', 0x10, 32,  0, 0x5000001e); // Edge Pixel id 14 assigned by metadata formatter
    createConfigParam("EdgePixId15", 'D', 0x11, 32,  0, 0x50000006); // Edge Pixel id 15 assigned by metadata formatter
    createConfigParam("EdgePixId16", 'D', 0x12, 32,  0, 0); // Edge Pixel id 16 assigned by metadata formatter
    createConfigParam("EdgePixId17", 'D', 0x13, 32,  0, 0); // Edge Pixel id 17 assigned by metadata formatter
    createConfigParam("EdgePixId18", 'D', 0x14, 32,  0, 0); // Edge Pixel id 18 assigned by metadata formatter
    createConfigParam("EdgePixId19", 'D', 0x15, 32,  0, 0); // Edge Pixel id 19 assigned by metadata formatter
    createConfigParam("EdgePixId20", 'D', 0x16, 32,  0, 0); // Edge Pixel id 20 assigned by metadata formatter
    createConfigParam("EdgePixId21", 'D', 0x17, 32,  0, 0); // Edge Pixel id 21 assigned by metadata formatter
    createConfigParam("EdgePixId22", 'D', 0x18, 32,  0, 0); // Edge Pixel id 22 assigned by metadata formatter
    createConfigParam("EdgePixId23", 'D', 0x19, 32,  0, 0); // Edge Pixel id 23 assigned by metadata formatter
    createConfigParam("EdgePixId24", 'D', 0x1A, 32,  0, 0); // Edge Pixel id 24 assigned by metadata formatter
    createConfigParam("EdgePixId25", 'D', 0x1B, 32,  0, 0); // Edge Pixel id 25 assigned by metadata formatter
    createConfigParam("EdgePixId26", 'D', 0x1C, 32,  0, 0); // Edge Pixel id 26 assigned by metadata formatter
    createConfigParam("EdgePixId27", 'D', 0x1D, 32,  0, 0); // Edge Pixel id 27 assigned by metadata formatter
    createConfigParam("EdgePixId28", 'D', 0x1E, 32,  0, 0); // Edge Pixel id 28 assigned by metadata formatter
    createConfigParam("EdgePixId29", 'D', 0x1F, 32,  0, 0); // Edge Pixel id 29 assigned by metadata formatter
    createConfigParam("EdgePixId30", 'D', 0x20, 32,  0, 0); // Edge Pixel id 30 assigned by metadata formatter
    createConfigParam("EdgePixId31", 'D', 0x21, 32,  0, 0); // Edge Pixel id 31 assigned by metadata formatter

    createConfigParam("EdgeCycAdj0", 'D', 0x22,  5,  0, 0); // Edge Cycle number adjustment for channel 0 (range 0-31)
    createConfigParam("EdgeCycAdj1", 'D', 0x22,  5,  5, 0); // Edge Cycle number adjustment for channel 1 (range 0-31)
    createConfigParam("EdgeCycAdj2", 'D', 0x22,  5, 10, 0); // Edge Cycle number adjustment for channel 2 (range 0-31)
    createConfigParam("EdgeCycAdj3", 'D', 0x22,  5, 15, 0); // Edge Cycle number adjustment for channel 3 (range 0-31)
    createConfigParam("EdgeCycAdj4", 'D', 0x22,  5, 20, 0); // Edge Cycle number adjustment for channel 4 (range 0-31)
    createConfigParam("EdgeCycAdj5", 'D', 0x22,  5, 25, 0); // Edge Cycle number adjustment for channel 5 (range 0-31)
    createConfigParam("EdgeCycAdj6", 'D', 0x23,  5,  0, 0); // Edge Cycle number adjustment for channel 6 (range 0-31)
    createConfigParam("EdgeCycAdj7", 'D', 0x23,  5,  5, 0); // Edge Cycle number adjustment for channel 7 (range 0-31)
    createConfigParam("EdgeCycAdj8", 'D', 0x23,  5, 10, 0); // Edge Cycle number adjustment for channel 8 (range 0-31)
    createConfigParam("EdgeCycAdj9", 'D', 0x23,  5, 15, 0); // Edge Cycle number adjustment for channel 9 (range 0-31)
    createConfigParam("EdgeCycAdj10",'D', 0x23,  5, 20, 0); // Edge Cycle number adjustment for channel 10 (range 0-31)
    createConfigParam("EdgeCycAdj11",'D', 0x23,  5, 25, 0); // Edge Cycle number adjustment for channel 11 (range 0-31)
    createConfigParam("EdgeCycAdj12",'D', 0x24,  5,  0, 0); // Edge Cycle number adjustment for channel 12 (range 0-31)
    createConfigParam("EdgeCycAdj13",'D', 0x24,  5,  5, 0); // Edge Cycle number adjustment for channel 13 (range 0-31)
    createConfigParam("EdgeCycAdj14",'D', 0x24,  5, 10, 0); // Edge Cycle number adjustment for channel 14 (range 0-31)
    createConfigParam("EdgeCycAdj15",'D', 0x24,  5, 15, 0); // Edge Cycle number adjustment for channel 15 (range 0-31)
    createConfigParam("EdgeCycAdj16",'D', 0x24,  5, 20, 0); // Edge Cycle number adjustment for channel 16 (range 0-31)
    createConfigParam("EdgeCycAdj17",'D', 0x24,  5, 25, 0); // Edge Cycle number adjustment for channel 17 (range 0-31)
    createConfigParam("EdgeCycAdj18",'D', 0x25,  5,  0, 0); // Edge Cycle number adjustment for channel 18 (range 0-31)
    createConfigParam("EdgeCycAdj19",'D', 0x25,  5,  5, 0); // Edge Cycle number adjustment for channel 19 (range 0-31)
    createConfigParam("EdgeCycAdj20",'D', 0x25,  5, 10, 0); // Edge Cycle number adjustment for channel 20 (range 0-31)
    createConfigParam("EdgeCycAdj21",'D', 0x25,  5, 15, 0); // Edge Cycle number adjustment for channel 21 (range 0-31)
    createConfigParam("EdgeCycAdj22",'D', 0x25,  5, 20, 0); // Edge Cycle number adjustment for channel 22 (range 0-31)
    createConfigParam("EdgeCycAdj23",'D', 0x25,  5, 25, 0); // Edge Cycle number adjustment for channel 23 (range 0-31)
    createConfigParam("EdgeCycAdj24",'D', 0x26,  5,  0, 0); // Edge Cycle number adjustment for channel 24 (range 0-31)
    createConfigParam("EdgeCycAdj25",'D', 0x26,  5,  5, 0); // Edge Cycle number adjustment for channel 25 (range 0-31)
    createConfigParam("EdgeCycAdj26",'D', 0x26,  5, 10, 0); // Edge Cycle number adjustment for channel 26 (range 0-31)
    createConfigParam("EdgeCycAdj27",'D', 0x26,  5, 15, 0); // Edge Cycle number adjustment for channel 27 (range 0-31)
    createConfigParam("EdgeCycAdj28",'D', 0x26,  5, 20, 0); // Edge Cycle number adjustment for channel 28 (range 0-31)
    createConfigParam("EdgeCycAdj29",'D', 0x26,  5, 25, 0); // Edge Cycle number adjustment for channel 29 (range 0-31)
    createConfigParam("EdgeCycAdj30",'D', 0x27,  5,  0, 0); // Edge Cycle number adjustment for channel 30 (range 0-31)
    createConfigParam("EdgeCycAdj31",'D', 0x27,  5,  5, 0); // Edge Cycle number adjustment for channel 31 (range 0-31)

    createConfigParam("EdgeDelay0",  'D', 0x28, 32,  0, 0); // Edge delay 0
    createConfigParam("EdgeDelay1",  'D', 0x29, 32,  0, 0); // Edge delay 1
    createConfigParam("EdgeDelay2",  'D', 0x2A, 32,  0, 0); // Edge delay 2
    createConfigParam("EdgeDelay3",  'D', 0x2B, 32,  0, 0); // Edge delay 3
    createConfigParam("EdgeDelay4",  'D', 0x2C, 32,  0, 0); // Edge delay 4
    createConfigParam("EdgeDelay5",  'D', 0x2D, 32,  0, 0); // Edge delay 5
    createConfigParam("EdgeDelay6",  'D', 0x2E, 32,  0, 0); // Edge delay 6
    createConfigParam("EdgeDelay7",  'D', 0x2F, 32,  0, 0); // Edge delay 7
    createConfigParam("EdgeDelay8",  'D', 0x30, 32,  0, 0); // Edge delay 8
    createConfigParam("EdgeDelay9",  'D', 0x31, 32,  0, 0); // Edge delay 9
    createConfigParam("EdgeDelay10", 'D', 0x32, 32,  0, 0); // Edge delay 10
    createConfigParam("EdgeDelay11", 'D', 0x33, 32,  0, 0); // Edge delay 11
    createConfigParam("EdgeDelay12", 'D', 0x34, 32,  0, 0); // Edge delay 12
    createConfigParam("EdgeDelay13", 'D', 0x35, 32,  0, 0); // Edge delay 13
    createConfigParam("EdgeDelay14", 'D', 0x36, 32,  0, 0); // Edge delay 14
    createConfigParam("EdgeDelay15", 'D', 0x37, 32,  0, 0); // Edge delay 15
    createConfigParam("EdgeDelay16", 'D', 0x38, 32,  0, 0); // Edge delay 16
    createConfigParam("EdgeDelay17", 'D', 0x39, 32,  0, 0); // Edge delay 17
    createConfigParam("EdgeDelay18", 'D', 0x3A, 32,  0, 0); // Edge delay 18
    createConfigParam("EdgeDelay19", 'D', 0x3B, 32,  0, 0); // Edge delay 19
    createConfigParam("EdgeDelay20", 'D', 0x3C, 32,  0, 0); // Edge delay 20
    createConfigParam("EdgeDelay21", 'D', 0x3D, 32,  0, 0); // Edge delay 21
    createConfigParam("EdgeDelay22", 'D', 0x3E, 32,  0, 0); // Edge delay 22
    createConfigParam("EdgeDelay23", 'D', 0x3F, 32,  0, 0); // Edge delay 23
    createConfigParam("EdgeDelay24", 'D', 0x40, 32,  0, 0); // Edge delay 24
    createConfigParam("EdgeDelay25", 'D', 0x41, 32,  0, 0); // Edge delay 25
    createConfigParam("EdgeDelay26", 'D', 0x42, 32,  0, 0); // Edge delay 26
    createConfigParam("EdgeDelay27", 'D', 0x43, 32,  0, 0); // Edge delay 27
    createConfigParam("EdgeDelay28", 'D', 0x44, 32,  0, 0); // Edge delay 28
    createConfigParam("EdgeDelay29", 'D', 0x45, 32,  0, 0); // Edge delay 29
    createConfigParam("EdgeDelay30", 'D', 0x46, 32,  0, 0); // Edge delay 30
    createConfigParam("EdgeDelay31", 'D', 0x47, 32,  0, 0); // Edge delay 31

    // LVDS & optical parameters
    createConfigParam("LvdsRxTxen0", 'E', 0x0,  1,  0, 1); // LVDS receiver transmit enable for channel 0 (0=sourced by flow control,1=active)
    createConfigParam("LvdsRxTxen1", 'E', 0x0,  1,  3, 0); // LVDS receiver transmit enable for channel 1 (0=sourced by flow control,1=active)
    createConfigParam("LvdsRxTxen2", 'E', 0x0,  1,  6, 0); // LVDS receiver transmit enable for channel 2 (0=sourced by flow control,1=active)
    createConfigParam("LvdsRxTxen3", 'E', 0x0,  1,  9, 0); // LVDS receiver transmit enable for channel 3 (0=sourced by flow control,1=active)
    createConfigParam("LvdsRxTxen4", 'E', 0x0,  1, 12, 0); // LVDS receiver transmit enable for channel 4 (0=sourced by flow control,1=active)
    createConfigParam("LvdsRxTxen5", 'E', 0x0,  1, 15, 0); // LVDS receiver transmit enable for channel 5 (0=sourced by flow control,1=active)
    createConfigParam("LvdsRxNoEr0", 'E', 0x0,  1,  1, 0); // LVDS ignore errors (0=discard erronous packet,1=keep all packets)
    createConfigParam("LvdsRxNoEr1", 'E', 0x0,  1,  4, 0); // LVDS ignore errors (0=discard erronous packet,1=keep all packets)
    createConfigParam("LvdsRxNoEr2", 'E', 0x0,  1,  7, 0); // LVDS ignore errors (0=discard erronous packet,1=keep all packets)
    createConfigParam("LvdsRxNoEr3", 'E', 0x0,  1, 10, 0); // LVDS ignore errors (0=discard erronous packet,1=keep all packets)
    createConfigParam("LvdsRxNoEr4", 'E', 0x0,  1, 13, 0); // LVDS ignore errors (0=discard erronous packet,1=keep all packets)
    createConfigParam("LvdsRxNoEr5", 'E', 0x0,  1, 16, 0); // LVDS ignore errors (0=discard erronous packet,1=keep all packets)
    createConfigParam("LvdsRxDis0",  'E', 0x0,  1,  2, 0); // LVDS disable channel 0 (0=allow packets,1=disable packet processing)
    createConfigParam("LvdsRxDis1",  'E', 0x0,  1,  5, 0); // LVDS disable channel 1 (0=allow packets,1=disable packet processing)
    createConfigParam("LvdsRxDis2",  'E', 0x0,  1,  8, 0); // LVDS disable channel 2 (0=allow packets,1=disable packet processing)
    createConfigParam("LvdsRxDis3",  'E', 0x0,  1, 11, 0); // LVDS disable channel 3 (0=allow packets,1=disable packet processing)
    createConfigParam("LvdsRxDis4",  'E', 0x0,  1, 14, 0); // LVDS disable channel 4 (0=allow packets,1=disable packet processing)
    createConfigParam("LvdsRxDis5",  'E', 0x0,  1, 17, 0); // LVDS disable channel 5 (0=allow packets,1=disable packet processing)
    createConfigParam("LvdsRxCmdMod",'E', 0x0,  1, 18, 0); // LVDS sorter FIFO parser mode for commands (0=DDPLP0 bit 16 set => command,1=data otherwise)
    createConfigParam("LvdsRxDatMod",'E', 0x0,  1, 19, 0); // LVDS sorter FIFO parser mode for data (0=DDPLP0 bit 16 set => data,1=command otherwise)
    createConfigParam("LvdsRxDatSiz",'E', 0x0,  8, 20, 4); // LVDS Number of Words in Channel Link Data Packet

    createConfigParam("LvdsCmdFilt", 'E', 0x1, 16,  0, 0xFFFF); // LVDS command to filter
    createConfigParam("LvdsCmdFiltM",'E', 0x1, 16, 16, 0); // LVDS command filter mask

    createConfigParam("LvdsTxTclkMo",'E', 0x2,  1,  0, 0); // LVDS transmit control TCLK mode (0=TCLK generated locally from 40MHz clock,1=TCLK generated from LVDS input clock)
    createConfigParam("LvdsTxTcCtrl",'E', 0x2,  2,  2, 0); // LVDS T&C TCLK control (0-1=TCLK,2=hardwired to 0,3=hardwired=1)
    createConfigParam("LvdsTscynoMo",'E', 0x2,  2,  4, 3); // LVDS TSYNC_O mode (0=TSYNC generated locally,1=TSYNC generated from TRefFreqStrb,2=TSYNC generated from LVDS TSYNC input,3=TSYNC generated from optical T&C interface)
    createConfigParam("LvdsTxTsyncC",'E', 0x2,  2,  6, 0); // LVDS TSYNC_NORMAL control (0=Pulse width modulated based on polarity state,1=Pulse width set by TSYNC_WIDTH,2=hardwired to 0,3=hardwired to 1)
    createConfigParam("LvdsTxSysrCt",'E', 0x2,  2,  8, 0); // LVDS T&C SYSRST# buffer control (0-1=sysrst#,2=hardwired to 0,3=hardwired to 1)
    createConfigParam("LvdsTxTxenCt",'E', 0x2,  2, 10, 0); // LVDS T&C TXEN# control when LVDS_TX_TC_TEST_PATTERN enabled (0-1=TXEN# is driven by Channel Link Packet Parser,2=TXEN# is driven by Channel Link RX FIFO Disable Channel mode bit,3=TXEN# is driven by inverted Channel Link RX FIFO Disable Channel mode bit)
    createConfigParam("LvdsTxOutClk",'E', 0x2,  2, 16, 0); // LVDS output clock mode (range 0-3)
    createConfigParam("LvdsTxCmdRet",'E', 0x2,  2, 18, 3); // LVDS Number of times to issue a downstream (DISCOVER, LVDS_VERIFY) command (range 0-3)
    createConfigParam("LvdsTxWrdL0", 'E', 0x2,  1, 20, 0); // LVDS channel 0 data word length (0=set by LVDS_RX_FIFO_MODE_DATA,1=set to 4)
    createConfigParam("LvdsTxWrdL1", 'E', 0x2,  1, 21, 0); // LVDS channel 1 data word length (0=set by LVDS_RX_FIFO_MODE_DATA,1=set to 4)
    createConfigParam("LvdsTxWrdL2", 'E', 0x2,  1, 22, 0); // LVDS channel 2 data word length (0=set by LVDS_RX_FIFO_MODE_DATA,1=set to 4)
    createConfigParam("LvdsTxWrdL3", 'E', 0x2,  1, 23, 0); // LVDS channel 3 data word length (0=set by LVDS_RX_FIFO_MODE_DATA,1=set to 4)
    createConfigParam("LvdsTxWrdL4", 'E', 0x2,  1, 24, 0); // LVDS channel 4 data word length (0=set by LVDS_RX_FIFO_MODE_DATA,1=set to 4)
    createConfigParam("LvdsTxWrdL5", 'E', 0x2,  1, 25, 0); // LVDS channel 5 data word length (0=set by LVDS_RX_FIFO_MODE_DATA,1=set to 4)
    createConfigParam("LvdsTxClkMar",'E', 0x2,  2, 26, 0); // LVDS clock margin (allowed values 0-3)
    createConfigParam("LvdsTxTestPa",'E', 0x2,  1, 30, 0); // LVDS T&C test pattern (0=disable,1=enable) TODO: need to verify values
    createConfigParam("LvdsTxTestEn",'E', 0x2,  1, 31, 0); // LVDS test enable (0=disable,1=enable) TODO: need to verify values

    createConfigParam("LvdsTsyncSrc0",'E', 0x3,  2,  0, 0); // LVDS TSYNC T&C source control for channels 0 (0=TSYNC_NORMAL, pulse width stretched according to LVDS_TSYNC_WIDTH,1=TSYNC_LOCAL, pulse width stretched according to LVDS_TSYNC_WIDTH,2=TSYNC_LOCAL, no pulse stretching,3=TRefStrbFixed,pulse width stretched according to LVDS_TSYNC_WIDTH
    createConfigParam("LvdsTsyncSrc1",'E', 0x3,  2,  2, 0); // LVDS TSYNC T&C source control for channels 1 (0=TSYNC_NORMAL, pulse width stretched according to LVDS_TSYNC_WIDTH,1=TSYNC_LOCAL, pulse width stretched according to LVDS_TSYNC_WIDTH,2=TSYNC_LOCAL, no pulse stretching,3=TRefStrbFixed,pulse width stretched according to LVDS_TSYNC_WIDTH
    createConfigParam("LvdsTsyncSrc2",'E', 0x3,  2,  4, 0); // LVDS TSYNC T&C source control for channels 2 (0=TSYNC_NORMAL, pulse width stretched according to LVDS_TSYNC_WIDTH,1=TSYNC_LOCAL, pulse width stretched according to LVDS_TSYNC_WIDTH,2=TSYNC_LOCAL, no pulse stretching,3=TRefStrbFixed,pulse width stretched according to LVDS_TSYNC_WIDTH
    createConfigParam("LvdsTsyncSrc3",'E', 0x3,  2,  6, 0); // LVDS TSYNC T&C source control for channels 3 (0=TSYNC_NORMAL, pulse width stretched according to LVDS_TSYNC_WIDTH,1=TSYNC_LOCAL, pulse width stretched according to LVDS_TSYNC_WIDTH,2=TSYNC_LOCAL, no pulse stretching,3=TRefStrbFixed,pulse width stretched according to LVDS_TSYNC_WIDTH
    createConfigParam("LvdsTsyncSrc4",'E', 0x3,  2,  8, 0); // LVDS TSYNC T&C source control for channels 4 (0=TSYNC_NORMAL, pulse width stretched according to LVDS_TSYNC_WIDTH,1=TSYNC_LOCAL, pulse width stretched according to LVDS_TSYNC_WIDTH,2=TSYNC_LOCAL, no pulse stretching,3=TRefStrbFixed,pulse width stretched according to LVDS_TSYNC_WIDTH
    createConfigParam("LvdsTsyncSrc5",'E', 0x3,  2, 10, 0); // LVDS TSYNC T&C source control for channels 5 (0=TSYNC_NORMAL, pulse width stretched according to LVDS_TSYNC_WIDTH,1=TSYNC_LOCAL, pulse width stretched according to LVDS_TSYNC_WIDTH,2=TSYNC_LOCAL, no pulse stretching,3=TRefStrbFixed,pulse width stretched according to LVDS_TSYNC_WIDTH
    createConfigParam("LvdsTsyncMeta",'E', 0x3,  2, 14, 0); // LVDS TSYNC metadata source control (0=RTDL,1=LVDS,2=detector TSYNC,3=OFB[0])

    createConfigParam("LvdsTsyncGen", 'E', 0x4, 32,  0, 20000); // LVDS TSYNC generation divisor, 40MHz clock is divided by this value to obtain TSYNC period
    createConfigParam("LvdsTsyncDely",'E', 0x5, 32,  0, 0); // LVDS TSYNC delay divisor (106.25MHz/this value)
    createConfigParam("LvdsTsyncWid", 'E', 0x6, 32,  0, 0); // LVDS TSYNC width divisor, width of TSYNC_NORMAL pulse = (10MHz/this value)

    createConfigParam("OptCrossSwA",  'E', 0x8,  2,  2, 0); // Crossbar Switch Pass Control for F/O Port A (1=Send to transceiver A,2=send to transceiver B)
    createConfigParam("OptCrossSwB",  'E', 0x8,  2, 10, 0); // Crossbar Switch Pass Control for F/O Port B (1=Send to transceiver A,2=send to transceiver B)
    createConfigParam("OptTxOutModeA",'E', 0x8,  2,  0, 0); // Optical transceiver A output mode (0=Normal,1=Timing,2=Chopper,3=Timing master)
    createConfigParam("OptTxEocA",    'E', 0x8,  1,  4, 0); // Optical transceiver A End of Chain TODO: describe values
    createConfigParam("OptTxCmdFiltA",'E', 0x8,  2,  5, 0); // Optical transceiver A Command Filter TODO: describe values
    createConfigParam("OptTxOutModeB",'E', 0x8,  2,  8, 0); // Optical transceiver B output mode (0=Normal,1=Timing,2=Chopper,3=Timing master)
    createConfigParam("OptTxEocB",    'E', 0x8,  1, 12, 0); // Optical transceiver B End of Chain TODO: describe values
    createConfigParam("OptTxCmdFiltB",'E', 0x8,  2, 13, 0); // Optical transceiver B Command Filter TODO: describe values
    createConfigParam("OptHystEn",    'E', 0x8,  1, 16, 0); // Optical hysteresis enable (0=control signals generated directly from TLK data port,1=control signals generated only if four consecutive samples are identical on the optical port)
    createConfigParam("OptBlankFrmEn",'E', 0x8,  1, 17, 0); // Optical Blank data frame after CRC (0=no blank frame, 1=add blank data frame after)
    createConfigParam("OptTxDelay",   'E', 0x8,  7, 24, 3); // Optical packet send delay. Number of 313ns cycles to wait between DSP packet transmissions
    createConfigParam("OptTxDelayCtr",'E', 0x8,  1, 31, 1); // Optical packet send delay control (0=use OPT_TX_DELAY value,1=word count in previous transmission)

    createConfigParam("OptPktMaxSize",'E', 0x9, 16,  0, 16111); // Optical data packet max size, in dwords (range 0-0xFFFF)
    createConfigParam("OptDataEopEn", 'E', 0x9,  1, 16, 1); // Optical Neutron data send EOP (0=disabled,1=enabled) - should be 1 if DSP submodules >0
    createConfigParam("OptMetaEopEn", 'E', 0x9,  1, 17, 0); // Optical Metadata send EOP (0=disabled,1=enabled) - should be 1 when edge detection enabled

    createConfigParam("SysResetMode", 'F', 0x0,  2,  0, 0); // Reset mode => SYSRST_O# (0-1=not used,2=use SYSRST# from LVDS T&C,3=use SYSRST# from optical T&C)
    createConfigParam("SysStartStopM",'F', 0x0,  3,  4, 0); // Start/Stop mode (0=normal, 1=fake data mode,2-3=not defined)
    createConfigParam("SysFakeTrigEn",'F', 0x0,  1,  7, 0); // Fake metadata trigger enable (0=disabled,1=enabled)
    createConfigParam("SysFastSendEn",'F', 0x0,  1,  8, 0); // Send data immediately switch (0=assemble large packet,wait for reference pulse,1=don't assemble large packets, send immediately)
    createConfigParam("SysPassthruEn",'F', 0x0,  1,  9, 0); // Send a command response for a passthru command (0=don't send response,1=send a response)
    createConfigParam("SysStartAckEn",'F', 0x0,  1, 10, 1); // When set, this bit causes the command handler wait for an acknowledgement from an individual device that it received the Start/Stop command. (0=disable,1=enable)
    createConfigParam("SysRtdlMode",  'F', 0x0,  2, 12, 0); // RTDL mode (0=no RTDL,1=master,2=slave,3=fake mode)
    createConfigParam("SysRtdlOutEnA",'F', 0x0,  1, 14, 1); // RTDL output enable to fiber optic port A (0=disable,1=enable)
    createConfigParam("SysRtdlOutEnB",'F', 0x0,  1, 15, 1); // RTDL output enable to fiber optic port B (0=disable,1=enable)
    createConfigParam("SysTofOffEn",  'F', 0x0,  1, 16, 0); // Enable TOF full offset (0=disable,1=enable)
    createConfigParam("SysFifSyncEn", 'F', 0x0,  1, 17, 0); // FIFO sync switch (0=disable,1=enable)
    createConfigParam("SysRtdlAsData",'F', 0x0,  1, 18, 1); // Send RTDL command as data (0=disable,1=enable)
    createConfigParam("SysFixRtdlEn", 'F', 0x0,  1, 19, 1); // Correct RTDL information (0=disable,1=enable)
    createConfigParam("SysBadPktEn",  'F', 0x0,  1, 30, 0); // Send bad packets (0=disable,1=enable)
    createConfigParam("SysReset",     'F', 0x0,  1, 31, 0); // Force system reset (0=disable,1=enable)
}

void DspPlugin::createStatusParams()
{
//      BLXXX:Det:DspX:| sig name  |                           | EPICS record description  | (bi and mbbi description)
    createStatusParam("Configured",     0x0,  1,  0);
    createStatusParam("AcquireStat",    0x0,  1,  1);
    createStatusParam("ProgramErr",     0x0,  1,  2);
    createStatusParam("PktLenErr",      0x0,  1,  3);
    createStatusParam("UnknwnCmdErr",   0x0,  1,  4);
    createStatusParam("LvdsTxFifFul",   0x0,  1,  5);
    createStatusParam("LvdsCmdErr",     0x0,  1,  6);
    createStatusParam("EepromInitOk",   0x0,  1,  7);
    createStatusParam("FoTransStatA",   0x0,  5, 16);
    createStatusParam("FoTransOutA",    0x0,  2, 22);
    createStatusParam("FoTransStatB",   0x0,  5, 24);
    createStatusParam("FoTransOutB",    0x0,  2, 30);

    createStatusParam("RxNumErrsA",     0x1,  8,  0);
    createStatusParam("RxErrFlagsA",    0x1, 13,  8);
    createStatusParam("RxGoodPacketA",  0x1,  1, 21); // Good packet. When set, this indicates that the last packet received was parsed with no errors.
    createStatusParam("RxPriFifNotEA",  0x1,  1, 22); // Stack FIFO Not Empty
    createStatusParam("RxPriFifAFulA",  0x1,  1, 24); // Stack FIFO Almost Full
    createStatusParam("RxSecFifNotEA",  0x1,  1, 25); // Secondary FIFO Not Empty
    createStatusParam("RxSecFifAFulA",  0x1,  1, 26); // Secondary FIFO Almost Full
    createStatusParam("RxPtCrsbNotEA",  0x1,  1, 27); // Pass-through FIFO to Crossbar Not Empty
    createStatusParam("RxPtCrbarAFuA",  0x1,  1, 28); // Pass-through FIFO to Crossbar Almost Full
    createStatusParam("RxTransTimeA",   0x1,  1, 29); // Timeout in transferring data between the stack and secondary FIFOs.
    createStatusParam("RxPriFifFulA",   0x1,  1, 30); // Packet received while the Stack FIFO is almost full.
    createStatusParam("RxPtCrbarFulA",  0x1,  1, 31); // Packet received while the Pass-through FIFO almost full.

    createStatusParam("RxNumErrsB",     0x2,  8,  0);
    createStatusParam("RxErrFlagsB",    0x2, 13,  8);
    createStatusParam("RxGoodPacketB",  0x2,  1, 21); // Good packet. When set, this indicates that the last packet received was parsed with no errors.
    createStatusParam("RxPriFifNotEB",  0x2,  1, 22); // Stack FIFO Not Empty
    createStatusParam("RxPriFifAFulB",  0x2,  1, 24); // Stack FIFO Almost Full
    createStatusParam("RxSecFifNotEB",  0x2,  1, 25); // Secondary FIFO Not Empty
    createStatusParam("RxSecFifAFulB",  0x2,  1, 26); // Secondary FIFO Almost Full
    createStatusParam("RxPtCrsbNotEB",  0x2,  1, 27); // Pass-through FIFO to Crossbar Not Empty
    createStatusParam("RxPtCrbarAFuB",  0x2,  1, 28); // Pass-through FIFO to Crossbar Almost Full
    createStatusParam("RxTransTimeB",   0x2,  1, 29); // Timeout in transferring data between the stack and secondary FIFOs.
    createStatusParam("RxPriFifFulB",   0x2,  1, 30); // Packet received while the Stack FIFO is almost full.
    createStatusParam("RxPtCrbarFulB",  0x2,  1, 31); // Packet received while the Pass-through FIFO almost full.

    createStatusParam("Ch0RxFlags",     0x3,  8,  0); // Error flags (0=parity error,1=packet type incosistent,2=first word/last word both set,3=packet size over 300 words,4=parser FIFO timeout,5=missing first word,6=first word before last word,7=out FIFO full)
    createStatusParam("Ch0RxStatus",    0x3,  2,  8); // Status OK (0=good cmd packet,1=good data packet)
    createStatusParam("Ch0RxHasData",   0x3,  1, 10); // External FIFO contains data (not empty)
    createStatusParam("Ch0RxFifoAlFu",  0x3,  1, 11); // External FIFO almost full
    createStatusParam("Ch0RxParsHD",    0x3,  1, 12); // Channel Link Packet Parser FIFO contains data
    createStatusParam("Ch0RxParsAF",    0x3,  1, 13); // Channel Link Packet Parser FIFO is almost full
    createStatusParam("Ch0ExtFifEn",    0x3,  1, 14); // External FIFO Read Enable asserted.
    createStatusParam("Ch0ParsFifEn",   0x3,  1, 15); // Channel Link Packet Parser FIFO Write Enable asserted
    createStatusParam("Ch0NumErrors",   0x3, 16, 16); // Number of data packets with errors detected

    createStatusParam("Ch1RxFlags",     0x4,  8,  0); // Error flags (0=parity error,1=packet type incosistent,2=first word/last word both set,3=packet size over 300 words,4=parser FIFO timeout,5=missing first word,6=first word before last word,7=out FIFO full)
    createStatusParam("Ch1RxStatus",    0x4,  2,  8); // Status OK (0=good cmd packet,1=good data packet)
    createStatusParam("Ch1RxHasData",   0x4,  1, 10); // External FIFO contains data (not empty)
    createStatusParam("Ch1RxFifoAlFu",  0x4,  1, 11); // External FIFO almost full
    createStatusParam("Ch1RxParsHD",    0x4,  1, 12); // Channel Link Packet Parser FIFO contains data
    createStatusParam("Ch1RxParsAF",    0x4,  1, 13); // Channel Link Packet Parser FIFO is almost full
    createStatusParam("Ch1ExtFifEn",    0x4,  1, 14); // External FIFO Read Enable asserted.
    createStatusParam("Ch1ParsFifEn",   0x4,  1, 15); // Channel Link Packet Parser FIFO Write Enable asserted
    createStatusParam("Ch1NumErrors",   0x4, 16, 16); // Number of data packets with errors detected

    createStatusParam("Ch2RxFlags",     0x5,  8,  0); // Error flags (0=parity error,1=packet type incosistent,2=first word/last word both set,3=packet size over 300 words,4=parser FIFO timeout,5=missing first word,6=first word before last word,7=out FIFO full)
    createStatusParam("Ch2RxStatus",    0x5,  2,  8); // Status OK (0=good cmd packet,1=good data packet)
    createStatusParam("Ch2RxHasData",   0x5,  1, 10); // External FIFO contains data (not empty)
    createStatusParam("Ch2RxFifoAlFu",  0x5,  1, 11); // External FIFO almost full
    createStatusParam("Ch2RxParsHD",    0x5,  1, 12); // Channel Link Packet Parser FIFO contains data
    createStatusParam("Ch2RxParsAF",    0x5,  1, 13); // Channel Link Packet Parser FIFO is almost full
    createStatusParam("Ch2ExtFifEn",    0x5,  1, 14); // External FIFO Read Enable asserted.
    createStatusParam("Ch2ParsFifEn",   0x5,  1, 15); // Channel Link Packet Parser FIFO Write Enable asserted
    createStatusParam("Ch2NumErrors",   0x5, 16, 16); // Number of data packets with errors detected

    createStatusParam("Ch3RxFlags",     0x6,  8,  0); // Error flags (0=parity error,1=packet type incosistent,2=first word/last word both set,3=packet size over 300 words,4=parser FIFO timeout,5=missing first word,6=first word before last word,7=out FIFO full)
    createStatusParam("Ch3RxStatus",    0x6,  2,  8); // Status OK (0=good cmd packet,1=good data packet)
    createStatusParam("Ch3RxHasData",   0x6,  1, 10); // External FIFO contains data (not empty)
    createStatusParam("Ch3RxFifoAlFu",  0x6,  1, 11); // External FIFO almost full
    createStatusParam("Ch3RxParsHD",    0x6,  1, 12); // Channel Link Packet Parser FIFO contains data
    createStatusParam("Ch3RxParsAF",    0x6,  1, 13); // Channel Link Packet Parser FIFO is almost full
    createStatusParam("Ch3ExtFifEn",    0x6,  1, 14); // External FIFO Read Enable asserted.
    createStatusParam("Ch3ParsFifEn",   0x6,  1, 15); // Channel Link Packet Parser FIFO Write Enable asserted
    createStatusParam("Ch3NumErrors",   0x6, 16, 16); // Number of data packets with errors detected

    createStatusParam("Ch4RxFlags",     0x7,  8,  0); // Error flags (0=parity error,1=packet type incosistent,2=first word/last word both set,3=packet size over 300 words,4=parser FIFO timeout,5=missing first word,6=first word before last word,7=out FIFO full)
    createStatusParam("Ch4RxStatus",    0x7,  2,  8); // Status OK (0=good cmd packet,1=good data packet)
    createStatusParam("Ch4RxHasData",   0x7,  1, 10); // External FIFO contains data (not empty)
    createStatusParam("Ch4RxFifoAlFu",  0x7,  1, 11); // External FIFO almost full
    createStatusParam("Ch4RxParsHD",    0x7,  1, 12); // Channel Link Packet Parser FIFO contains data
    createStatusParam("Ch4RxParsAF",    0x7,  1, 13); // Channel Link Packet Parser FIFO is almost full
    createStatusParam("Ch4ExtFifEn",    0x7,  1, 14); // External FIFO Read Enable asserted.
    createStatusParam("Ch4ParsFifEn",   0x7,  1, 15); // Channel Link Packet Parser FIFO Write Enable asserted
    createStatusParam("Ch4NumErrors",   0x7, 16, 16); // Number of data packets with errors detected

    createStatusParam("Ch5RxFlags",     0x8,  8,  0); // Error flags (0=parity error,1=packet type incosistent,2=first word/last word both set,3=packet size over 300 words,4=parser FIFO timeout,5=missing first word,6=first word before last word,7=out FIFO full)
    createStatusParam("Ch5RxStatus",    0x8,  2,  8); // Status OK (0=good cmd packet,1=good data packet)
    createStatusParam("Ch5RxHasData",   0x8,  1, 10); // External FIFO contains data (not empty)
    createStatusParam("Ch5RxFifoAlFu",  0x8,  1, 11); // External FIFO almost full
    createStatusParam("Ch5RxParsHD",    0x8,  1, 12); // Channel Link Packet Parser FIFO contains data
    createStatusParam("Ch5RxParsAF",    0x8,  1, 13); // Channel Link Packet Parser FIFO is almost full
    createStatusParam("Ch5ExtFifEn",    0x8,  1, 14); // External FIFO Read Enable asserted.
    createStatusParam("Ch5ParsFifEn",   0x8,  1, 15); // Channel Link Packet Parser FIFO Write Enable asserted
    createStatusParam("Ch5NumErrors",   0x8, 16, 16); // Number of data packets with errors detected

    createStatusParam("NumGoodPkts",    0x9, 32,  0); // Good data packet counter

    createStatusParam("NumCmds",        0xA, 16,  0); // Number of Filtered Command Packets Received from the Channel Link Ports
    createStatusParam("NumAcks",        0xA,  8, 16); // Number of Filtered Acknowledge Responses (ACK) Received from the Channel Link Ports
    createStatusParam("NumNacks",       0xA,  8, 24); // Number of Filtered Error Responses (NACK) Received from the Channel Link Ports

    createStatusParam("NumHwIds",       0xB,  8,  0); // Number of Hardware IDs detected on the Channel Link Ports
    createStatusParam("NewHwId",        0xB,  1,  9); // New Hardware ID detected on the Channel Link Ports
    createStatusParam("MissHwId",       0xB,  1, 10); // Missing Hardware ID
    createStatusParam("CmdFifoHasDat",  0xB,  1, 12); // Sorter Command FIFO contains data
    createStatusParam("CmdFifoAlFull",  0xB,  1, 13); // Sorter Command FIFO is almost full
    createStatusParam("ReadFifHasDat",  0xB,  1, 14); // Channel Link Command Reader FIFO contains data
    createStatusParam("ReadFifAlFull",  0xB,  1, 15); // Channel Link Command Reader FIFO is almost full
    createStatusParam("CrbarCtrlA",     0xB,  2, 16); // Crossbar Switch Pass Control for F/O Port A from OPT_FLOW_CTL
    createStatusParam("CrbarCtrlB",     0xB,  2, 18); // Crossbar Switch Pass Control for F/O Port B from OPT_FLOW_CTL

    createStatusParam("ClkThreshMin",   0xC, 32,  0); // Minimum number of 40 MHz clock pulses between TSYNC reference pulses
    createStatusParam("ClkThreshMax",   0xD, 32,  0); // Maximum number of 40 MHz clock pulses between TSYNC reference pulses
    createStatusParam("EepromStatus",   0xE, 32,  0); // LVDS status
    createStatusParam("LvdsStatus",     0xF, 32,  0); // EEPROM Status - bitfields too faingrained
    createStatusParam("MetadataInfo0",  0x10, 32,  0); // Metadata channel info - bitfields too faingrained
    createStatusParam("MetadataInfo1",  0x11, 32,  0); // Metadata channel info - bitfields too faingrained
    createStatusParam("MetadataInfo2",  0x12, 32,  0); // Metadata channel info - bitfields too faingrained
    createStatusParam("DetailInfo",     0x13, 32,  0); // Detailed info - bitfields too faingrained
    createStatusParam("TofOffset",      0x14, 32,  0); // TOF offset
    createStatusParam("RtdlInfo",       0x15, 32,  0); // RTDL info - bitfields too faingrained

    createStatusParam("NumBadRtdl",     0x16, 16,  0); // Number of RTDL frames with CRC error
    createStatusParam("NumBadData",     0x16, 16, 16); // Number of Event Link frames with CRC error
}
