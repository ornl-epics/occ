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
static const int asynInterfaceMask = asynInt32Mask | asynOctetMask | asynGenericPointerMask | asynDrvUserMask; // don't remove DrvUserMask or you'll break callback's reasons
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
    occ_status_t occstatus;

    // Register params with asyn
    createParam("Status",           asynParamInt32,     &Status);
    createParam("BoardStatus",      asynParamInt32,     &BoardStatus);
    createParam("BoardType",        asynParamInt32,     &BoardType);
    createParam("BoardFwVer",       asynParamInt32,     &BoardFwVer);
    createParam("OpticsPresent",    asynParamInt32,     &OpticsPresent);
    createParam("OpticsEnabled",    asynParamInt32,     &OpticsEnabled);
    createParam("Command",          asynParamInt32,     &Command);

    setIntegerParam(Status, STAT_OK);

    // Initialize OCC board
    status = occ_open(portName, OCC_INTERFACE_OPTICAL, &m_occ);
    setIntegerParam(BoardStatus, -status);
    if (status != 0) {
        setIntegerParam(Status, STAT_OCC_ERROR);
        asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR, "Unable to open OCC device - %s(%d)\n", strerror(-status), status);
        m_occ = NULL;
        return;
    }

    // Query OCC board status and populate PVs
    status = occ_status(m_occ, &occstatus);
    if (status != 0) {
        asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR, "Unable to query OCC device status - %s(%d)\n", strerror(-status), status);
        setIntegerParam(BoardStatus, -ENODATA);
    }
    status =  setIntegerParam(BoardType,     (int)occstatus.board);
    status |= setIntegerParam(BoardFwVer,    (int)occstatus.firmware_ver);
    status |= setIntegerParam(OpticsEnabled, (int)occstatus.rx_enabled);
    status |= setIntegerParam(OpticsPresent, (int)occstatus.optical_signal);
    if (status) {
        asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR, "Unable to set device parameters\n");
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

/**
 * Handler for reading asynInt32 values.
 *
 * Some of the values need to be pulled directly from registers. Doing so will also update
 * whatever other PVs we got info for.
 */
asynStatus OccPortDriver::readInt32(asynUser *pasynUser, epicsInt32 *value)
{
    return asynPortDriver::readInt32(pasynUser, value);
}

asynStatus OccPortDriver::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
    int ret;
    if (pasynUser->reason == Command) {
        switch (value) {
        case CMD_OPTICS_ENABLE:
            ret = occ_enable_rx(m_occ, value != 0);
            if (ret != 0) {
                LOG_ERROR("Unable to enable optical link - %s(%d)", strerror(-ret), ret);
                return asynError;
            }
            setIntegerParam(OpticsEnabled, value == 0 ? 0 : 1);
            callParamCallbacks();
            return asynSuccess;
        default:
            LOG_ERROR("Unrecognized command '%d'", value);
            return asynError;
        }
    } else if (pasynUser->reason == OpticsPresent) {
        asynStatus status;
        occ_status_t occstatus;
        ret = occ_status(m_occ, &occstatus);
        if (ret == 0) {
            setIntegerParam(OpticsEnabled, (int)occstatus.rx_enabled);
            setIntegerParam(OpticsPresent, (int)occstatus.optical_signal);
            status = asynSuccess;
        } else {
            setIntegerParam(BoardStatus, -ret);
            setIntegerParam(Status, STAT_OCC_ERROR);
            LOG_ERROR("Unable to query OCC device status - %s(%d)\n", strerror(-ret), ret);
            status = asynError;
        }
        callParamCallbacks();
        return status;
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
            setIntegerParam(BoardStatus, -ret);
            setIntegerParam(Status, STAT_OCC_ERROR);
            callParamCallbacks();
            asynPrint(pasynUser, ASYN_TRACE_ERROR, "Unable to send data to OCC - %s(%d)\n", strerror(-ret), ret);
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
            setIntegerParam(BoardStatus, ret);
            setIntegerParam(Status, ret == -EOVERFLOW ? STAT_BUFFER_FULL : STAT_OCC_ERROR);
            callParamCallbacks();
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "Unable to receive data from OCC, stopped - %s(%d)\n", strerror(-ret), ret);
            break;
        }

        if (!packetsList.reset(reinterpret_cast<uint8_t*>(data), length)) {
            // This should not happen. If it does it's certainly a code error that needs to be fixed.
            if (!resetErrorRatelimit) {
                asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "PluginDriver:%s ERROR failed to reset DasPacketList\n", __func__);
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
            consumed += packet->length();
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
