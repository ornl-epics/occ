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
        const void *m_addrError;
        size_t m_size;
    public:
        WinData(int y);

        /**
         * Display data in HEX, starting with address followed by 4 dwords.
         */
        void redraw(bool frame=false);

        void setAddr(const void *addrBase, size_t size, const void *addrPacket, const void *addrError=0);
};

#endif // WIN_DATA_H
