#include "Common.h"
#include "LabPacket.h"
#include "WinData.h"

WinData::WinData(int y)
    : Window("Analyze data", y)
{
    setFooter("[h]elp");
}

void WinData::redraw(bool frame)
{
    static const size_t dwordsPerLine = 4;

    if (!m_window)
        return;

    if (m_addrPacket) {
        int width, height;
        getSize(width, height);

        // Calculate starting address - display 3 lines of previous packet unless
        // error address can't fit on the same screen
        const void *addr = (const char *)m_addrPacket - 3*dwordsPerLine*sizeof(uint32_t);
        if (m_addrError) {
            int line = 3 + (uint32_t)((const char *)m_addrError - (const char *)m_addrPacket) / (dwordsPerLine*sizeof(uint32_t));
            if (line > height) {
                addr = (const char *)m_addrError - (height-2)*dwordsPerLine*sizeof(uint32_t);
            }
        }
        addr = (const char *)addr + m_lineOffset*4*sizeof(uint32_t);

        LabPacket packet(m_addrPacket);
        size_t packetLen = packet.getLength();
        if (packetLen == 0)
            packetLen = m_size;

        // Print memory address followed by 4 dwords in hex
        for (int i=0; i<height; i++) {
            uint8_t pos = 19;
            mvwprintw(m_window, i+1, 1, "%016p: ", addr);

            for (size_t j=0; j<dwordsPerLine; j++) {
                if (addr >= m_addrBase && addr < ((const char *)m_addrBase + m_size)) {
                    if (addr == m_addrError) {
                        mvwprintw_c(m_window, TEXT_COLOR_RED, i+1, pos, "0x%08X ", *(const uint32_t *)addr);
                    } else if (addr >= m_addrPacket && addr < ((const char *)m_addrPacket + packetLen)) {
                        mvwprintw_c(m_window, TEXT_COLOR_YELLOW, i+1, pos, "0x%08X ", *(const uint32_t *)addr);
                    } else {
                        mvwprintw(m_window, i+1, pos, "0x%08X ", *(const uint32_t *)addr);
                    }
                }
                pos += 11;
                addr = (const char *)addr + 4;
            }
        }
    }

    Window::redraw(frame);
}

void WinData::setAddr(const void *addrBase, size_t size, const void *addrPacket, const void *addrError)
{
    m_addrBase = addrBase;
    m_size = size;
    m_addrPacket = addrPacket;
    m_addrError = addrError;
    m_lineOffset = 0;
}
