#include "AnalyzeOutput.hpp"
#include "LabPacket.hpp"

#include <iomanip>
#include <ncurses.h>
#include <sstream>

using namespace std;

#define MAX_EVENTS_PER_PACKET               1800
#define PRINT_RATELIMIT                     1e8 // define how often to print metrics [in ns]

AnalyzeOutput::AnalyzeOutput(const string &devfile, const string &dumpfile) :
    AnalyzeOCC(devfile),
    m_dumpFile(dumpfile),
    m_dumpStream(dumpfile.c_str(), ofstream::out | ofstream::binary),
    m_lastPrintTime(0)
{
    initscr();
}

AnalyzeOutput::~AnalyzeOutput()
{
    m_dumpStream.close();
    endwin();
}

void AnalyzeOutput::analyzePacket(const LabPacket * const packet)
{
    AnalyzeOCC::analyzePacket(packet);

    showMetrics();
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
	double totalThroughput = (m_metrics.total.bytes      - m_lastMetrics.total.bytes)      / runtime;
	double cmdThroughput   = (m_metrics.commands.bytes   - m_lastMetrics.commands.bytes)   / runtime;
	double dataThroughput  = (m_metrics.data.bytes - m_lastMetrics.total.bytes) / runtime;
	double rtdlThroughput  = (m_metrics.rtdl.bytes  - m_lastMetrics.rtdl.bytes)  / runtime;
	double metaThroughput  = (m_metrics.meta.bytes  - m_lastMetrics.meta.bytes)  / runtime;
	double eventThroughput = (m_metrics.event.bytes - m_lastMetrics.event.bytes) / runtime;
	double rampThroughput  = (m_metrics.ramp.bytes  - m_lastMetrics.ramp.bytes)  / runtime;

	mvprintw(0, 0, "OCC incoming packets metrics");
	mvprintw(1, 0, "Total   : %lu packets", m_metrics.total.goodCount    + m_metrics.total.badCount);
	mvprintw(2, 0, "Commands: %lu packets", m_metrics.commands.goodCount + m_metrics.commands.badCount);
	mvprintw(3, 0, "Data    : %lu packets", m_metrics.data.goodCount     + m_metrics.data.badCount);
	mvprintw(4, 0, "|- RTDL : %lu (%lu bad) packets", m_metrics.rtdl.goodCount,  m_metrics.rtdl.badCount);
	mvprintw(5, 0, "|- Meta : %lu (%lu bad) packets", m_metrics.meta.goodCount,  m_metrics.meta.badCount);
	mvprintw(6, 0, "|- Event: %lu (%lu bad) packets", m_metrics.event.goodCount, m_metrics.event.badCount);
	mvprintw(7, 0, "|- Ramp : %lu (%lu bad) packets", m_metrics.ramp.goodCount,  m_metrics.ramp.badCount);

	mvprintw(1, 40, "[%6.2f%cp/s %6.2f%cB/s]", speedFormatted(totalSpeed), speedTag(totalSpeed), speedFormatted(totalThroughput), speedTag(totalThroughput));
	mvprintw(2, 40, "[%6.2f%cp/s %6.2f%cB/s]", speedFormatted(cmdSpeed),   speedTag(cmdSpeed),   speedFormatted(cmdThroughput),   speedTag(cmdThroughput));
	mvprintw(3, 40, "[%6.2f%cp/s %6.2f%cB/s]", speedFormatted(dataSpeed),  speedTag(dataSpeed),  speedFormatted(dataThroughput),  speedTag(dataThroughput));
	mvprintw(4, 40, "[%6.2f%cp/s %6.2f%cB/s]", speedFormatted(rtdlSpeed),  speedTag(rtdlSpeed),  speedFormatted(rtdlThroughput),  speedTag(rtdlThroughput));
	mvprintw(5, 40, "[%6.2f%cp/s %6.2f%cB/s]", speedFormatted(metaSpeed),  speedTag(metaSpeed),  speedFormatted(metaThroughput),  speedTag(metaThroughput));
	mvprintw(6, 40, "[%6.2f%cp/s %6.2f%cB/s]", speedFormatted(eventSpeed), speedTag(eventSpeed), speedFormatted(eventThroughput), speedTag(eventThroughput));
	mvprintw(7, 40, "[%6.2f%cp/s %6.2f%cB/s]", speedFormatted(rampSpeed),  speedTag(rampSpeed),  speedFormatted(rampThroughput),  speedTag(rampThroughput));


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
		for (uint32_t i = 0; i < min(100UL, packet->payload_length / sizeof(uint32_t)); i++) {
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
			DasNeutronEvent *event = reinterpret_cast<DasNeutronEvent *>(packet->data[i]);
			ss << i << " tof " << SS_HEX(event->tof) << " pix " << SS_HEX(event->pixelid);
			if (errorOffset == i)
				ss << " *** MISMATCH";
			ss << std::endl;
		}
	} else if (packet->isDataEvent()) {
		for (uint32_t i = 0; i < packet->payload_length/sizeof(DasNeutronEvent); i++) {
			DasNeutronEvent *event = reinterpret_cast<DasNeutronEvent *>(packet->data[i]);
			ss << i << " tof " << SS_HEX(event->tof) << " pix " << SS_HEX(event->pixelid);
			if (errorOffset == i)
				ss << " *** MISMATCH";
			ss << std::endl;
		}
	} else if (packet->isDataRamp()) {
		for (uint32_t i = 0; i < packet->payload_length/sizeof(DasNeutronEvent); i++) {
			DasNeutronEvent *event = reinterpret_cast<DasNeutronEvent *>(packet->data[i]);
			ss << i << " tof " << SS_HEX(event->tof) << " pix " << SS_HEX(event->pixelid);
			if (errorOffset == i)
				ss << " *** MISMATCH " << errorOffset;
			ss << std::endl;
		}
	} else {
		for (uint32_t i = 0; i < min((unsigned long)MAX_EVENTS_PER_PACKET, packet->payload_length/sizeof(DasNeutronEvent)); i++) {
			DasNeutronEvent *event = reinterpret_cast<DasNeutronEvent *>(packet->data[i]);
			ss << i << " tof " << SS_HEX(event->tof) << " pix " << SS_HEX(event->pixelid);
		}
	}

	ss << "bad index = " << errorOffset << std::endl;
	ss << std::endl << std::endl;

    m_dumpStream << ss;
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
