#ifndef INTERLOCKED_FIFO_H
#define INTERLOCKED_FIFO_H

#include <list>

template <class T>
class InterlockedFifo : private std::list<T> {
    private:
        epicsMutex m_mutex;

    public:
        void enqueue(const T data)
        {
            m_mutex.lock();
            std::list<T>::push_back(data);
            m_mutex.unlock();
        }

        size_t dequeue(T buffer, size_t size)
        {
            m_mutex.lock();
            if (size > std::list<T>::size())
                size = std::list<T>::size();

            for (size_t i = 0; i < size; i++) {
                (*buffer)++ = std::list<T>::front();
            }
            m_mutex.unlock();

            return size;
        }

        size_t dequeue(T buffer, size_t size, double timeout)
        {
            epicsTimeStamp expire, now;
            epicsTimeGetCurrent(&expire);
            epicsTimeAddSeconds(&expire, timeout);

            do {
                size_t ret = dequeue(buffer, size);
                if (ret > 0)
                    return ret;

                epicsTimeGetCurrent(&now);
            } while (epicsTimeGreaterThan(&now, &expire) != 0);
            return 0;
        }
};

#endif // INTERLOCKED_FIFO_H
