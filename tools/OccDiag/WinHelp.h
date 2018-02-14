/*
 * Copyright (c) 2018 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 *
 * @author Klemen Vodopivec <vodopiveck@ornl.gov>
 */

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
        int m_statsLogInt;
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

        /**
         * For on-screen display purposes only.
         */
        void setStopOnBad(bool enable) {
            m_stopOnBad = enable;
        };

        /**
         * For on-screen display purposes only.
         */
        void setStatsLogInt(int interval) {
            m_statsLogInt = interval;
        };

};

#endif // WIN_HELP_H
