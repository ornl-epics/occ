#ifndef FIFO_H
#define FIFO_H

#include <deque>

template <class T>
class Fifo : private std::deque<T> {
    private:
        epicsMutex m_mutex;
        epicsEvent m_event;

    public:
        /**
         * Push an element to FIFO.
         */
        void enqueue(const T data)
        {
            m_mutex.lock();
            std::deque<T>::push_back(data);
            m_mutex.unlock();
        }

        /**
         * Push array of elements to FIFO.
         */
        void enqueue(const T *data, size_t count)
        {
            m_mutex.lock();
            while (count--) {
                std::deque<T>::push_back(*data++);
            }
            m_event.signal();
            m_mutex.unlock();
        }

        /**
         * Retrieve and remove record from FIFO.
         */
        size_t dequeue(T *buffer, double timeout=0.0) {
            return dequeue(buffer, 1, timeout);
        }

        /**
         * Retrieve and remove as much as count elements from FIFO.
         */
        size_t dequeue(T *buffer, size_t count, double timeout=0.0)
        {
            epicsTimeStamp endTime, now;
            epicsTimeGetCurrent(&endTime);
            epicsTimeAddSeconds(&endTime, timeout);

            m_mutex.lock();

            // We'll wakeup for each event, but might consume more than one at
            // a time. Make sure we have some data regarless of the event.
            while (std::deque<T>::size() == 0 && timeout > 0.0) {
                m_mutex.unlock();

                if (m_event.wait(timeout) == false)
                    return 0;

                epicsTimeGetCurrent(&now);
                timeout = epicsTimeDiffInSeconds(&endTime, &now);

                m_mutex.lock();
            }

            if (count > std::deque<T>::size())
                count = std::deque<T>::size();

            for (size_t i = 0; i < count; i++) {
                *buffer++ = std::deque<T>::front();
                std::deque<T>::pop_front();
            }

            m_mutex.unlock();

            return count;
        }
};

#endif // FIFO_H
