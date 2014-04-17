#include "DmaCircularBuffer.h"

#include <occlib.h>

DmaCircularBuffer::DmaCircularBuffer(struct occ_handle *occ)
    : m_occ(occ)
{
}

DmaCircularBuffer::~DmaCircularBuffer()
{
}

int DmaCircularBuffer::wait(void **data, uint32_t *len)
{
    size_t l = 0;
    int status = occ_data_wait(m_occ, data, &l, 0);
    *len = l;
    return status;
}

int DmaCircularBuffer::consume(uint32_t len)
{
    return occ_data_ack(m_occ, len);
}

bool DmaCircularBuffer::empty()
{
    int status;
    void *data;
    size_t len;

    status = occ_data_wait(m_occ, &data, &len, 1);
    return (status == 0 && len > 0);
}
