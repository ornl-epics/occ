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
         * This function schedules a single-shot user defined function to be called in the future,
         * while giving function the ability to schedule further invocations.
         *
         * @param callback Function to be called when timer expires, function should return delay
         *                 in seconds when to run timer next time, or 0 to stop the timer.
         * @param delay Delay in seconds when timer should expire.
         * @return true if scheduled successfully, false is previous task has not yet expired.
         */
        bool schedule(std::function<float()> &callback, float delay);

        /**
         * Cancel the timer.
         *
         * EPICS timer, while thread safe, does not give us notification whether canceling
         * it was done before or after it expired. Since the callers of this class are
         * always locked while calling this class, let's help them determine whether or not
         * timer expired when canceled. It would be wrong to call directly the EPICS timer
         * cancel, see their notes.
         *
         * @retval true if task has been scheduled and it call to this function canceled it
         * @retval false if task has already been executed
         */
        bool cancel();

        /**
         * Is a task scheduled and waiting to be executed.
         *
         * @return true if task has been scheduled and it hasn't yet been processed or canceled
         */
        bool isActive();

    private:
        epicsTimerQueueActive &m_queue;
        epicsTimer &m_timer;
        std::function<float()> m_callback;
        void *m_ctx;
        bool m_active;

        expireStatus expire(const epicsTime & currentTime);
};

#endif // TIMER_H
