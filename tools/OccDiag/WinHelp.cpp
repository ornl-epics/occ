/*
 * Copyright (c) 2018 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 *
 * @author Klemen Vodopivec <vodopiveck@ornl.gov>
 */

#include "WinHelp.h"

WinHelp::WinHelp()
    : Window("Help", 0)
    , m_statsLogInt(0)
{}

void WinHelp::show()
{
    if (!m_window) {
        int rows = 9; // Number of lines of text in redraw()
        int cols = 50; // Max length of any line
        int width, height;
        getmaxyx(stdscr, height, width);

        m_window = newwin(rows+2, cols+2, height-rows-2, 0);
    }
    redraw(true);
}

void WinHelp::redraw(bool frame)
{
    char statsLogEn[16];
    if (!m_window)
        return;

    if (m_statsLogInt > 0)
        snprintf(statsLogEn, sizeof(statsLogEn), "every %ds", m_statsLogInt);
    else
        snprintf(statsLogEn, sizeof(statsLogEn), "disabled");

    box(m_window, 0, 0);
    mvwprintw(m_window,  1, 1, "b - pause on bad packet (%s)   ", (m_stopOnBad ? "enabled" : "disabled"));
    mvwprintw(m_window,  2, 1, "d - show data");
    mvwprintw(m_window,  3, 1, "i - show registers");
    mvwprintw(m_window,  4, 1, "l - toggle statistics log lines (%s)    ", statsLogEn);
    mvwprintw(m_window,  5, 1, "c - show console");
    mvwprintw(m_window,  6, 1, "p - pause/unpause processing");
    mvwprintw(m_window,  7, 1, "s - stop/continue processing, toggles RX");
    mvwprintw(m_window,  8, 1, "r - restart, clear all counters, reset OCC");
    mvwprintw(m_window,  9, 1, "q - quit");

    Window::redraw(frame);
}
