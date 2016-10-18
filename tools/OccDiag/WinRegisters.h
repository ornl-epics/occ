#ifndef WIN_REGISTERS_H
#define WIN_REGISTERS_H

#include "Window.h"

#include <map>
#include <string>

/**
 * Window to display data for analysis
 */
class WinRegisters : public Window {
    private:
        /**
         * Register offset,value pairs.
         */
        std::map<uint32_t, uint32_t> m_registers;
    public:
        WinRegisters(int y);

        /**
         * Display registers in HEX, starting with address followed by 4 dwords.
         */
        void redraw(bool frame=false);

        void setRegisters(const std::map<uint32_t, uint32_t> &registers) {
            m_registers = registers;
        };
};

#endif // WIN_REGISTERS_H
