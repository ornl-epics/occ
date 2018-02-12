#ifndef WIN_DATA_H
#define WIN_DATA_H

#include "Window.h"

/**
 * Window to display data for analysis
 */
class WinData : public Window {
    private:
        const void *m_addrBase;
        const void *m_addrPacket;
        uint32_t m_packetLen;
        const void *m_addrError;
        size_t m_size;
        int m_lineOffset;
    public:
        WinData(int y);

        /**
         * Display data in HEX, starting with address followed by 4 dwords.
         */
        void redraw(bool frame=false);

        void setAddr(const void *addrBase, size_t size, const void *addrPacket, uint32_t packetLen, const void *addrError=0);

        /**
         * Move displayed data up for one line.
         */
        void moveUp()
        {
            m_lineOffset -= 1;
        }

        /**
         * Move displayed data down for one line.
         */
        void moveDown()
        {
            m_lineOffset += 1;
        }
};

#endif // WIN_DATA_H
