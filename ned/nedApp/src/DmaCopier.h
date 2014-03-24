#ifndef DMA_COPIER_H
#define DMA_COPIER_H

#include <epicsThread.h>

#include "CircularBuffer.h"

struct occ_handle;

class DmaCopier : public epicsThreadRunable, public CircularBuffer {
    public:
        DmaCopier(struct occ_handle *occ, uint32_t bufferSize);
        ~DmaCopier();
        void run();
    private:
        epicsThread m_thread;
        struct occ_handle *m_occ;
        bool m_shutdown;
};

#endif // DMA_COPIER_H
