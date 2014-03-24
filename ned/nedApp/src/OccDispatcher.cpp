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

        sendToPlugins(REASON_NORMAL, &m_packetsList);

        m_packetsList.release(); // reset() set it to 1
        consumed = m_packetsList.waitAllReleased();
        if (consumed == 0) {
            // There could be two reasons, either no suitable plugin running or
            // the data is not valid. In either case consume valid packets.
            const DasPacket *pkt = m_packetsList.first();
            while (pkt != 0) {
                consumed += pkt->length();
                pkt = m_packetsList.next(pkt);
            }
        }

        m_circularBuffer->consume(consumed);
    }
}
