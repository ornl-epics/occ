#ifndef GUI_NCURSES_H
#define GUI_NCURSES_H

#include "OccAdapter.h"
#include "WinConsole.h"
#include "WinData.h"
#include "WinHelp.h"
#include "WinRegisters.h"
#include "WinStats.h"

#include <stdint.h>
#include <time.h>
#include <vector>

class GuiNcurses {
    public:
        OccAdapter m_occAdapter;
        double m_runtime;
        bool m_shutdown;
        bool m_paused;
        bool m_rxEnabled;
        bool m_stopOnBad;
        OccAdapter::AnalyzeStats m_cachedStats;

        WinHelp m_winHelp;
        WinConsole m_winConsole;
        WinData m_winData;
        WinRegisters m_winRegisters;
        WinStats m_winStats;
    public:
        GuiNcurses(const char *occDevice, const std::map<uint32_t, uint32_t> &initRegisters);
        ~GuiNcurses();

        void run();
        void shutdown();
        void toggleRx(bool enable);
        void pause(bool pause_);

        /**
         * Show and regenerate all windows.
         */
        void show();

        /**
         * Hide all optional windows and show data window, redraw Help if visible.
         */
        void showDataWin();

        /**
         * Hide all optional windows and show console window, redraw Help if visible.
         */
        void showConsoleWin();

        /**
         * Hide all optional windows and show registers window, redraw Help if visible.
         */
        void showRegistersWin();

        /**
         * Handle user input
         */
        void input();

        /**
         * Return a brief status line, containing run time, OCC status etc.
         *
         * Does not exceed 78 characters.
         */
        std::string getBriefStatus();

        /**
         * Log message
         */
        void log(const char *str, ...);

        /**
         * Update registers window with available register values.
         */
        void updateRegistersWin();

        /**
         * Reset OCC, re-enable RX, make a log entry.
         */
        void resetOcc();
};

#endif // GUI_NCURSES_H
