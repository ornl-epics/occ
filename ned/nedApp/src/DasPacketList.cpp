#include "DasPacketList.h"

#include <epicsAlgorithm.h>

DasPacketList::DasPacketList()
    : m_address(0)
    , m_length(0)
    , m_consumed(0)
    , m_refcount(0)
{
    //ctor
}

const DasPacket *DasPacketList::first()
{
    const DasPacket *pkt = 0;

    m_lock.lock();
    if (m_refcount != 0)
        pkt = _verifyPacket((DasPacket *)m_address);
    m_lock.unlock();

    return pkt;
}

const DasPacket *DasPacketList::next(const DasPacket *current)
{
    const DasPacket *pkt = 0;
    const uint8_t *currAddr = reinterpret_cast<const uint8_t *>(current);

    m_lock.lock();
    if (m_refcount != 0) {
        // Basic boundary check for current packet, rest is taken care of by _verifyPacket
        if (m_address >= currAddr && currAddr < (m_address + m_length)) {
            const DasPacket *tmp = reinterpret_cast<const DasPacket *>(m_address + current->length());
            pkt = _verifyPacket(tmp);
        }
    }
    m_lock.unlock();

    return pkt;
}

const DasPacket *DasPacketList::_verifyPacket(const DasPacket *pkt) const
{
    const uint8_t *pktAddr = reinterpret_cast<const uint8_t *>(pkt);

    if (pktAddr >= (m_address + m_length))
        return 0;

    if (pkt->length() <= DasPacket::MinLength || pkt->length() >= DasPacket::MaxLength)
        return 0;

    if ((pktAddr + pkt->length()) >= (m_address + m_length))
        return 0;

    return pkt;
}

bool DasPacketList::reserve()
{
    bool reserved;

    m_lock.lock();
    if (m_refcount > 0) {
        reserved = true;
        m_refcount++;
    }
    m_lock.unlock();

    return reserved;
}

void DasPacketList::release(DasPacket *lastProcessed)
{
    const uint8_t *endAddr = reinterpret_cast<const uint8_t *>(next(lastProcessed));

    m_lock.lock();
    if (m_refcount > 0) {
        uint32_t consumed = epicsMin(static_cast<uint32_t>(endAddr - m_address), m_length);
        if (consumed > m_consumed)
            m_consumed = consumed;
        m_refcount--;
    }
    m_lock.unlock();
}

void DasPacketList::release()
{
    m_lock.lock();
    if (m_refcount > 0)
        m_refcount--;
    m_lock.unlock();
}

bool DasPacketList::reset(uint8_t *addr, uint32_t length)
{
    bool reseted = false;

    m_lock.lock();
    if (m_refcount == 0) {
        m_address = addr;
        m_length = length;
        m_consumed = 0;
        m_refcount = 1;
        reseted = true;
    }
    m_lock.unlock();

    return reseted;
}

uint32_t DasPacketList::waitAllReleased() const
{
    uint32_t consumed;

    while (!released())
        m_event.wait();

    m_lock.lock();
    consumed = m_consumed;
    m_lock.unlock();

    return consumed;
}

bool DasPacketList::released() const
{
    bool released;

    m_lock.lock();
    released = (m_refcount == 0);
    m_lock.unlock();

    return released;
}
