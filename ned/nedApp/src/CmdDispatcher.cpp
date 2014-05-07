#include "CmdDispatcher.h"

static const int asynMaxAddr       = 1;
static const int asynInterfaceMask = asynGenericPointerMask;
static const int asynInterruptMask = asynGenericPointerMask;
static const int asynFlags         = 0;
static const int asynAutoConnect   = 1;
static const int asynPriority      = 0;
static const int asynStackSize     = 0;

CmdDispatcher::CmdDispatcher(const char *portName, const char *connectPortName)
    : BasePlugin(portName, connectPortName, REASON_OCCDATA, /*blocking=*/0, /*numparams=*/0,
                 asynMaxAddr, asynInterfaceMask, asynInterruptMask, asynFlags, asynAutoConnect,
                 asynPriority, asynStackSize)
    , m_nReceived(0)
    , m_nProcessed(0)
{
}

void CmdDispatcher::processData(const DasPacketList * const packetList)
{
    const DasPacket *first = 0;
    const DasPacket *last = 0;
    DasPacketList cmdList;

    for (const DasPacket *packet = packetList->first(); packet != 0; packet = packetList->next(packet)) {
        m_nReceived++;

        if (packet->isCommand()) {
            if (first == 0)
                first = packet;
            last = packet;
            m_nProcessed++;
        } else if (first) {
            sendToPlugins(first, last);
            first = last = 0;
        }
    }
    if (first) {
        sendToPlugins(first, last);
    }

    // Update parameters
    setIntegerParam(ProcCount,  m_nProcessed);
    setIntegerParam(RxCount,    m_nReceived);
    callParamCallbacks();
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
