#ifndef WIN_HELP_H
#define WIN_HELP_H

#include "Window.h"

/**
 * Help window, floating and possibly overwriting other windows. It's always
 * positioned from bottom left corner up.
 */
class WinHelp : public Window {
    private:
        bool m_stopOnBad;
        uint32_t m_rampGenRate;
    public:
        WinHelp();

        /**
         * Override base implementation and position window bottom-left.
         */
        void show();

        /**
         * Draw help message.
         */
        void redraw(bool frame=false);

        void setStopOnBad(bool enable) {
            m_stopOnBad = enable;
        };

};

#endif // WIN_HELP_H
