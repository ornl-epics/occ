#include "WinHelp.h"

WinHelp::WinHelp()
    : Window("Help", 0)
{}

void WinHelp::show()
{
    if (!m_window) {
        int rows = 8; // Number of lines of text in redraw()
        int cols = 50; // Max length of any line
        int width, height;
        getmaxyx(stdscr, height, width);

        m_window = newwin(rows+2, cols+2, height-rows-2, 0);
    }
    redraw(true);
}

void WinHelp::redraw(bool frame)
{
    if (!m_window)
        return;

    box(m_window, 0, 0);
    mvwprintw(m_window,  1, 1, "b - pause on bad packet (%s)   ", (m_stopOnBad ? "enabled" : "disabled"));
    mvwprintw(m_window,  2, 1, "d - show data");
    mvwprintw(m_window,  3, 1, "i - show registers");
    mvwprintw(m_window,  4, 1, "c - show console");
    mvwprintw(m_window,  5, 1, "p - pause/unpause processing");
    mvwprintw(m_window,  6, 1, "s - stop/continue processing, toggles RX");
    mvwprintw(m_window,  7, 1, "r - restart, clear all counters, reset OCC");
    mvwprintw(m_window,  8, 1, "q - quit");

    Window::redraw(frame);
}
