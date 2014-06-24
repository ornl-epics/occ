#ifndef CIRCULAR_BUFFER_H
#define CIRCULAR_BUFFER_H

#include "BaseCircularBuffer.h"

#include <epicsMutex.h>
#include <epicsEvent.h>

/**
 * A circular buffer class with a local copy of data.
 */
class CircularBuffer : public BaseCircularBuffer {
    public:
        /**
         * Create circular buffer, allocate memory.
         *
         * Buffer size should be at least the size of the maximum allowed packet size:
         * for OCC that is 1800*8 bytes.
         */
        CircularBuffer(uint32_t size);

        /**
         * Destroy circular buffer, free memory.
         */
        ~CircularBuffer();

        /**
         * Copy data from memory area data to the circular buffer.
         *
         * @param[in] data Pointer to the start of the source buffer.
         * @param[out] len Number of bytes available in data.
         * @return Number of bytes copied.
         */
        uint32_t push(void *data, uint32_t len);

        /**
         * Wait until some data is available in circular buffer.
         *
         * This is a blocking function. After it returns, it returns
         * pointer to the start of the memory where the data is. It also
         * modifies the len parameter to reflect the amount of data that
         * is available.
         *
         * @retval 0 on success
         * @retval -EOVERFLOW on buffer full
         * @retval -EFAULT on internal error, ie. the buffers have not been allocated
         */
        int wait(void **data, uint32_t *len);

        /**
         * Advance consumer index.
         *
         * @retval 0 on success
         * @retval -EINVAL on invalid len value
         * @retval -EFAULT on internal error, ie. the buffers have not been allocated
         */
        int consume(uint32_t len);

        /**
         * Return true when no data is available in circular buffer.
         */
        bool empty();

        /**
         * Return true when circular buffer is full.
         */
        bool full();

        /**
         * Wakeup consumer in case of an outside error.
         */
        void wakeUpConsumer(int error);

        /**
         * Return current buffer utilization in percents.
         */
        uint32_t utilization();

    protected:
        static uint32_t _align(uint32_t value, uint8_t base);
        static uint32_t _alignDown(uint32_t value, uint8_t base);

    private:
        // These never change after constructor and are safe without the lock
        const uint32_t m_unit;      //!< Used to keep indexes aligned
        void *m_buffer;             //!< Circular buffer
        const uint32_t m_size;      //!< Size of the circular buffer in bytes
        void *m_rollover;           //!< Rollover buffer for the last packet in circular buffer that is cut in two parts
        const uint32_t m_rolloverSize; //!< Size of the rollover buffer in bytes
        int m_error;                //!< Non-zero error value

        // These change frequently and should be secured by a lock
        uint32_t m_consumer;        //!< Index into circular buffer where consumer is
        uint32_t m_producer;        //!< Index into circular buffer where producer is

        epicsMutex m_lock;          //!< Protecting consumer and producer indexes
        epicsEvent m_event;         //!< Semaphore used between single consumer and single producer

};

#endif // CIRCULAR_BUFFER_H
