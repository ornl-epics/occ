#include "CmdDispatcher.h"

static const int asynMaxAddr       = 1;
static const int asynInterfaceMask = asynGenericPointerMask;
static const int asynInterruptMask = asynGenericPointerMask;
static const int asynFlags         = 0;
static const int asynAutoConnect   = 1;
static const int asynPriority      = 0;
static const int asynStackSize     = 0;

EPICS_REGISTER_PLUGIN(CmdDispatcher, 2, "Port name", string, "Dispatcher port name", string);

CmdDispatcher::CmdDispatcher(const char *portName, const char *connectPortName)
    : BasePlugin(portName, connectPortName, REASON_OCCDATA, /*blocking=*/1, /*numparams=*/0,
                 asynMaxAddr, asynInterfaceMask, asynInterruptMask, asynFlags, asynAutoConnect,
                 asynPriority, asynStackSize)
    , m_nReceived(0)
    , m_nProcessed(0)
{
}

void CmdDispatcher::processDataUnlocked(const DasPacketList * const packetList)
{
    const DasPacket *first = 0;
    const DasPacket *last = 0;
    DasPacketList cmdList;
    uint32_t nReceived = 0;
    uint32_t nProcessed = 0;

    for (const DasPacket *packet = packetList->first(); packet != 0; packet = packetList->next(packet)) {
        nReceived++;

        if (packet->isCommand() && packet->cmdinfo.command != DasPacket::CMD_RTDL && packet->cmdinfo.command != DasPacket::CMD_TSYNC) {
            if (first == 0)
                first = packet;
            last = packet;
            nProcessed++;
        } else if (first) {
            sendToPlugins(first, last);
            first = last = 0;
        }
    }
    if (first) {
        sendToPlugins(first, last);
    }

    // Update parameters
    this->lock();
    m_nReceived += nReceived;
    m_nProcessed += nProcessed;
    setIntegerParam(ProcCount,  m_nProcessed);
    setIntegerParam(RxCount,    m_nReceived);
    callParamCallbacks();
    this->unlock();
}

void CmdDispatcher::sendToPlugins(const DasPacket *first, const DasPacket *last)
{
    DasPacketList cmdList;
    uint32_t length = (reinterpret_cast<const uint8_t*>(last) - reinterpret_cast<const uint8_t*>(first)) + last->length();

    cmdList.reset(reinterpret_cast<const uint8_t*>(first), length);
    doCallbacksGenericPointer(reinterpret_cast<void *>(&cmdList), REASON_OCCDATA, 0);
    cmdList.release();
    cmdList.waitAllReleased();
}

asynStatus CmdDispatcher::writeGenericPointer(asynUser *pasynUser, void *pointer)
{
    if (pasynUser->reason == REASON_OCCDATA) {
        asynInterface *interface = pasynManager->findInterface(m_pasynuser, asynGenericPointerType, 1);
        if (!interface) {
            asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                      "BasePlugin::%s ERROR: Can't find %s interface on array port %s\n",
                      __func__, asynGenericPointerType, m_dispatcherPortName.c_str());
            return asynError;
        }

        asynGenericPointer *asynGenericPointerInterface = reinterpret_cast<asynGenericPointer *>(interface->pinterface);
        void *ptr = reinterpret_cast<void *>(reinterpret_cast<DasPacketList *>(pointer));
        asynGenericPointerInterface->write(interface->drvPvt, m_pasynuser, ptr);
    }
    return asynSuccess;
}
