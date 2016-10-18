#include "Common.h"
#include "LabPacket.h"
#include "WinStats.h"

#include <cstring>

#define __STDC_FORMAT_MACROS // Bring in PRIu64 like macros
#include <inttypes.h>

WinStats::WinStats(int y, int height)
    : Window("Incoming OCC packets stats", y, height)
    , m_totalStats(7)
{}

void WinStats::redraw(bool frame)
{
    std::vector<std::string> lines = generateReport();
    for (size_t i=0; i<lines.size(); i++) {
        mvwprintw(m_window, i+1, 1, "%s", lines[i].c_str());
    }

    Window::redraw(frame);
}

std::string WinStats::formatRate(double rate, const std::string &suffix)
{
    char buffer[128];
    const char *qualifier;

    if (rate > 1e9) {
        rate /= 1e9;
        qualifier = " G";
    } else if (rate > 1e6) {
        rate /= 1e6;
        qualifier = " M";
    } else if (rate > 1e3) {
        rate /= 1e3;
        qualifier = " K";
    } else {
        qualifier = "  ";
    }

    snprintf(buffer, sizeof(buffer), "%6.2f%s%s", rate, qualifier, suffix.c_str());
    return std::string(buffer);
}

std::string WinStats::generateReportLine(const char *title, const WinStats::AnalyzeStats &stats)
{
    char buffer[128];

    std::string s1 = formatRate(stats.rate, "pkt/s");
    std::string s2 = formatRate(stats.throughput, "B/s");

    // Print the second half of the line first, will padd beginning with spaces
    snprintf(buffer, sizeof(buffer), "%50s[%s %s]", " ", formatRate(stats.rate, "pkt/s").c_str(), formatRate(stats.throughput, "B/s").c_str());

    // Now overwrite the beginning and stich it together
    snprintf(buffer, 50, "%-10s: %" PRIu64 " good, %" PRIu64 " bad packets", title, stats.good, stats.bad);
    buffer[strlen(buffer)] = ' ';

    return std::string(buffer);
}

std::vector<std::string> WinStats::generateReport()
{
    std::vector<std::string> lines;

    lines.push_back( generateReportLine("Commands", m_totalStats[LabPacket::TYPE_COMMAND]) );
    lines.push_back( generateReportLine("RTDL",     m_totalStats[LabPacket::TYPE_RTDL]) );
    lines.push_back( generateReportLine("TSYNC",    m_totalStats[LabPacket::TYPE_TSYNC]) );
    lines.push_back( generateReportLine("Metadata", m_totalStats[LabPacket::TYPE_METADATA]) );
    lines.push_back( generateReportLine("Neutron",  m_totalStats[LabPacket::TYPE_NEUTRONS]) );
    lines.push_back( generateReportLine("Ramp",     m_totalStats[LabPacket::TYPE_RAMP]) );
    lines.push_back( generateReportLine("Other",    m_totalStats[LabPacket::TYPE_UNKNOWN]) );

    return lines;
}

void WinStats::update(const OccAdapter::AnalyzeStats &stats)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double period = Common::timeDiff(now, m_lastUpdate);
    m_lastUpdate = now;

    for (auto it=m_totalStats.begin(); it!=m_totalStats.end(); it++) {
        it->rate = 0;
        it->throughput = 0;
    }
    for (size_t i=0; i<stats.good.size(); i++) {
        m_totalStats[i].good += stats.good[i];
        m_totalStats[i].rate += stats.good[i];
    }
    for (size_t i=0; i<stats.bad.size(); i++) {
        m_totalStats[i].bad  += stats.bad[i];
        m_totalStats[i].rate += stats.bad[i];
    }
    for (size_t i=0; i<m_totalStats.size(); i++) {
        m_totalStats[i].rate /= period;
        m_totalStats[i].throughput = stats.bytes[i] / period;
    }
}
