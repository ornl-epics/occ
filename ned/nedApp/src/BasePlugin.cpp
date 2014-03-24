#include "BasePlugin.h"
#include "DasPacketList.h"

#define NUM_PLUGINDRIVER_PARAMS ((int)(&LAST_PLUGINDRIVER_PARAM - &FIRST_PLUGINDRIVER_PARAM + 1))

#define MESSAGE_QUEUE_SIZE 1   //!< Size of the message queue for callbacks. Since using the synchronous callbacks, 1 should be enough.

BasePlugin::BasePlugin(const char *portName, const char *dispatcherPortName, bool blockingCallbacks, int reason,
                           int maxAddr, int numParams, int interfaceMask,
                           int interruptMask, int asynFlags, int autoConnect,
                           int priority, int stackSize)
	: asynPortDriver(portName, maxAddr, NUM_PLUGINDRIVER_PARAMS + numParams, interfaceMask,
	                 interruptMask, asynFlags, autoConnect, priority, stackSize)
    , m_messageQueue(MESSAGE_QUEUE_SIZE, sizeof(void*))
{

    m_pasynuserDispatcher = pasynManager->createAsynUser(0, 0);
    m_pasynuserDispatcher->userPvt = this;
    m_pasynuserDispatcher->reason = reason;

    createParam("BLOCKING_CALLBACKS",       asynParamInt32,     &PluginDriverBlockingCallbacks);
    createParam("ENABLE_CALLBACKS",         asynParamInt32,     &PluginDriverEnableCallbacks);

    setIntegerParam(PluginDriverBlockingCallbacks,      blockingCallbacks);
    setIntegerParam(PluginDriverEnableCallbacks,        1);

    // TODO: add worker thread

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

void BasePlugin::dispatcherCallback(asynUser *pasynUser, void *genericPointer)
{
    DasPacketList *packetList = reinterpret_cast<DasPacketList *>(genericPointer);
    int status=0;
    int blockingCallbacks;

    this->lock();

    status |= getIntegerParam(PluginDriverBlockingCallbacks, &blockingCallbacks);

    /* Increase the reference counter so that the processData() does not need to
     * decide whether or not to release - simply release it every time.
     */
    packetList->reserve();

    if (blockingCallbacks) {
        /* In blocking mode, process the callback in calling thread. Return when
         * processing is complete.
         */
        processData(packetList);
    } else {
        /* Non blocking mode means the callback will be processed in our background
         * thread.
         */
        bool sent = m_messageQueue.trySend(&packetList, sizeof(&packetList));
        if (!sent) {
            packetList->release();

            asynPrint(pasynUser, ASYN_TRACE_FLOW, "BasePlugin:%s message queue full\n", __func__);
        }
    }

    callParamCallbacks();

    this->unlock();
}

extern "C" {
    static void dispatcherCallback(void *drvPvt, asynUser *pasynUser, void *genericPointer)
    {
        BasePlugin *plugin = (BasePlugin *)drvPvt;
        plugin->dispatcherCallback(pasynUser, genericPointer);
    }
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

    getIntegerParam(PluginDriverEnableCallbacks, &enableCallbacks);
    status = setCallbacks(enableCallbacks);

    return(status);
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
