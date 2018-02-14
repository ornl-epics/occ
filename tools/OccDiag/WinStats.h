/*
 * Copyright (c) 2018 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 *
 * @author Klemen Vodopivec <vodopiveck@ornl.gov>
 */

#ifndef WIN_STATS_H
#define WIN_STATS_H

#include "OccAdapter.h"
#include "Window.h"

#include <time.h>
#include <vector>

/**
 * Window to display data for analysis
 */
class WinStats : public Window {
    public:
            struct AnalyzeStats {
                uint64_t good;
                uint64_t bad;
                double rate;
                double throughput;
                AnalyzeStats()
                {
                    clear();
                }
                void clear()
                {
                    good = 0;
                    bad = 0;
                    rate = 0.0;
                    throughput = 0.0;
                }
            };

    private:
        std::map<Packet::Type, AnalyzeStats> m_totalStats;
        AnalyzeStats m_combinedStats;
        struct timespec m_lastUpdate;

    public:

        WinStats(int x, int height);

        /**
         * Display received packets stats
         */
        void redraw(bool frame=false);

        /**
         * Format a floating number.
         * Example: 23,487,297 => 23.48 Mpkt/s
         */
        static std::string formatRate(double rate, const std::string &suffix);

        /**
         * Generate a single line of report, taking numbers from stats parameter.
         */
        static std::string generateReportLine(const std::string &title, const AnalyzeStats &stats);

        /**
         * Generates 9 lines of packet statistics displaying numbers from m_stats.
         */
        std::vector<std::string> generateReport();

        /**
         * Push newly gathered statistics.
         *
         * @param[in] stats Gathered statistics since last time this function was called.
         */
        void update(const OccAdapter::AnalyzeStats &stats);

        /**
         * Reset counters to 0.
         */
        void clear();

        /**
         * Retrieve good/bad packets stats.
         */
        AnalyzeStats getCombinedStats();
};

#endif // WIN_STATS_H
