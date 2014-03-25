#include "OccPortDriver.h"
#include "DmaCircularBuffer.h"
#include "DmaCopier.h"

#include "OccDispatcher.h"
#include "AdaraPlugin.h" // for testing only

#include <cstring> // strerror
#include <cstddef>
#include <errno.h>
#include <occlib.h>

static const int asynMaxAddr       = 1;
static const int asynInterfaceMask = asynInt32Mask | asynOctetMask | asynDrvUserMask; // don't remove DrvUserMask or you'll break callback's reasons
static const int asynInterruptMask = asynInt32Mask | asynOctetMask;
static const int asynFlags         = 0;
static const int asynAutoConnect   = 1;
static const int asynPriority      = 0;
static const int asynStackSize     = 0;

#define NUM_OCCPORTDRIVER_PARAMS ((int)(&LAST_OCCPORTDRIVER_PARAM - &FIRST_OCCPORTDRIVER_PARAM + 1))

OccPortDriver::OccPortDriver(const char *portName, int deviceId, uint32_t localBufferSize)
	: asynPortDriver(portName, asynMaxAddr, NUM_OCCPORTDRIVER_PARAMS, asynInterfaceMask,
	                 asynInterruptMask, asynFlags, asynAutoConnect, asynPriority, asynStackSize)
    , m_occ(NULL)
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

    // Create dispatcher thread
    OccDispatcher *d = new OccDispatcher("OCCdispatcher", m_circularBuffer);

    // For testing only - create a plugin and send some data to it
    BasePlugin *p = new AdaraPlugin("testPort1", "OCCdispatcher");
    if (localBufferSize)
        dynamic_cast<DmaCopier*>(m_circularBuffer)->push((void *)"testing1", 8);
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
