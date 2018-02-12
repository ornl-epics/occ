#include "Common.h"
#include "LabPacket.h"
#include "WinStats.h"

#include <cstring>
#include <map>
#include <sstream>

#define __STDC_FORMAT_MACROS // Bring in PRIu64 like macros
#include <inttypes.h>

WinStats::WinStats(int y, int height)
    : Window("Incoming OCC packets stats", y, height)
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

std::string WinStats::generateReportLine(const std::string &title, const WinStats::AnalyzeStats &stats)
{
    char buffer[128];

    std::string s1 = formatRate(stats.rate, "pkt/s");
    std::string s2 = formatRate(stats.throughput, "B/s");

    // Print the second half of the line first, will padd beginning with spaces
    snprintf(buffer, sizeof(buffer), "%50s[%s %s]", " ", formatRate(stats.rate, "pkt/s").c_str(), formatRate(stats.throughput, "B/s").c_str());

    // Now overwrite the beginning and stich it together
    snprintf(buffer, 50, "%-10s: %" PRIu64 " good, %" PRIu64 " bad packets", title.c_str(), stats.good, stats.bad);
    buffer[strlen(buffer)] = ' ';

    return std::string(buffer);
}

std::vector<std::string> WinStats::generateReport()
{
    std::vector<std::string> lines;

    // Static map, populate the first time this functions is called
    static std::map<LabPacket::PacketType, std::string> packetTypes;
    if (packetTypes.empty()) {
        packetTypes[LabPacket::TYPE_ERROR] = "Error";
        packetTypes[LabPacket::TYPE_DAS_RTDL] = "DAS RTDL";
        packetTypes[LabPacket::TYPE_DAS_DATA] = "DAS data";
        packetTypes[LabPacket::TYPE_DAS_CMD] = "DAS cmd";
        packetTypes[LabPacket::TYPE_ACC_TIME] = "ACC timing";
    }

    for (auto it = m_totalStats.begin(); it != m_totalStats.end(); it++) {
        std::string name;
        if (packetTypes.find(it->first) != packetTypes.end()) {
            name = packetTypes[it->first];
        } else {
            std::ostringstream ss;
            ss << "Pkt type " << it->first;
            name = ss.str();
        }
        lines.push_back( generateReportLine(name,  it->second) );
    }

    return lines;
}

void WinStats::update(const OccAdapter::AnalyzeStats &stats)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double period = Common::timeDiff(now, m_lastUpdate);
    m_lastUpdate = now;

    for (auto it=m_totalStats.begin(); it!=m_totalStats.end(); it++) {
        it->second.rate = 0;
        it->second.throughput = 0;
    }
    for (auto it = stats.good.begin(); it != stats.good.end(); it++) {
        m_totalStats[it->first].good += it->second;
        m_totalStats[it->first].rate += it->second;
        m_combinedStats.good += it->second;
        m_combinedStats.rate += it->second;
    }
    for (auto it = stats.bad.begin(); it != stats.bad.end(); it++) {
        m_totalStats[it->first].bad  += it->second;
        m_totalStats[it->first].rate += it->second;
        m_combinedStats.bad += it->second;
        m_combinedStats.rate += it->second;
    }
    uint64_t combinedBytes = 0;
    for (auto it = m_totalStats.begin(); it != m_totalStats.end(); it++) {
        it->second.rate /= period;
        auto jt = stats.bytes.find(it->first);
        if (jt != stats.bytes.end()) {
            it->second.throughput = jt->second / period;
            combinedBytes += jt->second;
        }
    }
    m_combinedStats.rate /= period;
    m_combinedStats.throughput = combinedBytes / period;
}

void WinStats::clear()
{
    for (auto it=m_totalStats.begin(); it!=m_totalStats.end(); it++) {
        it->second.clear();
    }
}

WinStats::AnalyzeStats WinStats::getCombinedStats()
{
    return m_combinedStats;
}
