#ifndef DASPACKETLIST_HPP
#define DASPACKETLIST_HPP

#include "DasPacket.h"

#include <epicsEvent.h>
#include <epicsMutex.h>

/**
 * List of DAS Packets with reference counter
 *
 * This list points to the memory block with DAS packets. The memory block
 * length needs not be aligned with the packet. This class ensures that
 * all DAS packets returned by it are contiguous and complete. The last block
 * in the memory block might be partial DAS packet and will not be considered
 * by this class.
 *
 * The reference count keeps data pointed to by this object valid until
 * reference count drops to 0. At that point the data can be replaced with
 * new data but not earlier.
 */
class DasPacketList
{
    public:
        /**
         * Constructor initialized internal structures.
         */
        DasPacketList();

        /**
         * Return first DAS packet in the list.
         *
         * @return First DAS packet or 0 if none.
         */
        const DasPacket *first();

        /**
         * Return next DAS packet in the list.
         *
         * @param[in] current Any valid packet previously returned from first() or next().
         * @return Next DAS packet or 0 if none.
         */
        const DasPacket *next(const DasPacket *current);

        /**
         * Increase internal reference count.
         *
         * After making the reservation, data pointed to by this class
         * is guaranteed not to change until a release() is called.
         *
         * @return True if reservation was made, false if there's no data.
         */
        bool reserve();

        /**
         * Decrease internal reference count and claim consumed data.
         *
         * Every call to reserve() must be followed by a call to this
         * function. In addition, reset() sets reference count to 1.
         *
         * After the client gets finished with processing DAS packets
         * described by this list, it should release the data by calling
         * this function with the last DAS packet it actually processed.
         * The last packet processed helps DasPacketList class determine
         * the size of the data processed which could be flagged consumed.
         * The remaining of the data in the memory gets rescheduled for
         * later processing.
         * Client is encouraged to consume as much packets from this list
         * as possible.
         *
         * @param[in] lastProcessed The last DAS packets processed by the client
         *            or 0 if no data.
         */
        void release(const DasPacket *lastProcessed);

        /**
         * Reset the list with new OCC data.
         *
         * The reference count must be 0 for this function to succeed.
         * The function should only be used by producers, check
         * the list friend classes.
         * This function will set the reference count to 1.
         *
         * @param[in] addr Pointer to the memory block where DAS packets data starts.
         * @param[in] length Memory block length in bytes.
         * @return true when object points to new data, false if reference count is not 0.
         */
        bool reset(uint8_t *addr, uint32_t length);

        /**
         * Wait for all consumers to release the object.
         *
         * After the function returns, reference counter is guaranteed to be 0 and
         * object can not be reserved() again until next reset() returns.
         *
         * @return Number of bytes consumed from the memory block.
         */
        uint32_t waitAllReleased() const;

    private:
        const uint8_t *m_address;
        uint32_t m_length;
        uint32_t m_consumed;

        unsigned long m_refcount;
        mutable epicsMutex m_lock;
        mutable epicsEvent m_event;

        /**
         * Verify that the data pointed to is legimite DAS packet.
         *
         * @return Return the same pointer or 0 on error.
         */
        const DasPacket *_verifyPacket(const DasPacket *current) const;

        /**
         * Return true if reference count is 0, false otherwise.
         */
        bool released() const;
};

#endif // DASPACKETLIST_HPP
