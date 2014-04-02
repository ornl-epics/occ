#include "BasePlugin.h"
#include "DasPacketList.h"

#include <epicsThread.h>
#include <string>

#define NUM_BASEPLUGIN_PARAMS ((int)(&LAST_BASEPLUGIN_PARAM - &FIRST_BASEPLUGIN_PARAM + 1))

#define MESSAGE_QUEUE_SIZE 1   //!< Size of the message queue for callbacks. Since using the synchronous callbacks, 1 should be enough.

/* Helper C functions for asyn/EPICS registration
 */
extern "C" {
    static void processDataThread(void *drvPvt)
    {
        BasePlugin *plugin = reinterpret_cast<BasePlugin *>(drvPvt);
        plugin->processDataThread();
    }
    static void dispatcherCallback(void *drvPvt, asynUser *pasynUser, void *genericPointer)
    {
        BasePlugin *plugin = reinterpret_cast<BasePlugin *>(drvPvt);
        plugin->dispatcherCallback(pasynUser, genericPointer);
    }
}

BasePlugin::BasePlugin(const char *portName, const char *dispatcherPortName, int reason, int blocking,
                       int numParams, int maxAddr, int interfaceMask,
                       int interruptMask, int asynFlags, int autoConnect,
                       int priority, int stackSize)
	: asynPortDriver(portName, maxAddr, NUM_BASEPLUGIN_PARAMS + numParams, interfaceMask | defaultInterfaceMask,
	                 interruptMask | defaultInterruptMask, asynFlags, autoConnect, priority, stackSize)
    , m_asynGenericPointerInterruptPvt(0)
    , m_messageQueue(MESSAGE_QUEUE_SIZE, sizeof(void*))
    , m_portName(portName)
    , m_dispatcherPortName(dispatcherPortName)
    , m_threadId(0)
    , m_shutdown(false)
{

    m_pasynuserDispatcher = pasynManager->createAsynUser(0, 0);
    m_pasynuserDispatcher->userPvt = this;
    m_pasynuserDispatcher->reason = reason;

    createParam("ENABLE_CALLBACKS",         asynParamInt32,     &EnableCallbacks); // Plugin does not receive any data until callbacks are enabled
    createParam("PROCESSED_COUNT",          asynParamInt32,     &ProcessedCount);
    createParam("RECEIVED_COUNT",           asynParamInt32,     &ReceivedCount);

    setIntegerParam(ProcessedCount,         0);
    setIntegerParam(ReceivedCount,          0);

    // Connect to dispatcher port permanently. Don't allow connecting to different port at runtime.
    // Callbacks need to be enabled separately in order to actually get triggered from dispatcher.
    connectToDispatcherPort(dispatcherPortName);

    if (blocking) {
        std::string threadName = m_portName + "_Thread";
        m_threadId = epicsThreadCreate(threadName.c_str(),
                                       epicsThreadPriorityMedium,
                                       epicsThreadGetStackSize(epicsThreadStackMedium),
                                       (EPICSTHREADFUNC)::processDataThread,
                                       this);
        if (m_threadId == (epicsThreadId)0) {
            asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR, "Failed to create data processing thread - %s\n", threadName.c_str());
        }
    }
}

BasePlugin::~BasePlugin()
{
    if (m_threadId != (epicsThreadId)0) {
        // Wake-up processing thread by sending a dummy message. The thread
        // will then exit based on the changed m_shutdown param.
        m_shutdown = true;
        m_messageQueue.send(0, 0);
    }
}

asynStatus BasePlugin::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
    int reason = pasynUser->reason;
    asynStatus status;

    if (reason == EnableCallbacks) {
        this->lock();
        status = setCallbacks(value != 0);
        this->unlock();
    }

    if (status == asynSuccess) {
        status = setIntegerParam(reason, value);
        if (status == asynSuccess)
            status = callParamCallbacks();
    }

    return status;
}

void BasePlugin::dispatcherCallback(asynUser *pasynUser, void *genericPointer)
{
    DasPacketList *packetList = reinterpret_cast<DasPacketList *>(genericPointer);
    int status=0;

    if (packetList == 0)
        return;


    if (m_threadId == (epicsThreadId)0) {
        /* In blocking mode, process the callback in calling thread. Return when
         * processing is complete.
         */
        this->lock();
        processData(packetList);
        this->unlock();
    } else {
        /* Non blocking mode means the callback will be processed in our background
         * thread. Make a reservation so that it doesn't go away.
         */
        packetList->reserve();
        if (!m_messageQueue.trySend(&packetList, sizeof(&packetList))) {
            packetList->release();
            asynPrint(pasynUser, ASYN_TRACE_FLOW, "BasePlugin:%s message queue full\n", __func__);
        }
    }
}

asynStatus BasePlugin::connectToDispatcherPort(const char *portName)
{
    asynStatus status;

    /* Disconnect the port from our asynUser. Ignore error if there is no device
     * currently connected. */
    pasynManager->disconnect(m_pasynuserDispatcher);

    /* Connect to the port driver */
    status = pasynManager->connectDevice(m_pasynuserDispatcher, portName, 0);
    if (status != asynSuccess) {
        asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                  "BasePlugin::%s Error calling pasynManager->connectDevice to port %s address %d, status=%d, error=%s\n",
                  __func__, portName, 0, status, m_pasynuserDispatcher->errorMessage);
    }

    status = setCallbacks(false);

    return(status);
}

void BasePlugin::processDataThread(void)
{
    while (!m_shutdown) {
        DasPacketList *packetList;

        m_messageQueue.receive(&packetList, sizeof(&packetList));
        if (packetList == 0)
            continue;

        this->lock();
        processData(packetList);
        this->unlock();

        packetList->release();
    }
}

asynStatus BasePlugin::setCallbacks(bool enableCallbacks)
{
    asynStatus status;

    asynInterface *pasynInterface = pasynManager->findInterface(m_pasynuserDispatcher, asynGenericPointerType, 1);
    if (!pasynInterface) {
        asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                  "BasePlugin::%s ERROR: Can't find asynGenericPointer interface on array port %s address %d\n",
                  __func__, portName, 0);
        return(asynError);
    }

    asynGenericPointer *asynGenericPointerInterface = (asynGenericPointer *)pasynInterface->pinterface;
    void *asynGenericPointerPvt = pasynInterface->drvPvt;

    if (enableCallbacks && !m_asynGenericPointerInterruptPvt) {
        status = asynGenericPointerInterface->registerInterruptUser(
                    asynGenericPointerPvt, m_pasynuserDispatcher,
                    ::dispatcherCallback, this, &m_asynGenericPointerInterruptPvt);
        if (status != asynSuccess) {
            asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                "BasePlugin::%s ERROR: Can't register for interrupt callbacks on dispatcher port: %s\n",
                __func__, m_pasynuserDispatcher->errorMessage);
        }
    }
    else if (!enableCallbacks && m_asynGenericPointerInterruptPvt) {
        status = asynGenericPointerInterface->cancelInterruptUser(asynGenericPointerPvt,
                        m_pasynuserDispatcher, m_asynGenericPointerInterruptPvt);
        m_asynGenericPointerInterruptPvt = NULL;
        if (status != asynSuccess) {
            asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                "BasePlugin::%s ERROR: Can't unregister for interrupt callbacks on dispatcher port: %s\n",
                __func__, m_pasynuserDispatcher->errorMessage);
        }
    }

    return status;
}
