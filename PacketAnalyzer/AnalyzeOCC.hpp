#ifndef ANALYZEOCC_HPP
#define ANALYZEOCC_HPP

#include <stdint.h>
#include <string>

// Forward declarations
struct occ_handle;
class LabPacket;

class AnalyzeOCC
{
    public:
        AnalyzeOCC(const std::string &devfile);
        virtual ~AnalyzeOCC();

        void process();
        virtual void analyzePacket(const LabPacket * const packet);
        virtual void dumpPacket(const LabPacket * const packet, uint32_t errorOffset) {};
    protected:
        struct counter {
            uint64_t bytes;
            uint64_t goodCount;
            uint64_t badCount;
        };

        struct metrics {
            struct counter total;

            struct counter commands;
            struct counter data;

            struct counter rtdl;
            struct counter meta;
            struct counter event;
            struct counter ramp;
            struct counter other;
        };
        struct metrics m_metrics;

    private:
        struct occ_handle *m_occ;
        uint32_t m_rampCounter;

        std::string occErrorString(int error);

};

#endif // ANALYZEOCC_HPP
