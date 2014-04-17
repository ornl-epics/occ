#ifndef BASE_CIRCULAR_BUFFER_H
#define BASE_CIRCULAR_BUFFER_H

#include <stdint.h>

/**
 * An abstract circular buffer class.
 */
class BaseCircularBuffer {
    public:
        /**
         * Virtual destructor for polymorphism to work.
         */
        virtual ~BaseCircularBuffer() {}

        /**
         * Copy data from memory area data to the circular buffer.
         *
         * @param[in] data Pointer to the start of the source buffer.
         * @param[out] len Number of bytes available in data.
         * @return Number of bytes copied.
         */
        virtual uint32_t push(void *data, uint32_t len) { return 0; }

        /**
         * Wait until some data is available in circular buffer.
         *
         * This is a blocking function. After it returns, it updates data
         * pointer to the start of the memory where the data is. It also
         * modifies the len parameter to reflect the amount of data that
         * is available.
         *
         * @retval 0 on success
         * @retval negative on error, actual values are implementation specific
         */
        virtual int wait(void **data, uint32_t *len) = 0;

        /**
         * Advance consumer index.
         *
         * @retval 0 on success
         * @retval negative on error, actual values are implementation specific
         */
        virtual int consume(uint32_t len) = 0;

        /**
         * Return true when no data is available in circular buffer.
         */
        virtual bool empty() = 0;
};

#endif // BASE_CIRCULAR_BUFFER_H
