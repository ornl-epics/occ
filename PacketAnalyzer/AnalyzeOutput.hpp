#ifndef ANALYZEOUTPUT_HPP
#define ANALYZEOUTPUT_HPP

#include "AnalyzeOCC.hpp"

#include <fstream>

class LabPacket;

class AnalyzeOutput : public AnalyzeOCC
{
    public:
        AnalyzeOutput(const std::string &devfile, const std::string &dumpfile);
        ~AnalyzeOutput();

        void analyzePacket(const LabPacket * const packet);
        void dumpPacket(const LabPacket * const packet, uint32_t errorOffset);
    private:
        std::string m_dumpFile;
        std::ofstream m_dumpStream;
        uint64_t m_lastPrintTime;
        struct metrics m_lastMetrics;

        // TODO
        int m_datasync;
        int m_metatsync;
        int m_metasubpacket;
        int m_datatsync;
        int m_datasubpacket;

        void showMetrics();
        std::string formatTime(time_t t);
        double speedFormatted(double speed);
        char speedTag(double speed);
};

#endif //ANALYZEOUTPUT_HPP
