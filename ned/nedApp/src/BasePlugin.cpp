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

BasePlugin::BasePlugin(const char *portName, const char *dispatcherPortName, int reason,
                       int numParams, int maxAddr, int interfaceMask,
                       int interruptMask, int asynFlags, int autoConnect,
                       int priority, int stackSize)
	: asynPortDriver(portName, maxAddr, NUM_BASEPLUGIN_PARAMS + numParams, interfaceMask | defaultInterfaceMask,
	                 interruptMask | defaultInterruptMask, asynFlags, autoConnect, priority, stackSize)
    , m_messageQueue(MESSAGE_QUEUE_SIZE, sizeof(void*))
    , m_portName(portName)
    , m_processDataThread(0)
{
    int status;

    m_pasynuserDispatcher = pasynManager->createAsynUser(0, 0);
    m_pasynuserDispatcher->userPvt = this;
    m_pasynuserDispatcher->reason = reason;

    createParam("BLOCKING_CALLBACKS",       asynParamInt32,     &PluginBlockingCallbacks);
    createParam("ENABLE_CALLBACKS",         asynParamInt32,     &PluginEnableCallbacks);
    createParam("PROCESSED_COUNT",          asynParamInt32,     &ProcessedCount);
    createParam("TRANSMITTED_COUNT",        asynParamInt32,     &ReceivedCount);

    setIntegerParam(PluginEnableCallbacks,  1);
    setIntegerParam(PluginBlockingCallbacks,1);

    // Connect to dispatcher port permanently. Don't allow connecting to different port at runtime.
    // TODO: Don't do it here, registered callbacks might get called before object constructed
    connectToDispatcherPort(dispatcherPortName);
}

#if 0
void notifyDispatcher() {}
    // Proof-of-concept how signaling back to dispatcher can be done
    asynInterface *pasynInterface = pasynManager->findInterface(m_pasynuserDispatcher, asynInt32Type, 1);
    asynInt32 *interface = (asynInt32 *)pasynInterface->pinterface;
    interface->write(asynGenericPointerPvt, m_pasynuserDispatcher, 1717);
}
#endif // 0

asynStatus BasePlugin::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
    int reason = pasynUser->reason;
    asynStatus status;
    int addr;

    status = getAddress(pasynUser, &addr);
    if (status != asynSuccess)
        return(status);

    // Must lock to ensure paramters are synchronized between all threads.
    // Potentially message could get lost in the worker thread, consequently dead-lock the producer.
    this->lock();

    status = setIntegerParam(addr, reason, value);

    if (reason == PluginBlockingCallbacks) {
        if (value > 0 && m_processDataThread == (epicsThreadId)0) {
            std::string threadName = m_portName + "_Thread";
            m_processDataThread = epicsThreadCreate(threadName.c_str(),
                                                    epicsThreadPriorityMedium,
                                                    epicsThreadGetStackSize(epicsThreadStackMedium),
                                                    (EPICSTHREADFUNC)::processDataThread,
                                                    this);
            if (m_processDataThread == (epicsThreadId)0) {
                asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR, "Failed to create data processing thread - %s\n", threadName.c_str());
                status = asynError;
            }
        }
        if (value == 0 && m_processDataThread != (epicsThreadId)0) {
            // Wake-up processing thread by sending a dummy message. The thread
            // will then exit based on the changed PluginBlockingCallbacks param.
            m_messageQueue.send(0, 0);
            m_processDataThread = 0;
        }
    }
    this->unlock();

    status = callParamCallbacks(addr);

    return status;
}

void BasePlugin::dispatcherCallback(asynUser *pasynUser, void *genericPointer)
{
    DasPacketList *packetList = reinterpret_cast<DasPacketList *>(genericPointer);
    int status=0;
    int blockingCallbacks = 0;

    if (packetList == 0)
        return;

    this->lock();

    getIntegerParam(PluginBlockingCallbacks, &blockingCallbacks);

    if (blockingCallbacks) {
        /* In blocking mode, process the callback in calling thread. Return when
         * processing is complete.
         */
        processData(packetList);
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

    callParamCallbacks();

    this->unlock();
}

asynStatus BasePlugin::connectToDispatcherPort(const char *portName)
{
    asynStatus status;
    int enableCallbacks = 1;

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

    getIntegerParam(PluginEnableCallbacks, &enableCallbacks);
    status = setCallbacks(enableCallbacks);

    return(status);
}

void BasePlugin::processDataThread(void)
{
    DasPacketList *packetList;
    int blockingCallbacks;

    getIntegerParam(PluginBlockingCallbacks, &blockingCallbacks);

    while (blockingCallbacks > 0) {
        m_messageQueue.receive(&packetList, sizeof(&packetList));
        if (packetList == 0)
            continue;

        this->lock();

        processData(packetList);
        packetList->release();

        this->unlock();
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
