#include "OccDispatcher.h"

#include "BasePlugin.h" // For reasons

extern "C" {
    static void occBufferReadThreadC(void *drvPvt)
    {
        OccDispatcher *dispatcher = reinterpret_cast<OccDispatcher *>(drvPvt);
        dispatcher->occBufferReadThread();
    }
}

OccDispatcher::OccDispatcher(const char *portName, BaseCircularBuffer *circularBuffer)
    : BaseDispatcher(portName)
    , m_circularBuffer(circularBuffer)
{
    m_occBufferReadThread = epicsThreadCreate(portName,
                                              epicsThreadPriorityHigh,
                                              epicsThreadGetStackSize(epicsThreadStackMedium),
                                              (EPICSTHREADFUNC)occBufferReadThreadC,
                                              this);
}

void OccDispatcher::occBufferReadThread()
{
    void *data;
    uint32_t length;
    uint32_t consumed;
    bool resetErrorRatelimit = false;

    while (true) {
        m_circularBuffer->wait(&data, &length);

        if (!m_packetsList.reset(reinterpret_cast<uint8_t*>(data), length)) {
            // This should not happen. If it does it's certainly a code error that needs to be fixed.
            if (!resetErrorRatelimit) {
                asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "PluginDriver:%s ERROR failed to reset DasPacketList\n", __func__);
                resetErrorRatelimit = true;
            }
            continue;
        }
        resetErrorRatelimit = false;

        sendToPlugins(REASON_OCCDATA, &m_packetsList);

        consumed = 0;
        // Plugins have been notified, hopefully they're all threads.
        // While waiting, calculate how much data can be consumed from circular buffer.
        for (const DasPacket *packet = m_packetsList.first(); packet != 0; packet = m_packetsList.next(packet)) {
            consumed += packet->length();
        }

        m_packetsList.release(); // reset() set it to 1
        m_packetsList.waitAllReleased();

        m_circularBuffer->consume(consumed);
    }
}
