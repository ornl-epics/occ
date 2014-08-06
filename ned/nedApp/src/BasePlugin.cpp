#include "BasePlugin.h"
#include "DasPacketList.h"
#include "Timer.h"

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
    , m_portName(portName)
    , m_dispatcherPortName(dispatcherPortName)
    , m_asynGenericPointerInterrupt(0)
    , m_messageQueue(MESSAGE_QUEUE_SIZE, sizeof(void*))
    , m_threadId(0)
    , m_shutdown(false)
{
    int status;

    m_pasynuser = pasynManager->createAsynUser(0, 0);
    m_pasynuser->userPvt = this;
    m_pasynuser->reason = reason;

    createParam("Enable",       asynParamInt32,     &Enable); // Plugin does not receive any data until callbacks are enabled
    createParam("ProcCount",    asynParamInt32,     &ProcCount);
    createParam("RxCount",      asynParamInt32,     &RxCount);

    setIntegerParam(ProcCount,  0);
    setIntegerParam(RxCount,    0);

    // Connect to dispatcher port permanently. Don't allow connecting to different port at runtime.
    // Callbacks need to be enabled separately in order to actually get triggered from dispatcher.
    status = pasynManager->connectDevice(m_pasynuser, dispatcherPortName, 0);
    if (status != asynSuccess) {
        asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                  "BasePlugin: Error calling pasynManager->connectDevice to port %s (status=%d, error=%s)\n",
                  portName, status, m_pasynuser->errorMessage);
    }

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

    // Make sure to disconnect from asyn ports
    (void)enableCallbacks(false);
}

asynStatus BasePlugin::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
    if (pasynUser->reason == Enable) {
        if (enableCallbacks(value > 0) == false)
            return asynError;
    }

    return asynPortDriver::writeInt32(pasynUser, value);
}

void BasePlugin::dispatcherCallback(asynUser *pasynUser, void *genericPointer)
{
    DasPacketList *packetList = reinterpret_cast<DasPacketList *>(genericPointer);

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
        if (m_messageQueue.trySend(&packetList, sizeof(&packetList)) == -1) {
            packetList->release();
            asynPrint(pasynUser, ASYN_TRACE_FLOW, "BasePlugin:%s message queue full\n", __func__);
        }
    }
}

void BasePlugin::sendToDispatcher(const DasPacket *packet)
{
    asynInterface *interface = pasynManager->findInterface(m_pasynuser, asynGenericPointerType, 1);
    if (!interface) {
        asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                  "BasePlugin::%s ERROR: Can't find %s interface on array port %s\n",
                  __func__, asynGenericPointerType, m_dispatcherPortName.c_str());
        return;
    }

    DasPacketList packetsList;
    packetsList.reset(packet);
    asynGenericPointer *asynGenericPointerInterface = reinterpret_cast<asynGenericPointer *>(interface->pinterface);
    void *ptr = reinterpret_cast<void *>(const_cast<DasPacketList *>(&packetsList));
    asynGenericPointerInterface->write(interface->drvPvt, m_pasynuser, ptr);
    packetsList.release();
}

std::shared_ptr<Timer> BasePlugin::scheduleCallback(std::function<float(void)> &callback, double delay)
{
    std::shared_ptr<Timer> timer(new Timer(true));
    if (timer) {
        std::function<float(void)> timerCb = std::bind(&BasePlugin::timerExpire, this, timer, callback);
        timer->schedule(timerCb, delay);
    }
    return timer;
}

const char *BasePlugin::getParamName(int index)
{
    const char *name = "<error>";
    asynPortDriver::getParamName(index, &name);
    return name;
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

bool BasePlugin::enableCallbacks(bool enable)
{
    asynStatus status = asynSuccess;

    asynInterface *interface = pasynManager->findInterface(m_pasynuser, asynGenericPointerType, 1);
    if (!interface) {
        asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                  "BasePlugin::%s ERROR: Can't find asynGenericPointer interface on array port %s\n",
                  __func__, m_dispatcherPortName.c_str());
        return false;
    }

    if (enable && !m_asynGenericPointerInterrupt) {
        asynGenericPointer *asynGenericPointerInterface = reinterpret_cast<asynGenericPointer *>(interface->pinterface);
        status = asynGenericPointerInterface->registerInterruptUser(
                    interface->drvPvt, m_pasynuser,
                    ::dispatcherCallback, this, &m_asynGenericPointerInterrupt);
        if (status != asynSuccess) {
            asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                      "BasePlugin::%s ERROR: Can't enable interrupt callbacks on dispatcher port: %s\n",
                      __func__, m_pasynuser->errorMessage);
        }
    }
    if (!enable && m_asynGenericPointerInterrupt) {
        asynGenericPointer *asynGenericPointerInterface = reinterpret_cast<asynGenericPointer *>(interface->pinterface);
        status = asynGenericPointerInterface->cancelInterruptUser(
            interface->drvPvt,
            m_pasynuser,
            m_asynGenericPointerInterrupt);
        m_asynGenericPointerInterrupt = NULL;
        if (status != asynSuccess) {
            asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
              "BasePlugin::%s ERROR: Can't disable interrupt callbacks on dispatcher port: %s\n",
              __func__, m_pasynuser->errorMessage);
        }
    }

    return (status == asynSuccess);
}

float BasePlugin::timerExpire(std::shared_ptr<Timer> &timer, std::function<float(void)> callback)
{
    float delay = 0.0;
    this->lock();
    if (timer->isActive()) {
        delay = callback();
    }
    this->unlock();
    return delay;
}
