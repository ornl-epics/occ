#ifndef TIMER_H
#define TIMER_H

#include <epicsTimer.h>
#include <functional>

/**
 * Timer is used for scheduling tasks in the future.
 *
 * The Timer class is used to schedule a future asychronously.
 * It's using the EPICS epicsTimer and friends and it's running
 * in custom thread. The thread can be shared or private. When shared,
 * all timers share single thread.
 *
 * A single timer can only accept one task in the future until it
 * expires.
 */
class Timer : private epicsTimerNotify {
    public:
        /**
         * Constructor.
         */
        Timer(bool shared);

        /**
         * Destructor frees resource and releases thread reference.
         */
        ~Timer();

        /**
         * Schedule a new task in the future.
         *
         * @param return true if scheduled successfully, false is previous task has not yet expired.
         */
        bool schedule(std::function<void()> &callback, double delay);

    private:
        epicsTimerQueueActive &m_queue;
        epicsTimer &m_timer;
        std::function<void()> m_callback;
        void *m_ctx;

        expireStatus expire(const epicsTime & currentTime);
};

#endif // TIMER_H
