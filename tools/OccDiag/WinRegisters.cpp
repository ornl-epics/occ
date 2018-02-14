/*
 * Copyright (c) 2018 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 *
 * @author Klemen Vodopivec <vodopiveck@ornl.gov>
 */

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
        if (x > height) {
            y += 20;
            x = 1;
        }
        mvwprintw(m_window, x%(height+1), y, "0x%04X: 0x%08X ", it->first, it->second);
        x++;
    }

    Window::redraw(frame);
}
