/*
 * Copyright (c) 2018 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 *
 * @author Klemen Vodopivec <vodopiveck@ornl.gov>
 */

#include "Common.h"

#include <inttypes.h>

bool Common::timeExpired(const struct timespec &start, double timeout)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (timeDiff(now, start) > timeout);
}

double Common::timeDiff(const struct timespec &left, const struct timespec &right)
{
    int64_t sl = left.tv_sec;
    int32_t nl = left.tv_nsec;
    int64_t sr = right.tv_sec;
    int32_t nr = right.tv_nsec;
    double sdiff = sl - sr;
    sdiff += (nl - nr)/1e9;
    return sdiff;
}
