#include "OccPortDriver.h"
#include "DmaCircularBuffer.h"
#include "DmaCopier.h"
#include "EpicsRegister.h"
#include "BasePlugin.h"

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
    createParam("DEVICE_STATUS",        asynParamInt32,     &DeviceStatus);
    createParam("BOARD_TYPE",           asynParamInt32,     &BoardType);
    createParam("BOARD_FIRMWARE_VER",   asynParamInt32,     &BoardFirmwareVersion);
    createParam("OPTICS_PRESENT",       asynParamInt32,     &OpticsPresent);
    createParam("RX_ENABLED",           asynParamInt32,     &RxEnabled);

    // Initialize OCC board
    status = occ_open(portName, OCC_INTERFACE_OPTICAL, &m_occ);
    setIntegerParam(DeviceStatus, -status);
    if (status != 0) {
        asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR, "Unable to open OCC device - %s(%d)\n", strerror(-status), status);
        m_occ = NULL;
    }

    // Query OCC board status and populate PVs
    status = occ_status(m_occ, &occstatus);
    if (status != 0) {
        asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR, "Unable to query OCC device status - %s(%d)\n", strerror(-status), status);
        setIntegerParam(DeviceStatus, -ENODATA);
    }
    status =  setIntegerParam(BoardType, (int)occstatus.board);
    status |= setIntegerParam(BoardFirmwareVersion, (int)occstatus.firmware_ver);
    status |= setIntegerParam(RxEnabled, (int)occstatus.rx_enabled);
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

    DspPlugin *dsp = new DspPlugin("DspPlugin", "/dev/snsocb0", 0x15FACB2D);
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
    if (pasynUser->reason == OpticsPresent) {
        occ_status_t occstatus;
        int status = occ_status(m_occ, &occstatus);
        if (status != 0) {
            asynPrint(pasynUser, ASYN_TRACE_ERROR, "Unable to query OCC device status - %s(%d)\n", strerror(-status), status);
            return asynError;
        }
        setIntegerParam(RxEnabled, (int)occstatus.rx_enabled);
        setIntegerParam(OpticsPresent, (int)occstatus.optical_signal);
        callParamCallbacks();
    }
    return asynPortDriver::readInt32(pasynUser, value);
}

asynStatus OccPortDriver::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
    if (pasynUser->reason == RxEnabled) {
        int ret = occ_enable_rx(m_occ, value != 0);
        if (ret != 0) {
            asynPrint(pasynUser, ASYN_TRACE_ERROR, "Unable to enable RX - %s(%d)\n", strerror(-ret), ret);
            return asynError;
        }
        setIntegerParam(RxEnabled, value == 0 ? 0 : 1);
        callParamCallbacks();
        return asynSuccess;
    }
    return asynPortDriver::writeInt32(pasynUser, value);
}

asynStatus OccPortDriver::writeGenericPointer(asynUser *pasynUser, void *pointer)
{
    if (pasynUser->reason == REASON_OCCDATA) {
        DasPacketList *packets = reinterpret_cast<DasPacketList *>(pointer);
        const DasPacket *packet = packets->first();
        uint32_t len = packet->payload_length;

        int ret = occ_send(m_occ, reinterpret_cast<const void *>(packet), packet->length());
        if (ret != 0) {
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
        m_circularBuffer->wait(&data, &length);

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
