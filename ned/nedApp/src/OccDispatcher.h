#ifndef OCC_DISPATCHER_H
#define OCC_DISPATCHER_H

#include "BaseDispatcher.h"
#include "BaseCircularBuffer.h"
#include <epicsThread.h>

class OccDispatcher : public BaseDispatcher {
    public:
        OccDispatcher(const char *portName, BaseCircularBuffer *circularBuffer);

        void occBufferReadThread();

    private:
        DasPacketList m_packetsList;

        BaseCircularBuffer *m_circularBuffer;
        epicsThreadId m_occBufferReadThread;

};

#endif // OCC_DISPATCHER_H
