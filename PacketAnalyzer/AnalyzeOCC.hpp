#ifndef ANALYZEOCC_HPP
#define ANALYZEOCC_HPP

#include "OccAdapter.hpp"

#include <stdint.h>
#include <string>

// Forward declarations
class LabPacket;

class AnalyzeOCC : public OccAdapter
{
    public:
        AnalyzeOCC(const std::string &devfile);

        void process(bool no_analyze);
        virtual void showMetrics() {};
        virtual void dumpPacket(const LabPacket * const packet, uint32_t errorOffset) {};
    protected:
        struct counter {
            uint64_t bytes;
            uint64_t goodCount;
            uint64_t badCount;
            counter() :
                bytes(0),
                goodCount(0),
                badCount(0)
            {}
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
        uint32_t m_rampCounter;

        void analyzePacket(const LabPacket * const packet);
};

#endif // ANALYZEOCC_HPP
