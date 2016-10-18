#include "WinConsole.h"


WinConsole::WinConsole(int y)
    : Window("Console", y)
    , m_maxLogs(50)
{
    setFooter("[h]elp");
}

void WinConsole::redraw(bool frame)
{
    if (!m_window)
        return;

    int width, height;
    getmaxyx(m_window, height, width);
    if (height > 1) {
        height -= 2;
        width -= 2;
        int off = std::max((int)m_logs.size() - height, 0);
        for (int i=0; i<height && (off+i) < (int)m_logs.size(); i++) {
            // Trim long lines and pad with spaces to overwrite previous lines
            std::string line = m_logs[off+i].substr(0, width-1);
            size_t padLen = std::max(0, width - (int)line.length());
            mvwprintw(m_window, i+1, 1, "%s%*s", line.c_str(), padLen, " ");
        }
        wrefresh(m_window);
    }

    Window::redraw(frame);
}

void WinConsole::append(const std::string &msg)
{
    while (m_logs.size() >= m_maxLogs) {
        m_logs.pop_front();
    }
    m_logs.push_back(msg);

    // Is console opened to be displayed in real-time?
    redraw();
}
