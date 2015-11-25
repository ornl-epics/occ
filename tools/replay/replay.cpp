#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <cmath>
#include <cstdio>
#include <iostream>

using namespace std;

static void usage(const char *progname) {
    cout << "Usage: " << progname << " [OPTIONS] filename" << endl;
    cout << "occ_replay pipes raw OCC file obeying RTDL timing " << endl;
    cout << endl;
    cout << "Options:" << endl;
    cout << "  -s, --speed SPEED   Fast forward by dividing the time by this number" << endl;
    cout << endl;
}

template<typename S, typename N>
double timediff(S sec1, N nsec1, S sec2, N nsec2) {
    double diff = sec2 - sec1;
    if (nsec2 > nsec1) {
        diff += (nsec2 - nsec1) / 1000000000.0;
    } else {
        diff += (1000000000 - nsec2 + nsec1) / 1000000000.0;
    }
    return diff;
}

void process(int fd, int speed) {
    uint32_t buffer[50*1024]; // Legacy DSP puts max ~32000 bytes
    ssize_t count;
    uint32_t rtdlSec = 0;
    uint32_t rtdlNsec = 0;
    struct timespec lastTime;
    lastTime.tv_sec = 0;
    lastTime.tv_nsec = 0;

    while (1) {
        count = read(fd, buffer, 24);
        if (count < 24 || (size_t)count > sizeof(buffer)) break;
        count = read(fd, &buffer[24], buffer[3]);
        if (count != buffer[3]) break;

        // Send first packet ever
        if (lastTime.tv_sec == 0) {
            clock_gettime(CLOCK_MONOTONIC, &lastTime);
        } 

        // Act only on data packets with RTDL timing info
        else if ((buffer[2] & 0x80000008) == 0x8) {
            // Calculate the requested time difference
            double rtdlDiff = speed * timediff(rtdlSec, rtdlNsec, buffer[6], buffer[7]);

            // Pass same time packets immediately
            if (rtdlDiff > 0.0) {

                // How much time we used in processing already
                struct timespec now;
                clock_gettime(CLOCK_MONOTONIC, &now);
                double passed = timediff(lastTime.tv_sec, lastTime.tv_nsec, now.tv_sec, now.tv_nsec);
                double remain = rtdlDiff - passed;
        
                // Sleep
                if (remain > 0.0)
                    usleep(floor(remain * 1000000));
        
                // Adjust for next time
                lastTime.tv_sec = now.tv_sec;
                lastTime.tv_nsec = now.tv_nsec;
            }
        }

        // Now write the packet to stdout
        write(STDOUT_FILENO, buffer, 24+count);
    }
}

int main(int argc, char **argv) {
    const char *filename = NULL;
    int speed = 1;
    int fd;

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
            speed = atoi(argv[++i]);
            if (speed < 1)
                speed = 1;
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

    process(fd, speed);

    close(fd);
    return 0;
}
