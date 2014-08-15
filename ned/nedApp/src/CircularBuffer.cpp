#include "CircularBuffer.h"

#include <cantProceed.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <limits>
#include <stdexcept>

static const uint32_t UNIT_SIZE            = 4;              // OCC data is 4 byte aligned
static const uint32_t ROLLOVER_BUFFER_SIZE = 1800*UNIT_SIZE; // Comes from the OCC protocol, max 1800 events in packet

using namespace std;

CircularBuffer::CircularBuffer(uint32_t size)
    : m_unit(UNIT_SIZE)
    , m_size(_align(size, m_unit))
    , m_rolloverSize(min(m_size - m_unit, ROLLOVER_BUFFER_SIZE))
    , m_error(0)
    , m_consumer(0)
    , m_producer(0)
{
	if (size > (numeric_limits<uint32_t>::max()/2)) { // I wish there was a portable way like numeric_limits<typeof(m_size)>::max
        // Consult also comment in CircularBuffer:consume()
        throw length_error("Requested buffer size too big");
    }

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

    if (!m_buffer || !m_rollover) {
        m_error = -EFAULT;
        return 0;
    }
    if (len < m_unit)
        return 0;
    if (m_error)
        return 0;

    m_lock.lock();
    cons = m_consumer;
    prod = m_producer;
    m_lock.unlock();

    len = std::min(_alignDown(len, m_unit), (m_size + cons - prod - m_unit) % m_size);

    if (len == 0) {
        m_error = -EOVERFLOW;
        m_event.signal();
        return 0;
    }

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

int CircularBuffer::wait(void **data, uint32_t *len)
{
    if (!m_buffer || !m_rollover) {
        *len = 0;
        return -EFAULT;
    }

    while (m_error == 0 && empty())
        m_event.wait();

    if (m_error != 0)
        return m_error;

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

    return 0;
}

int CircularBuffer::consume(uint32_t len)
{
    uint32_t used;

    if (!m_buffer || !m_rollover)
        return -EFAULT;
    if (len < m_unit)
        return -EINVAL;

    len = _alignDown(len, m_unit);

    m_lock.lock();
    // The next computation limits the size of the buffer to half the size
    // of the type used for m_size,m_producer,m_consumer variables.
    used = (m_size + m_producer - m_consumer) % m_size;
    if (used < len) {
		// If this happens, the client code is broken and should be fixed.
        // Likely this will cause the next wait() to return address
        // in the middle of the packet.
        len = used;
    }
    m_consumer = (m_consumer + len) % m_size;
    m_lock.unlock();

    return 0;
}

bool CircularBuffer::empty()
{
    bool isEmpty = false;

    m_lock.lock();
    isEmpty = (m_consumer == m_producer);
    m_lock.unlock();

    return isEmpty;
}

bool CircularBuffer::full() {
    bool isFull = false;

    m_lock.lock();
    isFull = (((m_size + m_consumer - m_producer - m_unit) % m_size) == 0);
    m_lock.unlock();

    return isFull;
}

void CircularBuffer::wakeUpConsumer(int error)
{
    m_error = error;
    m_event.signal();
}

uint32_t CircularBuffer::_alignDown(uint32_t value, uint8_t base)
{
    return _align(value - base + 1, base);
}

uint32_t CircularBuffer::_align(uint32_t value, uint8_t base)
{
    return (value + base - 1) & ~(base - 1);
}

uint32_t CircularBuffer::used()
{
    return (m_size + m_producer - m_consumer) % m_size;
}

uint32_t CircularBuffer::size()
{
    return m_size;
}
