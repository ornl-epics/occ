/*
 * Copyright (c) 2018 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 *
 * @author Klemen Vodopivec <vodopiveck@ornl.gov>
 */

#ifndef WIN_CONSOLE_H
#define WIN_CONSOLE_H

#include "Window.h"
#include <ncurses.h>
#include <deque>

/**
 * Simple Console window implementation, priting simple text to window.
 */
class WinConsole : public Window {
    private:
        std::deque<std::string> m_logs;
        const size_t m_maxLogs;
    public:
        WinConsole(int y);

        /**
         * Display messages in order they were added top-down.
         */
        void redraw(bool frame=false);

        /**
         * Append message to the end of the FIFO.
         */
        void append(const std::string &msg);
};

#endif // WIN_CONSOLE_H
