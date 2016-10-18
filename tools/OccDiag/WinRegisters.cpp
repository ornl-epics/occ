#include "Common.h"
#include "WinRegisters.h"

#include <math.h>

WinRegisters::WinRegisters(int y)
    : Window("OCC registers", y)
{
    setFooter("[h]elp");
}

void WinRegisters::redraw(bool frame)
{
    if (!m_window)
        return;

    int height = ceil(m_registers.size()/4.0);

    int x = 1;
    int y = 1;
    for (auto it=m_registers.begin(); it!=m_registers.end(); it++) {
        mvwprintw(m_window, x%height, y, "0x%04X: 0x%08X ", it->first, it->second);
        if (++x > height) {
            y += 20;
            x = 1;
        }
    }

    Window::redraw(frame);
}
