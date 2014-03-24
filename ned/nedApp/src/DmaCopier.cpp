#include "DmaCopier.h"

#include <occlib.h>

DmaCopier::DmaCopier(struct occ_handle *occ, uint32_t bufferSize)
    : CircularBuffer(bufferSize)
    , m_thread(*this, "DmaCopier", epicsThreadGetStackSize(epicsThreadStackBig), epicsThreadPriorityHigh)
    , m_occ(occ)
    , m_shutdown(false)
{
    m_thread.start();
}

DmaCopier::~DmaCopier()
{
    m_shutdown = true;
    m_thread.exitWait();
}

void DmaCopier::run()
{
    while (!m_shutdown) {
        void *data;
        size_t len;
        int status;

        status = occ_data_wait(m_occ, &data, &len, 0);
        if (status != 0) {
            // TODO: report error
            break;
        }

        len = CircularBuffer::push(data, len);

        status = occ_data_ack(m_occ, len);
    }
}
