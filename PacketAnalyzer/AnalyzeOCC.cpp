#include "AnalyzeOCC.hpp"
#include "LabPacket.hpp"

#include <errno.h>

#include <occlib.h>
#include <stdexcept>
#include <sstream>
#include <iostream>

extern bool shutdown;

using namespace std;

AnalyzeOCC::AnalyzeOCC(const string &devfile) :
    OccAdapter(devfile),
    m_rampCounter(-1)
{
}

void AnalyzeOCC::process()
{
    while (!shutdown) {
        unsigned char *data = NULL;
        size_t datalen = 0;
        unsigned char *it;

        int ret = occ_data_wait(m_occ, reinterpret_cast<void **>(&data), &datalen, 100);
        if (ret != 0) {
            if (ret == -ETIME)
                continue;
            throw runtime_error("Failed to read from OCC device - " + occErrorString(ret));
        }

        for (it = data; it < (data + datalen); ) {
            LabPacket *packet;
            try {
                packet = new (it) LabPacket(datalen - (it - data));
            } catch (overflow_error &e) {
                break;
            }
            analyzePacket(packet);
            it += packet->length();
        }

        if ((it - data) > 0) {
            ret = occ_data_ack(m_occ, it - data);
            if (ret != 0) {
                throw runtime_error("Failed to advance consumer index - " + occErrorString(ret));
            }
        }
    }
}

void AnalyzeOCC::analyzePacket(const LabPacket * const packet)
{
    bool good = false;
    uint32_t packet_length = packet->length();
    uint32_t errorOffset = 0;

    if (packet_length == 0) {
        // header length is not aligned
    } else if (packet->isCommand()) {
        good = true;
        m_metrics.commands.goodCount++;
        m_metrics.commands.bytes += packet_length;
    } else if (packet->isData()) {

        if (packet->isDataRtdl()) {
            m_metrics.rtdl.bytes += packet_length;
            if (packet->verifyRtdl(errorOffset)) {
                good = true;
                m_metrics.rtdl.goodCount++;
            } else {
                m_metrics.rtdl.badCount++;
            }
        } else if (packet->isDataMeta()) {
            m_metrics.meta.bytes += packet_length;
            if (packet->verifyMeta(errorOffset)) {
                good = true;
                m_metrics.meta.goodCount++;
            } else {
                m_metrics.meta.badCount++;
            }
        } else if (packet->isDataEvent()) {
            m_metrics.event.bytes += packet_length;
            if (packet->verifyEvent(errorOffset)) {
                good = true;
                m_metrics.event.goodCount++;
            } else {
                m_metrics.event.badCount++;
            }
        } else if (packet->isDataRamp()) {
            m_metrics.ramp.bytes += packet_length;
            if (packet->verifyRamp(errorOffset, m_rampCounter)) {
                good = true;
                m_metrics.ramp.goodCount++;
            } else {
                m_metrics.ramp.badCount++;
            }
        } else {
            good = true;
            m_metrics.other.bytes += packet_length;
            m_metrics.ramp.goodCount++;
        }

        m_metrics.data.bytes += packet_length;
        if (good)
            m_metrics.data.goodCount++;
        else
            m_metrics.data.badCount++;
    }

    m_metrics.total.bytes += packet_length;
    if (good)
        m_metrics.total.goodCount++;
    else
        m_metrics.total.badCount++;

    if (!good) {
        dumpPacket(packet, errorOffset);
    }
}
