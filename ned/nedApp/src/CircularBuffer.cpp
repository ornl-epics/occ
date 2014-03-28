#include "CircularBuffer.h"

#include <cantProceed.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

static const uint32_t UNIT_SIZE            = 8;              // OCC data is 8 byte aligned
static const uint32_t ROLLOVER_BUFFER_SIZE = 1800*UNIT_SIZE; // Comes from the OCC protocol, max 1800 events in packet

using namespace std;

CircularBuffer::CircularBuffer(uint32_t size)
    : m_unit(UNIT_SIZE)
    , m_size(_align(size, m_unit))
    , m_rolloverSize(min(m_size - m_unit, ROLLOVER_BUFFER_SIZE))
    , m_consumer(0)
    , m_producer(0)
{
    // Don't need or want the initialization provided by new operator.
    // This is fine with C++ as long as we're consistent and use free (not delete) at the end.
    m_buffer = mallocMustSucceed(size, "Can't allocate CircularBuffer buffer");
    if (!m_buffer)
        return;

    m_rollover = mallocMustSucceed(m_rolloverSize, "Can't allocate CircularBuffer rollover buffer");
    if (!m_rollover)
        return;
}

CircularBuffer::~CircularBuffer()
{
    m_event.signal();

    if (m_buffer)
        free(m_buffer);
    if (m_rollover)
        free(m_rollover);
    m_buffer = NULL;
}

uint32_t CircularBuffer::push(void *data, uint32_t len)
{
    uint32_t prod, cons;
    uint32_t head, tail;

    if (!m_buffer || !m_rollover)
        return 0;
    if (len < m_unit)
        return 0;

    m_lock.lock();
    cons = m_consumer;
    prod = m_producer;
    m_lock.unlock();

    len = std::min(_alignDown(len, m_unit), (m_size + cons - prod - m_unit) % m_size);

    head = m_size - prod;
    if (len < head) {
        head = len;
        tail = 0;
    } else {
        tail = len - head;
    }

    memcpy(static_cast<char*>(m_buffer) + prod, data, head);
    memcpy(m_buffer, static_cast<char*>(data) + head, tail);

    m_lock.lock();
    m_producer += len;
    m_producer %= m_size;
    m_lock.unlock();

    m_event.signal();

    return (head + tail);
}

void CircularBuffer::wait(void **data, uint32_t *len)
{
    if (!m_buffer || !m_rollover) {
        *len = 0;
        return;
    }

    while (empty())
        m_event.wait();

    m_lock.lock();
    *data = (char *)m_buffer + m_consumer;
    if (m_producer >= m_consumer)
        *len = m_producer - m_consumer;
    else {
        *len = m_size - m_consumer;
        if (*len < m_rolloverSize) {
            uint32_t head = *len;
            uint32_t tail = min(m_rolloverSize - *len, m_producer);
            memcpy(m_rollover, (char *)m_buffer + m_consumer, head);
            memcpy((char *)m_rollover + head, m_buffer, tail);

            *len = head + tail;
            *data = m_rollover;
        }
    }
    m_lock.unlock();
}

void CircularBuffer::consume(uint32_t len)
{
    uint32_t used;

    if (!m_buffer || !m_rollover)
        return;
    if (len < m_unit)
        return;

    len = _alignDown(len, m_unit);

    m_lock.lock();
    used = (m_producer - m_consumer) % m_size;
    len = std::min(len, used);
    m_consumer = (m_consumer + len) % m_size;
    m_lock.unlock();
}

bool CircularBuffer::empty()
{
    bool isEmpty = false;

    m_lock.lock();
    isEmpty = (m_consumer == m_producer);
    m_lock.unlock();

    return isEmpty;
}

uint32_t CircularBuffer::_alignDown(uint32_t value, uint8_t base)
{
    return _align(value - base + 1, base);
}

uint32_t CircularBuffer::_align(uint32_t value, uint8_t base)
{
    return (value + base - 1) & ~(base - 1);
}
