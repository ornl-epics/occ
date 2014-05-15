#include "DspPlugin.h"
#include "Log.h"

#include <epicsAlgorithm.h>
#include <osiSock.h>
#include <string.h>

#include <functional>
#include <string>

#define NUM_DSPPLUGIN_PARAMS    ((int)(&LAST_DSPPLUGIN_PARAM - &FIRST_DSPPLUGIN_PARAM + 1))
#define HEX_BYTE_TO_DEC(a)      ((((a)&0xFF)/16)*10 + ((a)&0xFF)%16)

EPICS_REGISTER_PLUGIN(DspPlugin, 4, "Port name", string, "Dispatcher port name", string, "Hardware ID", string, "Blocking", int);

const unsigned DspPlugin::NUM_DSPPLUGIN_CONFIGPARAMS    = 272;
const unsigned DspPlugin::NUM_DSPPLUGIN_STATUSPARAMS    = 100;
const double DspPlugin::DSP_RESPONSE_TIMEOUT            = 1.0;

DspPlugin::DspPlugin(const char *portName, const char *dispatcherPortName, const char *hardwareId, int blocking)
    : BaseModulePlugin(portName, dispatcherPortName, hardwareId, false,
                       blocking, NUM_DSPPLUGIN_PARAMS + NUM_DSPPLUGIN_CONFIGPARAMS + NUM_DSPPLUGIN_CONFIGPARAMS)
{

    createParam("HwDate",       asynParamOctet, &HardwareDate);
    createParam("HwVer",        asynParamInt32, &HardwareVer);
    createParam("HwRev",        asynParamInt32, &HardwareRev);
    createParam("FwDate",       asynParamOctet, &FirmwareDate);
    createParam("FwVer",        asynParamInt32, &FirmwareVer);
    createParam("FwRev",        asynParamInt32, &FirmwareRev);

    createConfigParams();
    createStatusParams();

    if (m_configParams.size() != NUM_DSPPLUGIN_CONFIGPARAMS) {
        LOG_ERROR("Number of config params mismatch, expected %d but got %lu", NUM_DSPPLUGIN_CONFIGPARAMS, m_configParams.size());
        return;
    }

    callParamCallbacks();
    setCallbacks(true);
}

bool DspPlugin::rspDiscover(const DasPacket *packet)
{
    return (packet->cmdinfo.module_type == DasPacket::MOD_TYPE_DSP);
}

bool DspPlugin::rspReadVersion(const DasPacket *packet)
{
    char date[20];
    struct RspVersion {
#ifdef BITFIELD_LSB_FIRST
        struct {
            unsigned day:8;
            unsigned month:8;
            unsigned year:8;
            unsigned revision:4;
            unsigned version:4;
        } hardware;
        struct {
            unsigned day:8;
            unsigned month:8;
            unsigned year:8;
            unsigned revision:4;
            unsigned version:4;
        } firmware;
        uint32_t eeprom_code1;
        uint32_t eeprom_code2;
#else
#error Missing DspVersionRegister declaration
#endif
    };

    const RspVersion *payload = reinterpret_cast<const RspVersion*>(packet->payload);

    if (packet->getPayloadLength() != sizeof(RspVersion)) {
        LOG_ERROR("Received unexpected READ_VERSION response for this DSP type; received %u, expected %lu", packet->payload_length, sizeof(RspVersion));
        return false;
    }

    setIntegerParam(HardwareVer, payload->hardware.version);
    setIntegerParam(HardwareRev, payload->hardware.revision);
    snprintf(date, sizeof(date), "20%d/%d/%d", HEX_BYTE_TO_DEC(payload->hardware.year),
                                               HEX_BYTE_TO_DEC(payload->hardware.month),
                                               HEX_BYTE_TO_DEC(payload->hardware.day));
    setStringParam(HardwareDate, date);

    setIntegerParam(FirmwareVer, payload->firmware.version);
    setIntegerParam(FirmwareRev, payload->firmware.revision);
    snprintf(date, sizeof(date), "20%d/%d/%d", HEX_BYTE_TO_DEC(payload->firmware.year),
                                               HEX_BYTE_TO_DEC(payload->firmware.month),
                                               HEX_BYTE_TO_DEC(payload->firmware.day));

    setStringParam(FirmwareDate, date);

    callParamCallbacks();
    return true;
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

void DspPlugin::createConfigParams() {
//      BLXXX:Det:DspX:| sig name  |                            | EPICS record description  | (bi and mbbi description)
    createConfigParam("PixIdOffset",   'B', 0x0,  32,  0, 0); // Pixel id offset

    // Chopper parameters
    createConfigParam("Chop0Delay",    'C', 0x0,  32,  0, 0); // Chop0 delay for N*9.4ns cycls
    createConfigParam("Chop1Delay",    'C', 0x1,  32,  0, 0); // Chop1 delay for N*9.4ns cycls
    createConfigParam("Chop2Delay",    'C', 0x2,  32,  0, 0); // Chop2 delay for N*9.4ns cycls
    createConfigParam("Chop3Delay",    'C', 0x3,  32,  0, 0); // Chop3 delay for N*9.4ns cycls
    createConfigParam("Chop4Delay",    'C', 0x4,  32,  0, 0); // Chop4 delay for N*9.4ns cycls
    createConfigParam("Chop5Delay",    'C', 0x5,  32,  0, 0); // Chop5 delay for N*9.4ns cycls
    createConfigParam("Chop6Delay",    'C', 0x6,  32,  0, 0); // Chop6 delay for N*9.4ns cycls
    createConfigParam("Chop7Delay",    'C', 0x7,  32,  0, 0); // Chop7 delay for N*9.4ns cycls

    createConfigParam("Chop0Freq",     'C', 0x8,   4,  0, 0); // Chop0 frequency selector      (0=60Hz,1=30Hz,2=20Hz,3=15Hz,4=12.5Hz,5=10Hz,6=7.5Hz,7=6Hz,8=5Hz,9=4Hz,10=3Hz,11=2.4Hz,12=2Hz,13=1.5Hz,14=1.25Hz,15=1Hz)
    createConfigParam("Chop1Freq",     'C', 0x8,   4,  4, 0); // Chop1 frequency selector      (0=60Hz,1=30Hz,2=20Hz,3=15Hz,4=12.5Hz,5=10Hz,6=7.5Hz,7=6Hz,8=5Hz,9=4Hz,10=3Hz,11=2.4Hz,12=2Hz,13=1.5Hz,14=1.25Hz,15=1Hz)
    createConfigParam("Chop2Freq",     'C', 0x8,   4,  8, 0); // Chop2 frequency selector      (0=60Hz,1=30Hz,2=20Hz,3=15Hz,4=12.5Hz,5=10Hz,6=7.5Hz,7=6Hz,8=5Hz,9=4Hz,10=3Hz,11=2.4Hz,12=2Hz,13=1.5Hz,14=1.25Hz,15=1Hz)
    createConfigParam("Chop3Freq",     'C', 0x8,   4, 12, 0); // Chop3 frequency selector      (0=60Hz,1=30Hz,2=20Hz,3=15Hz,4=12.5Hz,5=10Hz,6=7.5Hz,7=6Hz,8=5Hz,9=4Hz,10=3Hz,11=2.4Hz,12=2Hz,13=1.5Hz,14=1.25Hz,15=1Hz)
    createConfigParam("Chop4Freq",     'C', 0x8,   4, 16, 0); // Chop4 frequency selector      (0=60Hz,1=30Hz,2=20Hz,3=15Hz,4=12.5Hz,5=10Hz,6=7.5Hz,7=6Hz,8=5Hz,9=4Hz,10=3Hz,11=2.4Hz,12=2Hz,13=1.5Hz,14=1.25Hz,15=1Hz)
    createConfigParam("Chop5Freq",     'C', 0x8,   4, 20, 0); // Chop5 frequency selector      (0=60Hz,1=30Hz,2=20Hz,3=15Hz,4=12.5Hz,5=10Hz,6=7.5Hz,7=6Hz,8=5Hz,9=4Hz,10=3Hz,11=2.4Hz,12=2Hz,13=1.5Hz,14=1.25Hz,15=1Hz)
    createConfigParam("Chop6Freq",     'C', 0x8,   4, 24, 0); // Chop6 frequency selector      (0=60Hz,1=30Hz,2=20Hz,3=15Hz,4=12.5Hz,5=10Hz,6=7.5Hz,7=6Hz,8=5Hz,9=4Hz,10=3Hz,11=2.4Hz,12=2Hz,13=1.5Hz,14=1.25Hz,15=1Hz)
    createConfigParam("Chop7Freq",     'C', 0x8,   4, 28, 0); // Chop7 frequency selector      (0=60Hz,1=30Hz,2=20Hz,3=15Hz,4=12.5Hz,5=10Hz,6=7.5Hz,7=6Hz,8=5Hz,9=4Hz,10=3Hz,11=2.4Hz,12=2Hz,13=1.5Hz,14=1.25Hz,15=1Hz)

    createConfigParam("ChopDutyCycl",  'C', 0x9,  32,  0, 83400); // N*100ns ref pulse high
    createConfigParam("ChopMaxPerio",  'C', 0xA,  32,  0, 166800); // N*100ns master/ref delay
    createConfigParam("ChopFixedOff",  'C', 0xB,  32,  0, 0); // Chopper TOF fixed offset       - todo: make proper record links to chopper ioc

    createConfigParam("ChopRtdlFr6",   'C', 0xC,   8,  0, 4); // RTDL Frame 6
    createConfigParam("ChopRtdlFr7",   'C', 0xC,   8,  8, 5); // RTDL Frame 7
    createConfigParam("ChopRtdlFr8",   'C', 0xC,   8, 16, 6); // RTDL Frame 8
    createConfigParam("ChopRtdlFr9",   'C', 0xC,   8, 24, 7); // RTDL Frame 9
    createConfigParam("ChopRtdlFr10",  'C', 0xD,   8,  0, 8); // RTDL Frame 10
    createConfigParam("ChopRtdlFr11",  'C', 0xD,   8,  8, 15); // RTDL Frame 11
    createConfigParam("ChopRtdlFr12",  'C', 0xD,   8, 16, 17); // RTDL Frame 12
    createConfigParam("ChopRtdlFr13",  'C', 0xD,   8, 24, 24); // RTDL Frame 13
    createConfigParam("ChopRtdlFr14",  'C', 0xE,   8,  0, 25); // RTDL Frame 14
    createConfigParam("ChopRtdlFr15",  'C', 0xE,   8,  8, 26); // RTDL Frame 15
    createConfigParam("ChopRtdlFr16",  'C', 0xE,   8, 16, 28); // RTDL Frame 16
    createConfigParam("ChopRtdlFr17",  'C', 0xE,   8, 24, 29); // RTDL Frame 17
    createConfigParam("ChopRtdlFr18",  'C', 0xF,   8,  0, 30); // RTDL Frame 18
    createConfigParam("ChopRtdlFr19",  'C', 0xF,   8,  8, 31); // RTDL Frame 19
    createConfigParam("ChopRtdlFr20",  'C', 0xF,   8, 16, 32); // RTDL Frame 20
    createConfigParam("ChopRtdlFr21",  'C', 0xF,   8, 24, 33); // RTDL Frame 21
    createConfigParam("ChopRtdlFr22",  'C', 0x10,  8,  0, 34); // RTDL Frame 22
    createConfigParam("ChopRtdlFr23",  'C', 0x10,  8,  8, 35); // RTDL Frame 23
    createConfigParam("ChopRtdlFr24",  'C', 0x10,  8, 16, 36); // RTDL Frame 24
    createConfigParam("ChopRtdlFr25",  'C', 0x10,  8, 24, 37); // RTDL Frame 25
    createConfigParam("ChopRtdlFr26",  'C', 0x11,  8,  0, 38); // RTDL Frame 26
    createConfigParam("ChopRtdlFr27",  'C', 0x11,  8,  8, 39); // RTDL Frame 27
    createConfigParam("ChopRtdlFr28",  'C', 0x11,  8, 16, 40); // RTDL Frame 28
    createConfigParam("ChopRtdlFr29",  'C', 0x11,  8, 24, 41); // RTDL Frame 29
    createConfigParam("ChopRtdlFr30",  'C', 0x12,  8,  0, 1); // RTDL Frame 30
    createConfigParam("ChopRtdlFr31",  'C', 0x12,  8,  8, 2); // RTDL Frame 31
// dcomserver thinks this one is valid
//    createConfigParam1("ChopRtdlFr32",'C', 0x12,  8, 16, 3); // RTDL Frame 32

    createConfigParam("ChopTrefTrig",  'C', 0x13,  2,  0, 1); // Chopper TREF trigger select   (0=Extract,1=Cycle Start,2=Beam On,3=TREF event)
    createConfigParam("ChopTrefFreq",  'C', 0x13,  4,  2, 0); // TREF frequency select         (0=60Hz,1=30Hz,2=20Hz,3=15Hz,4=12.5Hz,5=10Hz,6=7.5Hz,7=6Hz,8=5Hz,9=4Hz,10=3Hz,11=2.4Hz,12=2Hz,13=1.5Hz,14=1.25Hz,15=1Hz)
    createConfigParam("ChopRtdlOff",   'C', 0x13,  4,  8, 0); // Chopper RTDL frame offset
    createConfigParam("ChopTrefEvNo",  'C', 0x13,  8, 12, 39); // Chop TREF event trig [0:255]
    createConfigParam("ChopHystMinL",  'C', 0x13,  4, 20, 4); // Chop HYST minimum low [0:7]
    createConfigParam("ChopHystMinH",  'C', 0x13,  4, 24, 4); // Chop HYST minimum high [0:7]
    createConfigParam("ChopFreqCnt",   'C', 0x13,  2, 28, 1); // Chop frequency count control  (0=strobe at X, 1=strobe at X-1, 2=strobe at X-2)
    createConfigParam("ChopFreqCyc",   'C', 0x13,  1, 30, 1); // Chop frequency cycle select   (0=Present cycle number, 1=Next cycle number)
    createConfigParam("ChopSweepEn",   'C', 0x13,  1, 31, 0); // Chop sweep enable             (0=TOF fixed off,1=TOF fract off)

    createConfigParam("STsyncDelMax",  'C', 0x14, 32,  0, 0); // Synth master strobe max delay
    createConfigParam("STsyncDelAdj",  'C', 0x15, 32,  0, 0); // Synth master strobe delay adj
    createConfigParam("STsyncFraAdj",  'C', 0x16, 32,  0, 0); // Synth master strobe fract adj
    createConfigParam("TimestHiFake",  'C', 0x17, 32,  0, 0); // Fake mode timestmp high DWord

    // Meta parameters
    createConfigParam("Ch0EdgeDet",    'D', 0x0,   2,  0, 0); // Chan0 edge detection mode     (0=disable channel,1=detect ris edge,2=detect fall edge,3=detect any edge)
    createConfigParam("Ch1EdgeDet",    'D', 0x0,   2,  2, 0); // Chan1 edge detection mode     (0=disable channel,1=detect ris edge,2=detect fall edge,3=detect any edge)
    createConfigParam("Ch2EdgeDet",    'D', 0x0,   2,  4, 0); // Chan2 edge detection mode     (0=disable channel,1=detect ris edge,2=detect fall edge,3=detect any edge)
    createConfigParam("Ch3EdgeDet",    'D', 0x0,   2,  6, 0); // Chan3 edge detection mode     (0=disable channel,1=detect ris edge,2=detect fall edge,3=detect any edge)
    createConfigParam("Ch4EdgeDet",    'D', 0x0,   2,  8, 0); // Chan4 edge detection mode     (0=disable channel,1=detect ris edge,2=detect fall edge,3=detect any edge)
    createConfigParam("Ch5EdgeDet",    'D', 0x0,   2, 10, 0); // Chan5 edge detection mode     (0=disable channel,1=detect ris edge,2=detect fall edge,3=detect any edge)
    createConfigParam("Ch6EdgeDet",    'D', 0x0,   2, 12, 0); // Chan6 edge detection mode     (0=disable channel,1=detect ris edge,2=detect fall edge,3=detect any edge)
    createConfigParam("Ch7EdgeDet",    'D', 0x0,   2, 14, 0); // Chan7 edge detection mode     (0=disable channel,1=detect ris edge,2=detect fall edge,3=detect any edge)
    createConfigParam("Ch8EdgeDet",    'D', 0x0,   2, 16, 0); // Chan8 edge detection mode     (0=disable channel,1=detect ris edge,2=detect fall edge,3=detect any edge)
    createConfigParam("Ch9EdgeDet",    'D', 0x0,   2, 18, 0); // Chan9 edge detection mode     (0=disable channel,1=detect ris edge,2=detect fall edge,3=detect any edge)
    createConfigParam("Ch10EdgeDet",   'D', 0x0,   2, 20, 0); // Chan10 edge detection mode    (0=disable channel,1=detect ris edge,2=detect fall edge,3=detect any edge)
    createConfigParam("Ch11EdgeDet",   'D', 0x0,   2, 22, 0); // Chan11 edge detection mode    (0=disable channel,1=detect ris edge,2=detect fall edge,3=detect any edge)
    createConfigParam("Ch12EdgeDet",   'D', 0x0,   2, 24, 0); // Chan12 edge detection mode    (0=disable channel,1=detect ris edge,2=detect fall edge,3=detect any edge)
    createConfigParam("Ch13EdgeDet",   'D', 0x0,   2, 26, 0); // Chan13 edge detection mode    (0=disable channel,1=detect ris edge,2=detect fall edge,3=detect any edge)
    createConfigParam("Ch14EdgeDet",   'D', 0x0,   2, 28, 0); // Chan14 edge detection mode    (0=disable channel,1=detect ris edge,2=detect fall edge,3=detect any edge)
    createConfigParam("Ch15EdgeDet",   'D', 0x0,   2, 30, 0); // Chan15 edge detection mode    (0=disable channel,1=detect ris edge,2=detect fall edge,3=detect any edge)
    createConfigParam("Ch16EdgeDet",   'D', 0x1,   2,  0, 0); // Chan16 edge detection mode    (0=disable channel,1=detect ris edge,2=detect fall edge,3=detect any edge)
    createConfigParam("Ch17EdgeDet",   'D', 0x1,   2,  2, 0); // Chan17 edge detection mode    (0=disable channel,1=detect ris edge,2=detect fall edge,3=detect any edge)
    createConfigParam("Ch18EdgeDet",   'D', 0x1,   2,  4, 0); // Chan18 edge detection mode    (0=disable channel,1=detect ris edge,2=detect fall edge,3=detect any edge)
    createConfigParam("Ch19EdgeDet",   'D', 0x1,   2,  6, 0); // Chan19 edge detection mode    (0=disable channel,1=detect ris edge,2=detect fall edge,3=detect any edge)
    createConfigParam("Ch20EdgeDet",   'D', 0x1,   2,  8, 0); // Chan20 edge detection mode    (0=disable channel,1=detect ris edge,2=detect fall edge,3=detect any edge)
    createConfigParam("Ch21EdgeDet",   'D', 0x1,   2, 10, 0); // Chan21 edge detection mode    (0=disable channel,1=detect ris edge,2=detect fall edge,3=detect any edge)
    createConfigParam("Ch22EdgeDet",   'D', 0x1,   2, 12, 0); // Chan22 edge detection mode    (0=disable channel,1=detect ris edge,2=detect fall edge,3=detect any edge)
    createConfigParam("Ch23EdgeDet",   'D', 0x1,   2, 14, 0); // Chan23 edge detection mode    (0=disable channel,1=detect ris edge,2=detect fall edge,3=detect any edge)
    createConfigParam("Ch24EdgeDet",   'D', 0x1,   2, 16, 0); // Chan24 edge detection mode    (0=disable channel,1=detect ris edge,2=detect fall edge,3=detect any edge)
    createConfigParam("Ch25EdgeDet",   'D', 0x1,   2, 18, 0); // Chan25 edge detection mode    (0=disable channel,1=detect ris edge,2=detect fall edge,3=detect any edge)
    createConfigParam("Ch26EdgeDet",   'D', 0x1,   2, 20, 0); // Chan26 edge detection mode    (0=disable channel,1=detect ris edge,2=detect fall edge,3=detect any edge)
    createConfigParam("Ch27EdgeDet",   'D', 0x1,   2, 22, 0); // Chan27 edge detection mode    (0=disable channel,1=detect ris edge,2=detect fall edge,3=detect any edge)
    createConfigParam("Ch28EdgeDet",   'D', 0x1,   2, 24, 0); // Chan28 edge detection mode    (0=disable channel,1=detect ris edge,2=detect fall edge,3=detect any edge)
    createConfigParam("Ch29EdgeDet",   'D', 0x1,   2, 26, 0); // Chan29 edge detection mode    (0=disable channel,1=detect ris edge,2=detect fall edge,3=detect any edge)
    createConfigParam("Ch30EdgeDet",   'D', 0x1,   2, 28, 0); // Chan30 edge detection mode    (0=disable channel,1=detect ris edge,2=detect fall edge,3=detect any edge)
    createConfigParam("Ch31EdgeDet",   'D', 0x1,   2, 30, 0); // Chan31 edge detection mode    (0=disable channel,1=detect ris edge,2=detect fall edge,3=detect any edge)

    createConfigParam("Ch0EdgePixId",  'D', 0x2,  32,  0, 0x50000000); // Chan0 edge pixel id
    createConfigParam("Ch1EdgePixId",  'D', 0x3,  32,  0, 0x50000002); // Chan1 edge pixel id
    createConfigParam("Ch2EdgePixId",  'D', 0x4,  32,  0, 0x50000004); // Chan2 edge pixel id
    createConfigParam("Ch3EdgePixId",  'D', 0x5,  32,  0, 0x50000008); // Chan3 edge pixel id
    createConfigParam("Ch4EdgePixId",  'D', 0x6,  32,  0, 0x5000000a); // Chan4 edge pixel id
    createConfigParam("Ch5EdgePixId",  'D', 0x7,  32,  0, 0x5000000c); // Chan5 edge pixel id
    createConfigParam("Ch6EdgePixId",  'D', 0x8,  32,  0, 0x5000000e); // Chan6 edge pixel id
    createConfigParam("Ch7EdgePixId",  'D', 0x9,  32,  0, 0x50000010); // Chan7 edge pixel id
    createConfigParam("Ch8EdgePixId",  'D', 0xA,  32,  0, 0x50000012); // Chan8 edge pixel id
    createConfigParam("Ch9EdgePixId",  'D', 0xB,  32,  0, 0x50000014); // Chan9 edge pixel id
    createConfigParam("Ch10EdgePixId", 'D', 0xC,  32,  0, 0x50000016); // Chan10 edge pixel id
    createConfigParam("Ch11EdgePixId", 'D', 0xD,  32,  0, 0x50000018); // Chan11 edge pixel id
    createConfigParam("Ch12EdgePixId", 'D', 0xE,  32,  0, 0x5000001a); // Chan12 edge pixel id
    createConfigParam("Ch13EdgePixId", 'D', 0xF,  32,  0, 0x5000001c); // Chan13 edge pixel id
    createConfigParam("Ch14EdgePixId", 'D', 0x10, 32,  0, 0x5000001e); // Chan14 edge pixel id
    createConfigParam("Ch15EdgePixId", 'D', 0x11, 32,  0, 0x50000006); // Chan15 edge pixel id
    createConfigParam("Ch16EdgePixId", 'D', 0x12, 32,  0, 0); // Chan16 edge pixel id
    createConfigParam("Ch17EdgePixId", 'D', 0x13, 32,  0, 0); // Chan17 edge pixel id
    createConfigParam("Ch18EdgePixId", 'D', 0x14, 32,  0, 0); // Chan18 edge pixel id
    createConfigParam("Ch19EdgePixId", 'D', 0x15, 32,  0, 0); // Chan19 edge pixel id
    createConfigParam("Ch20EdgePixId", 'D', 0x16, 32,  0, 0); // Chan20 edge pixel id
    createConfigParam("Ch21EdgePixId", 'D', 0x17, 32,  0, 0); // Chan21 edge pixel id
    createConfigParam("Ch22EdgePixId", 'D', 0x18, 32,  0, 0); // Chan22 edge pixel id
    createConfigParam("Ch23EdgePixId", 'D', 0x19, 32,  0, 0); // Chan23 edge pixel id
    createConfigParam("Ch24EdgePixId", 'D', 0x1A, 32,  0, 0); // Chan24 edge pixel id
    createConfigParam("Ch25EdgePixId", 'D', 0x1B, 32,  0, 0); // Chan25 edge pixel id
    createConfigParam("Ch26EdgePixId", 'D', 0x1C, 32,  0, 0); // Chan26 edge pixel id
    createConfigParam("Ch27EdgePixId", 'D', 0x1D, 32,  0, 0); // Chan27 edge pixel id
    createConfigParam("Ch28EdgePixId", 'D', 0x1E, 32,  0, 0); // Chan28 edge pixel id
    createConfigParam("Ch29EdgePixId", 'D', 0x1F, 32,  0, 0); // Chan29 edge pixel id
    createConfigParam("Ch30EdgePixId", 'D', 0x20, 32,  0, 0); // Chan30 edge pixel id
    createConfigParam("Ch31EdgePixId", 'D', 0x21, 32,  0, 0); // Chan31 edge pixel id

    createConfigParam("Ch0EdgCycAdj",  'D', 0x22,  5,  0, 0); // Chan0 edge cycle number adj
    createConfigParam("Ch1EdgCycAdj",  'D', 0x22,  5,  5, 0); // Chan1 edge cycle number adj
    createConfigParam("Ch2EdgCycAdj",  'D', 0x22,  5, 10, 0); // Chan2 edge cycle number adj
    createConfigParam("Ch3EdgCycAdj",  'D', 0x22,  5, 15, 0); // Chan3 edge cycle number adj
    createConfigParam("Ch4EdgCycAdj",  'D', 0x22,  5, 20, 0); // Chan4 edge cycle number adj
    createConfigParam("Ch5EdgCycAdj",  'D', 0x22,  5, 25, 0); // Chan5 edge cycle number adj
    createConfigParam("Ch6EdgCycAdj",  'D', 0x23,  5,  0, 0); // Chan6 edge cycle number adj
    createConfigParam("Ch7EdgCycAdj",  'D', 0x23,  5,  5, 0); // Chan7 edge cycle number adj
    createConfigParam("Ch8EdgCycAdj",  'D', 0x23,  5, 10, 0); // Chan8 edge cycle number adj
    createConfigParam("Ch9EdgCycAdj",  'D', 0x23,  5, 15, 0); // Chan9 edge cycle number adj
    createConfigParam("Ch10EdgCycAdj", 'D', 0x23,  5, 20, 0); // Chan10 edge cycle number adj
    createConfigParam("Ch11EdgCycAdj", 'D', 0x23,  5, 25, 0); // Chan11 edge cycle number adj
    createConfigParam("Ch12EdgCycAdj", 'D', 0x24,  5,  0, 0); // Chan12 edge cycle number adj
    createConfigParam("Ch13EdgCycAdj", 'D', 0x24,  5,  5, 0); // Chan13 edge cycle number adj
    createConfigParam("Ch14EdgCycAdj", 'D', 0x24,  5, 10, 0); // Chan14 edge cycle number adj
    createConfigParam("Ch15EdgCycAdj", 'D', 0x24,  5, 15, 0); // Chan15 edge cycle number adj
    createConfigParam("Ch16EdgCycAdj", 'D', 0x24,  5, 20, 0); // Chan16 edge cycle number adj
    createConfigParam("Ch17EdgCycAdj", 'D', 0x24,  5, 25, 0); // Chan17 edge cycle number adj
    createConfigParam("Ch18EdgCycAdj", 'D', 0x25,  5,  0, 0); // Chan18 edge cycle number adj
    createConfigParam("Ch19EdgCycAdj", 'D', 0x25,  5,  5, 0); // Chan19 edge cycle number adj
    createConfigParam("Ch20EdgCycAdj", 'D', 0x25,  5, 10, 0); // Chan20 edge cycle number adj
    createConfigParam("Ch21EdgCycAdj", 'D', 0x25,  5, 15, 0); // Chan21 edge cycle number adj
    createConfigParam("Ch22EdgCycAdj", 'D', 0x25,  5, 20, 0); // Chan22 edge cycle number adj
    createConfigParam("Ch23EdgCycAdj", 'D', 0x25,  5, 25, 0); // Chan23 edge cycle number adj
    createConfigParam("Ch24EdgCycAdj", 'D', 0x26,  5,  0, 0); // Chan24 edge cycle number adj
    createConfigParam("Ch25EdgCycAdj", 'D', 0x26,  5,  5, 0); // Chan25 edge cycle number adj
    createConfigParam("Ch26EdgCycAdj", 'D', 0x26,  5, 10, 0); // Chan26 edge cycle number adj
    createConfigParam("Ch27EdgCycAdj", 'D', 0x26,  5, 15, 0); // Chan27 edge cycle number adj
    createConfigParam("Ch28EdgCycAdj", 'D', 0x26,  5, 20, 0); // Chan28 edge cycle number adj
    createConfigParam("Ch29EdgCycAdj", 'D', 0x26,  5, 25, 0); // Chan29 edge cycle number adj
    createConfigParam("Ch30EdgCycAdj", 'D', 0x27,  5,  0, 0); // Chan30 edge cycle number adj
    createConfigParam("Ch31EdgCycAdj", 'D', 0x27,  5,  5, 0); // Chan31 edge cycle number adj

    createConfigParam("Ch0EdgeDelay",  'D', 0x28, 32,  0, 0); // Chan0 edge delay
    createConfigParam("Ch1EdgeDelay",  'D', 0x29, 32,  0, 0); // Chan1 edge delay
    createConfigParam("Ch2EdgeDelay",  'D', 0x2A, 32,  0, 0); // Chan2 edge delay
    createConfigParam("Ch3EdgeDelay",  'D', 0x2B, 32,  0, 0); // Chan3 edge delay
    createConfigParam("Ch4EdgeDelay",  'D', 0x2C, 32,  0, 0); // Chan4 edge delay
    createConfigParam("Ch5EdgeDelay",  'D', 0x2D, 32,  0, 0); // Chan5 edge delay
    createConfigParam("Ch6EdgeDelay",  'D', 0x2E, 32,  0, 0); // Chan6 edge delay
    createConfigParam("Ch7EdgeDelay",  'D', 0x2F, 32,  0, 0); // Chan7 edge delay
    createConfigParam("Ch8EdgeDelay",  'D', 0x30, 32,  0, 0); // Chan8 edge delay
    createConfigParam("Ch9EdgeDelay",  'D', 0x31, 32,  0, 0); // Chan9 edge delay
    createConfigParam("Ch10EdgeDelay", 'D', 0x32, 32,  0, 0); // Chan10 edge delay
    createConfigParam("Ch11EdgeDelay", 'D', 0x33, 32,  0, 0); // Chan11 edge delay
    createConfigParam("Ch12EdgeDelay", 'D', 0x34, 32,  0, 0); // Chan12 edge delay
    createConfigParam("Ch13EdgeDelay", 'D', 0x35, 32,  0, 0); // Chan13 edge delay
    createConfigParam("Ch14EdgeDelay", 'D', 0x36, 32,  0, 0); // Chan14 edge delay
    createConfigParam("Ch15EdgeDelay", 'D', 0x37, 32,  0, 0); // Chan15 edge delay
    createConfigParam("Ch16EdgeDelay", 'D', 0x38, 32,  0, 0); // Chan16 edge delay
    createConfigParam("Ch17EdgeDelay", 'D', 0x39, 32,  0, 0); // Chan17 edge delay
    createConfigParam("Ch18EdgeDelay", 'D', 0x3A, 32,  0, 0); // Chan18 edge delay
    createConfigParam("Ch19EdgeDelay", 'D', 0x3B, 32,  0, 0); // Chan19 edge delay
    createConfigParam("Ch20EdgeDelay", 'D', 0x3C, 32,  0, 0); // Chan20 edge delay
    createConfigParam("Ch21EdgeDelay", 'D', 0x3D, 32,  0, 0); // Chan21 edge delay
    createConfigParam("Ch22EdgeDelay", 'D', 0x3E, 32,  0, 0); // Chan22 edge delay
    createConfigParam("Ch23EdgeDelay", 'D', 0x3F, 32,  0, 0); // Chan23 edge delay
    createConfigParam("Ch24EdgeDelay", 'D', 0x40, 32,  0, 0); // Chan24 edge delay
    createConfigParam("Ch25EdgeDelay", 'D', 0x41, 32,  0, 0); // Chan25 edge delay
    createConfigParam("Ch26EdgeDelay", 'D', 0x42, 32,  0, 0); // Chan26 edge delay
    createConfigParam("Ch27EdgeDelay", 'D', 0x43, 32,  0, 0); // Chan27 edge delay
    createConfigParam("Ch28EdgeDelay", 'D', 0x44, 32,  0, 0); // Chan28 edge delay
    createConfigParam("Ch29EdgeDelay", 'D', 0x45, 32,  0, 0); // Chan29 edge delay
    createConfigParam("Ch30EdgeDelay", 'D', 0x46, 32,  0, 0); // Chan30 edge delay
    createConfigParam("Ch31EdgeDelay", 'D', 0x47, 32,  0, 0); // Chan31 edge delay

    // LVDS & optical parameters
    createConfigParam("LvdsRxTxen0",   'E', 0x0,  1,  0, 1); // LVDS chan0 RX/TX enable       (0=disabled,1=enabled)
    createConfigParam("LvdsRxTxen1",   'E', 0x0,  1,  3, 0); // LVDS chan1 RX/TX enable       (0=disabled,1=enabled)
    createConfigParam("LvdsRxTxen2",   'E', 0x0,  1,  6, 0); // LVDS chan2 RX/TX enable       (0=disabled,1=enabled)
    createConfigParam("LvdsRxTxen3",   'E', 0x0,  1,  9, 0); // LVDS chan3 RX/TX enable       (0=disabled,1=enabled)
    createConfigParam("LvdsRxTxen4",   'E', 0x0,  1, 12, 0); // LVDS chan4 RX/TX enable       (0=disabled,1=enabled)
    createConfigParam("LvdsRxTxen5",   'E', 0x0,  1, 15, 0); // LVDS chan5 RX/TX enable       (0=disabled,1=enabled)
    createConfigParam("LvdsRxNoEr0",   'E', 0x0,  1,  1, 0); // LVDS chan0 ignore error pkts  (0=ignore,1=keep)
    createConfigParam("LvdsRxNoEr1",   'E', 0x0,  1,  4, 0); // LVDS chan1 ignore error pkts  (0=ignore,1=keep)
    createConfigParam("LvdsRxNoEr2",   'E', 0x0,  1,  7, 0); // LVDS chan2 ignore error pkts  (0=ignore,1=keep)
    createConfigParam("LvdsRxNoEr3",   'E', 0x0,  1, 10, 0); // LVDS chan3 ignore error pkts  (0=ignore,1=keep)
    createConfigParam("LvdsRxNoEr4",   'E', 0x0,  1, 13, 0); // LVDS chan4 ignore error pkts  (0=ignore,1=keep)
    createConfigParam("LvdsRxNoEr5",   'E', 0x0,  1, 16, 0); // LVDS chan5 ignore error pkts  (0=ignore,1=keep)
    createConfigParam("LvdsRxDis0",    'E', 0x0,  1,  2, 0); // LVDS chan0 disable            (0=enable,1=disable)
    createConfigParam("LvdsRxDis1",    'E', 0x0,  1,  5, 0); // LVDS chan1 disable            (0=enable,1=disable)
    createConfigParam("LvdsRxDis2",    'E', 0x0,  1,  8, 0); // LVDS chan2 disable            (0=enable,1=disable)
    createConfigParam("LvdsRxDis3",    'E', 0x0,  1, 11, 0); // LVDS chan3 disable            (0=enable,1=disable)
    createConfigParam("LvdsRxDis4",    'E', 0x0,  1, 14, 0); // LVDS chan4 disable            (0=enable,1=disable)
    createConfigParam("LvdsRxDis5",    'E', 0x0,  1, 17, 0); // LVDS chan5 disable            (0=enable,1=disable)
    createConfigParam("LvdsRxCmdMod",  'E', 0x0,  1, 18, 0); // LVDS command parser mode      (0=as command,1=as data)
    createConfigParam("LvdsRxDatMod",  'E', 0x0,  1, 19, 0); // LVDS data parser mode         (0=as data,1=as command)
    createConfigParam("LvdsRxDatSiz",  'E', 0x0,  8, 20, 4); // LVDS ChLnk data pkt num words
    createConfigParam("LvdsRxPowCtr",  'E', 0x0,  1, 28, 0); // Power Down during reset       (0=power down,1=power up)
    createConfigParam("LvdsRxPowRst",  'E', 0x0,  1, 29, 0); // Execite power down sequence   (0=execute,1=bypass)
    createConfigParam("LvdsRxFilter",  'E', 0x0,  2, 30, 0); // Filter all commands

    createConfigParam("LvdsCmdFilt",   'E', 0x1, 16,  0, 0xFFFF); // LVDS command to filter
    createConfigParam("LvdsCmdFiltM",  'E', 0x1, 16, 16, 0); // LVDS command filter mask

    createConfigParam("LvdsTxTclkMo",  'E', 0x2,  1,  0, 0); // LVDS TX control TCLK mode     (0=TCLK from int,1=TCLK from LVDS)
    createConfigParam("LvdsTxTcCtrl",  'E', 0x2,  2,  2, 0); // LVDS TX control T&C TCLK mode (0-1=TCLK,2=always 0,3=always 1)
    createConfigParam("LvdsTscynoMo",  'E', 0x2,  2,  4, 3); // LVDS TSYNC_O mode             (0=local TSYNC,1=TSYNC from TREF,2=TSYNC from LVDS,3=TSYNC from opt)
    createConfigParam("LvdsTxTsyncC",  'E', 0x2,  2,  6, 0); // LVDS TSYNC_NORMAL control     (0=polarity,1=TSYNC WIDTH,2=always 0,3=always 1)
    createConfigParam("LvdsTxSysrCt",  'E', 0x2,  2,  8, 0); // LVDS T&C SYSRST# buffer ctrl  (0=sysrst,1=sysrst,2=always 0,3=always 1)
    createConfigParam("LvdsTxTxenCt",  'E', 0x2,  2, 10, 0); // LVDS T&C TXEN# control        (0=ChLnk parser,1=ChLnk parser,2=ChLnk RX,3=ChLnk inv RX)
    createConfigParam("LvdsTxOutClk",  'E', 0x2,  2, 16, 0); // LVDS output clock mode
    createConfigParam("LvdsTxCmdRet",  'E', 0x2,  2, 18, 3); // LVDS downstream retrys
    createConfigParam("LvdsTxWrdL0",   'E', 0x2,  1, 20, 0); // LVDS chan0 data word length   (0=RX FIFO data,1=set to 4)
    createConfigParam("LvdsTxWrdL1",   'E', 0x2,  1, 21, 0); // LVDS chan1 data word length   (0=RX FIFO data,1=set to 4)
    createConfigParam("LvdsTxWrdL2",   'E', 0x2,  1, 22, 0); // LVDS chan2 data word length   (0=RX FIFO data,1=set to 4)
    createConfigParam("LvdsTxWrdL3",   'E', 0x2,  1, 23, 0); // LVDS chan3 data word length   (0=RX FIFO data,1=set to 4)
    createConfigParam("LvdsTxWrdL4",   'E', 0x2,  1, 24, 0); // LVDS chan4 data word length   (0=RX FIFO data,1=set to 4)
    createConfigParam("LvdsTxWrdL5",   'E', 0x2,  1, 25, 0); // LVDS chan5 data word length   (0=RX FIFO data,1=set to 4)
    createConfigParam("LvdsTxClkMar",  'E', 0x2,  2, 26, 0); // LVDS clock margin
    createConfigParam("LvdsTxTestPa",  'E', 0x2,  1, 30, 0); // LVDS T&C test pattern         (0=disable,1=enable)
    createConfigParam("LvdsTxTestEn",  'E', 0x2,  1, 31, 0); // LVDS test enable              (0=disable,1=enable)

    createConfigParam("LvdsTsyncSrc0", 'E', 0x3,  2,  0, 0); // LVDS chan0 TSYNC T&C src ctrl  (0=TSYNC_NORMAL,1=TSYNC_LOCAL str,2=TSYNC_LOCA no s,3=TRefStrbFixed)
    createConfigParam("LvdsTsyncSrc1", 'E', 0x3,  2,  2, 0); // LVDS chan1 TSYNC T&C src ctrl  (0=TSYNC_NORMAL,1=TSYNC_LOCAL str,2=TSYNC_LOCA no s,3=TRefStrbFixed)
    createConfigParam("LvdsTsyncSrc2", 'E', 0x3,  2,  4, 0); // LVDS chan2 TSYNC T&C src ctrl  (0=TSYNC_NORMAL,1=TSYNC_LOCAL str,2=TSYNC_LOCA no s,3=TRefStrbFixed)
    createConfigParam("LvdsTsyncSrc3", 'E', 0x3,  2,  6, 0); // LVDS chan3 TSYNC T&C src ctrl  (0=TSYNC_NORMAL,1=TSYNC_LOCAL str,2=TSYNC_LOCA no s,3=TRefStrbFixed)
    createConfigParam("LvdsTsyncSrc4", 'E', 0x3,  2,  8, 0); // LVDS chan4 TSYNC T&C src ctrl  (0=TSYNC_NORMAL,1=TSYNC_LOCAL str,2=TSYNC_LOCA no s,3=TRefStrbFixed)
    createConfigParam("LvdsTsyncSrc5", 'E', 0x3,  2, 10, 0); // LVDS chan5 TSYNC T&C src ctrl  (0=TSYNC_NORMAL,1=TSYNC_LOCAL str,2=TSYNC_LOCA no s,3=TRefStrbFixed)
    createConfigParam("LvdsTsyncMeta", 'E', 0x3,  2, 14, 0); // LVDS TSYNC metadata src ctrl   (0=RTDL,1=LVDS,2=detector TSYNC,3=OFB[0])

    createConfigParam("LvdsTsyncGen",  'E', 0x4, 32,  0, 20000); // LVDS TSYNC generation divisor   - 40MHz/this value to obtain TSYNC period
    createConfigParam("LvdsTsyncDely", 'E', 0x5, 32,  0, 0); // LVDS TSYNC delay divisor            - 106.25MHz/this value
    createConfigParam("LvdsTsyncWid",  'E', 0x6, 32,  0, 0); // LVDS TSYNC width divisor            - 10MHz/this value

//      BLXXX:Det:DspX:| sig name  |                            | EPICS record description  | (bi and mbbi description)
    createConfigParam("OptCrossSwA",   'E', 0x8,  2,  2, 0); // Crossbar Switch Pass ctrl A    (1=Send to trans A,2=send to trans B)
    createConfigParam("OptCrossSwB",   'E', 0x8,  2, 10, 0); // Crossbar Switch Pass ctrl B    (1=Send to trans A,2=send to trans B)
    createConfigParam("OptTxOutModeA", 'E', 0x8,  2,  0, 0); // Optical TX A output mode       (0=Normal,1=Timing,2=Chopper,3=Timing master)
    createConfigParam("OptTxEocA",     'E', 0x8,  1,  4, 0); // Optical TX A End of Chain
    createConfigParam("OptTxCmdFiltA", 'E', 0x8,  2,  5, 0); // Optical TX A Command Filter
    createConfigParam("OptTxOutModeB", 'E', 0x8,  2,  8, 0); // Optical TX B output mode       (0=Normal,1=Timing,2=Chopper,3=Timing master)
    createConfigParam("OptTxEocB",     'E', 0x8,  1, 12, 0); // Optical TX B End of Chain
    createConfigParam("OptTxCmdFiltB", 'E', 0x8,  2, 13, 0); // Optical TX B Command Filter
    createConfigParam("OptHystEn",     'E', 0x8,  1, 16, 0); // Optical hysteresis enable      (0=from TLK data,1=match optical)
    createConfigParam("OptBlankFrmEn", 'E', 0x8,  1, 17, 0); // Optical empty data frame CRC   (0=no blank frame, 1=add blank frame)
    createConfigParam("OptTxDelay",    'E', 0x8,  7, 24, 3); // Optical packet send delay           - Number of 313ns cycles to wait between DSP packet transmissions
    createConfigParam("OptTxDelayCtr", 'E', 0x8,  1, 31, 1); // Optical packet send delay ctr (0=use OPT_TX_DELAY,1=prev word count)

    createConfigParam("OptPktMaxSize", 'E', 0x9, 16,  0, 16111); // Optical packet max dwords
    createConfigParam("OptDataEopEn",  'E', 0x9,  1, 16, 1); // Optical Neutron data send EOP  (0=disabled,1=enabled)
    createConfigParam("OptMetaEopEn",  'E', 0x9,  1, 17, 0); // Optical Metadata send EOP      (0=disabled,1=enabled)
    createConfigParam("OptTofCtrl",    'E', 0x9,  1, 18, 0); // TOF control                    (0=fixed TOF,1=full time offset)

    createConfigParam("FakeTrigInfo",  'E', 0xA, 32,  0, 0); // Fake Trigger Information

    createConfigParam("SysResetMode",  'F', 0x0,  2,  0, 0); // Reset mode => SYSRST_O#        (0=not used,1=not used,2=from LVDS T&C,3=from optical T&C)
    createConfigParam("SysStartStopM", 'F', 0x0,  3,  4, 0); // Start/Stop mode                (0=normal, 1=fake data mode,2-3=not defined)
    createConfigParam("SysFakeTrigEn", 'F', 0x0,  1,  7, 0); // Fake metadata trigger enable   (0=disabled,1=enabled)
    createConfigParam("SysFastSendEn", 'F', 0x0,  1,  8, 0); // Send data immediately switch   (0=big packets,1=send immediately)
    createConfigParam("SysPassthruEn", 'F', 0x0,  1,  9, 0); // Response for passthru command  (0=don't send,1=send)
    createConfigParam("SysStartAckEn", 'F', 0x0,  1, 10, 1); // Wait for Start/Stop response   (0=don't wait,1=wait)
    createConfigParam("SysRtdlMode",   'F', 0x0,  2, 12, 0); // RTDL mode                      (0=no RTDL,1=master,2=slave,3=fake mode)
    createConfigParam("SysRtdlOutEnA", 'F', 0x0,  1, 14, 1); // RTDL port A output enable      (0=disable,1=enable)
    createConfigParam("SysRtdlOutEnB", 'F', 0x0,  1, 15, 1); // RTDL port B output enable      (0=disable,1=enable)
    createConfigParam("SysTofOffEn",   'F', 0x0,  1, 16, 0); // Enable TOF full offset         (0=disable,1=enable)
    createConfigParam("SysFifSyncEn",  'F', 0x0,  1, 17, 0); // FIFO sync switch               (0=disable,1=enable)
    createConfigParam("SysRtdlAsData", 'F', 0x0,  1, 18, 1); // Send RTDL command as data      (0=disable,1=enable)
    createConfigParam("SysFixRtdlEn",  'F', 0x0,  1, 19, 1); // Correct RTDL information       (0=disable,1=enable)
    createConfigParam("SysBadPktEn",   'F', 0x0,  1, 30, 0); // Send bad packets               (0=disable,1=enable)
    createConfigParam("SysReset",      'F', 0x0,  1, 31, 0); // Force system reset             (0=disable,1=enable)
}

void DspPlugin::createStatusParams()
{
//      BLXXX:Det:DspX:| sig name  |                     | EPICS record description  | (bi and mbbi description)
    createStatusParam("Configured",     0x0,  1,  0); // Configured                    (0=not configured,1=configured)
    createStatusParam("AcquireStat",    0x0,  1,  1); // Acquiring data                (0=not acquiring,1=acquiring)
    createStatusParam("ProgramErr",     0x0,  1,  2); // WRITE_CNFG during ACQUISITION (0=no error,1=error)
    createStatusParam("PktLenErr",      0x0,  1,  3); // Packet length error           (0=no error,1=error)
    createStatusParam("UnknwnCmdErr",   0x0,  1,  4); // Unrecognized command error    (0=no error,1=error)
    createStatusParam("LvdsTxFifFul",   0x0,  1,  5); // LVDS TxFIFO went full         (0=not full,1=full)
    createStatusParam("LvdsCmdErr",     0x0,  1,  6); // LVDS command error            (0=no error,1=error)
    createStatusParam("EepromInitOk",   0x0,  1,  7); // EEPROM initialization status  (0=not ok,1=ok)
    createStatusParam("FoTransStatA",   0x0,  5, 16);
    createStatusParam("FoTransOutA",    0x0,  2, 22);
    createStatusParam("FoTransStatB",   0x0,  5, 24);
    createStatusParam("FoTransOutB",    0x0,  2, 30);

    createStatusParam("RxNumErrsA",     0x1,  8,  0); // RX A errors count
    createStatusParam("RxErrFlagsA",    0x1, 13,  8); // Error flags                   (8=packet timeout,9=SOF/address sw,10=EOF/address sw,11=SOF/hdr sw,12=EOF/hdr sw,13=SOF/payload sw,14=EOF/payload sw,15=SOF/CRC switch,16=EOF/CRC switch,17=CRC low word,18=CRC high word,19=pri FIFO al full,20=sec FIFO al full)
    createStatusParam("RxGoodPacketA",  0x1,  1, 21); // Last packet was good          (0=no,1=yes)
    createStatusParam("RxPriFifNotEA",  0x1,  1, 23); // Stack FIFO Not Empty          (0=empty,1=not empty)
    createStatusParam("RxPriFifAFulA",  0x1,  1, 24); // Stack FIFO Almost Full        (0=not full,1=almost full)
    createStatusParam("RxSecFifNotEA",  0x1,  1, 25); // Secondary FIFO Not Empty      (0=empty,1=not empty)
    createStatusParam("RxSecFifAFulA",  0x1,  1, 26); // Secondary FIFO Almost Full    (0=not full,1=almost full)
    createStatusParam("RxPtCrsbNotEA",  0x1,  1, 27); // PassThrough FIFO Not Empty    (0=empty,1=not empty)
    createStatusParam("RxPtCrbarAFuA",  0x1,  1, 28); // PassThrough FIFO Almost Full  (0=not full,1=almost full)
    createStatusParam("RxTransTimeA",   0x1,  1, 29); // Timeout Pri/Sec FIFO transfer (0=no timeout,1=timeout)
    createStatusParam("RxPriFifFulA",   0x1,  1, 30); // RX pkt while stack almost fu  (0=no,1=yes)
    createStatusParam("RxPtCrbarFulA",  0x1,  1, 31); // RX while the FIFO almost full

    createStatusParam("RxNumErrsB",     0x2,  8,  0); // RX B errors count
    createStatusParam("RxErrFlagsB",    0x2, 13,  8); // Error flags                   (8=packet timeout,9=SOF/address sw,10=EOF/address sw,11=SOF/hdr sw,12=EOF/hdr sw,13=SOF/payload sw,14=EOF/payload sw,15=SOF/CRC switch,16=EOF/CRC switch,17=CRC low word,18=CRC high word,19=pri FIFO al full,20=sec FIFO al full)
    createStatusParam("RxGoodPacketB",  0x2,  1, 21); // Last packet was good          (0=no,1=yes)
    createStatusParam("RxPriFifNotEB",  0x2,  1, 22); // Stack FIFO Not Empty          (0=empty,1=not empty)
    createStatusParam("RxPriFifAFulB",  0x2,  1, 24); // Stack FIFO Almost Full        (0=not full,1=almost full)
    createStatusParam("RxSecFifNotEB",  0x2,  1, 25); // Secondary FIFO Not Empty      (0=empty,1=not empty)
    createStatusParam("RxSecFifAFulB",  0x2,  1, 26); // Secondary FIFO Almost Full    (0=not full,1=almost full)
    createStatusParam("RxPtCrsbNotEB",  0x2,  1, 27); // PassThrough FIFO Not Empty    (0=empty,1=not empty)
    createStatusParam("RxPtCrbarAFuB",  0x2,  1, 28); // PassThrough FIFO Almost Full  (0=not full,1=almost full)
    createStatusParam("RxTransTimeB",   0x2,  1, 29); // Timeout Pri/Sec FIFO transfer (0=no timeout,1=timeout)
    createStatusParam("RxPriFifFulB",   0x2,  1, 30); // RX pkt while stack almost fu  (0=no,1=yes)
    createStatusParam("RxPtCrbarFulB",  0x2,  1, 31); // RX while the FIFO almost full

    createStatusParam("Ch0RxFlags",     0x3,  8,  0); // Error flags                   (0=parity error,1=packet type err,2=start&last set,3=len >300 words,4=FIFO timeout,5=no first word,6=last befor first,7=out FIFO full)
    createStatusParam("Ch0RxStatus",    0x3,  2,  8); // Status OK                     (0=good cmd packet,1=good data packet)
    createStatusParam("Ch0RxHasData",   0x3,  1, 10); // External FIFO has data        (0=empty,1=has data)
    createStatusParam("Ch0RxFifoAlFu",  0x3,  1, 11); // External FIFO almost full     (0=not full,1=almost full)
    createStatusParam("Ch0RxParsHD",    0x3,  1, 12); // ChLnk pkt pars FIFO has data  (0=empty,1=has data)
    createStatusParam("Ch0RxParsAF",    0x3,  1, 13); // ChLnk pkt pars FIFO almost fu (0=not full,1=almost full)
    createStatusParam("Ch0ExtFifEn",    0x3,  1, 14); // External FIFO Read enabled    (0=disabled,1=enabled)
    createStatusParam("Ch0ParsFifEn",   0x3,  1, 15); // ChLnk pkt pars FIFO Write en  (0=disabled,1=enabled)
    createStatusParam("Ch0NumErrors",   0x3, 16, 16); // Data packet errors count

    createStatusParam("Ch1RxFlags",     0x4,  8,  0); // Error flags                   (0=parity error,1=packet type err,2=start&last set,3=len >300 words,4=FIFO timeout,5=no first word,6=last befor first,7=out FIFO full)
    createStatusParam("Ch1RxStatus",    0x4,  2,  8); // Status OK                     (0=good cmd packet,1=good data packet)
    createStatusParam("Ch1RxHasData",   0x4,  1, 10); // External FIFO has data        (0=empty,1=has data)
    createStatusParam("Ch1RxFifoAlFu",  0x4,  1, 11); // External FIFO almost full     (0=not full,1=almost full)
    createStatusParam("Ch1RxParsHD",    0x4,  1, 12); // ChLnk pkt pars FIFO has data  (0=empty,1=has data)
    createStatusParam("Ch1RxParsAF",    0x4,  1, 13); // ChLnk pkt pars FIFO almost fu (0=not full,1=almost full)
    createStatusParam("Ch1ExtFifEn",    0x4,  1, 14); // External FIFO Read enabled    (0=disabled,1=enabled)
    createStatusParam("Ch1ParsFifEn",   0x4,  1, 15); // ChLnk pkt pars FIFO Write en  (0=disabled,1=enabled)
    createStatusParam("Ch1NumErrors",   0x4, 16, 16); // Data packet errors count

    createStatusParam("Ch2RxFlags",     0x5,  8,  0); // Error flags                   (0=parity error,1=packet type err,2=start&last set,3=len >300 words,4=FIFO timeout,5=no first word,6=last befor first,7=out FIFO full)
    createStatusParam("Ch2RxStatus",    0x5,  2,  8); // Status OK                     (0=good cmd packet,1=good data packet)
    createStatusParam("Ch2RxHasData",   0x5,  1, 10); // External FIFO has data        (0=empty,1=has data)
    createStatusParam("Ch2RxFifoAlFu",  0x5,  1, 11); // External FIFO almost full     (0=not full,1=almost full)
    createStatusParam("Ch2RxParsHD",    0x5,  1, 12); // ChLnk pkt pars FIFO has data  (0=empty,1=has data)
    createStatusParam("Ch2RxParsAF",    0x5,  1, 13); // ChLnk pkt pars FIFO almost fu (0=not full,1=almost full)
    createStatusParam("Ch2ExtFifEn",    0x5,  1, 14); // External FIFO Read enabled    (0=disabled,1=enabled)
    createStatusParam("Ch2ParsFifEn",   0x5,  1, 15); // ChLnk pkt pars FIFO Write en  (0=disabled,1=enabled)
    createStatusParam("Ch2NumErrors",   0x5, 16, 16); // Data packet errors count
    createStatusParam("Ch3RxFlags",     0x6,  8,  0); // Error flags                   (0=parity error,1=packet type err,2=start&last set,3=len >300 words,4=FIFO timeout,5=no first word,6=last befor first,7=out FIFO full)
    createStatusParam("Ch3RxStatus",    0x6,  2,  8); // Status OK                     (0=good cmd packet,1=good data packet)
    createStatusParam("Ch3RxHasData",   0x6,  1, 10); // External FIFO has data        (0=empty,1=has data)
    createStatusParam("Ch3RxFifoAlFu",  0x6,  1, 11); // External FIFO almost full     (0=not full,1=almost full)
    createStatusParam("Ch3RxParsHD",    0x6,  1, 12); // ChLnk pkt pars FIFO has data  (0=empty,1=has data)
    createStatusParam("Ch3RxParsAF",    0x6,  1, 13); // ChLnk pkt pars FIFO almost fu (0=not full,1=almost full)
    createStatusParam("Ch3ExtFifEn",    0x6,  1, 14); // External FIFO Read enabled    (0=disabled,1=enabled)
    createStatusParam("Ch3ParsFifEn",   0x6,  1, 15); // ChLnk pkt pars FIFO Write en  (0=disabled,1=enabled)
    createStatusParam("Ch3NumErrors",   0x6, 16, 16); // Data packet errors count

    createStatusParam("Ch4RxFlags",     0x7,  8,  0); // Error flags                   (0=parity error,1=packet type err,2=start&last set,3=len >300 words,4=FIFO timeout,5=no first word,6=last befor first,7=out FIFO full)
    createStatusParam("Ch4RxStatus",    0x7,  2,  8); // Status OK                     (0=good cmd packet,1=good data packet)
    createStatusParam("Ch4RxHasData",   0x7,  1, 10); // External FIFO has data        (0=empty,1=has data)
    createStatusParam("Ch4RxFifoAlFu",  0x7,  1, 11); // External FIFO almost full     (0=not full,1=almost full)
    createStatusParam("Ch4RxParsHD",    0x7,  1, 12); // ChLnk pkt pars FIFO has data  (0=empty,1=has data)
    createStatusParam("Ch4RxParsAF",    0x7,  1, 13); // ChLnk pkt pars FIFO almost fu (0=not full,1=almost full)
    createStatusParam("Ch4ExtFifEn",    0x7,  1, 14); // External FIFO Read enabled    (0=disabled,1=enabled)
    createStatusParam("Ch4ParsFifEn",   0x7,  1, 15); // ChLnk pkt pars FIFO Write en  (0=disabled,1=enabled)
    createStatusParam("Ch4NumErrors",   0x7, 16, 16); // Data packet errors count

    createStatusParam("Ch5RxFlags",     0x8,  8,  0); // Error flags                   (0=parity error,1=packet type err,2=start&last set,3=len >300 words,4=FIFO timeout,5=no first word,6=last befor first,7=out FIFO full)
    createStatusParam("Ch5RxStatus",    0x8,  2,  8); // Status OK                     (0=good cmd packet,1=good data packet)
    createStatusParam("Ch5RxHasData",   0x8,  1, 10); // External FIFO has data        (0=empty,1=has data)
    createStatusParam("Ch5RxFifoAlFu",  0x8,  1, 11); // External FIFO almost full     (0=not full,1=almost full)
    createStatusParam("Ch5RxParsHD",    0x8,  1, 12); // ChLnk pkt pars FIFO has data  (0=empty,1=has data)
    createStatusParam("Ch5RxParsAF",    0x8,  1, 13); // ChLnk pkt pars FIFO almost fu (0=not full,1=almost full)
    createStatusParam("Ch5ExtFifEn",    0x8,  1, 14); // External FIFO Read enabled    (0=disabled,1=enabled)
    createStatusParam("Ch5ParsFifEn",   0x8,  1, 15); // ChLnk pkt pars FIFO Write en  (0=disabled,1=enabled)
    createStatusParam("Ch5NumErrors",   0x8, 16, 16); // Data packet errors count

    createStatusParam("NumGoodPkts",    0x9, 32,  0); // Good data packet count

    createStatusParam("NumCmds",        0xA, 16,  0); // Filtered command count
    createStatusParam("NumAcks",        0xA,  8, 16); // Filtered ACKSs count
    createStatusParam("NumNacks",       0xA,  8, 24); // Filtered NACKs count

    createStatusParam("NumHwIds",       0xB,  8,  0); // Detected Hardware IDs count
    createStatusParam("NewHwId",        0xB,  1,  9); // New Hardware ID detected
    createStatusParam("MissHwId",       0xB,  1, 10); // Missing Hardware ID
    createStatusParam("CmdFifoHasDat",  0xB,  1, 12); // Sorter cmd FIFO has data       (0=empty,1=has data)
    createStatusParam("CmdFifoAlFull",  0xB,  1, 13); // Sorter cmd FIFO almost full    (0=not full,1=almost full)
    createStatusParam("ReadFifHasDat",  0xB,  1, 14); // Channel Link cmd FIFO has dat  (0=empty,1=has data)
    createStatusParam("ReadFifAlFull",  0xB,  1, 15); // Channel Link cmd FIFO al. full (0=not full,1=almost full)
    createStatusParam("CrbarCtrlA",     0xB,  2, 16); // Crossbar Switch A pass ctrl
    createStatusParam("CrbarCtrlB",     0xB,  2, 18); // Crossbar Switch B pass ctrl

    createStatusParam("ClkThreshMin",   0xC, 32,  0); // Min N*40MHz between TSYNC/ref
    createStatusParam("ClkThreshMax",   0xD, 32,  0); // Max N*40MHz between TSYNC/ref
    createStatusParam("EepromStatus",   0xE, 32,  0); // LVDS status
    createStatusParam("LvdsStatus",     0xF, 32,  0); // EEPROM Status
    createStatusParam("MetadataInfo0",  0x10, 32,  0); // Metadata channel info
    createStatusParam("MetadataInfo1",  0x11, 32,  0); // Metadata channel info
    createStatusParam("MetadataInfo2",  0x12, 32,  0); // Metadata channel info
    createStatusParam("DetailInfo",     0x13, 32,  0); // Detailed info
    createStatusParam("TofOffset",      0x14, 32,  0); // TOF offset
    createStatusParam("RtdlInfo",       0x15, 32,  0); // RTDL info

    createStatusParam("NumBadRtdl",     0x16, 16,  0); // RTDL frame CRC errors count
    createStatusParam("NumBadData",     0x16, 16, 16); // Ev Link frame CRC errors cnt
}
