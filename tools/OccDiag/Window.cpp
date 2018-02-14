/*
 * Copyright (c) 2018 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 *
 * @author Klemen Vodopivec <vodopiveck@ornl.gov>
 */

#include "Window.h"

Window::Window(const std::string &title, int y, int height)
    : m_title(title)
    , m_y(y)
    , m_height(height)
    , m_window(0)
{}

bool Window::isVisible()
{
    return !!m_window;
}

void Window::toggle()
{
    if (m_window)
        hide();
    else
        show();
}

void Window::show()
{
    if (!m_window) {
        int width, height;
        getmaxyx(stdscr, height, width);

        int rows = (m_height > 0 ? m_height : height - m_y);
        int cols = 80;

        if (rows > 0) {
            m_window = newwin(rows, cols, m_y, 0);
        }
    }
    redraw(true);
}

void Window::hide()
{
    if (!m_window)
        return;

    int width, height;
    getmaxyx(m_window, height, width);

    for (int i=1; i<height-1; i++) {
        mvwprintw(m_window, i, 1, "%*s", width-2, " ");
    }
    wborder(m_window, ' ', ' ', ' ',' ',' ',' ',' ',' ');
    wrefresh(m_window);
    delwin(m_window);
    m_window = 0;
}

void Window::redraw(bool frame)
{
    if (!m_window)
        return;

    if (frame) {
        box(m_window, 0, 0);
        mvwprintw(m_window, 0, 2, m_title.c_str());
        if (!m_footer.empty()) {
            int width, height;
            getmaxyx(m_window, height, width);
            mvwprintw(m_window, height-1, 2, m_footer.substr(0, 78).c_str());
        }
    }
    wrefresh(m_window);
}

void Window::getSize(int &width, int &height)
{
    getmaxyx(m_window, height, width);
    height = std::max(0, height-2);
    width  = std::max(0, width-2);
}
