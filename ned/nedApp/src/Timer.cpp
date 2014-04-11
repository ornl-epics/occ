#include "Timer.h"

Timer::Timer(bool shared)
    : m_queue(epicsTimerQueueActive::allocate(shared))
    , m_timer(m_queue.createTimer())
{
}

Timer::~Timer()
{
    m_timer.destroy();
}

bool Timer::schedule(std::function<void()> &callback, double delay) {
    if (m_timer.getExpireDelay() == DBL_MAX)
        return false;
    m_callback = callback;
    m_timer.start(*this, delay);
    return true;
}

epicsTimerNotify::expireStatus Timer::expire(const epicsTime & currentTime)
{
    m_callback();
    return noRestart;
}
