/* replay.cpp
 *
 * Copyright (c) 2015 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 *
 * @author Klemen Vodopivec
 * @date November 2015
 */

#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <cinttypes>

using namespace std;

static void usage(const char *progname) {
    cout << "Usage: " << progname << " [OPTIONS] filename" << endl;
    cout << "Throtle raw OCC data using RTDL timing from data packets" << endl;
    cout << endl;
    cout << "Options:" << endl;
    cout << "  -m, --meta          Use meta data packets for time reference rather than neutrons" << endl;
    cout << "  -s, --speed SPEED   Fast forward by dividing the time by this number" << endl;
    cout << "                      Default is 1, use 0 for as fast as possible" << endl;
    cout << endl;
}

template<typename S, typename N>
double timediff(S sec1, N nsec1, S sec2, N nsec2) {
    double diff = sec2 - sec1;
    if (nsec2 > nsec1) {
        diff += (nsec2 - nsec1) / 1000000000.0;
    } else {
        diff -= (nsec1 - nsec2) / 1000000000.0;
    }
    return diff;
}

void process(int fd, float speed, bool meta) {
    uint32_t buffer[50*1024]; // Must be good for single packet, legacy DSP puts max ~32000 bytes
    ssize_t count;
    uint32_t rtdlSec = 0;
    uint32_t rtdlNsec = 0;
    struct timespec lastTime;
    lastTime.tv_sec = 0;
    lastTime.tv_nsec = 0;
    uint32_t pktType = (meta ? 0x8 : 0xC);

    while (1) {
        count = read(fd, buffer, 24);
        if (count < 24 || (size_t)count > sizeof(buffer)) break;
        count = read(fd, &buffer[6], buffer[3]);
        if (count != buffer[3]) break;

        // Act only on data packets with RTDL timing info
        if ((buffer[2] & 0xA000000C) == pktType && speed > 0) {

            // Skip sleep for first qualified packet
            if (lastTime.tv_sec > 0) {

                // Calculate the requested time difference
                double rtdlDiff = timediff(rtdlSec, rtdlNsec, buffer[6], buffer[7]);

                // Pass same time packets immediately
                if (rtdlDiff > 0.0) {

                    // How much time we used in processing already
                    struct timespec now;
                    clock_gettime(CLOCK_MONOTONIC, &now);
                    double passed = timediff(lastTime.tv_sec, lastTime.tv_nsec, now.tv_sec, now.tv_nsec);
                    double remain = (rtdlDiff/speed) - passed;

                    // Sleep
                    if (remain > 0.0)
                        usleep(floor(remain * 1000000));
                }
            }

            // Adjust for next time
            clock_gettime(CLOCK_MONOTONIC, &lastTime);
            rtdlSec = buffer[6];
            rtdlNsec = buffer[7];
        }

        // Now write the packet to stdout
        write(STDOUT_FILENO, buffer, 24+count);
    }

    cerr << "process() done" << endl;
}

int main(int argc, char **argv) {
    const char *filename = NULL;
    float speed = 1;
    int fd;
    bool meta = false;

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    for (int i = 1; i < argc-1; i++) {
        string key(argv[i]);

        if (key == "-h" || key == "--help") {
            usage(argv[0]);
            return 0;
        }
        if (key == "-s" || key == "--speed") {
            if ((i + 1) >= argc)
                break;
            speed = atof(argv[++i]);
            if (speed < 0)
                speed = 0.0;
        }
        if (key == "-m" || key == "--meta") {
            meta = true;
        }
    }
    filename = argv[argc-1];

    if (string(filename) == "-") {
        fd = STDIN_FILENO;
    } else {
        fd = open(filename, O_RDONLY);
        if (fd == -1) {
            cerr << "ERROR: cannot open output file" << endl;
            return 3;
        }
    }

    process(fd, speed, meta);

    close(fd);
    return 0;
}
