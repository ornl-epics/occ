#include "RocPlugin.h"
#include "Log.h"
#include "StateMachine.h"

#include <epicsAlgorithm.h>
#include <osiSock.h>
#include <string.h>

#include <functional>
#include <string>

#define NUM_ROCPLUGIN_PARAMS    ((int)(&LAST_ROCPLUGIN_PARAM - &FIRST_ROCPLUGIN_PARAM + 1))
#define HEX_BYTE_TO_DEC(a)      ((((a)&0xFF)/16)*10 + ((a)&0xFF)%16)

EPICS_REGISTER_PLUGIN(RocPlugin, 5, "Port name", string, "Dispatcher port name", string, "Hardware ID", string, "Hw & SW version", string, "Blocking", int);

const unsigned RocPlugin::NUM_ROCPLUGIN_STATUSPARAMS    = 300;  //!< Since supporting multiple versions with different number of PVs, this is just a maximum value
const float    RocPlugin::NO_RESPONSE_TIMEOUT           = 1.0;

RocPlugin::RocPlugin(const char *portName, const char *dispatcherPortName, const char *hardwareId, const char *version, int blocking)
    : BaseModulePlugin(portName, dispatcherPortName, hardwareId, true,
                       blocking, NUM_ROCPLUGIN_PARAMS + NUM_ROCPLUGIN_STATUSPARAMS)
    , m_version(version)
{
    createParam("HwDate",       asynParamOctet, &HardwareDate);
    createParam("HwVer",        asynParamInt32, &HardwareVer);
    createParam("HwRev",        asynParamInt32, &HardwareRev);
    createParam("FwVer",        asynParamInt32, &FirmwareVer);
    createParam("FwRev",        asynParamInt32, &FirmwareRev);

    if (m_version == "5.2/5.2") {
        createStatusParams_V5_52();
/*
    } else if (m_version == "2.x/5.x") {
        createStatusParams_V2_5x();
    } else if (m_version == "2.x/4.5") {
        createStatusParams_V2_45();
    } else if (m_version == "2.x/4.1") {
        createStatusParams_V2_41();
*/
    } else {
        LOG_ERROR("Unsupported ROC version '%s'", version);
        return;
    }

    callParamCallbacks();
    setCallbacks(true);
}

bool RocPlugin::rspDiscover(const DasPacket *packet)
{
    return (packet->cmdinfo.module_type == DasPacket::MOD_TYPE_ROC);
}

bool RocPlugin::rspReadVersion(const DasPacket *packet)
{
    if (m_version == "5.2/5.2") {
        return rspReadVersion_V5_5x(packet);
    }
    return false;
}

bool RocPlugin::rspReadVersion_V5_5x(const DasPacket *packet)
{
    char date[20];

    struct RspReadVersionV5 {
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

    const RspReadVersionV5 *response = reinterpret_cast<const RspReadVersionV5*>(packet->getPayload());

    if (packet->getPayloadLength() != sizeof(RspReadVersionV5)) {
        LOG_ERROR("Received unexpected READ_VERSION response for this ROC type, received %u, expected %lu", packet->payload_length, sizeof(RspReadVersionV5));
        return false;
    }

    setIntegerParam(HardwareVer, response->hw_version);
    setIntegerParam(HardwareRev, response->hw_revision);
    setIntegerParam(FirmwareVer, response->fw_version);
    setIntegerParam(FirmwareRev, response->fw_revision);
    snprintf(date, sizeof(date), "%d%d/%d/%d", HEX_BYTE_TO_DEC(response->year >> 8),
                                               HEX_BYTE_TO_DEC(response->year),
                                               HEX_BYTE_TO_DEC(response->month),
                                               HEX_BYTE_TO_DEC(response->day));
    setStringParam(HardwareDate, date);

    callParamCallbacks();

    if (response->hw_version != 5 || response->fw_version != 5) {
        LOG_ERROR("ROC version does not match configuration: %d.%d/%d.%d != %s", response->hw_version,
                                                                                 response->hw_revision,
                                                                                 response->fw_version,
                                                                                 response->fw_revision,
                                                                                 m_version.c_str());
        return false;
    }

    return true;
}

void RocPlugin::timeout(DasPacket::CommandType command)
{
    if (command == DasPacket::CMD_READ_VERSION) {
        // Proper states definition takes place and do the right job
        m_stateMachine.transition(TIMEOUT);
    }
}

void RocPlugin::createStatusParams_V5_52()
{
//    BLXXX:Det:RocXXX:| sig nam |                       | EPICS record description  | (bi and mbbi description)
    createStatusParam("UartByteErr",    0x0,  1, 13); // UART: Byte error              (0=no error,1=error)
    createStatusParam("UartParErr",     0x0,  1, 12); // UART: Parity error            (0=no error,1=error)
    createStatusParam("ProgramErr",     0x0,  1, 11); // WRITE_CNFG during ACQUISITION (0=no error,1=error)
    createStatusParam("CmdLenErr",      0x0,  1, 10); // Command length error          (0=no error,1=error)
    createStatusParam("UnknownCmd",     0x0,  1,  9); // Unrecognized command error    (0=no error,1=error)
    createStatusParam("NoTsync",        0x0,  1,  8); // Timestamp overflow error.     (0=no error,1=error)
    createStatusParam("LvdsFifoFul",    0x0,  1,  6); // LVDS FIFO went full.          (0=not full,1=full)
    createStatusParam("LvdsStartEr",    0x0,  1,  5); // LVDS start before stop bit    (0=no error,1=error)
    createStatusParam("LvdsNoStart",    0x0,  1,  4); // LVDS data without start.      (0=no error,1=error)
    createStatusParam("LvdsTimeout",    0x0,  1,  3); // LVDS packet timeout.          (0=no timeout,1=timeout)
    createStatusParam("LvdsLenErr",     0x0,  1,  2); // LVDS packet length error.     (0=no error,1=error)
    createStatusParam("LvdsTypeErr",    0x0,  1,  1); // LVDS data type error.         (0=no error,1=error)
    createStatusParam("LvdsParErr",     0x0,  1,  0); // LVDS parity error.            (0=no error,1=error)

    createStatusParam("LvdsVerDet",     0x1,  1, 14); // LVDS VERIFY detected          (0=not detected,1=detected)
    createStatusParam("IntFifoFull",    0x1,  1, 13); // Internal Data FIFO Almost ful (0=not full,1=full)
    createStatusParam("IntFifoEmp",     0x1,  1, 12); // Internal Data FIFO Empty flag (0=not empty,1=empty)
    createStatusParam("CalcBadFin",     0x1,  1, 11); // Calc: Bad Final Calculation.  (0=no error,1=error)
    createStatusParam("CalcBadEff",     0x1,  1, 10); // Calc: Bad Effective Calculati (0=no error,1=error)
    createStatusParam("CalcBadOver",    0x1,  1,  9); // Calc: Data overflow detected. (0=no error,1=error)
    createStatusParam("CalcBadCnt",     0x1,  1,  8); // Calc: Bad word count.         (0=no error,1=error)
    createStatusParam("DataFifoFul",    0x1,  1,  7); // Data FIFO Almost full flag.   (0=not full,1=almost full)
    createStatusParam("DataFifoEmp",    0x1,  1,  6); // Data FIFO Empty flag.         (0=not empty,1=empty)
    createStatusParam("HVStatus",       0x1,  1,  5); // High Voltage Status bit       (0=no,1=yes)
    createStatusParam("CalcActive",     0x1,  1,  4); // Calculation: Active           (0=not active,1=active)
    createStatusParam("AcquireStat",    0x1,  1,  3); // Acquiring data                (0=not acquiring,1=acquiring)
    createStatusParam("Discovered",     0x1,  1,  2); // Discovered.                   (0=not discovered,1=discovered)
    createStatusParam("Configured2",    0x1,  1,  1); // Configured (Section 2)        (0=not configured,1=configured)
    createStatusParam("Configured1",    0x1,  1,  0); // Configured (Section 1)        (0=not configured,1=configured)

    createStatusParam("RisEdgeA",       0x2,  8,  8); // Discriminator A set
    createStatusParam("SysrstBHigh",    0x2,  1,  7); // SYSRST_B Got HIGH             (0=no,1=yes)
    createStatusParam("SysrstBLow",     0x2,  1,  6); // SYSRST_B Got LOW              (0=no,1=yes)
    createStatusParam("TxenBHigh",      0x2,  1,  5); // TXEN_B Got HIGH               (0=no,1=yes)
    createStatusParam("TxenBLow",       0x2,  1,  4); // TXEN_B Got LOW                (0=no,1=yes)
    createStatusParam("TsyncHigh",      0x2,  1,  3); // TSYNC Got HIGH                (0=no,1=yes)
    createStatusParam("TsyncLow",       0x2,  1,  2); // TSYNC Got LOW                 (0=no,1=yes)
    createStatusParam("TclkHigh",       0x2,  1,  1); // TCLK Got HIGH                 (0=no,1=yes)
    createStatusParam("TclkLow",        0x2,  1,  0); // TCLK Got LOW                  (0=no,1=yes)

    createStatusParam("RisEdgeSum",     0x3,  8,  8); // Discriminator SUM set
    createStatusParam("RisEdgeB",       0x3,  8,  0); // Discriminator B set

    createStatusParam("Ch1AAdjEn",      0x4,  1, 15); // Chan1 A Auto-Adjust Active    (0=not active,1=active)
    createStatusParam("Ch1AAdjTrig",    0x4,  1, 14); // Chan1 A Auto-Adjust Got sampl (0=no sample,1=got sample)
    createStatusParam("Ch1AAdjSamL",    0x4,  1, 13); // Chan1 A Auto-Adjust Sample li (0=not active,1=active)
    createStatusParam("Ch1ASlopLim",    0x4,  1, 12); // Chan1 A Auto-Adjust Slope lim (0=not active,1=active)
    createStatusParam("Ch1AOffLim",     0x4,  1, 11); // Chan1 A Auto-Adjust Offset li (0=not active,1=active)
    createStatusParam("Ch1ASlopLi1",    0x4,  1, 10); // Chan1 A Auto-Adjust Slope? li (0=not active,1=active)
    createStatusParam("Ch1AOverflw",    0x4,  1,  9); // Chan1 A Auto-Adjust overflow  (0=no,1=yes)
    createStatusParam("Ch1ASlope",      0x4,  9,  0); // Chan1 A Input Offset value

    createStatusParam("Ch1BAdcOff",     0x5,  8,  8); // Chan1 B ADC Offset value
    createStatusParam("Ch1AAdcOff",     0x5,  8,  0); // Chan1 A ADC Offset value

    createStatusParam("Ch1BAdjEn",      0x6,  1, 15); // Chan1 B Auto-Adjust Active    (0=not active,1=active)
    createStatusParam("Ch1BAdjTrig",    0x6,  1, 14); // Chan1 B Auto-Adjust Got sampl (0=no sample,1=got sample)
    createStatusParam("Ch1BAdjSamL",    0x6,  1, 13); // Chan1 B Auto-Adjust Sample li (0=not active,1=active)
    createStatusParam("Ch1BSlopLim",    0x6,  1, 12); // Chan1 B Auto-Adjust Slope lim (0=not active,1=active)
    createStatusParam("Ch1BOffLim",     0x6,  1, 11); // Chan1 B Auto-Adjust Offset li (0=not active,1=active)
    createStatusParam("Ch1BSlopLi1",    0x6,  1, 10); // Chan1 B Auto-Adjust Slope? li (0=not active,1=active)
    createStatusParam("Ch1BOverflw",    0x6,  1,  9); // Chan1 B Auto-Adjust overflow  (0=no,1=yes)
    createStatusParam("Ch1BSlope",      0x6,  9,  0); // Chan1 B Input Offset value

    // NOTE: The next parameter spans over the LVDS words (16bits) *and* it also spans over the
    //       OCC dword (32 bits). BaseModulePlugin::createStatusParam() and BaseModulePlugin:rspReadStatus()
    //       functions are smart enough to accomodate the behaviour.
    createStatusParam("Ch2ASlope",      0x7,  9,  8); // Chan2 A input offset value
    createStatusParam("Ch1AdcMax",      0x7,  1,  7); // Chan1 got ADC max             (0=no,1=yes)
    createStatusParam("Ch1AdcMin",      0x7,  1,  6); // Chan1 got ADC min             (0=no,1=yes)
    createStatusParam("Ch1MDEv",        0x7,  1,  5); // Chan1 got multi-discp event   (0=no,1=yes)
    createStatusParam("Ch1Ev",          0x7,  1,  4); // Chan1 got event               (0=no,1=yes)
    createStatusParam("Ch1FifFul",      0x7,  1,  3); // Chan1 FIFO full detectec      (0=no,1=yes)
    createStatusParam("Ch1FifAmFF",     0x7,  1,  2); // Chan1 FIFO almost full detec  (0=no,1=yes)
    createStatusParam("Ch1FifAmFul",    0x7,  1,  1); // Chan1 FIFO almost full        (0=no,1=yes)
    createStatusParam("Ch1NotEmpty",    0x7,  1,  0); // Chan1 FIFO has data           (0=no,1=yes)

    createStatusParam("Ch2AAdcOff",     0x8,  8,  8); // Chan2 A ADC Offset value
    createStatusParam("Ch2AAdjEn",      0x8,  1,  7); // Chan2 A Auto-Adjust Active    (0=not active,1=active)
    createStatusParam("Ch2AAdjTrig",    0x8,  1,  6); // Chan2 A Auto-Adjust Got sampl (0=no sample,1=got sample)
    createStatusParam("Ch2AAdjSamL",    0x8,  1,  5); // Chan2 A Auto-Adjust Sample li (0=not active,1=active)
    createStatusParam("Ch2ASlopLim",    0x8,  1,  4); // Chan2 A Auto-Adjust Slope lim (0=not active,1=active)
    createStatusParam("Ch2AOffLim",     0x8,  1,  3); // Chan2 A Auto-Adjust Offset li (0=not active,1=active)
    createStatusParam("Ch2ASlopLi1",    0x8,  1,  2); // Chan2 A Auto-Adjust Slope? li (0=not active,1=active)
    createStatusParam("Ch2AOverflw",    0x8,  1,  1); // Chan2 A Auto-Adjust overflow  (0=no,1=yes)

    createStatusParam("Ch2BSlope",      0x9,  9,  8); // Chan2 B input offset value
    createStatusParam("Ch2BAdcOff",     0x9,  8,  0); // Chan2 B ADC Offset value

    createStatusParam("Ch2AdcMax",      0xA,  1, 15); // Chan2 got ADC max             (0=no,1=yes)
    createStatusParam("Ch2AdcMin",      0xA,  1, 14); // Chan2 got ADC min             (0=no,1=yes)
    createStatusParam("Ch2MDEv",        0xA,  1, 13); // Chan2 got multi-discp event   (0=no,1=yes)
    createStatusParam("Ch2Ev",          0xA,  1, 12); // Chan2 got event               (0=no,1=yes)
    createStatusParam("Ch2FifFul",      0xA,  1, 11); // Chan2 FIFO full detectec      (0=no,1=yes)
    createStatusParam("Ch2FifAmFF",     0xA,  1, 10); // Chan2 FIFO almost full detec  (0=no,1=yes)
    createStatusParam("Ch2FifAmFul",    0xA,  1,  9); // Chan2 FIFO almost full        (0=no,1=yes)
    createStatusParam("Ch2NotEmpty",    0xA,  1,  8); // Chan2 FIFO has data           (0=no,1=yes)
    createStatusParam("Ch2BAdjEn",      0xA,  1,  7); // Chan2 B Auto-Adjust Active    (0=not active,1=active)
    createStatusParam("Ch2BAdjTrig",    0xA,  1,  6); // Chan2 B Auto-Adjust Got sampl (0=no sample,1=got sample)
    createStatusParam("Ch2BAdjSamL",    0xA,  1,  5); // Chan2 B Auto-Adjust Sample li (0=not active,1=active)
    createStatusParam("Ch2BSlopLim",    0xA,  1,  4); // Chan2 B Auto-Adjust Slope lim (0=not active,1=active)
    createStatusParam("Ch2BOffLim",     0xA,  1,  3); // Chan2 B Auto-Adjust Offset li (0=not active,1=active)
    createStatusParam("Ch2BSlopLi1",    0xA,  1,  2); // Chan2 B Auto-Adjust Slope? li (0=not active,1=active)
    createStatusParam("Ch2BOverflw",    0xA,  1,  1); // Chan2 B Auto-Adjust overflow  (0=no,1=yes)

    createStatusParam("Ch3AAdjEn",      0xB,  1, 15); // Chan3 A Auto-Adjust Active    (0=not active,1=active)
    createStatusParam("Ch3AAdjTrig",    0xB,  1, 14); // Chan3 A Auto-Adjust Got sampl (0=no sample,1=got sample)
    createStatusParam("Ch3AAdjSamL",    0xB,  1, 13); // Chan3 A Auto-Adjust Sample li (0=not active,1=active)
    createStatusParam("Ch3ASlopLim",    0xB,  1, 12); // Chan3 A Auto-Adjust Slope lim (0=not active,1=active)
    createStatusParam("Ch3AOffLim",     0xB,  1, 11); // Chan3 A Auto-Adjust Offset li (0=not active,1=active)
    createStatusParam("Ch3ASlopLi1",    0xB,  1, 10); // Chan3 A Auto-Adjust Slope? li (0=not active,1=active)
    createStatusParam("Ch3AOverflw",    0xB,  1,  9); // Chan3 A Auto-Adjust overflow  (0=no,1=yes)
    createStatusParam("Ch3ASlope",      0xB,  9,  0); // Chan3 A input offset value

    createStatusParam("Ch3AAdcOff",     0xC,  8,  0); // Chan3 A ADC Offset value
    createStatusParam("Ch3BAdcOff",     0xC,  8,  8); // Chan3 B ADC Offset value

    createStatusParam("Ch3BAdjEn",      0xD,  1, 15); // Chan3 B Auto-Adjust Active    (0=not active,1=active)
    createStatusParam("Ch3BAdjTrig",    0xD,  1, 14); // Chan3 B Auto-Adjust Got sampl (0=no sample,1=got sample)
    createStatusParam("Ch3BAdjSamL",    0xD,  1, 13); // Chan3 B Auto-Adjust Sample li (0=not active,1=active)
    createStatusParam("Ch3BSlopLim",    0xD,  1, 12); // Chan3 B Auto-Adjust Slope lim (0=not active,1=active)
    createStatusParam("Ch3BOffLim",     0xD,  1, 11); // Chan3 B Auto-Adjust Offset li (0=not active,1=active)
    createStatusParam("Ch3BSlopLi1",    0xD,  1, 10); // Chan3 B Auto-Adjust Slope? li (0=not active,1=active)
    createStatusParam("Ch3BOverflw",    0xD,  1,  9); // Chan3 B Auto-Adjust overflow  (0=no,1=yes)
    createStatusParam("Ch3BSlope",      0xD,  9,  0); // Chan3 B input offset value

    createStatusParam("Ch4ASlope",      0xE,  9,  8); // Chan4 A input offset value
    createStatusParam("Ch3AdcMax",      0xE,  1,  7); // Chan3 got ADC max             (0=no,1=yes)
    createStatusParam("Ch3AdcMin",      0xE,  1,  6); // Chan3 got ADC min             (0=no,1=yes)
    createStatusParam("Ch3MDEv",        0xE,  1,  5); // Chan3 got multi-discp event   (0=no,1=yes)
    createStatusParam("Ch3Ev",          0xE,  1,  4); // Chan3 got event               (0=no,1=yes)
    createStatusParam("Ch3FifFul",      0xE,  1,  3); // Chan3 FIFO full detectec      (0=no,1=yes)
    createStatusParam("Ch3FifAmFF",     0xE,  1,  2); // Chan3 FIFO almost full detec  (0=no,1=yes)
    createStatusParam("Ch3FifAmFul",    0xE,  1,  1); // Chan3 FIFO almost full        (0=no,1=yes)
    createStatusParam("Ch3NotEmpty",    0xE,  1,  0); // Chan3 FIFO has data           (0=no,1=yes)

    createStatusParam("Ch4AAdcOff",     0xF,  8,  8); // Chan4 A ADC Offset value
    createStatusParam("Ch4AAdjEn",      0xF,  1,  7); // Chan4 A Auto-Adjust Active    (0=not active,1=active)
    createStatusParam("Ch4AAdjTrig",    0xF,  1,  6); // Chan4 A Auto-Adjust Got sampl (0=no sample,1=got sample)
    createStatusParam("Ch4AAdjSamL",    0xF,  1,  5); // Chan4 A Auto-Adjust Sample li (0=not active,1=active)
    createStatusParam("Ch4ASlopLim",    0xF,  1,  4); // Chan4 A Auto-Adjust Slope lim (0=not active,1=active)
    createStatusParam("Ch4AOffLim",     0xF,  1,  3); // Chan4 A Auto-Adjust Offset li (0=not active,1=active)
    createStatusParam("Ch4ASlopLi1",    0xF,  1,  2); // Chan4 A Auto-Adjust Slope? li (0=not active,1=active)
    createStatusParam("Ch4AOverflw",    0xF,  1,  1); // Chan4 A Auto-Adjust overflow  (0=no,1=yes)

    createStatusParam("Ch4BSlope",      0x10, 9,  8); // Chan4 B input offset value
    createStatusParam("Ch4BAdcOff",     0x10, 8,  0); // Chan4 B ADC Offset value

    createStatusParam("Ch4AdcMax",      0x11, 1, 15); // Chan4 got ADC max             (0=no,1=yes)
    createStatusParam("Ch4AdcMin",      0x11, 1, 14); // Chan4 got ADC min             (0=no,1=yes)
    createStatusParam("Ch4MDEv",        0x11, 1, 13); // Chan4 got multi-discp event   (0=no,1=yes)
    createStatusParam("Ch4Ev",          0x11, 1, 12); // Chan4 got event               (0=no,1=yes)
    createStatusParam("Ch4FifFul",      0x11, 1, 11); // Chan4 FIFO full detectec      (0=no,1=yes)
    createStatusParam("Ch4FifAmFF",     0x11, 1, 10); // Chan4 FIFO almost full detec  (0=no,1=yes)
    createStatusParam("Ch4FifAmFul",    0x11, 1,  9); // Chan4 FIFO almost full        (0=no,1=yes)
    createStatusParam("Ch4NotEmpty",    0x11, 1,  8); // Chan4 FIFO has data           (0=no,1=yes)
    createStatusParam("Ch4BAdjEn",      0x11, 1,  7); // Chan4 B Auto-Adjust Active    (0=not active,1=active)
    createStatusParam("Ch4BAdjTrig",    0x11, 1,  6); // Chan4 B Auto-Adjust Got sampl (0=no sample,1=got sample)
    createStatusParam("Ch4BAdjSamL",    0x11, 1,  5); // Chan4 B Auto-Adjust Sample li (0=not active,1=active)
    createStatusParam("Ch4BSlopLim",    0x11, 1,  4); // Chan4 B Auto-Adjust Slope lim (0=not active,1=active)
    createStatusParam("Ch4BOffLim",     0x11, 1,  3); // Chan4 B Auto-Adjust Offset li (0=not active,1=active)
    createStatusParam("Ch4BSlopLi1",    0x11, 1,  2); // Chan4 B Auto-Adjust Slope? li (0=not active,1=active)
    createStatusParam("Ch4BOverflw",    0x11, 1,  1); // Chan4 B Auto-Adjust overflow  (0=no,1=yes)

    createStatusParam("Ch5AAdjEn",      0x12, 1, 15); // Chan5 A Auto-Adjust Active    (0=not active,1=active)
    createStatusParam("Ch5AAdjTrig",    0x12, 1, 14); // Chan5 A Auto-Adjust Got sampl (0=no sample,1=got sample)
    createStatusParam("Ch5AAdjSamL",    0x12, 1, 13); // Chan5 A Auto-Adjust Sample li (0=not active,1=active)
    createStatusParam("Ch5ASlopLim",    0x12, 1, 12); // Chan5 A Auto-Adjust Slope lim (0=not active,1=active)
    createStatusParam("Ch5AOffLim",     0x12, 1, 11); // Chan5 A Auto-Adjust Offset li (0=not active,1=active)
    createStatusParam("Ch5ASlopLi1",    0x12, 1, 10); // Chan5 A Auto-Adjust Slope? li (0=not active,1=active)
    createStatusParam("Ch5AOverflw",    0x12, 1,  9); // Chan5 A Auto-Adjust overflow  (0=no,1=yes)
    createStatusParam("Ch5ASlope",      0x12, 9,  0); // Chan5 A Input Offset value

    createStatusParam("Ch5BAdcOff",     0x13, 8,  8); // Chan5 B ADC Offset value
    createStatusParam("Ch5AAdcOff",     0x13, 8,  0); // Chan5 A ADC Offset value

    createStatusParam("Ch5BAdjEn",      0x14, 1, 15); // Chan5 B Auto-Adjust Active    (0=not active,1=active)
    createStatusParam("Ch5BAdjTrig",    0x14, 1, 14); // Chan5 B Auto-Adjust Got sampl (0=no sample,1=got sample)
    createStatusParam("Ch5BAdjSamL",    0x14, 1, 13); // Chan5 B Auto-Adjust Sample li (0=not active,1=active)
    createStatusParam("Ch5BSlopLim",    0x14, 1, 12); // Chan5 B Auto-Adjust Slope lim (0=not active,1=active)
    createStatusParam("Ch5BOffLim",     0x14, 1, 11); // Chan5 B Auto-Adjust Offset li (0=not active,1=active)
    createStatusParam("Ch5BSlopLi1",    0x14, 1, 10); // Chan5 B Auto-Adjust Slope? li (0=not active,1=active)
    createStatusParam("Ch5BOverflw",    0x14, 1,  9); // Chan5 B Auto-Adjust overflow  (0=no,1=yes)
    createStatusParam("Ch5BSlope",      0x14, 9,  0); // Chan5 B Input Offset value

    createStatusParam("Ch6ASlope",      0x15, 9,  8); // Chan6 A input offset value
    createStatusParam("Ch5AdcMax",      0x15, 1,  7); // Chan5 got ADC max             (0=no,1=yes)
    createStatusParam("Ch5AdcMin",      0x15, 1,  6); // Chan5 got ADC min             (0=no,1=yes)
    createStatusParam("Ch5MDEv",        0x15, 1,  5); // Chan5 got multi-discp event   (0=no,1=yes)
    createStatusParam("Ch5Ev",          0x15, 1,  4); // Chan5 got event               (0=no,1=yes)
    createStatusParam("Ch5FifFul",      0x15, 1,  3); // Chan5 FIFO full detectec      (0=no,1=yes)
    createStatusParam("Ch5FifAmFF",     0x15, 1,  2); // Chan5 FIFO almost full detec  (0=no,1=yes)
    createStatusParam("Ch5FifAmFul",    0x15, 1,  1); // Chan5 FIFO almost full        (0=no,1=yes)
    createStatusParam("Ch5NotEmpty",    0x15, 1,  0); // Chan5 FIFO has data           (0=no,1=yes)

    createStatusParam("Ch6AAdcOff",     0x16, 8,  8); // Chan6 A ADC Offset value
    createStatusParam("Ch6AAdjEn",      0x16, 1,  7); // Chan6 A Auto-Adjust Active    (0=not active,1=active)
    createStatusParam("Ch6AAdjTrig",    0x16, 1,  6); // Chan6 A Auto-Adjust Got sampl (0=no sample,1=got sample)
    createStatusParam("Ch6AAdjSamL",    0x16, 1,  5); // Chan6 A Auto-Adjust Sample li (0=not active,1=active)
    createStatusParam("Ch6ASlopLim",    0x16, 1,  4); // Chan6 A Auto-Adjust Slope lim (0=not active,1=active)
    createStatusParam("Ch6AOffLim",     0x16, 1,  3); // Chan6 A Auto-Adjust Offset li (0=not active,1=active)
    createStatusParam("Ch6ASlopLi1",    0x16, 1,  2); // Chan6 A Auto-Adjust Slope? li (0=not active,1=active)
    createStatusParam("Ch6AOverflw",    0x16, 1,  1); // Chan6 A Auto-Adjust overflow  (0=no,1=yes)

    createStatusParam("Ch6BSlope",      0x17, 9,  8); // Chan6 B input offset value
    createStatusParam("Ch6BAdcOff",     0x17, 8,  0); // Chan6 B ADC Offset value

    createStatusParam("Ch6AdcMax",      0x18, 1, 15); // Chan6 got ADC max             (0=no,1=yes)
    createStatusParam("Ch6AdcMin",      0x18, 1, 14); // Chan6 got ADC min             (0=no,1=yes)
    createStatusParam("Ch6MDEv",        0x18, 1, 13); // Chan6 got multi-discp event   (0=no,1=yes)
    createStatusParam("Ch6Ev",          0x18, 1, 12); // Chan6 got event               (0=no,1=yes)
    createStatusParam("Ch6FifFul",      0x18, 1, 11); // Chan6 FIFO full detectec      (0=no,1=yes)
    createStatusParam("Ch6FifAmFF",     0x18, 1, 10); // Chan6 FIFO almost full detec  (0=no,1=yes)
    createStatusParam("Ch6FifAmFul",    0x18, 1,  9); // Chan6 FIFO almost full        (0=no,1=yes)
    createStatusParam("Ch6NotEmpty",    0x18, 1,  8); // Chan6 FIFO has data           (0=no,1=yes)
    createStatusParam("Ch6BAdjEn",      0x18, 1,  7); // Chan6 B Auto-Adjust Active    (0=not active,1=active)
    createStatusParam("Ch6BAdjTrig",    0x18, 1,  6); // Chan6 B Auto-Adjust Got sampl (0=no sample,1=got sample)
    createStatusParam("Ch6BAdjSamL",    0x18, 1,  5); // Chan6 B Auto-Adjust Sample li (0=not active,1=active)
    createStatusParam("Ch6BSlopLim",    0x18, 1,  4); // Chan6 B Auto-Adjust Slope lim (0=not active,1=active)
    createStatusParam("Ch6BOffLim",     0x18, 1,  3); // Chan6 B Auto-Adjust Offset li (0=not active,1=active)
    createStatusParam("Ch6BSlopLi1",    0x18, 1,  2); // Chan6 B Auto-Adjust Slope? li (0=not active,1=active)
    createStatusParam("Ch6BOverflw",    0x18, 1,  1); // Chan6 B Auto-Adjust overflow  (0=no,1=yes)

    createStatusParam("Ch7AAdjEn",      0x19, 1, 15); // Chan7 A Auto-Adjust Active    (0=not active,1=active)
    createStatusParam("Ch7AAdjTrig",    0x19, 1, 14); // Chan7 A Auto-Adjust Got sampl (0=no sample,1=got sample)
    createStatusParam("Ch7AAdjSamL",    0x19, 1, 13); // Chan7 A Auto-Adjust Sample li (0=not active,1=active)
    createStatusParam("Ch7ASlopLim",    0x19, 1, 12); // Chan7 A Auto-Adjust Slope lim (0=not active,1=active)
    createStatusParam("Ch7AOffLim",     0x19, 1, 11); // Chan7 A Auto-Adjust Offset li (0=not active,1=active)
    createStatusParam("Ch7ASlopLi1",    0x19, 1, 10); // Chan7 A Auto-Adjust Slope? li (0=not active,1=active)
    createStatusParam("Ch7AOverflw",    0x19, 1,  9); // Chan7 A Auto-Adjust overflow  (0=no,1=yes)
    createStatusParam("Ch7ASlope",      0x19, 9,  0); // Chan7 A input offset value

    createStatusParam("Ch7AAdcOff",     0x1A, 8,  0); // Chan7 A ADC Offset value
    createStatusParam("Ch7BAdcOff",     0x1A, 8,  8); // Chan7 B ADC Offset value

    createStatusParam("Ch7BAdjEn",      0x1B, 1, 15); // Chan7 B Auto-Adjust Active    (0=not active,1=active)
    createStatusParam("Ch7BAdjTrig",    0x1B, 1, 14); // Chan7 B Auto-Adjust Got sampl (0=no sample,1=got sample)
    createStatusParam("Ch7BAdjSamL",    0x1B, 1, 13); // Chan7 B Auto-Adjust Sample li (0=not active,1=active)
    createStatusParam("Ch7BSlopLim",    0x1B, 1, 12); // Chan7 B Auto-Adjust Slope lim (0=not active,1=active)
    createStatusParam("Ch7BOffLim",     0x1B, 1, 11); // Chan7 B Auto-Adjust Offset li (0=not active,1=active)
    createStatusParam("Ch7BSlopLi1",    0x1B, 1, 10); // Chan7 B Auto-Adjust Slope? li (0=not active,1=active)
    createStatusParam("Ch7BOverflw",    0x1B, 1,  9); // Chan7 B Auto-Adjust overflow  (0=no,1=yes)
    createStatusParam("Ch7BSlope",      0x1B, 9,  0); // Chan7 B input offset value

    createStatusParam("Ch8ASlope",      0x1C, 9,  8); // Chan8 A input offset value
    createStatusParam("Ch7AdcMax",      0x1C, 1,  7); // Chan7 got ADC max             (0=no,1=yes)
    createStatusParam("Ch7AdcMin",      0x1C, 1,  6); // Chan7 got ADC min             (0=no,1=yes)
    createStatusParam("Ch7MDEv",        0x1C, 1,  5); // Chan7 got multi-discp event   (0=no,1=yes)
    createStatusParam("Ch7Ev",          0x1C, 1,  4); // Chan7 got event               (0=no,1=yes)
    createStatusParam("Ch7FifFul",      0x1C, 1,  3); // Chan7 FIFO full detectec      (0=no,1=yes)
    createStatusParam("Ch7FifAmFF",     0x1C, 1,  2); // Chan7 FIFO almost full detec  (0=no,1=yes)
    createStatusParam("Ch7FifAmFul",    0x1C, 1,  1); // Chan7 FIFO almost full        (0=no,1=yes)
    createStatusParam("Ch7NotEmpty",    0x1C, 1,  0); // Chan7 FIFO has data           (0=no,1=yes)

    createStatusParam("Ch8AAdcOff",     0x1D, 8,  8); // Chan8 A ADC Offset value
    createStatusParam("Ch8AAdjEn",      0x1D, 1,  7); // Chan8 A Auto-Adjust Active    (0=not active,1=active)
    createStatusParam("Ch8AAdjTrig",    0x1D, 1,  6); // Chan8 A Auto-Adjust Got sampl (0=no sample,1=got sample)
    createStatusParam("Ch8AAdjSamL",    0x1D, 1,  5); // Chan8 A Auto-Adjust Sample li (0=not active,1=active)
    createStatusParam("Ch8ASlopLim",    0x1D, 1,  4); // Chan8 A Auto-Adjust Slope lim (0=not active,1=active)
    createStatusParam("Ch8AOffLim",     0x1D, 1,  3); // Chan8 A Auto-Adjust Offset li (0=not active,1=active)
    createStatusParam("Ch8ASlopLi1",    0x1D, 1,  2); // Chan8 A Auto-Adjust Slope? li (0=not active,1=active)
    createStatusParam("Ch8AOverflw",    0x1D, 1,  1); // Chan8 A Auto-Adjust overflow  (0=no,1=yes)

    createStatusParam("Ch8BSlope",      0x1E, 9,  8); // Chan8 B input offset value
    createStatusParam("Ch8BAdcOff",     0x1E, 8,  0); // Chan8 B ADC Offset value

    createStatusParam("Ch8AdcMax",      0x1F, 1, 15); // Chan8 got ADC max             (0=no,1=yes)
    createStatusParam("Ch8AdcMin",      0x1F, 1, 14); // Chan8 got ADC min             (0=no,1=yes)
    createStatusParam("Ch8MDEv",        0x1F, 1, 13); // Chan8 got multi-discp event   (0=no,1=yes)
    createStatusParam("Ch8Ev",          0x1F, 1, 12); // Chan8 got event               (0=no,1=yes)
    createStatusParam("Ch8FifFul",      0x1F, 1, 11); // Chan8 FIFO full detectec      (0=no,1=yes)
    createStatusParam("Ch8FifAmFF",     0x1F, 1, 10); // Chan8 FIFO almost full detec  (0=no,1=yes)
    createStatusParam("Ch8FifAmFul",    0x1F, 1,  9); // Chan8 FIFO almost full        (0=no,1=yes)
    createStatusParam("Ch8NotEmpty",    0x1F, 1,  8); // Chan8 FIFO has data           (0=no,1=yes)
    createStatusParam("Ch8BAdjEn",      0x1F, 1,  7); // Chan8 B Auto-Adjust Active    (0=not active,1=active)
    createStatusParam("Ch8BAdjTrig",    0x1F, 1,  6); // Chan8 B Auto-Adjust Got sampl (0=no sample,1=got sample)
    createStatusParam("Ch8BAdjSamL",    0x1F, 1,  5); // Chan8 B Auto-Adjust Sample li (0=not active,1=active)
    createStatusParam("Ch8BSlopLim",    0x1F, 1,  4); // Chan8 B Auto-Adjust Slope lim (0=not active,1=active)
    createStatusParam("Ch8BOffLim",     0x1F, 1,  3); // Chan8 B Auto-Adjust Offset li (0=not active,1=active)
    createStatusParam("Ch8BSlopLi1",    0x1F, 1,  2); // Chan8 B Auto-Adjust Slope? li (0=not active,1=active)
    createStatusParam("Ch8BOverflw",    0x1F, 1,  1); // Chan8 B Auto-Adjust overflow  (0=no,1=yes)
}
