#include "AnalyzeOutput.hpp"
#include "LabPacket.hpp"

#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <iomanip>
#include <ncurses.h>
#include <sstream>
#include <map>
#include <occlib_hw.h>
#include <stdexcept>

using namespace std;

#define MAX_EVENTS_PER_PACKET               1800U
#define PRINT_RATELIMIT                     1e9 // define how often to print metrics [in ns]

AnalyzeOutput::AnalyzeOutput(const string &devfile, const string &dumpfile, bool dmadump) :
    AnalyzeOCC(devfile, dmadump),
    m_dumpFile(dumpfile),
    m_dumpStream(dumpfile.c_str(), ofstream::out | ofstream::binary),
    m_lastPrintTime(0)
{
    initscr();
    mvprintw(0, 0, "Waiting for OCC data...");
    refresh();
}

AnalyzeOutput::~AnalyzeOutput()
{
    m_dumpStream.close();
    endwin();
}

void AnalyzeOutput::showMetrics()
{
    struct timespec t;
    uint64_t now;

    clock_gettime(CLOCK_MONOTONIC, &t);
    now = t.tv_sec * 1e9 + t.tv_nsec;

#ifdef PRINT_RATELIMIT
    if (m_lastPrintTime + PRINT_RATELIMIT > now)
        return;
#endif

    double runtime = (double)(now - m_lastPrintTime) / 1e9;
    double totalSpeed = (m_metrics.total.goodCount    + m_metrics.total.badCount    - m_lastMetrics.total.goodCount    - m_lastMetrics.total.badCount)    / runtime;
    double cmdSpeed   = (m_metrics.commands.goodCount + m_metrics.commands.badCount - m_lastMetrics.commands.goodCount - m_lastMetrics.commands.badCount) / runtime;
    double dataSpeed  = (m_metrics.data.goodCount     + m_metrics.data.badCount     - m_lastMetrics.data.goodCount     - m_lastMetrics.data.badCount)     / runtime;
    double rtdlSpeed  = (m_metrics.rtdl.goodCount     + m_metrics.rtdl.badCount     - m_lastMetrics.rtdl.goodCount     - m_lastMetrics.rtdl.badCount)     / runtime;
    double metaSpeed  = (m_metrics.meta.goodCount     + m_metrics.meta.badCount     - m_lastMetrics.meta.goodCount     - m_lastMetrics.meta.badCount)     / runtime;
    double eventSpeed = (m_metrics.event.goodCount    + m_metrics.event.badCount    - m_lastMetrics.event.goodCount    - m_lastMetrics.event.badCount)    / runtime;
    double rampSpeed  = (m_metrics.ramp.goodCount     + m_metrics.ramp.badCount     - m_lastMetrics.ramp.goodCount     - m_lastMetrics.ramp.badCount)     / runtime;
    double otherSpeed = (m_metrics.other.goodCount    + m_metrics.other.badCount    - m_lastMetrics.other.goodCount    - m_lastMetrics.other.badCount)    / runtime;
    double totalThroughput = (m_metrics.total.bytes      - m_lastMetrics.total.bytes)      / runtime;
    double cmdThroughput   = (m_metrics.commands.bytes   - m_lastMetrics.commands.bytes)   / runtime;
    double dataThroughput  = (m_metrics.data.bytes  - m_lastMetrics.data.bytes)  / runtime;
    double rtdlThroughput  = (m_metrics.rtdl.bytes  - m_lastMetrics.rtdl.bytes)  / runtime;
    double metaThroughput  = (m_metrics.meta.bytes  - m_lastMetrics.meta.bytes)  / runtime;
    double eventThroughput = (m_metrics.event.bytes - m_lastMetrics.event.bytes) / runtime;
    double rampThroughput  = (m_metrics.ramp.bytes  - m_lastMetrics.ramp.bytes)  / runtime;
    double otherThroughput = (m_metrics.other.bytes - m_lastMetrics.other.bytes) / runtime;

    mvprintw(0, 0, "OCC incoming packets metrics");
    mvprintw(1, 0, "Total   : %" PRIu64 " packets", m_metrics.total.goodCount    + m_metrics.total.badCount);
    mvprintw(2, 0, "Commands: %" PRIu64 " packets", m_metrics.commands.goodCount + m_metrics.commands.badCount);
    mvprintw(3, 0, "Data    : %" PRIu64 " packets", m_metrics.data.goodCount     + m_metrics.data.badCount);
    mvprintw(4, 0, "|- RTDL : %" PRIu64 " (%" PRIu64 " bad) packets", m_metrics.rtdl.goodCount,  m_metrics.rtdl.badCount);
    mvprintw(5, 0, "|- Meta : %" PRIu64 " (%" PRIu64 " bad) packets", m_metrics.meta.goodCount,  m_metrics.meta.badCount);
    mvprintw(6, 0, "|- Event: %" PRIu64 " (%" PRIu64 " bad) packets", m_metrics.event.goodCount, m_metrics.event.badCount);
    mvprintw(7, 0, "|- Ramp : %" PRIu64 " (%" PRIu64 " bad) packets", m_metrics.ramp.goodCount,  m_metrics.ramp.badCount);
    mvprintw(8, 0, "|- Other: %" PRIu64 " (%" PRIu64 " bad) packets", m_metrics.other.goodCount, m_metrics.other.badCount);

    mvprintw(1, 40, "[%6.2f%cp/s %6.2f%cB/s]            ", speedFormatted(totalSpeed), speedTag(totalSpeed), speedFormatted(totalThroughput), speedTag(totalThroughput));
    mvprintw(2, 40, "[%6.2f%cp/s %6.2f%cB/s]            ", speedFormatted(cmdSpeed),   speedTag(cmdSpeed),   speedFormatted(cmdThroughput),   speedTag(cmdThroughput));
    mvprintw(3, 40, "[%6.2f%cp/s %6.2f%cB/s]            ", speedFormatted(dataSpeed),  speedTag(dataSpeed),  speedFormatted(dataThroughput),  speedTag(dataThroughput));
    mvprintw(4, 40, "[%6.2f%cp/s %6.2f%cB/s]            ", speedFormatted(rtdlSpeed),  speedTag(rtdlSpeed),  speedFormatted(rtdlThroughput),  speedTag(rtdlThroughput));
    mvprintw(5, 40, "[%6.2f%cp/s %6.2f%cB/s]            ", speedFormatted(metaSpeed),  speedTag(metaSpeed),  speedFormatted(metaThroughput),  speedTag(metaThroughput));
    mvprintw(6, 40, "[%6.2f%cp/s %6.2f%cB/s]            ", speedFormatted(eventSpeed), speedTag(eventSpeed), speedFormatted(eventThroughput), speedTag(eventThroughput));
    mvprintw(7, 40, "[%6.2f%cp/s %6.2f%cB/s]            ", speedFormatted(rampSpeed),  speedTag(rampSpeed),  speedFormatted(rampThroughput),  speedTag(rampThroughput));
    mvprintw(8, 40, "[%6.2f%cp/s %6.2f%cB/s]            ", speedFormatted(otherSpeed), speedTag(otherSpeed), speedFormatted(otherThroughput), speedTag(otherThroughput));


    if (!m_dumpFile.empty())
        mvprintw(10, 0, "Saving bad packets to: '%s'", m_dumpFile.c_str());
    refresh();

    // it's all basic types, operator= works well
    m_lastMetrics = m_metrics;
    m_lastPrintTime = now;
}

#define SS_HEX(n) std::hex << std::setw(8) << std::setfill('0') << n << std::dec << std::setw(0)

/**
 * Dump packet to a file. File format matches the Windows implementation of PacketAnalyzer.
 */
void AnalyzeOutput::dumpPacket(const LabPacket * const packet, uint32_t errorOffset)
{
    ostringstream ss;

    if (m_dumpStream.bad())
        return;

    if (packet->isDataRtdl())
        ss << "bad rtdl";
    else if (packet->isDataMeta())
        ss << "bad meta";
    else if (packet->isDataEvent())
        ss << "bad event data";
    else if (packet->isDataRamp())
        ss << "bad ramp";
    else
        ss << "other";

    ss << " data sync " << m_datatsync << " data sub " << m_datasubpacket
       << " meta tsync " << m_metatsync << " meta sub " << m_metasubpacket
       << " " << formatTime(time(NULL)) << std::endl;

    ss << "imq source " << packet->source << " dest " << SS_HEX(packet->destination)
       << " pckinfo " << SS_HEX(packet->info) << " len " << SS_HEX(packet->payload_length)
       << " rsv1 " << SS_HEX(packet->reserved1) << " rsv2 " << SS_HEX(packet->reserved2)
       << " rsv3 " << SS_HEX(packet->reserved1) << " rsv4 " << SS_HEX(packet->reserved2)
       << std::endl;

    if (packet->isDataRtdl()) {
        for (uint32_t i = 0; i < min(100U, (unsigned)(packet->payload_length / sizeof(uint32_t))); i++) {
            ss << i << " " << SS_HEX(packet->data[i]);
            if (errorOffset == i)
                ss << " *** MISMATCH";
            ss << std::endl;
        }
    } else if (packet->isDataMeta()) {
        ss << "rtdl tshigh " << SS_HEX(packet->data[0]) << " tslow " << SS_HEX(packet->data[1])
           << " charge " << SS_HEX(packet->data[2]) << " geninfo " << SS_HEX(packet->data[3])
           << " tswidth " << SS_HEX(packet->data[4]) << " tsdelay " << SS_HEX(packet->data[4])
           << std::endl;
        // Skip first 6 words for RTDL info
        for (uint32_t i = 3; i < packet->payload_length/sizeof(struct DasNeutronEvent); i++) {
            const DasNeutronEvent *event = reinterpret_cast<const DasNeutronEvent *>(&packet->data[i]);
            ss << i << " tof " << SS_HEX(event->tof) << " pix " << SS_HEX(event->pixelid);
            if (errorOffset == i)
                ss << " *** MISMATCH";
            ss << std::endl;
        }
    } else if (packet->isDataEvent()) {
        const DasNeutronEvent *event = reinterpret_cast<const DasNeutronEvent *>(packet->data);
        for (uint32_t i = 0; i < packet->payload_length/sizeof(DasNeutronEvent); i++, event++) {
            ss << i << " tof " << SS_HEX(event->tof) << " pix " << SS_HEX(event->pixelid);
            if (errorOffset == i)
                ss << " *** MISMATCH";
            ss << std::endl;
        }
    } else if (packet->isDataRamp()) {
        const DasNeutronEvent *event = reinterpret_cast<const DasNeutronEvent *>(packet->data);
        for (uint32_t i = 0; i < packet->payload_length/sizeof(DasNeutronEvent); i++, event++) {
            ss << i << " tof " << SS_HEX(event->tof) << " pix " << SS_HEX(event->pixelid);
            if (errorOffset == i)
                ss << " *** MISMATCH " << errorOffset;
            ss << std::endl;
        }
    } else {
        const DasNeutronEvent *event = reinterpret_cast<const DasNeutronEvent *>(packet->data);
        for (uint32_t i = 0; i < min(MAX_EVENTS_PER_PACKET, (unsigned)(packet->payload_length/sizeof(DasNeutronEvent))); i++, event++) {
            ss << i << " tof " << SS_HEX(event->tof) << " pix " << SS_HEX(event->pixelid);
        }
    }

    ss << "bad index = " << errorOffset << std::endl;
    ss << std::endl << std::endl;

    m_dumpStream << ss.str();
}


string AnalyzeOutput::formatTime(time_t t)
{
    char timeStr[100];
    struct tm *tm = localtime(&t);
    strftime(timeStr, sizeof(timeStr), "%Y_%m_%dT%H:%M:%S", tm);
    return std::string(timeStr);
}

double AnalyzeOutput::speedFormatted(double speed) {
    if (speed > 1e9) return speed/1e9;
    else if (speed > 1e6) return speed/1e6;
    else if (speed > 1e3) return speed/1e3;
    else return speed;
}

char AnalyzeOutput::speedTag(double speed) {
    if (speed > 1e9) return 'G';
    else if (speed > 1e6) return 'M';
    else if (speed > 1e3) return 'K';
    else return ' ';
}

void AnalyzeOutput::dumpOccRegs()
{
    ostringstream ss;

    map<uint32_t, string> m;
    uint32_t val;

    m.insert(map<uint32_t,string>::value_type(0, "VERSION"));
    m.insert(map<uint32_t,string>::value_type(0x4, "CONFIG"));
    m.insert(map<uint32_t,string>::value_type(0x8, "STATUS_0"));
    m.insert(map<uint32_t,string>::value_type(0x14, "MODULE_ID"));
    m.insert(map<uint32_t,string>::value_type(0x70, "DQ_ADDR0"));
    m.insert(map<uint32_t,string>::value_type(0x74, "DQ_ADDR1"));
    m.insert(map<uint32_t,string>::value_type(0x80, "DQ_CONS_IDX"));
    m.insert(map<uint32_t,string>::value_type(0x84, "DQ_PROD_IDX"));
    m.insert(map<uint32_t,string>::value_type(0x88, "DQ_LENGTH"));
    m.insert(map<uint32_t,string>::value_type(0x90, "TXBUF_CONS_IDX"));
    m.insert(map<uint32_t,string>::value_type(0x94, "TXBUF_PROD_IDX"));
    m.insert(map<uint32_t,string>::value_type(0x98, "TXBUF_LENGTH"));
    m.insert(map<uint32_t,string>::value_type(0xC0, "INTERRUPT_STATUS"));
    m.insert(map<uint32_t,string>::value_type(0xC4, "INTERRUPT_ENABLE"));
    m.insert(map<uint32_t,string>::value_type(0x100, "DATE_CODE"));
    m.insert(map<uint32_t,string>::value_type(0x300, "PCI_COMMAND_REG"));
    m.insert(map<uint32_t,string>::value_type(0x304, "PCI_DEVICE_CNTL"));
    m.insert(map<uint32_t,string>::value_type(0x308, "PCI_DEVICE_CNTL2"));
    m.insert(map<uint32_t,string>::value_type(0x30C, "PCI_DEVICE_CNTL3"));
    m.insert(map<uint32_t,string>::value_type(0x310, "SYSMON_TEMP"));
    m.insert(map<uint32_t,string>::value_type(0x314, "SYSMON_VCCINT"));
    m.insert(map<uint32_t,string>::value_type(0x318, "SYSMON_VCCAUX"));
    m.insert(map<uint32_t,string>::value_type(0x380, "HW_PKTSIM_1"));
    m.insert(map<uint32_t,string>::value_type(0x384, "HW_PKTSIM_2"));

    map<uint32_t,string>::iterator it = m.begin();

    while (it != m.end())
    {
        if (occ_io_read(m_occ, 0, it->first, &val, 1) != 1)
            throw runtime_error("Failed to read OCC register");

        ss << setfill(' ') << setw(20) << it->second << " 0x" << SS_HEX(val) << endl;
        it++;
    }

    m_dumpStream << ss.str();
}

