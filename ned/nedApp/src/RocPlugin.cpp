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

const unsigned RocPlugin::NUM_ROCPLUGIN_STATUSPARAMS    = 200;  //!< Since supporting multiple versions with different number of PVs, this is just a maximum value

RocPlugin::RocPlugin(const char *portName, const char *dispatcherPortName, const char *hardwareId, const char *version, int blocking)
    : BaseModulePlugin(portName, dispatcherPortName, hardwareId, BaseModulePlugin::CONN_TYPE_LVDS,
                       blocking, NUM_ROCPLUGIN_PARAMS + NUM_ROCPLUGIN_STATUSPARAMS)
    , m_version(version)
    , m_stateMachine(STAT_NOT_INITIALIZED)
{
    createParam("HwDate",       asynParamOctet, &FirmwareDate);
    createParam("HwVer",        asynParamInt32, &HardwareVer);
    createParam("HwRev",        asynParamInt32, &HardwareRev);
    createParam("FwVer",        asynParamInt32, &FirmwareVer);
    createParam("FwRev",        asynParamInt32, &FirmwareRev);
    createParam("Command",      asynParamInt32, &Command);
    createParam("Status",       asynParamInt32, &Status);

    if (m_version == "5.x/5.x") {
        createStatusParams_V5();
        rspReadVersion = std::bind(&RocPlugin::rspReadVersion_V5_5x, this, std::placeholders::_1);
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

    m_statusPayloadSize = 0;
    for (std::map<int, BaseModulePlugin::StatusParamDesc>::iterator it=m_statusParams.begin(); it != m_statusParams.end(); it++) {
        if (m_statusPayloadSize < it->second.offset)
            m_statusPayloadSize = it->second.offset;
    }
    m_statusPayloadSize++;

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

    setIntegerParam(Status, STAT_NOT_INITIALIZED);
    callParamCallbacks();
    setCallbacks(true);
}

asynStatus RocPlugin::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
    std::function<void(void)> timeoutCb;
    if (pasynUser->reason == Command) {
        switch (value) {
        case BaseModulePlugin::CMD_INITIALIZE:
            sendToDispatcher(DasPacket::CMD_DISCOVER);
            sendToDispatcher(DasPacket::CMD_READ_VERSION);
            timeoutCb = std::bind(&RocPlugin::timeout, this, DasPacket::CMD_READ_VERSION);
            scheduleCallback(timeoutCb, NO_RESPONSE_TIMEOUT);
            break;
        case BaseModulePlugin::CMD_READ_STATUS:
            sendToDispatcher(DasPacket::CMD_READ_STATUS);
            break;
        default:
            LOG_ERROR("Unrecognized command '%d'", value);
            return asynError;
        }
        return asynSuccess;
    }
    return BaseModulePlugin::writeInt32(pasynUser, value);
}


void RocPlugin::processData(const DasPacketList * const packetList)
{
    int nReceived = 0;
    int nProcessed = 0;
    int status;
    getIntegerParam(ReceivedCount,  &nReceived);
    getIntegerParam(ProcessedCount, &nProcessed);
    getIntegerParam(Status,         &status);

    for (const DasPacket *packet = packetList->first(); packet != 0; packet = packetList->next(packet)) {
        nReceived++;

        if (!packet->isResponse())
            continue;

        // Silently skip packets we're not interested in
        if (!packet->isResponse() || packet->getSourceAddress() != m_hardwareId)
            continue;

        // Parse only responses valid in pre-initialization state
        switch (packet->cmdinfo.command) {
        case DasPacket::CMD_DISCOVER:
            rspDiscover(packet);
            nProcessed++;
            break;
        case DasPacket::CMD_READ_VERSION:
            rspReadVersion(packet);
            nProcessed++;
            break;
        default:
            break;
        }

        if (status != STAT_READY) {
            LOG_WARN("Module type and versions not yet verified, ignoring packet");
            continue;
        }

        switch (packet->cmdinfo.command) {
        case DasPacket::CMD_READ_STATUS:
            rspReadStatus(packet);
            nProcessed++;
            break;
        default:
            LOG_WARN("Received unhandled response 0x%02X", packet->cmdinfo.command);
            break;
        }
    }

    setIntegerParam(ReceivedCount,  nReceived);
    setIntegerParam(ProcessedCount, nProcessed);
    callParamCallbacks();
}

void RocPlugin::rspDiscover(const DasPacket *packet)
{
    enum Status status;
    if (packet->cmdinfo.module_type == DasPacket::MOD_TYPE_ROC)
        status = m_stateMachine.transition(DISCOVER_OK);
    else
        status = m_stateMachine.transition(DISCOVER_MISMATCH);
    setIntegerParam(Status, status);
    callParamCallbacks();
}

void RocPlugin::rspReadVersion_V5_5x(const DasPacket *packet)
{
    enum Status status;

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
        status = m_stateMachine.transition(VERSION_READ_MISMATCH);
    } else {
        char date[20];
        setIntegerParam(HardwareVer, response->hw_version);
        setIntegerParam(HardwareRev, response->hw_revision);
        setIntegerParam(FirmwareVer, response->fw_version);
        setIntegerParam(FirmwareRev, response->fw_revision);
        snprintf(date, sizeof(date), "%d%d/%d/%d", HEX_BYTE_TO_DEC(response->year >> 8),
                                                   HEX_BYTE_TO_DEC(response->year),
                                                   HEX_BYTE_TO_DEC(response->month),
                                                   HEX_BYTE_TO_DEC(response->day));
        setStringParam(HardwareDate, date);

        if (response->hw_version == 5 && response->fw_version == 5) {
            status = m_stateMachine.transition(VERSION_READ_OK);
        } else {
            status = m_stateMachine.transition(VERSION_READ_MISMATCH);
            LOG_ERROR("ROC version does not match configuration: %d.%d/%d.%d != %s", response->hw_version,
                                                                                     response->hw_revision,
                                                                                     response->fw_version,
                                                                                     response->fw_revision,
                                                                                     m_version.c_str());
        }
        setIntegerParam(Status, status);
    }

    callParamCallbacks();
}

void RocPlugin::rspReadStatus(const DasPacket *packet)
{
    // This function could not yet be verified. ROC V5 5.2 is sending us 64bytes but we only know
    // 28 of them. dcomserver knows 32 bytes for V5 5.0 so we're still missing half of them.
    if (packet->getPayloadLength() != m_statusPayloadSize) {
        LOG_ERROR("Status response packet size mismatch, expected %uB got %uB (ROC version mismatch?)", m_statusPayloadSize, packet->getPayloadLength());
        return;
    }
    BaseModulePlugin::rspReadStatus(packet);
}

void RocPlugin::timeout(DasPacket::CommandType command)
{
    if (command == DasPacket::CMD_READ_VERSION) {
        // Proper states definition takes place and do the right job
        m_stateMachine.transition(TIMEOUT);
    }
}

void RocPlugin::createStatusParams_V5()
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

    createStatusParam("IntFifoFull",    0x1,  1, 13); // Internal Data FIFO Almost ful (0=not full,1=full)
    createStatusParam("IntFifoEmp",     0x1,  1, 12); // Internal Data FIFO Empty flag (0=not empty,1=empty)
    createStatusParam("CalcBadFin",     0x1,  1, 11); // Calc: Bad Final Calculation.  (0=no error,1=error)
    createStatusParam("CalcBadEff",     0x1,  1, 10); // Calc: Bad Effective Calculati (0=no error,1=error)
    createStatusParam("CalcBadOver",    0x1,  1,  9); // Calc: Data overflow detected. (0=no error,1=error)
    createStatusParam("CalcBadCnt",     0x1,  1,  8); // Calc: Bad word count.         (0=no error,1=error)
    createStatusParam("DataFifoFul",    0x1,  1,  7); // Data FIFO Almost full flag.   (0=not full,1=almost full)
    createStatusParam("DataFifoEmp",    0x1,  1,  6); // Data FIFO Empty flag.         (0=not empty,1=empty)
    createStatusParam("HVStatus",       0x1,  1,  5); // High Voltage Status bit
    createStatusParam("CalcActive",     0x1,  1,  4); // Calculation: Active           (0=not active,1=active)
    createStatusParam("AcquireStat",    0x1,  1,  3); // Acquiring data                (0=not acquiring,1=acquiring)
    createStatusParam("Discovered",     0x1,  1,  2); // Discovered.                   (0=not discovered,1=discovered)
    createStatusParam("Configured2",    0x1,  1,  1); // Configured (Section 2)        (0=not configured,1=configured)
    createStatusParam("Configured1",    0x1,  1,  0); // Configured (Section 1)        (0=not configured,1=configured)

    createStatusParam("RisEdgeA",       0x2,  8,  8); // Rising edge in A[8:1]
    createStatusParam("SysrstBHigh",    0x2,  1,  7); // SYSRST_B Got HIGH             (0=no,1=yes)
    createStatusParam("SysrstBLow",     0x2,  1,  6); // SYSRST_B Got LOW              (0=no,1=yes)
    createStatusParam("TxenBHigh",      0x2,  1,  5); // TXEN_B Got HIGH               (0=no,1=yes)
    createStatusParam("TxenBLow",       0x2,  1,  4); // TXEN_B Got LOW                (0=no,1=yes)
    createStatusParam("TsyncHigh",      0x2,  1,  3); // TSYNC Got HIGH                (0=no,1=yes)
    createStatusParam("TsyncLow",       0x2,  1,  2); // TSYNC Got LOW                 (0=no,1=yes)
    createStatusParam("TclkHigh",       0x2,  1,  1); // TCLK Got HIGH                 (0=no,1=yes)
    createStatusParam("TclkLow",        0x2,  1,  0); // TCLK Got LOW                  (0=no,1=yes)

    createStatusParam("RisEdgeSum",     0x3,  8,  8); // Rising edge in SUM[8:1]
    createStatusParam("RisEdgeB",       0x3,  8,  0); // Rising edge in B[8:1]

    createStatusParam("A1AutoAdjEn",    0x4,  1, 15); // A1 Auto-Adjust Active         (0=not active,1=active)
    createStatusParam("A1AdjSample",    0x4,  1, 14); // A1 Auto-Adjust Got sample     (0=no sample,1=got sample)
    createStatusParam("A1AdjSamLim",    0x4,  1, 13); // A1 Auto-Adjust Sample limit   (0=not active,1=active)
    createStatusParam("A1AdjSloLim",    0x4,  1, 12); // A1 Auto-Adjust Slope limit    (0=not active,1=active)
    createStatusParam("A1AdjOffLim",    0x4,  1, 11); // A1 Auto-Adjust Offset limit   (0=not active,1=active)
    createStatusParam("A1AdjSlXLim",    0x4,  1, 10); // A1 Auto-Adjust Slope? limit   (0=not active,1=active)
    createStatusParam("A1AdjOverfl",    0x4,  1,  9); // A1 Auto-Adjust overflow       (0=no,1=yes)
    createStatusParam("A1InOffset",     0x4,  9,  0); // A1 Input Offset value

    createStatusParam("A2AdcOff",       0x5,  8,  8); // A2 ADC Offset value
    createStatusParam("A1AdcOff",       0x5,  8,  0); // A1 ADC Offset value

    createStatusParam("A2AutoAdjEn",    0x6,  1, 15); // A2 Auto-Adjust Active         (0=not active,1=active)
    createStatusParam("A2AdjSample",    0x6,  1, 14); // A2 Auto-Adjust Got sample     (0=no sample,1=got sample)
    createStatusParam("A2AdjSamLim",    0x6,  1, 13); // A2 Auto-Adjust Sample limit   (0=not active,1=active)
    createStatusParam("A2AdjSloLim",    0x6,  1, 12); // A2 Auto-Adjust Slope limit    (0=not active,1=active)
    createStatusParam("A2AdjOffLim",    0x6,  1, 11); // A2 Auto-Adjust Offset limit   (0=not active,1=active)
    createStatusParam("A2AdjSlXLim",    0x6,  1, 10); // A2 Auto-Adjust Slope? limit   (0=not active,1=active)
    createStatusParam("A2AdjOverfl",    0x6,  1,  9); // A2 Auto-Adjust overflow       (0=no,1=yes)
    createStatusParam("A2InOffset",     0x6,  9,  0); // A2 Input Offset value

    createStatusParam("A3AutoAdjEn",    0x7,  1, 15); // A3 Auto-Adjust Active         (0=not active,1=active)
    createStatusParam("A3AdjSample",    0x7,  1, 14); // A3 Auto-Adjust Got sample     (0=no sample,1=got sample)
    createStatusParam("A3AdjSamLim",    0x7,  1, 13); // A3 Auto-Adjust Sample limit   (0=not active,1=active)
    createStatusParam("A3AdjSloLim",    0x7,  1, 12); // A3 Auto-Adjust Slope limit    (0=not active,1=active)
    createStatusParam("A3AdjOffLim",    0x7,  1, 11); // A3 Auto-Adjust Offset limit   (0=not active,1=active)
    createStatusParam("A3AdjSlXLim",    0x7,  1, 10); // A3 Auto-Adjust Slope? limit   (0=not active,1=active)
    createStatusParam("A3AdjOverfl",    0x7,  1,  9); // A3 Auto-Adjust overflow       (0=no,1=yes)
    createStatusParam("A3InOffset",     0x7,  9,  0); // A3 Input Offset value

    createStatusParam("A4AdcOff",       0x8,  8,  8); // A4 ADC Offset value
    createStatusParam("A3AdcOff",       0x8,  8,  0); // A3 ADC Offset value

    createStatusParam("A4AutoAdjEn",    0x9,  1, 15); // A4 Auto-Adjust Active         (0=not active,1=active)
    createStatusParam("A4AdjSample",    0x9,  1, 14); // A4 Auto-Adjust Got sample     (0=no sample,1=got sample)
    createStatusParam("A4AdjSamLim",    0x9,  1, 13); // A4 Auto-Adjust Sample limit   (0=not active,1=active)
    createStatusParam("A4AdjSloLim",    0x9,  1, 12); // A4 Auto-Adjust Slope limit    (0=not active,1=active)
    createStatusParam("A4AdjOffLim",    0x9,  1, 11); // A4 Auto-Adjust Offset limit   (0=not active,1=active)
    createStatusParam("A4AdjSlXLim",    0x9,  1, 10); // A4 Auto-Adjust Slope? limit   (0=not active,1=active)
    createStatusParam("A4AdjOverfl",    0x9,  1,  9); // A4 Auto-Adjust overflow       (0=no,1=yes)
    createStatusParam("A4InOffset",     0x9,  9,  0); // A4 Input Offset value

    createStatusParam("A5AutoAdjEn",    0xA,  1, 15); // A5 Auto-Adjust Active         (0=not active,1=active)
    createStatusParam("A5AdjSample",    0xA,  1, 14); // A5 Auto-Adjust Got sample     (0=no sample,1=got sample)
    createStatusParam("A5AdjSamLim",    0xA,  1, 13); // A5 Auto-Adjust Sample limit   (0=not active,1=active)
    createStatusParam("A5AdjSloLim",    0xA,  1, 12); // A5 Auto-Adjust Slope limit    (0=not active,1=active)
    createStatusParam("A5AdjOffLim",    0xA,  1, 11); // A5 Auto-Adjust Offset limit   (0=not active,1=active)
    createStatusParam("A5AdjSlXLim",    0xA,  1, 10); // A5 Auto-Adjust Slope? limit   (0=not active,1=active)
    createStatusParam("A5AdjOverfl",    0xA,  1,  9); // A5 Auto-Adjust overflow       (0=no,1=yes)
    createStatusParam("A5InOffset",     0xA,  9,  0); // A5 Input Offset value

    createStatusParam("A6AdcOff",       0xB,  8,  8); // A6 ADC Offset value
    createStatusParam("A5AdcOff",       0xB,  8,  0); // A5 ADC Offset value

    createStatusParam("A6AutoAdjEn",    0xC,  1, 15); // A6 Auto-Adjust Active         (0=not active,1=active)
    createStatusParam("A6AdjSample",    0xC,  1, 14); // A6 Auto-Adjust Got sample     (0=no sample,1=got sample)
    createStatusParam("A6AdjSamLim",    0xC,  1, 13); // A6 Auto-Adjust Sample limit   (0=not active,1=active)
    createStatusParam("A6AdjSloLim",    0xC,  1, 12); // A6 Auto-Adjust Slope limit    (0=not active,1=active)
    createStatusParam("A6AdjOffLim",    0xC,  1, 11); // A6 Auto-Adjust Offset limit   (0=not active,1=active)
    createStatusParam("A6AdjSlXLim",    0xC,  1, 10); // A6 Auto-Adjust Slope? limit   (0=not active,1=active)
    createStatusParam("A6AdjOverfl",    0xC,  1,  9); // A6 Auto-Adjust overflow       (0=no,1=yes)
    createStatusParam("A6InOffset",     0xC,  9,  0); // A6 Input Offset value

    createStatusParam("A7AutoAdjEn",    0xD,  1, 15); // A7 Auto-Adjust Active         (0=not active,1=active)
    createStatusParam("A7AdjSample",    0xD,  1, 14); // A7 Auto-Adjust Got sample     (0=no sample,1=got sample)
    createStatusParam("A7AdjSamLim",    0xD,  1, 13); // A7 Auto-Adjust Sample limit   (0=not active,1=active)
    createStatusParam("A7AdjSloLim",    0xD,  1, 12); // A7 Auto-Adjust Slope limit    (0=not active,1=active)
    createStatusParam("A7AdjOffLim",    0xD,  1, 11); // A7 Auto-Adjust Offset limit   (0=not active,1=active)
    createStatusParam("A7AdjSlXLim",    0xD,  1, 10); // A7 Auto-Adjust Slope? limit   (0=not active,1=active)
    createStatusParam("A7AdjOverfl",    0xD,  1,  9); // A7 Auto-Adjust overflow       (0=no,1=yes)
    createStatusParam("A7InOffset",     0xD,  9,  0); // A7 Input Offset value

    createStatusParam("A8AdcOff",       0xE,  8,  8); // A8 ADC Offset value
    createStatusParam("A7AdcOff",       0xE,  8,  0); // A7 ADC Offset value

    createStatusParam("A8AutoAdjEn",    0xF,  1, 15); // A8 Auto-Adjust Active         (0=not active,1=active)
    createStatusParam("A8AdjSample",    0xF,  1, 14); // A8 Auto-Adjust Got sample     (0=no sample,1=got sample)
    createStatusParam("A8AdjSamLim",    0xF,  1, 13); // A8 Auto-Adjust Sample limit   (0=not active,1=active)
    createStatusParam("A8AdjSloLim",    0xF,  1, 12); // A8 Auto-Adjust Slope limit    (0=not active,1=active)
    createStatusParam("A8AdjOffLim",    0xF,  1, 11); // A8 Auto-Adjust Offset limit   (0=not active,1=active)
    createStatusParam("A8AdjSlXLim",    0xF,  1, 10); // A8 Auto-Adjust Slope? limit   (0=not active,1=active)
    createStatusParam("A8AdjOverfl",    0xF,  1,  9); // A8 Auto-Adjust overflow       (0=no,1=yes)
    createStatusParam("A8InOffset",     0xF,  9,  0); // A8 Input Offset value

    createStatusParam("B1AutoAdjEn",    0x10, 1, 15); // B1 Auto-Adjust Active         (0=not active,1=active)
    createStatusParam("B1AdjSample",    0x10, 1, 14); // B1 Auto-Adjust Got sample     (0=no sample,1=got sample)
    createStatusParam("B1AdjSamLim",    0x10, 1, 13); // B1 Auto-Adjust Sample limit   (0=not active,1=active)
    createStatusParam("B1AdjSloLim",    0x10, 1, 12); // B1 Auto-Adjust Slope limit    (0=not active,1=active)
    createStatusParam("B1AdjOffLim",    0x10, 1, 11); // B1 Auto-Adjust Offset limit   (0=not active,1=active)
    createStatusParam("B1AdjSlXLim",    0x10, 1, 10); // B1 Auto-Adjust Slope? limit   (0=not active,1=active)
    createStatusParam("B1AdjOverfl",    0x10, 1,  9); // B1 Auto-Adjust overflow       (0=no,1=yes)
    createStatusParam("B1InOffset",     0x10, 9,  0); // B1 Input Offset value

    createStatusParam("B2AdcOff",       0x11, 8,  8); // B2 ADC Offset value
    createStatusParam("B1AdcOff",       0x11, 8,  0); // B1 ADC Offset value

    createStatusParam("B2AutoAdjEn",    0x12, 1, 15); // B2 Auto-Adjust Active         (0=not active,1=active)
    createStatusParam("B2AdjSample",    0x12, 1, 14); // B2 Auto-Adjust Got sample     (0=no sample,1=got sample)
    createStatusParam("B2AdjSamLim",    0x12, 1, 13); // B2 Auto-Adjust Sample limit   (0=not active,1=active)
    createStatusParam("B2AdjSloLim",    0x12, 1, 12); // B2 Auto-Adjust Slope limit    (0=not active,1=active)
    createStatusParam("B2AdjOffLim",    0x12, 1, 11); // B2 Auto-Adjust Offset limit   (0=not active,1=active)
    createStatusParam("B2AdjSlXLim",    0x12, 1, 10); // B2 Auto-Adjust Slope? limit   (0=not active,1=active)
    createStatusParam("B2AdjOverfl",    0x12, 1,  9); // B2 Auto-Adjust overflow       (0=no,1=yes)
    createStatusParam("B2InOffset",     0x12, 9,  0); // B2 Input Offset value

    createStatusParam("B3AutoAdjEn",    0x13, 1, 15); // B3 Auto-Adjust Active         (0=not active,1=active)
    createStatusParam("B3AdjSample",    0x13, 1, 14); // B3 Auto-Adjust Got sample     (0=no sample,1=got sample)
    createStatusParam("B3AdjSamLim",    0x13, 1, 13); // B3 Auto-Adjust Sample limit   (0=not active,1=active)
    createStatusParam("B3AdjSloLim",    0x13, 1, 12); // B3 Auto-Adjust Slope limit    (0=not active,1=active)
    createStatusParam("B3AdjOffLim",    0x13, 1, 11); // B3 Auto-Adjust Offset limit   (0=not active,1=active)
    createStatusParam("B3AdjSlXLim",    0x13, 1, 10); // B3 Auto-Adjust Slope? limit   (0=not active,1=active)
    createStatusParam("B3AdjOverfl",    0x13, 1,  9); // B3 Auto-Adjust overflow       (0=no,1=yes)
    createStatusParam("B3InOffset",     0x13, 9,  0); // B3 Input Offset value

    createStatusParam("B4AdcOff",       0x14, 8,  8); // B4 ADC Offset value
    createStatusParam("B3AdcOff",       0x14, 8,  0); // B3 ADC Offset value

    createStatusParam("B4AutoAdjEn",    0x15, 1, 15); // B4 Auto-Adjust Active         (0=not active,1=active)
    createStatusParam("B4AdjSample",    0x15, 1, 14); // B4 Auto-Adjust Got sample     (0=no sample,1=got sample)
    createStatusParam("B4AdjSamLim",    0x15, 1, 13); // B4 Auto-Adjust Sample limit   (0=not active,1=active)
    createStatusParam("B4AdjSloLim",    0x15, 1, 12); // B4 Auto-Adjust Slope limit    (0=not active,1=active)
    createStatusParam("B4AdjOffLim",    0x15, 1, 11); // B4 Auto-Adjust Offset limit   (0=not active,1=active)
    createStatusParam("B4AdjSlXLim",    0x15, 1, 10); // B4 Auto-Adjust Slope? limit   (0=not active,1=active)
    createStatusParam("B4AdjOverfl",    0x15, 1,  9); // B4 Auto-Adjust overflow       (0=no,1=yes)
    createStatusParam("B4InOffset",     0x15, 0,  0); // B4 Input Offset value

    createStatusParam("B5AutoAdjEn",    0x16, 1, 15); // B5 Auto-Adjust Active         (0=not active,1=active)
    createStatusParam("B5AdjSample",    0x16, 1, 14); // B5 Auto-Adjust Got sample     (0=no sample,1=got sample)
    createStatusParam("B5AdjSamLim",    0x16, 1, 13); // B5 Auto-Adjust Sample limit   (0=not active,1=active)
    createStatusParam("B5AdjSloLim",    0x16, 1, 12); // B5 Auto-Adjust Slope limit    (0=not active,1=active)
    createStatusParam("B5AdjOffLim",    0x16, 1, 11); // B5 Auto-Adjust Offset limit   (0=not active,1=active)
    createStatusParam("B5AdjSlXLim",    0x16, 1, 10); // B5 Auto-Adjust Slope? limit   (0=not active,1=active)
    createStatusParam("B5AdjOverfl",    0x16, 1,  9); // B5 Auto-Adjust overflow       (0=no,1=yes)
    createStatusParam("B5InOffset",     0x16, 9,  0); // B5 Input Offset value

    createStatusParam("B6AdcOff",       0x17, 8,  8); // B6 ADC Offset value
    createStatusParam("B5AdcOff",       0x17, 8,  0); // B5 ADC Offset value

    createStatusParam("B6AutoAdjEn",    0x18, 1, 15); // B6 Auto-Adjust Active         (0=not active,1=active)
    createStatusParam("B6AdjSample",    0x18, 1, 14); // B6 Auto-Adjust Got sample     (0=no sample,1=got sample)
    createStatusParam("B6AdjSamLim",    0x18, 1, 13); // B6 Auto-Adjust Sample limit   (0=not active,1=active)
    createStatusParam("B6AdjSloLim",    0x18, 1, 12); // B6 Auto-Adjust Slope limit    (0=not active,1=active)
    createStatusParam("B6AdjOffLim",    0x18, 1, 11); // B6 Auto-Adjust Offset limit   (0=not active,1=active)
    createStatusParam("B6AdjSlXLim",    0x18, 1, 10); // B6 Auto-Adjust Slope? limit   (0=not active,1=active)
    createStatusParam("B6AdjOverfl",    0x18, 1,  9); // B6 Auto-Adjust overflow       (0=no,1=yes)
    createStatusParam("B6InOffset",     0x18, 9,  0); // B6 Input Offset value

    createStatusParam("B7AutoAdjEn",    0x19, 1, 15); // B7 Auto-Adjust Active         (0=not active,1=active)
    createStatusParam("B7AdjSample",    0x19, 1, 14); // B7 Auto-Adjust Got sample     (0=no sample,1=got sample)
    createStatusParam("B7AdjSamLim",    0x19, 1, 13); // B7 Auto-Adjust Sample limit   (0=not active,1=active)
    createStatusParam("B7AdjSloLim",    0x19, 1, 12); // B7 Auto-Adjust Slope limit    (0=not active,1=active)
    createStatusParam("B7AdjOffLim",    0x19, 1, 11); // B7 Auto-Adjust Offset limit   (0=not active,1=active)
    createStatusParam("B7AdjSlXLim",    0x19, 1, 10); // B7 Auto-Adjust Slope? limit   (0=not active,1=active)
    createStatusParam("B7AdjOverfl",    0x19, 1,  9); // B7 Auto-Adjust overflow       (0=no,1=yes)
    createStatusParam("B7InOffset",     0x19, 0,  0); // B7 Input Offset value

    createStatusParam("B8AdcOff",       0x1A, 8,  8); // B8 ADC Offset value
    createStatusParam("B7AdcOff",       0x1A, 8,  0); // B7 ADC Offset value

    createStatusParam("B8AutoAdjEn",    0x1B, 1, 15); // B8 Auto-Adjust Active         (0=not active,1=active)
    createStatusParam("B8AdjSample",    0x1B, 1, 14); // B8 Auto-Adjust Got sample     (0=no sample,1=got sample)
    createStatusParam("B8AdjSamLim",    0x1B, 1, 13); // B8 Auto-Adjust Sample limit   (0=not active,1=active)
    createStatusParam("B8AdjSloLim",    0x1B, 1, 12); // B8 Auto-Adjust Slope limit    (0=not active,1=active)
    createStatusParam("B8AdjOffLim",    0x1B, 1, 11); // B8 Auto-Adjust Offset limit   (0=not active,1=active)
    createStatusParam("B8AdjSlXLim",    0x1B, 1, 10); // B8 Auto-Adjust Slope? limit   (0=not active,1=active)
    createStatusParam("B8AdjOverfl",    0x1B, 1,  9); // B8 Auto-Adjust overflow       (0=no,1=yes)
    createStatusParam("B8InOffset",     0x1B, 0,  0); // B8 Input Offset value
}

/*
 * Following functions have not yet been verified, still they represent direct match to the LPSD document
 *
void RocPlugin::createStatusParams_V2_5x()
{
//    BLXXX:Det:RocXXX:| sig nam |                       | EPICS record description  | (bi and mbbi description)
    createStatusParam("LogicErr",       0x0,  1, 15); // WRITE_CNFG during ACQUISITION (0=no error,1=error)
    createStatusParam("CmdLenErr",      0x0,  1, 14); // Command length error          (0=no error,1=error)
    createStatusParam("UnknownCmd",     0x0,  1, 13); // Unrecognized command error    (0=no error,1=error)
    createStatusParam("IntFifoFull",    0x1,  1, 12); // Internal Data FIFO Almost ful (0=not full,1=full)
    createStatusParam("IntFifoEmp",     0x1,  1, 11); // Internal Data FIFO Empty flag (0=not empty,1=empty)
    createStatusParam("CalcBadFin",     0x1,  1, 10); // Calc: Bad Final Calculation.  (0=no error,1=error)
    createStatusParam("CalcBadEff",     0x1,  1,  9); // Calc: Bad Effective Calculati (0=no error,1=error)
    createStatusParam("CalcBadOver",    0x1,  1,  8); // Calc: Data overflow detected. (0=no error,1=error)
    createStatusParam("CalcBadCnt",     0x1,  1,  7); // Calc: Bad word count.         (0=no error,1=error)
    createStatusParam("LvdsFifoFul",    0x0,  1,  6); // LVDS FIFO went full.          (0=not full,1=full)
    createStatusParam("LvdsStartEr",    0x0,  1,  5); // LVDS start before stop bit    (0=no error,1=error)
    createStatusParam("LvdsNoStart",    0x0,  1,  4); // LVDS data without start.      (0=no error,1=error)
    createStatusParam("LvdsTimeout",    0x0,  1,  3); // LVDS packet timeout.          (0=no timeout,1=timeout)
    createStatusParam("LvdsLenErr",     0x0,  1,  2); // LVDS packet length error.     (0=no error,1=error)
    createStatusParam("LvdsTypeErr",    0x0,  1,  1); // LVDS data type error.         (0=no error,1=error)
    createStatusParam("LvdsParErr",     0x0,  1,  0); // LVDS parity error.            (0=no error,1=error)

    createStatusParam("LvdsPwrDown",    0x1,  1,  9); // Got LVDS powerdown sequence   (0=no,1=yes)
    createStatusParam("RcvdSysRst",     0x1,  1,  8); // Got system reset              (0=no,1=yes)
    createStatusParam("RcvdTimeErr",    0x1,  1,  1); // Got timer error               (0=no error,1=error)
    createStatusParam("RcvdFatalEr",    0x1,  1,  0); // Got fatal error               (0=no error,1=error)

    createStatusParam("DataRdyChan",    0x2,  8,  8); // Data ready channel
    createStatusParam("ChanProg",       0x2,  8,  0); // Channel programmed

    createStatusParam("HVStatus",       0x3,  1, 15); // High Voltage Status bit       (0=no error,1=error)
    createStatusParam("CalcFifoFul",    0x3,  1, 14); // Calc: FIFO almost full        (0=not full,1=almost full)
    createStatusParam("CalcFifoDat",    0x3,  1, 13); // Calc: FIFO has data           (0=no,1=yes)
    createStatusParam("CalcInFifF",     0x3,  1, 12); // Calc: Input FIFO almost full  (0=not full,1=almost full)
    createStatusParam("CalcInFifD",     0x3,  1, 11); // Calc: Input FIFO almost full  (0=not full,1=almost full)
    createStatusParam("TxenBHigh",      0x3,  1, 10); // TXEN_B Got HIGH               (0=no,1=yes)
    createStatusParam("TxenBLow",       0x3,  1,  9); // TXEN_B Got LOW                (0=no,1=yes)
    createStatusParam("SysrstBHigh",    0x3,  1,  8); // SYSRST_B Got HIGH             (0=no,1=yes)
    createStatusParam("SysrstBLow",     0x3,  1,  7); // SYSRST_B Got LOW              (0=no,1=yes)
    createStatusParam("TsyncHigh",      0x3,  1,  6); // TSYNC Got HIGH                (0=no,1=yes)
    createStatusParam("TsyncLow",       0x3,  1,  5); // TSYNC Got LOW                 (0=no,1=yes)
    createStatusParam("TclkHigh",       0x3,  1,  4); // TCLK Got HIGH                 (0=no,1=yes)
    createStatusParam("TclkLow",        0x3,  1,  3); // TCLK Got LOW                  (0=no,1=yes)
    createStatusParam("CalcActive",     0x3,  1,  2); // Calculation: Active           (0=not active,1=active)
    createStatusParam("AcquireStat",    0x3,  1,  1); // Acquiring data                (0=not acquiring,1=acquiring)
    createStatusParam("Discovered",     0x3,  1,  0); // Discovered.                   (0=not discovered,1=discovered)

    for (unsigned i=0; i<NUM_CHANNELS; i++) {
        createChannelStatusParam("A%uAutoAdjEn", i+1, 0x0+i,  1, 15); // Ai Auto-Adjust Active         (0=not active,1=active)
        createChannelStatusParam("A%uAdjSample", i+1, 0x0+i,  1, 14); // Ai Auto-Adjust Got sample     (0=no sample,1=got sample)
        createChannelStatusParam("A%uAdjSamLim", i+1, 0x0+i,  1, 13); // Ai Auto-Adjust Sample limit   (0=not active,1=active)
        createChannelStatusParam("A%uAdjSloLim", i+1, 0x0+i,  1, 12); // Ai Auto-Adjust Slope limit    (0=not active,1=active)
        createChannelStatusParam("A%uAdjOffLim", i+1, 0x0+i,  1, 11); // Ai Auto-Adjust Offset limit   (0=not active,1=active)
        createChannelStatusParam("A%uAdjSlXLim", i+1, 0x0+i,  1, 10); // Ai Auto-Adjust Slope? limit   (0=not active,1=active)
        createChannelStatusParam("A%uAdjOverfl", i+1, 0x0+i,  1,  9); // Ai Auto-Adjust overflow       (0=no,1=yes)
        createChannelStatusParam("A%uInOffset",  i+1, 0x0+i,  9,  0); // A Input Offset value

        createChannelStatusParam("A%uAdcOff",    i+1, 0x1+i,  8,  8); // Ai ADC Offset value
        createChannelStatusParam("B%uAdcOff",    i+1, 0x1+i,  8,  0); // Bi ADC Offset value

        createChannelStatusParam("B%uAutoAdjEn", i+1, 0x2+i,  1, 15); // Bi Auto-Adjust Active         (0=not active,1=active)
        createChannelStatusParam("B%uAdjSample", i+1, 0x2+i,  1, 14); // Bi Auto-Adjust Got sample     (0=no sample,1=got sample)
        createChannelStatusParam("B%uAdjSamLim", i+1, 0x2+i,  1, 13); // Bi Auto-Adjust Sample limit   (0=not active,1=active)
        createChannelStatusParam("B%uAdjSloLim", i+1, 0x2+i,  1, 12); // Bi Auto-Adjust Slope limit    (0=not active,1=active)
        createChannelStatusParam("B%uAdjOffLim", i+1, 0x2+i,  1, 11); // Bi Auto-Adjust Offset limit   (0=not active,1=active)
        createChannelStatusParam("B%uAdjSlXLim", i+1, 0x2+i,  1, 10); // Bi Auto-Adjust Slope? limit   (0=not active,1=active)
        createChannelStatusParam("B%uAdjOverfl", i+1, 0x2+i,  1,  9); // Bi Auto-Adjust overflow       (0=no,1=yes)
        createChannelStatusParam("B%uInOffset",  i+1, 0x2+i,  9,  0); // B Input Offset value

        createChannelStatusParam("Ch%uMulDiscE", i+1, 0x3+i,  1, 15); // A multi-disc event occured    (0=no,1=yes)
        createChannelStatusParam("Ch%uEvent",    i+1, 0x3+i,  1, 14); // An event occured              (0=no,1=yes)
        createChannelStatusParam("Ch%uUnknwCmd", i+1, 0x3+i,  1, 13); // Unrecognized command          (0=no error,1=error)
        createChannelStatusParam("Ch%uPktLenEr", i+1, 0x3+i,  1, 12); // Packet length error           (0=no error,1=error)
        createChannelStatusParam("Ch%uLogicErr", i+1, 0x3+i,  1, 11); // WRITE_CNFG during ACQUISITION (0=no error,1=error)
        createChannelStatusParam("Ch%uMaxAdc",   i+1, 0x3+i,  1, 10); // A max ADC condition occurred  (0=no,1=yes)
        createChannelStatusParam("Ch%uMinAdc",   i+1, 0x3+i,  1,  9); // A min ADC condition occurred  (0=no,1=yes)
        createChannelStatusParam("Ch%uFifoFull", i+1, 0x3+i,  1,  8); // Event FIFO almost full        (0=no,1=yes)
        createChannelStatusParam("Ch%uAdcFifFu", i+1, 0x3+i,  1,  7); // ADC FIFO went full            (0=no,1=yes)
        createChannelStatusParam("Ch%uAdcFifAF", i+1, 0x3+i,  1,  6); // ADC FIFO almost full          (0=no,1=yes)
        createChannelStatusParam("Ch%uAdcFifFF", i+1, 0x3+i,  1,  5); // ADC FIFO almost full flag     (0=no,1=yes)
        createChannelStatusParam("Ch%uAdcFifEm", i+1, 0x3+i,  1,  4); // ADC FIFO empty                (0=no,1=yes)
        createChannelStatusParam("Ch%uOutputEm", i+1, 0x3+i,  1,  3); // Output FIFO almost full       (0=no,1=yes)
        createChannelStatusParam("Ch%uAcqStat",  i+1, 0x3+i,  1,  2); // Acquiring data                (0=no,1=yes)
        createChannelStatusParam("Ch%uEnabled",  i+1, 0x3+i,  1,  1); // Channel enabled               (0=no,1=yes)
        createChannelStatusParam("Ch%uConfigrd", i+1, 0x3+i,  1,  0); // Configured                    (0=no,1=yes)
    }
}

void RocPlugin::createStatusParams_V2_45()
{
//    BLXXX:Det:RocXXX:| sig nam |                       | EPICS record description  | (bi and mbbi description)
    createStatusParam("LogicErr",       0x0,  1, 15); // WRITE_CNFG during ACQUISITION (0=no error,1=error)
    createStatusParam("CmdLenErr",      0x0,  1, 14); // Command length error          (0=no error,1=error)
    createStatusParam("UnknownCmd",     0x0,  1, 13); // Unrecognized command error    (0=no error,1=error)
    createStatusParam("IntFifoFull",    0x0,  1, 12); // Internal Data FIFO Almost ful (0=not full,1=full)
    createStatusParam("IntFifoEmp",     0x0,  1, 11); // Internal Data FIFO Empty flag (0=not empty,1=empty)
    createStatusParam("CalcBadFin",     0x0,  1, 10); // Calc: Bad Final Calculation.  (0=no error,1=error)
    createStatusParam("CalcBadEff",     0x0,  1,  9); // Calc: Bad Effective Calculati (0=no error,1=error)
    createStatusParam("CalcBadOver",    0x0,  1,  8); // Calc: Data overflow detected. (0=no error,1=error)
    createStatusParam("CalcBadCnt",     0x0,  1,  7); // Calc: Bad word count.         (0=no error,1=error)
    createStatusParam("LvdsFifoFul",    0x0,  1,  6); // LVDS FIFO went full.          (0=not full,1=full)
    createStatusParam("LvdsStartEr",    0x0,  1,  5); // LVDS start before stop bit    (0=no error,1=error)
    createStatusParam("LvdsNoStart",    0x0,  1,  4); // LVDS data without start.      (0=no error,1=error)
    createStatusParam("LvdsTimeout",    0x0,  1,  3); // LVDS packet timeout.          (0=no timeout,1=timeout)
    createStatusParam("LvdsLenErr",     0x0,  1,  2); // LVDS packet length error.     (0=no error,1=error)
    createStatusParam("LvdsTypeErr",    0x0,  1,  1); // LVDS data type error.         (0=no error,1=error)
    createStatusParam("LvdsParErr",     0x0,  1,  0); // LVDS parity error.            (0=no error,1=error)

    createStatusParam("LvdsPwrDown",    0x1,  1,  9); // Got LVDS powerdown sequence   (0=no,1=yes)
    createStatusParam("RcvdSysRst",     0x1,  1,  8); // Got system reset              (0=no,1=yes)
    createStatusParam("RcvdTimeErr",    0x1,  1,  1); // Got timer error               (0=no error,1=error)
    createStatusParam("RcvdFatalEr",    0x1,  1,  0); // Got fatal error               (0=no error,1=error)

    createStatusParam("DataRdyChan",    0x2,  8,  8); // Data ready channel
    createStatusParam("ChanProg",       0x2,  8,  0); // Channel programmed

    createStatusParam("CalcFifoFul",    0x3,  1, 14); // Calc: FIFO almost full        (0=not full,1=almost full)
    createStatusParam("CalcFifoDat",    0x3,  1, 13); // Calc: FIFO has data           (0=no,1=yes)
    createStatusParam("CalcInFifF",     0x3,  1, 12); // Calc: Input FIFO almost full  (0=not full,1=almost full)
    createStatusParam("CalcInFifD",     0x3,  1, 11); // Calc: Input FIFO almost full  (0=not full,1=almost full)
    createStatusParam("HVStatus",       0x3,  1, 10); // High Voltage Status bit       (0=no error,1=error)
    createStatusParam("TsyncGood",      0x3,  1,  9); // Good TSYNC                    (0=no,1=yes)
    createStatusParam("TsyncInRang",    0x3,  1,  8); // TSYNC in specification range  (0=no,1=yes)
    createStatusParam("TsyncHigh",      0x3,  1,  7); // TSYNC Got HIGH                (0=no,1=yes)
    createStatusParam("TsyncLow",       0x3,  1,  6); // TSYNC Got LOW                 (0=no,1=yes)
    createStatusParam("TclkGood",       0x3,  1,  5); // Good TCLK                     (0=no,1=yes)
    createStatusParam("TclkInRange",    0x3,  1,  4); // TCLK in specification range   (0=no,1=yes)
    createStatusParam("TclkRcvd",       0x3,  1,  3); // Got TCLK                      (0=no,1=yes)
    createStatusParam("CalcActive",     0x3,  1,  2); // Calculation: Active           (0=not active,1=active)
    createStatusParam("AcquireStat",    0x3,  1,  1); // Acquiring data                (0=not acquiring,1=acquiring)
    createStatusParam("Discovered",     0x3,  1,  0); // Discovered.                   (0=not discovered,1=discovered)

    for (unsigned i=0; i<NUM_CHANNELS; i++) {
        createChannelStatusParam("B%uAdcCorrV",  i+1, 0x1+i,  8,  8); // Bi ADC correction value
        createChannelStatusParam("A%uAdcCorrV",  i+1, 0x1+i,  8,  0); // Ai ADC correction value

        createChannelStatusParam("A%uOvrflRst",  i+1, 0x1+i,  1, 15); // Ai Auto-Adjust overflow reset (0=not active,1=active)
        createChannelStatusParam("A%uAdjOffLim", i+1, 0x1+i,  1, 14); // Ai Auto-Adjust Offset limit   (0=not active,1=active)
        createChannelStatusParam("A%uAdjSlXLim", i+1, 0x1+i,  1, 13); // Ai Auto-Adjust Slope? limit   (0=not active,1=active)
        createChannelStatusParam("A%uAdjOverfl", i+1, 0x1+i,  1, 12); // Ai Auto-Adjust overflow       (0=no,1=yes)
        createChannelStatusParam("A%uInOffset",  i+1, 0x1+i,  9,  0); // Ai Input Offset value

        createChannelStatusParam("B%uOvrflRst",  i+1, 0x2+i,  1, 15); // Bi Auto-Adjust overflow reset (0=not active,1=active)
        createChannelStatusParam("B%uAdjOffLim", i+1, 0x2+i,  1, 14); // Bi Auto-Adjust Offset limit   (0=not active,1=active)
        createChannelStatusParam("B%uAdjSlXLim", i+1, 0x2+i,  1, 13); // Bi Auto-Adjust Slope? limit   (0=not active,1=active)
        createChannelStatusParam("B%uAdjOverfl", i+1, 0x2+i,  1, 12); // Bi Auto-Adjust overflow       (0=no,1=yes)
        createChannelStatusParam("B%uInOffset",  i+1, 0x2+i,  9,  0); // Bi Input Offset value

        createChannelStatusParam("Ch%uAdcFifAF", i+1, 0x3+i,  1, 15); // ADC FIFO almost full          (0=no,1=yes)
        createChannelStatusParam("Ch%uTimeOvrf", i+1, 0x3+i,  1, 14); // Timestamp overflow occurred   (0=no,1=yes)
        createChannelStatusParam("Ch%uUnknwCmd", i+1, 0x3+i,  1, 13); // Unrecognized command          (0=no error,1=error)
        createChannelStatusParam("Ch%uPktLenEr", i+1, 0x3+i,  1, 12); // Packet length error           (0=no error,1=error)
        createChannelStatusParam("Ch%uLogicErr", i+1, 0x3+i,  1, 11); // WRITE_CNFG during ACQUISITION (0=no error,1=error)
        createChannelStatusParam("Ch%uPosDiscr", i+1, 0x3+i,  1, 10); // Got positive init discriminat (0=no,1=yes)
        createChannelStatusParam("Ch%uNegDiscr", i+1, 0x3+i,  1,  9); // Got negative init discriminat (0=no,1=yes)
        createChannelStatusParam("Ch%uOutputEm", i+1, 0x3+i,  1,  8); // Output FIFO almost full       (0=no,1=yes)
        createChannelStatusParam("Ch%uMinAdc",   i+1, 0x3+i,  1,  7); // A min ADC condition occurred  (0=no,1=yes)
        createChannelStatusParam("Ch%uMaxAdc",   i+1, 0x3+i,  1,  6); // A max ADC condition occurred  (0=no,1=yes)
        createChannelStatusParam("Ch%uMulDiscE", i+1, 0x3+i,  1,  5); // A multi-disc event occured    (0=no,1=yes)
        createChannelStatusParam("Ch%uAdcFifFu", i+1, 0x3+i,  1,  4); // ADC FIFO went full            (0=no,1=yes)
        createChannelStatusParam("Ch%uOutEmpty", i+1, 0x3+i,  1,  3); // Output emptry flag            (0=no,1=yes)
        createChannelStatusParam("Ch%uAcqStat",  i+1, 0x3+i,  1,  2); // Acquiring data                (0=no,1=yes)
        createChannelStatusParam("Ch%uEnabled",  i+1, 0x3+i,  1,  1); // Channel enabled               (0=no,1=yes)
        createChannelStatusParam("Ch%uConfigrd", i+1, 0x3+i,  1,  0); // Configured                    (0=no,1=yes)
    }
}

void RocPlugin::createStatusParams_V2_41()
{
//    BLXXX:Det:RocXXX:| sig nam |                       | EPICS record description  | (bi and mbbi description)
    createStatusParam("DataRdyChan",    0x0,  8,  8); // Data ready channel
    createStatusParam("ChanProg",       0x0,  8,  0); // Channel programmed

    createStatusParam("CalcError",      0x1,  1, 15); // Calculation error             (0=no error,1=error)
    createStatusParam("CalcBadPkt",     0x1,  1, 14); // Calculation bad packet        (0=no error,1=error)
    createStatusParam("LvdsLenErr",     0x1,  1, 13); // LVDS packet length error      (0=no error,1=error)
    createStatusParam("HVStatus",       0x1,  1, 12); // High Voltage Status bit       (0=no error,1=error)
    createStatusParam("LogicErr",       0x1,  1, 11); // WRITE_CNFG during ACQUISITION (0=no error,1=error)
    createStatusParam("LvdsTimeout",    0x1,  1, 10); // LVDS packet timeout.          (0=no timeout,1=timeout)
    createStatusParam("LvdsTypeErr",    0x1,  1,  9); // LVDS data type error.         (0=no error,1=error)
    createStatusParam("UnknownCmd",     0x1,  1,  8); // Unrecognized command error    (0=no error,1=error)
    createStatusParam("LvdsParErr",     0x1,  1,  7); // LVDS parity error.            (0=no error,1=error)
    createStatusParam("LvdsTimeout",    0x1,  1,  6); // LVDS packet timeout           (0=no timeout,1=timeout)
    createStatusParam("Busy",           0x1,  1,  5); // Busy bit is set               (0=no,1=yes)
    createStatusParam("RcvdTimeErr",    0x1,  1,  4); // Got timer error               (0=no error,1=error)
    createStatusParam("LvdsStartEr",    0x0,  1,  3); // LVDS packet start issue       (0=no error,1=error)
    createStatusParam("AcquireStat",    0x3,  1,  2); // Acquiring data                (0=not acquiring,1=acquiring)
    createStatusParam("Discovered",     0x3,  1,  1); // Discovered.                   (0=not discovered,1=discovered)
    createStatusParam("HwIdValid",      0x3,  1,  0); // Hardware ID valid             (0=no,1=yes)

    for (unsigned i=0; i<NUM_CHANNELS; i++) {
        createChannelStatusParam("Ch%uAdcFifAF", i+1, 0x3+i,  1, 15); // ADC FIFO almost full          (0=no,1=yes)
        createChannelStatusParam("Ch%uTimeOvrf", i+1, 0x3+i,  1, 14); // Timestamp overflow occurred   (0=no,1=yes)
        createChannelStatusParam("Ch%uUnknwCmd", i+1, 0x3+i,  1, 13); // Unrecognized command          (0=no error,1=error)
        createChannelStatusParam("Ch%uPktLenEr", i+1, 0x3+i,  1, 12); // Packet length error           (0=no error,1=error)
        createChannelStatusParam("Ch%uLogicErr", i+1, 0x3+i,  1, 11); // WRITE_CNFG during ACQUISITION (0=no error,1=error)
        createChannelStatusParam("Ch%uPosDiscr", i+1, 0x3+i,  1, 10); // Got positive init discriminat (0=no,1=yes)
        createChannelStatusParam("Ch%uNegDiscr", i+1, 0x3+i,  1,  9); // Got negative init discriminat (0=no,1=yes)
        createChannelStatusParam("Ch%uOutputEm", i+1, 0x3+i,  1,  8); // Output FIFO almost full       (0=no,1=yes)
        createChannelStatusParam("Ch%uMinAdc",   i+1, 0x3+i,  1,  7); // A min ADC condition occurred  (0=no,1=yes)
        createChannelStatusParam("Ch%uMaxAdc",   i+1, 0x3+i,  1,  6); // A max ADC condition occurred  (0=no,1=yes)
        createChannelStatusParam("Ch%uMulDiscE", i+1, 0x3+i,  1,  5); // A multi-disc event occured    (0=no,1=yes)
        createChannelStatusParam("Ch%uAdcFifFu", i+1, 0x3+i,  1,  4); // ADC FIFO went full            (0=no,1=yes)
        createChannelStatusParam("Ch%uOutEmpty", i+1, 0x3+i,  1,  3); // Output emptry flag            (0=no,1=yes)
        createChannelStatusParam("Ch%uAcqStat",  i+1, 0x3+i,  1,  2); // Acquiring data                (0=no,1=yes)
        createChannelStatusParam("Ch%uEnabled",  i+1, 0x3+i,  1,  1); // Channel enabled               (0=no,1=yes)
        createChannelStatusParam("Ch%uConfigrd", i+1, 0x3+i,  1,  0); // Configured                    (0=no,1=yes)
    }
}

void RocPlugin::createChannelStatusParam(const char *fmt, unsigned channel, uint32_t offset, uint32_t nBits, uint32_t shift)
{
    char name[20];
    snprintf(name, sizeof(name), fmt, channel);
    createStatusParam(name, offset, nBits, shift);
}
*/
