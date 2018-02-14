/*
 * Copyright (c) 2018 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 *
 * @author Klemen Vodopivec <vodopiveck@ornl.gov>
 */

#ifndef WINDOW_H
#define WINDOW_H

#include <ncurses.h>
#include <string>

/**
 * Generic Window class providing common methods for displaying ncurses window.
 *
 * ncurses window can be positioned and dimensioned only when created. This
 * class implements resizing through show()/hide() functions.
 */
class Window {
    protected:
        std::string m_title;
        std::string m_footer;
        int m_y;
        int m_height;
        WINDOW *m_window;
    public:
        /**
         * Constructor
         *
         * @param[in] title of the window, included in the upper border
         * @param[in] y position of window.
         * @param[in] height of window, stretch to fit when 0.
         */
        Window(const std::string &title, int y, int height=0);

        /**
         * Is window visible?
         */
        virtual bool isVisible();

        /**
         * Toggle window on/off.
         */
        virtual void toggle();

        /**
         * Show window.
         *
         * Creates ncurses window by calculating new position and dimensions.
         * y position is defined in constructor, x is always 0. Dimensions are
         * calculated to fill the screen.
         */
        virtual void show();

        /**
         * Hide window.
         *
         * Destroys ncurses object to allow arbitrary size the next time it's
         * shown.
         */
        virtual void hide();

        /**
         * Redraw window contents.
         *
         * Default implementation only draws border and title.
         */
        virtual void redraw(bool frame=true);

        /**
         * Return text to be put on lower border.
         */
        virtual void setFooter(const std::string &footer) { m_footer = footer; };

        /**
         * Return printable size of the window (without borders).
         */
        void getSize(int &width, int &height);
};

#endif // WIN_CONSOLE_H
