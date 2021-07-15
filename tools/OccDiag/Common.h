/*
 * Copyright (c) 2018 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 *
 * @author Klemen Vodopivec <vodopiveck@ornl.gov>
 */

#ifndef COMMON_H
#define COMMON_H

#include <time.h>

#define TEXT_COLOR_WHITE   1
#define TEXT_COLOR_RED     2
#define TEXT_COLOR_CYAN    3
#define TEXT_COLOR_YELLOW  4

#define mvwprintw_c(win, color, x, y, format, ...) { \
    if (has_colors() == TRUE) { \
        wattron(win, COLOR_PAIR(color)); \
        mvwprintw(win, x, y, format, __VA_ARGS__); \
        wattroff(win, COLOR_PAIR(color)); \
    } else { \
        mvwprintw(win, x, y, format, __VA_ARGS__); \
    } \
}
#define wprintw_c(win, color, format, ...) { \
    if (has_colors() == TRUE) { \
        wattron(win, COLOR_PAIR(color)); \
        wprintw(win, format, __VA_ARGS__); \
        wattroff(win, COLOR_PAIR(color)); \
    } else { \
        wprintw(win, format, __VA_ARGS__); \
    } \
}

class Common {
    public:
        /**
         * Helper function returns true if time since start + timeout has expired.
         */
        static bool timeExpired(const struct timespec &start, double timeout);

        /**
         * Helper function to calculate time difference in seconds.
         */
        static double timeDiff(const struct timespec &left, const struct timespec &right);
};

#endif // COMMON_H
