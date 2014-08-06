#include "FemPlugin.h"
#include "Log.h"

#define NUM_FEMPLUGIN_PARAMS    0 //((int)(&LAST_FEMPLUGIN_PARAM - &FIRST_FEMPLUGIN_PARAM + 1))
#define HEX_BYTE_TO_DEC(a)      ((((a)&0xFF)/16)*10 + ((a)&0xFF)%16)

EPICS_REGISTER_PLUGIN(FemPlugin, 5, "Port name", string, "Dispatcher port name", string, "Hardware ID", string, "Hw & SW version", string, "Blocking", int);

const unsigned FemPlugin::NUM_FEMPLUGIN_DYNPARAMS       = 250;

FemPlugin::FemPlugin(const char *portName, const char *dispatcherPortName, const char *hardwareId, const char *version, int blocking)
    : BaseModulePlugin(portName, dispatcherPortName, hardwareId, true,
                       blocking, NUM_FEMPLUGIN_PARAMS + NUM_FEMPLUGIN_DYNPARAMS)
    , m_version(version)
{
    if (m_version == "10.0/5.0") {
        createStatusParams_V10();
        createConfigParams_V10();
    } else {
        LOG_ERROR("Unsupported FEM version '%s'", version);
        return;
    }

    LOG_DEBUG("Number of configured dynamic parameters: %zu", m_statusParams.size() + m_configParams.size());

    callParamCallbacks();
}

bool FemPlugin::rspDiscover(const DasPacket *packet)
{
    return (BaseModulePlugin::rspDiscover(packet) &&
            packet->cmdinfo.module_type == DasPacket::MOD_TYPE_FEM);
}

bool FemPlugin::rspReadVersion(const DasPacket *packet)
{
    if (!BaseModulePlugin::rspReadVersion(packet))
        return false;

    if (m_version == "10.0/5.0") {
        return rspReadVersion_V10(packet);
    }
    return false;
}

bool FemPlugin::rspReadVersion_V10(const DasPacket *packet)
{
    char date[20];

    struct RspReadVersionV10 {
#ifdef BITFIELD_LSB_FIRST
        unsigned hw_revision:8;     // Board revision number
        unsigned hw_version:8;      // Board version number
        unsigned hw_year:16;        // Board year
        unsigned hw_day:8;          // Board day
        unsigned hw_month:8;        // Board month
        unsigned fw_revision:8;     // Firmware revision number
        unsigned fw_version:8;      // Firmware version number
        unsigned fw_year:16;        // Firmware year
        unsigned fw_day:8;          // Firmware day
        unsigned fw_month:8;        // Firmware month
#else
#error Missing RspReadVersionV10 declaration
#endif // BITFIELD_LSB_FIRST
    };

    const RspReadVersionV10 *response = reinterpret_cast<const RspReadVersionV10*>(packet->getPayload());

    if (packet->getPayloadLength() != sizeof(RspReadVersionV10)) {
        LOG_ERROR("Received unexpected READ_VERSION response for this FEM type, received %u, expected %lu", packet->payload_length, sizeof(RspReadVersionV10));
        return false;
    }

    setIntegerParam(HardwareVer, response->hw_version);
    setIntegerParam(HardwareRev, response->hw_revision);
    snprintf(date, sizeof(date), "%d%d/%d/%d", HEX_BYTE_TO_DEC(response->hw_year >> 8),
                                               HEX_BYTE_TO_DEC(response->hw_year),
                                               HEX_BYTE_TO_DEC(response->hw_month),
                                               HEX_BYTE_TO_DEC(response->hw_day));
    setStringParam(HardwareDate, date);

    setIntegerParam(FirmwareVer, response->fw_version);
    setIntegerParam(FirmwareRev, response->fw_revision);
    snprintf(date, sizeof(date), "%d%d/%d/%d", HEX_BYTE_TO_DEC(response->fw_year >> 8),
                                               HEX_BYTE_TO_DEC(response->fw_year),
                                               HEX_BYTE_TO_DEC(response->fw_month),
                                               HEX_BYTE_TO_DEC(response->fw_day));
    setStringParam(FirmwareDate, date);

    callParamCallbacks();

    if (response->hw_version != 10) {
        LOG_ERROR("FEM version does not match configuration: %d.%d/%d.%d != %s", response->hw_version,
                                                                                 response->hw_revision,
                                                                                 response->fw_version,
                                                                                 response->fw_revision,
                                                                                 m_version.c_str());
        return false;
    }

    return true;
}

void FemPlugin::createStatusParams_V10()
{
//     BLXXX:Det:FemXX:| sig name |                      | EPICS record description  | (bi and mbbi description)
    createStatusParam("Ch2FifoFull",    0x0,  1, 15); // Chan2 FIFO went full          (0=no,1=yes)
    createStatusParam("Ch2StartErr",    0x0,  1, 14); // Chan2 got START during packet (0=no error,1=error)
    createStatusParam("Ch2NoStart",     0x0,  1, 13); // Chan2 got data without START  (0=no error,1=error)
    createStatusParam("Ch2Timeout",     0x0,  1, 12); // Chan2 got TIMEOUT             (0=no timeout,1=timeout)
    createStatusParam("Ch2LenLong",     0x0,  1, 11); // Chan2 long packet error       (0=no error,1=error)
    createStatusParam("Ch2LenShort",    0x0,  1, 10); // Chan2 short packet error      (0=no error,1=error)
    createStatusParam("Ch2DatCmdEr",    0x0,  1,  9); // Chan2 DATA/COMMAND type error (0=no error,1=error)
    createStatusParam("Ch2ParityEr",    0x0,  1,  8); // Chan2 parity error            (0=no error,1=error)
    createStatusParam("Ch1FifoFull",    0x0,  1,  7); // Chan1 FIFO went full          (0=no,1=yes)
    createStatusParam("Ch1StartErr",    0x0,  1,  6); // Chan1 got START during packet (0=no error,1=error)
    createStatusParam("Ch1NoStart",     0x0,  1,  5); // Chan1 got data without START  (0=no error,1=error)
    createStatusParam("Ch1Timeout",     0x0,  1,  4); // Chan1 got TIMEOUT             (0=no timeout,1=timeout)
    createStatusParam("Ch1LenLong",     0x0,  1,  3); // Chan1 long packet error       (0=no error,1=error)
    createStatusParam("Ch1LenShort",    0x0,  1,  2); // Chan1 short packet error      (0=no error,1=error)
    createStatusParam("Ch1DatCmdEr",    0x0,  1,  1); // Chan1 DATA/COMMAND type error (0=no error,1=error)
    createStatusParam("Ch1ParityEr",    0x0,  1,  0); // Chan1 parity error            (0=no error,1=error)

    createStatusParam("Ch4FifoFull",    0x1,  1, 15); // Chan4 FIFO went full          (0=no,1=yes)
    createStatusParam("Ch4StartErr",    0x1,  1, 14); // Chan4 got START during packet (0=no error,1=error)
    createStatusParam("Ch4NoStart",     0x1,  1, 13); // Chan4 got data without START  (0=no error,1=error)
    createStatusParam("Ch4Timeout",     0x1,  1, 12); // Chan4 got TIMEOUT             (0=no timeout,1=timeout)
    createStatusParam("Ch4LenLong",     0x1,  1, 11); // Chan4 long packet error       (0=no error,1=error)
    createStatusParam("Ch4LenShort",    0x1,  1, 10); // Chan4 short packet error      (0=no error,1=error)
    createStatusParam("Ch4DatCmdEr",    0x1,  1,  9); // Chan4 DATA/COMMAND type error (0=no error,1=error)
    createStatusParam("Ch4ParityEr",    0x1,  1,  8); // Chan4 parity error            (0=no error,1=error)
    createStatusParam("Ch3FifoFull",    0x1,  1,  7); // Chan3 FIFO went full          (0=no,1=yes)
    createStatusParam("Ch3StartErr",    0x1,  1,  6); // Chan3 got START during packet (0=no error,1=error)
    createStatusParam("Ch3NoStart",     0x1,  1,  5); // Chan3 got data without START  (0=no error,1=error)
    createStatusParam("Ch3Timeout",     0x1,  1,  4); // Chan3 got TIMEOUT             (0=no timeout,1=timeout)
    createStatusParam("Ch3LenLong",     0x1,  1,  3); // Chan3 long packet error       (0=no error,1=error)
    createStatusParam("Ch3LenShort",    0x1,  1,  2); // Chan3 short packet error      (0=no error,1=error)
    createStatusParam("Ch3DatCmdEr",    0x1,  1,  1); // Chan3 DATA/COMMAND type error (0=no error,1=error)
    createStatusParam("Ch3ParityEr",    0x1,  1,  0); // Chan3 parity error            (0=no error,1=error)

    createStatusParam("Ch6FifoFull",    0x2,  1, 15); // Chan6 FIFO went full          (0=no,1=yes)
    createStatusParam("Ch6StartErr",    0x2,  1, 14); // Chan6 got START during packet (0=no error,1=error)
    createStatusParam("Ch6NoStart",     0x2,  1, 13); // Chan6 got data without START  (0=no error,1=error)
    createStatusParam("Ch6Timeout",     0x2,  1, 12); // Chan6 got TIMEOUT             (0=no timeout,1=timeout)
    createStatusParam("Ch6LenLong",     0x2,  1, 11); // Chan6 long packet error       (0=no error,1=error)
    createStatusParam("Ch6LenShort",    0x2,  1, 10); // Chan6 short packet error      (0=no error,1=error)
    createStatusParam("Ch6DatCmdEr",    0x2,  1,  9); // Chan6 DATA/COMMAND type error (0=no error,1=error)
    createStatusParam("Ch6ParityEr",    0x2,  1,  8); // Chan6 parity error            (0=no error,1=error)
    createStatusParam("Ch5FifoFull",    0x2,  1,  7); // Chan5 FIFO went full          (0=no,1=yes)
    createStatusParam("Ch5StartErr",    0x2,  1,  6); // Chan5 got START during packet (0=no error,1=error)
    createStatusParam("Ch5NoStart",     0x2,  1,  5); // Chan5 got data without START  (0=no error,1=error)
    createStatusParam("Ch5Timeout",     0x2,  1,  4); // Chan5 got TIMEOUT             (0=no timeout,1=timeout)
    createStatusParam("Ch5LenLong",     0x2,  1,  3); // Chan5 long packet error       (0=no error,1=error)
    createStatusParam("Ch5LenShort",    0x2,  1,  2); // Chan5 short packet error      (0=no error,1=error)
    createStatusParam("Ch5DatCmdEr",    0x2,  1,  1); // Chan5 DATA/COMMAND type error (0=no error,1=error)
    createStatusParam("Ch5ParityEr",    0x2,  1,  0); // Chan5 parity error            (0=no error,1=error)

    createStatusParam("Ch8FifoFull",    0x3,  1, 15); // Chan8 FIFO went full          (0=no,1=yes)
    createStatusParam("Ch8StartErr",    0x3,  1, 14); // Chan8 got START during packet (0=no error,1=error)
    createStatusParam("Ch8NoStart",     0x3,  1, 13); // Chan8 got data without START  (0=no error,1=error)
    createStatusParam("Ch8Timeout",     0x3,  1, 12); // Chan8 got TIMEOUT             (0=no timeout,1=timeout)
    createStatusParam("Ch8LenLong",     0x3,  1, 11); // Chan8 long packet error       (0=no error,1=error)
    createStatusParam("Ch8LenShort",    0x3,  1, 10); // Chan8 short packet error      (0=no error,1=error)
    createStatusParam("Ch8DatCmdEr",    0x3,  1,  9); // Chan8 DATA/COMMAND type error (0=no error,1=error)
    createStatusParam("Ch8ParityEr",    0x3,  1,  8); // Chan8 parity error            (0=no error,1=error)
    createStatusParam("Ch7FifoFull",    0x3,  1,  7); // Chan7 FIFO went full          (0=no,1=yes)
    createStatusParam("Ch7StartErr",    0x3,  1,  6); // Chan7 got START during packet (0=no error,1=error)
    createStatusParam("Ch7NoStart",     0x3,  1,  5); // Chan7 got data without START  (0=no error,1=error)
    createStatusParam("Ch7Timeout",     0x3,  1,  4); // Chan7 got TIMEOUT             (0=no timeout,1=timeout)
    createStatusParam("Ch7LenLong",     0x3,  1,  3); // Chan7 long packet error       (0=no error,1=error)
    createStatusParam("Ch7LenShort",    0x3,  1,  2); // Chan7 short packet error      (0=no error,1=error)
    createStatusParam("Ch7DatCmdEr",    0x3,  1,  1); // Chan7 DATA/COMMAND type error (0=no error,1=error)
    createStatusParam("Ch7ParityEr",    0x3,  1,  0); // Chan7 parity error            (0=no error,1=error)

    createStatusParam("Ch10FifoFull",   0x4,  1, 15); // Chan10 FIFO went full         (0=no,1=yes)
    createStatusParam("Ch10StartEre",   0x4,  1, 14); // Chan10 got START during packe (0=no error,1=error)
    createStatusParam("Ch10NoStart",    0x4,  1, 13); // Chan10 got data without START (0=no error,1=error)
    createStatusParam("Ch10Timeout",    0x4,  1, 12); // Chan10 got TIMEOUT            (0=no timeout,1=timeout)
    createStatusParam("Ch10LenLong",    0x4,  1, 11); // Chan10 long packet error      (0=no error,1=error)
    createStatusParam("Ch10LenShort",   0x4,  1, 10); // Chan10 short packet error     (0=no error,1=error)
    createStatusParam("Ch10DatCmdEr",   0x4,  1,  9); // Chan10 DATA/COMMAND type err  (0=no error,1=error)
    createStatusParam("Ch10ParityEr",   0x4,  1,  8); // Chan10 parity error           (0=no error,1=error)
    createStatusParam("Ch9FifoFull",    0x4,  1,  7); // Chan9 FIFO went full          (0=no,1=yes)
    createStatusParam("Ch9StartErr",    0x4,  1,  6); // Chan9 got START during packet (0=no error,1=error)
    createStatusParam("Ch9NoStart",     0x4,  1,  5); // Chan9 got data without START  (0=no error,1=error)
    createStatusParam("Ch9Timeout",     0x4,  1,  4); // Chan9 got TIMEOUT             (0=no timeout,1=timeout)
    createStatusParam("Ch9LenLong",     0x4,  1,  3); // Chan9 long packet error       (0=no error,1=error)
    createStatusParam("Ch9LenShort",    0x4,  1,  2); // Chan9 short packet error      (0=no error,1=error)
    createStatusParam("Ch9DatCmdEr",    0x4,  1,  1); // Chan9 DATA/COMMAND type error (0=no error,1=error)
    createStatusParam("Ch9ParityEr",    0x4,  1,  0); // Chan9 parity error            (0=no error,1=error)

    createStatusParam("NacksFilterd",   0x5, 16, 14); // Filtered NACK                 (0=no,1=yes)
    createStatusParam("NackFilterd",    0x5,  1, 14); // Filtered NACK                 (0=no,1=yes)
    createStatusParam("EepromError",    0x5,  1, 13); // EEPROM error                  (0=no error,1=error)
    createStatusParam("HwMissing",      0x5,  1, 12); // Missing hardware              (0=not missing,1=missing)
    createStatusParam("Unconfig",       0x5,  1, 11); // Unconfigured                  (0=configured,1=not confiured)
    createStatusParam("ProgramErr",     0x5,  1, 10); // Programming error             (0=no error,1=error)
    createStatusParam("CmdLenErr",      0x5,  1,  9); // Command length error          (0=no error,1=error)
    createStatusParam("UnknownCmd",     0x5,  1,  8); // Unknown command               (0=no error,1=error)
    createStatusParam("CtrlFifoFull",   0x5,  1,  7); // CTRL FIFO went full           (0=no,1=yes)
    createStatusParam("CtrlStartErr",   0x5,  1,  6); // CTRL got START during packe   (0=no error,1=error)
    createStatusParam("CtrlNoStart",    0x5,  1,  5); // CTRL got data without START   (0=no error,1=error)
    createStatusParam("CtrlTimeout",    0x5,  1,  4); // CTRL got TIMEOUT              (0=no timeout,1=timeout)
    createStatusParam("CtrlLenLong",    0x5,  1,  3); // CTRL long packet error        (0=no error,1=error)
    createStatusParam("CtrlLenShort",   0x5,  1,  2); // CTRL short packet error       (0=no error,1=error)
    createStatusParam("CtrlDatCmdEr",   0x5,  1,  1); // CTRL DATA/COMMAND type err    (0=no error,1=error)
    createStatusParam("CtrlParityEr",   0x5,  1,  0); // CTRL parity error             (0=no error,1=error)

    // Verify, dcomserver thinks 	{SYSLEVEL_WARN,			{7,7,-1},{6,8,-1},	CHECK_VALUE,	"Filtered NACKs"},
    createStatusParam("FilterdNacks",   0x6,  8,  0); // Filtered NACKS

    createStatusParam("Ch8GotCmd",      0x7,  1, 15); // Chan8 got command packet      (0=no,1=yes)
    createStatusParam("Ch8GotData",     0x7,  1, 14); // Chan8 got data packet         (0=no,1=yes)
    createStatusParam("Ch7GotCmd",      0x7,  1, 13); // Chan7 got command packet      (0=no,1=yes)
    createStatusParam("Ch7GotData",     0x7,  1, 12); // Chan7 got data packet         (0=no,1=yes)
    createStatusParam("Ch6GotCmd",      0x7,  1, 11); // Chan6 got command packet      (0=no,1=yes)
    createStatusParam("Ch6GotData",     0x7,  1, 10); // Chan6 got data packet         (0=no,1=yes)
    createStatusParam("Ch5GotCmd",      0x7,  1,  9); // Chan5 got command packet      (0=no,1=yes)
    createStatusParam("Ch5GotData",     0x7,  1,  8); // Chan5 got data packet         (0=no,1=yes)
    createStatusParam("Ch4GotCmd",      0x7,  1,  7); // Chan4 got command packet      (0=no,1=yes)
    createStatusParam("Ch4GotData",     0x7,  1,  6); // Chan4 got data packet         (0=no,1=yes)
    createStatusParam("Ch3GotCmd",      0x7,  1,  5); // Chan3 got command packet      (0=no,1=yes)
    createStatusParam("Ch3GotData",     0x7,  1,  4); // Chan3 got data packet         (0=no,1=yes)
    createStatusParam("Ch2GotCmd",      0x7,  1,  3); // Chan2 got command packet      (0=no,1=yes)
    createStatusParam("Ch2GotData",     0x7,  1,  2); // Chan2 got data packet         (0=no,1=yes)
    createStatusParam("Ch1GotCmd",      0x7,  1,  1); // Chan1 got command packet      (0=no,1=yes)
    createStatusParam("Ch1GotData",     0x7,  1,  0); // Chan1 got data packet         (0=no,1=yes)

    createStatusParam("FilterdAcks",    0x8,  8,  8); // Filtered ACKS
    createStatusParam("AcquireStat",    0x8,  1,  7); // Acquiring data                (0=not acquiring,1=acquiring)
    createStatusParam("FoundHw",        0x8,  1,  6); // Found new hardware            (0=no,1=yes)
    createStatusParam("CtrlGotCmd",     0x8,  1,  5); // CTRL got command packet       (0=no,1=yes)
    createStatusParam("CtrlGotData",    0x8,  1,  4); // CTRL got data packet          (0=no,1=yes)
    createStatusParam("Ch10GotCmd",     0x8,  1,  3); // Chan10 got command packet     (0=no,1=yes)
    createStatusParam("Ch10GotData",    0x8,  1,  2); // Chan10 got data packet        (0=no,1=yes)
    createStatusParam("Ch9GotCmd",      0x8,  1,  1); // Chan9 got command packet      (0=no,1=yes)
    createStatusParam("Ch9GotData",     0x8,  1,  0); // Chan9 got data packet         (0=no,1=yes)

    createStatusParam("Ch8FifCmd",      0x9,  1, 15); // Chan8 FIFO almost full        (0=no,1=yes)
    createStatusParam("Ch8FifoNE",      0x9,  1, 14); // Chan8 FIFO has data           (0=empty,1=has data)
    createStatusParam("Ch7FifoAF",      0x9,  1, 13); // Chan7 FIFO almost full        (0=no,1=yes)
    createStatusParam("Ch7FifoNE",      0x9,  1, 12); // Chan7 FIFO has data           (0=empty,1=has data)
    createStatusParam("Ch6FifoAF",      0x9,  1, 11); // Chan6 FIFO almost full        (0=no,1=yes)
    createStatusParam("Ch6FifoNE",      0x9,  1, 10); // Chan6 FIFO has data           (0=empty,1=has data)
    createStatusParam("Ch5FifoAF",      0x9,  1,  9); // Chan5 FIFO almost full        (0=no,1=yes)
    createStatusParam("Ch5FifoNE",      0x9,  1,  8); // Chan5 FIFO has data           (0=empty,1=has data)
    createStatusParam("Ch4FifoAF",      0x9,  1,  7); // Chan4 FIFO almost full        (0=no,1=yes)
    createStatusParam("Ch4FifoNE",      0x9,  1,  6); // Chan4 FIFO has data           (0=empty,1=has data)
    createStatusParam("Ch3FifoAF",      0x9,  1,  5); // Chan3 FIFO almost full        (0=no,1=yes)
    createStatusParam("Ch3FifoNE",      0x9,  1,  4); // Chan3 FIFO has data           (0=empty,1=has data)
    createStatusParam("Ch2FifoAF",      0x9,  1,  3); // Chan2 FIFO almost full        (0=no,1=yes)
    createStatusParam("Ch2FifoNE",      0x9,  1,  2); // Chan2 FIFO has data           (0=empty,1=has data)
    createStatusParam("Ch1FifoAF",      0x9,  1,  1); // Chan1 FIFO almost full        (0=no,1=yes)
    createStatusParam("Ch1FifoNE",      0x9,  1,  0); // Chan1 FIFO not empty          (0=empty,1=has data)

    createStatusParam("CmdFifoAF",      0xA,  1, 15); // CMD FIFO almost full          (0=no,1=yes)
    createStatusParam("CmdFifoNE",      0xA,  1, 14); // CMD FIFO has data             (0=empty,1=has data)
    createStatusParam("CmdInFifAF",     0xA,  1, 13); // CMD input FIFO almost full    (0=no,1=yes)
    createStatusParam("CmdInFifNE",     0xA,  1, 12); // CMD input FIFO has data       (0=empty,1=has data)
    createStatusParam("CtrlFifoAF",     0xA,  1,  5); // CTRL FIFO almost full         (0=no,1=yes)
    createStatusParam("CtrlFifoNE",     0xA,  1,  4); // CTRL FIFO has data            (0=empty,1=has data)
    createStatusParam("Ch10FifoAF",     0xA,  1,  3); // Chan10 FIFO almost full       (0=no,1=yes)
    createStatusParam("Ch10FifoNE",     0xA,  1,  2); // Chan10 FIFO has data          (0=empty,1=has data)
    createStatusParam("Ch9FifoAF",      0xA,  1,  1); // Chan9 FIFO almost full        (0=no,1=yes)
    createStatusParam("Ch9FifoNE",      0xA,  1,  0); // Chan9 FIFO has data           (0=empty,1=has data)

    createStatusParam("FilterdResp",    0xB, 16,  8); // Filtered responses
    createStatusParam("SysrstB",        0xB,  2,  6); // Sysrst_B low/high detected    (0=empty,1=low - not ok,2=high,3=low and high - not ok)
    createStatusParam("TxEnable",       0xB,  2,  4); // TXEN low/high detected        (0=empty,1=low,2=high - not ok,3=low and high - not ok)
    createStatusParam("Tsync",          0xB,  2,  2); // TSYNC                         (0=empty,1=low - not ok,2=high - not ok,3=low and high)
    createStatusParam("Tclk",           0xB,  2,  0); // TCLK                          (0=empty,1=low - not ok,2=high - not ok,3=low and high)

    createStatusParam("HwIdCnt",        0xD,  9,  9); // Hardware ID count

    createStatusParam("Mlos4",          0xE,  2, 14); // MLOS1                         (0=empty,1=always low,2=always high,3=low and high)
    createStatusParam("Mlos3",          0xE,  2, 12); // MLOS2                         (0=empty,1=always low,2=always high,3=low and high)
    createStatusParam("Mlos2",          0xE,  2, 10); // MLOS3                         (0=empty,1=always low,2=always high,3=low and high)
    createStatusParam("Mlos1",          0xE,  2,  8); // MLOS4                         (0=empty,1=always low,2=always high,3=low and high)
    createStatusParam("LvdsVerf",       0xE,  1,  3); // Detected LVDS verify          (0=no,1=yes)

    createStatusParam("EepromCode",     0xF, 16,  0); // EEPROM code

    createStatusParam("LvdsDin6",       0x10, 2, 14); // LVDS_DIN6                     (0=empty,1=always low,2=always high,3=low and high)
    createStatusParam("LvdsDin5",       0x10, 2, 12); // LVDS_DIN5                     (0=empty,1=always low,2=always high,3=low and high)
    createStatusParam("LvdsDin4",       0x10, 2, 10); // LVDS_DIN4                     (0=empty,1=always low,2=always high,3=low and high)
    createStatusParam("LvdsDin3",       0x10, 2,  8); // LVDS_DIN3                     (0=empty,1=always low,2=always high,3=low and high)
    createStatusParam("LvdsDin2",       0x10, 2,  6); // LVDS_DIN2                     (0=empty,1=always low,2=always high,3=low and high)
    createStatusParam("LvdsDin1",       0x10, 2,  4); // LVDS_DIN1                     (0=empty,1=always low,2=always high,3=low and high)
    createStatusParam("LvdsDin0",       0x10, 2,  2); // LVDS_DIN0                     (0=empty,1=always low,2=always high,3=low and high)
    createStatusParam("LvdsClk",        0x10, 2,  0); // LVDS_CLK                      (0=empty,1=always low,2=always high,3=low and high)

    createStatusParam("LvdsDin14",      0x11, 2, 14); // LVDS_DIN14                    (0=empty,1=always low,2=always high,3=low and high)
    createStatusParam("LvdsDin13",      0x11, 2, 12); // LVDS_DIN13                    (0=empty,1=always low,2=always high,3=low and high)
    createStatusParam("LvdsDin12",      0x11, 2, 10); // LVDS_DIN12                    (0=empty,1=always low,2=always high,3=low and high)
    createStatusParam("LvdsDin11",      0x11, 2,  8); // LVDS_DIN11                    (0=empty,1=always low,2=always high,3=low and high)
    createStatusParam("LvdsDin10",      0x11, 2,  6); // LVDS_DIN10                    (0=empty,1=always low,2=always high,3=low and high)
    createStatusParam("LvdsDin9",       0x11, 2,  4); // LVDS_DIN9                     (0=empty,1=always low,2=always high,3=low and high)
    createStatusParam("LvdsDin8",       0x11, 2,  2); // LVDS_DIN8                     (0=empty,1=always low,2=always high,3=low and high)
    createStatusParam("LvdsDin7",       0x11, 2,  0); // LVDS_DIN7                     (0=empty,1=always low,2=always high,3=low and high)

    createStatusParam("LvdsPort",       0x12, 4, 12); // LVDS port
    createStatusParam("LvdsDin20",      0x12, 2, 10); // LVDS_DIN20                    (0=empty,1=always low,2=always high,3=low and high)
    createStatusParam("LvdsDin19",      0x12, 2,  8); // LVDS_DIN19                    (0=empty,1=always low,2=always high,3=low and high)
    createStatusParam("LvdsDin18",      0x12, 2,  6); // LVDS_DIN18                    (0=empty,1=always low,2=always high,3=low and high)
    createStatusParam("LvdsDin17",      0x12, 2,  4); // LVDS_DIN17                    (0=empty,1=always low,2=always high,3=low and high)
    createStatusParam("LvdsDin16",      0x12, 2,  2); // LVDS_DIN16                    (0=empty,1=always low,2=always high,3=low and high)
    createStatusParam("LvdsDin15",      0x12, 2,  0); // LVDS_DIN15                    (0=empty,1=always low,2=always high,3=low and high)
}

void FemPlugin::createConfigParams_V10()
{
//     BLXXX:Det:FemXX:| sig name |                                | EPICS record description  | (bi and mbbi description)
    createConfigParam("LvdsMonitor",  'E', 0x0,  1,  8, 0);     // LVDS debug monitor            (0=disable,1=enable)
    createConfigParam("TxenCtrl",     'E', 0x0,  2,  6, 0);     // TXEN control                  (0=TXEN,1=TXEN,2=always 0,3=always 1)
    createConfigParam("TsyncCtrl",    'E', 0x0,  2,  4, 0);     // TSYNC control                 (0=from polariry,1=from TSYNC width,2=always 0,3=always 1)
    createConfigParam("TclkCtrl",     'E', 0x0,  2,  2, 0);     // TCLK control                  (0=TCLK,1=TCLK,2=always 0,3=always 1)
    createConfigParam("ResetCtrl",    'E', 0x0,  2,  0, 0);     // Reset control                 (0=not used,1=not used,2=from LVDS,3=from optic)

    createConfigParam("VerboseDisc",  'F', 0x0,  1, 15, 0);     // Verbose discover              (0=disable,1=enable)
    createConfigParam("VerboseRsp",   'F', 0x0,  1, 14, 0);     // Verbose command response      (0=disable,1=enable)
    createConfigParam("Ch10Disable",  'F', 0x0,  1, 13, 0);     // Chan10 disable                (0=enable,1=disable)
    createConfigParam("Ch9Disable",   'F', 0x0,  1, 12, 0);     // Chan9 disable                 (0=enable,1=disable)
    createConfigParam("Ch8Disable",   'F', 0x0,  1, 11, 0);     // Chan8 disable                 (0=enable,1=disable)
    createConfigParam("Ch7Disable",   'F', 0x0,  1, 10, 0);     // Chan7 disable                 (0=enable,1=disable)
    createConfigParam("Ch6Disable",   'F', 0x0,  1,  9, 0);     // Chan6 disable                 (0=enable,1=disable)
    createConfigParam("Ch5Disable",   'F', 0x0,  1,  8, 0);     // Chan5 disable                 (0=enable,1=disable)
    createConfigParam("Ch4Disable",   'F', 0x0,  1,  7, 0);     // Chan4 disable                 (0=enable,1=disable)
    createConfigParam("Ch3Disable",   'F', 0x0,  1,  6, 0);     // Chan3 disable                 (0=enable,1=disable)
    createConfigParam("Ch2Disable",   'F', 0x0,  1,  5, 0);     // Chan2 disable                 (0=enable,1=disable)
    createConfigParam("Ch1Disable",   'F', 0x0,  1,  4, 0);     // Chan1 disable                 (0=enable,1=disable)
    createConfigParam("TxenMode",     'F', 0x0,  1,  3, 0);     // TXEN mode                     (0=external,1=internal)
    createConfigParam("TsyncMode",    'F', 0x0,  1,  2, 0);     // TSYNC mode                    (0=external,1=internal)
    createConfigParam("TclkMode",     'F', 0x0,  1,  1, 0);     // TCLK mode                     (0=external,1=internal)
    createConfigParam("ResetMode",    'F', 0x0,  1,  0, 1);     // Reset mode                    (0=internal,1=external)
}
