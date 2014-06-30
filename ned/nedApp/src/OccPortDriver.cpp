#include "OccPortDriver.h"
#include "DmaCircularBuffer.h"
#include "DmaCopier.h"
#include "EpicsRegister.h"
#include "BasePlugin.h"
#include "Log.h"

#include <cstring> // strerror
#include <cstddef>
#include <errno.h>
#include <occlib.h>

#include "DspPlugin.h"

static const int asynMaxAddr       = 1;
static const int asynInterfaceMask = asynInt32Mask | asynOctetMask | asynGenericPointerMask | asynDrvUserMask | asynFloat64Mask; // don't remove DrvUserMask or you'll break callback's reasons
static const int asynInterruptMask = asynInt32Mask | asynOctetMask | asynGenericPointerMask;
static const int asynFlags         = 0;
static const int asynAutoConnect   = 1;
static const int asynPriority      = 0;
static const int asynStackSize     = 0;

#define NUM_OCCPORTDRIVER_PARAMS ((int)(&LAST_OCCPORTDRIVER_PARAM - &FIRST_OCCPORTDRIVER_PARAM + 1))

EPICS_REGISTER(ned, OccPortDriver, 3, "Port name", string, "Device id", int, "Local buffer size", int);

extern "C" {
    static void processOccDataC(void *drvPvt)
    {
        OccPortDriver *driver = reinterpret_cast<OccPortDriver *>(drvPvt);
        driver->processOccData();
    }
}

OccPortDriver::OccPortDriver(const char *portName, int deviceId, uint32_t localBufferSize)
	: asynPortDriver(portName, asynMaxAddr, NUM_OCCPORTDRIVER_PARAMS, asynInterfaceMask,
	                 asynInterruptMask, asynFlags, asynAutoConnect, asynPriority, asynStackSize)
    , m_occ(NULL)
    , m_occBufferReadThreadId(0)
{
    int status;

    // Register params with asyn
    createParam("Status",           asynParamInt32,     &Status);
    createParam("LastErr",          asynParamInt32,     &LastErr);
    createParam("BoardType",        asynParamInt32,     &BoardType);
    createParam("BoardFwVer",       asynParamInt32,     &BoardFwVer);
    createParam("OpticsPresent",    asynParamInt32,     &OpticsPresent);
    createParam("OpticsEnabled",    asynParamInt32,     &OpticsEnabled);
    createParam("RxStalled",        asynParamInt32,     &RxStalled);
    createParam("ErrPktsEnabled",   asynParamInt32,     &ErrPktsEnabled);
    createParam("Command",          asynParamInt32,     &Command);
    createParam("FpgaTemp",         asynParamFloat64,   &FpgaTemp);
    createParam("FpgaCoreVolt",     asynParamFloat64,   &FpgaCoreVolt);
    createParam("FpgaAuxVolt",      asynParamFloat64,   &FpgaAuxVolt);
    createParam("ErrCrc",           asynParamInt32,     &ErrCrc);
    createParam("ErrLength",        asynParamInt32,     &ErrLength);
    createParam("ErrFrame",         asynParamInt32,     &ErrFrame);
    createParam("SfpTemp",          asynParamFloat64,   &SfpTemp);
    createParam("SfpRxPower",       asynParamFloat64,   &SfpRxPower);
    createParam("SfpTxPower",       asynParamFloat64,   &SfpTxPower);
    createParam("SfpVccPower",      asynParamFloat64,   &SfpVccPower);
    createParam("SfpTxBiasCur",     asynParamFloat64,   &SfpTxBiasCur);
    createParam("StatusInterval",   asynParamFloat64,   &StatusInterval);
    createParam("DmaBufUtil",       asynParamInt32,     &DmaBufUtil);
    createParam("CopyBufUtil",      asynParamInt32,     &CopyBufUtil);

    setIntegerParam(Status, STAT_OK);

    // Initialize OCC board
    status = occ_open(portName, OCC_INTERFACE_OPTICAL, &m_occ);
    setIntegerParam(LastErr, -status);
    if (status != 0) {
        setIntegerParam(Status, STAT_OCC_ERROR);
        asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR, "Unable to open OCC device - %s(%d)\n", strerror(-status), status);
        m_occ = NULL;
        return;
    }

    // Start DMA copy thread or use DMA buffer directly
    if (localBufferSize > 0)
        m_circularBuffer = new DmaCopier(m_occ, localBufferSize);
    else
        m_circularBuffer = new DmaCircularBuffer(m_occ);
    if (!m_circularBuffer) {
        asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR, "Unable to create circular buffer handler\n");
        return;
    }

    // Get OCC status
    m_lastStatusUpdateTime.secPastEpoch = 0;
    m_lastStatusUpdateTime.nsec = 0;
    setDoubleParam(StatusInterval, 1.0);
    refreshOccStatus();

    m_occBufferReadThreadId = epicsThreadCreate(portName,
                                                epicsThreadPriorityHigh,
                                                epicsThreadGetStackSize(epicsThreadStackMedium),
                                                (EPICSTHREADFUNC)processOccDataC,
                                                this);

    callParamCallbacks();
}

OccPortDriver::~OccPortDriver()
{
    // Kick threads out of waiting for OCC
    occ_reset(m_occ);

    delete m_circularBuffer;

    if (m_occ) {
        int status = occ_close(m_occ);
        if (status != 0) {
            asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR, "Unable to close OCC device - %s(%d)\n", strerror(-status), status);
        }
    }
}


bool OccPortDriver::refreshOccStatus()
{
    epicsTimeStamp now;
    epicsTimeGetCurrent(&now);
    double refreshPeriod;

    getDoubleParam(StatusInterval, &refreshPeriod);
    double diff = epicsTimeDiffInSeconds(&now, &m_lastStatusUpdateTime);
    if (epicsTimeDiffInSeconds(&now, &m_lastStatusUpdateTime) > refreshPeriod) {
        int ret, val;
        occ_status_t occstatus;
        ret = occ_status(m_occ, &occstatus);

        if (ret != 0) {
            setIntegerParam(LastErr, -ret);
            LOG_ERROR("Failed to query OCC status: %s(%d)", strerror(-ret), ret);
            return false;
        }

        setIntegerParam(BoardType,      occstatus.board);
        setIntegerParam(BoardFwVer,     occstatus.firmware_ver);
        setIntegerParam(OpticsPresent,  occstatus.optical_signal);
        setIntegerParam(OpticsEnabled,  occstatus.rx_enabled);
        setDoubleParam(FpgaTemp,        occstatus.fpga_temp);
        setDoubleParam(FpgaCoreVolt,    occstatus.fpga_core_volt);
        setDoubleParam(FpgaAuxVolt,     occstatus.fpga_aux_volt);
        setIntegerParam(ErrCrc,         occstatus.err_crc);
        setIntegerParam(ErrLength,      occstatus.err_length);
        setIntegerParam(ErrFrame,       occstatus.err_frame);
        setDoubleParam(SfpTemp,         occstatus.sfp_temp);
        setDoubleParam(SfpRxPower,      occstatus.sfp_rx_power);
        setDoubleParam(SfpTxPower,      occstatus.sfp_tx_power);
        setDoubleParam(SfpVccPower,     occstatus.sfp_vcc_power);
        setDoubleParam(SfpTxBiasCur,    occstatus.sfp_tx_bias_cur);

        setIntegerParam(DmaBufUtil,     100 * occstatus.dma_used / occstatus.dma_size);
        setIntegerParam(CopyBufUtil,    m_circularBuffer->utilization());

        getIntegerParam(RxStalled,      &val);
        setIntegerParam(RxStalled,      (occstatus.stalled ? val | 1 : val & ~1));

        callParamCallbacks();

        epicsTimeGetCurrent(&m_lastStatusUpdateTime);
    }
    return true;
}

asynStatus OccPortDriver::readFloat64(asynUser *pasynUser, epicsFloat64 *value)
{
    if (!refreshOccStatus())
        setIntegerParam(Status, STAT_OCC_ERROR);
    return asynPortDriver::readFloat64(pasynUser, value);
}

/**
 * Handler for reading asynInt32 values.
 *
 * Some of the values need to be pulled directly from registers. Doing so will also update
 * whatever other PVs we got info for.
 */
asynStatus OccPortDriver::readInt32(asynUser *pasynUser, epicsInt32 *value)
{
    if (!refreshOccStatus())
        setIntegerParam(Status, STAT_OCC_ERROR);
    return asynPortDriver::readInt32(pasynUser, value);
}

asynStatus OccPortDriver::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
    int ret;
    if (pasynUser->reason == Command) {
        switch (value) {
        case CMD_OPTICS_ENABLE:
        case CMD_OPTICS_DISABLE:
            ret = occ_enable_rx(m_occ, value == CMD_OPTICS_ENABLE);
            if (ret != 0) {
                LOG_ERROR("Unable to %s optical link - %s(%d)", (value == CMD_OPTICS_ENABLE ? "enable" : "disable"), strerror(-ret), ret);
                return asynError;
            }
            setIntegerParam(OpticsEnabled, value == CMD_OPTICS_ENABLE ? 1 : 0);
            callParamCallbacks();
            return asynSuccess;
        case CMD_ERROR_PACKETS_ENABLE:
        case CMD_ERROR_PACKETS_DISABLE:
            ret = occ_enable_error_packets(m_occ, value == CMD_ERROR_PACKETS_ENABLE);
            if (ret != 0) {
                LOG_ERROR("Unable to %s error packets - %s(%d)", (value == CMD_ERROR_PACKETS_ENABLE ? "enable" : "disable"), strerror(-ret), ret);
                return asynError;
            }
            setIntegerParam(ErrPktsEnabled, value == CMD_OPTICS_ENABLE ? 1 : 0);
            callParamCallbacks();
            return asynSuccess;
        default:
            LOG_ERROR("Unrecognized command '%d'", value);
            return asynError;
        }
    }
    return asynPortDriver::writeInt32(pasynUser, value);
}

asynStatus OccPortDriver::writeGenericPointer(asynUser *pasynUser, void *pointer)
{
    if (pasynUser->reason == REASON_OCCDATA) {
        DasPacketList *packets = reinterpret_cast<DasPacketList *>(pointer);
        const DasPacket *packet = packets->first();

        int ret = occ_send(m_occ, reinterpret_cast<const void *>(packet), packet->length());
        if (ret != 0) {
            setIntegerParam(LastErr, -ret);
            setIntegerParam(Status, STAT_OCC_ERROR);
            callParamCallbacks();
            LOG_ERROR("Unable to send data to OCC - %s(%d)\n", strerror(-ret), ret);
            return asynError;
        }

    }
    return asynSuccess;
}

void OccPortDriver::processOccData()
{
    void *data;
    uint32_t length;
    uint32_t consumed;
    bool resetErrorRatelimit = false;
    DasPacketList packetsList;

    while (true) {
        int ret = m_circularBuffer->wait(&data, &length);
        if (ret != 0) {
            setIntegerParam(LastErr, -ret);
            setIntegerParam(Status, ret == -EOVERFLOW ? STAT_BUFFER_FULL : STAT_OCC_ERROR);
            if (ret == -EOVERFLOW) {
                int val;
                getIntegerParam(RxStalled, &val);
                setIntegerParam(RxStalled, val | 2);
            }
            callParamCallbacks();
            LOG_ERROR("Unable to receive data from OCC, stopped - %s(%d)\n", strerror(-ret), ret);
            break;
        }

        if (!packetsList.reset(reinterpret_cast<uint8_t*>(data), length)) {
            // This should not happen. If it does it's certainly a code error that needs to be fixed.
            if (!resetErrorRatelimit) {
                LOG_ERROR("PluginDriver:%s ERROR failed to reset DasPacketList\n", __func__);
                resetErrorRatelimit = true;
            }
            continue;
        }
        resetErrorRatelimit = false;

        sendToPlugins(REASON_OCCDATA, &packetsList);

        consumed = 0;
        // Plugins have been notified, hopefully they're all non-blocking.
        // While waiting, calculate how much data can be consumed from circular buffer.
        for (const DasPacket *packet = packetsList.first(); packet != 0; packet = packetsList.next(packet)) {
#ifdef DWORD_PADDING_WORKAROUND
            consumed += packet->getAlignedLength();
#else
            consumed += packet->length();
#endif
        }

        packetsList.release(); // reset() set it to 1
        packetsList.waitAllReleased();

        m_circularBuffer->consume(consumed);
    }
}

void OccPortDriver::sendToPlugins(int messageType, const DasPacketList *packetList)
{
    const void *addr = reinterpret_cast<const void *>(packetList);
    doCallbacksGenericPointer(const_cast<void *>(addr), messageType, 0);
}
