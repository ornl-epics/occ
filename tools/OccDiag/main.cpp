/*
 * Copyright (c) 2018 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 *
 * @author Klemen Vodopivec <vodopiveck@ornl.gov>
 */

#include "GuiNcurses.h"

#include <signal.h>
#include <iostream>
#include <unistd.h>
#include <sys/stat.h>

#include <string>
#include <vector>

GuiNcurses *analyzer = NULL;

static void sighandler(int signal) {
    switch (signal) {
    case SIGTERM:
    case SIGINT:
        analyzer->shutdown();
        break;
    default:
        break;
    }
}

static void usage(const char *progname) {
    std::cout << "Usage: " << progname << " [OPTIONS] <DEVICE>" << std::endl;
    std::cout << "" << std::endl;
    std::cout << "OCC diagnostics verifies and shows received optical packets." << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -l <interval>   Periodically print statistics log, interval in seconds" << std::endl;
    std::cout << "  -o              Enable DAS 1.0 style packets" << std::endl;
    std::cout << "  -r <addr> <val> Set register value on startup and on reset." << std::endl;
    std::cout << "  -t <rate>       Enable test pattern at specified rate MB/s " << std::endl;
    std::cout << "                  (short for '-r 0x380 0x34000400 -r 0x384 <raw rate>')" << std::endl;
    std::cout << std::endl;
    std::cout << "Example: enable internal packet simulator with approx rate 0.5MB/s" << std::endl;
    std::cout << "  "<< progname << " /dev/occ1 -r 0x380 0x3E000E00 -r 0x384 0xFF" << std::endl;
    std::cout << std::endl;
}

static std::vector<std::string> findDevices()
{
    char buffer[20];
    struct stat st;
    std::vector<std::string> devices;
    for (int i=0; i<20; i++) {
        snprintf(buffer, sizeof(buffer), "/dev/snsocc%d", i);
        if (stat(buffer, &st) == 0)
            devices.push_back(buffer);
    }
    return devices;
}

int main(int argc, char **argv)
{
    const char *devfile = NULL;
    struct sigaction sigact;
    std::map<uint32_t, uint32_t> registers;
    uint32_t statsInt = 0;
    bool oldpkts = false;

    sigact.sa_handler = &sighandler;
    sigact.sa_flags = 0;
    sigemptyset(&sigact.sa_mask);
    sigaction(SIGTERM, &sigact, NULL);
    sigaction(SIGINT, &sigact, NULL);
    sigaction(SIGUSR1, &sigact, NULL);

    for (int i = 1; i < argc; i++) {
        std::string key(argv[i]);

        if (key == "-h" || key == "--help") {
            usage(argv[0]);
            return 1;
        }
        if (key == "-r") {
            uint32_t offset, value;
            if ((i + 2) >= argc) {
                usage(argv[0]);
                return 1;
            }

            if (std::string(argv[++i]).substr(0, 2) == "0x")
                offset = ::strtoul(argv[i], NULL, 16);
            else
                offset = ::strtoul(argv[i], NULL, 10);
            if (std::string(argv[++i]).substr(0, 2) == "0x")
                value = ::strtoul(argv[i], NULL, 16);
            else
                value = ::strtoul(argv[i], NULL, 10);
            registers[offset] = value;
        }
        if (key == "-l") {
            if ((i + 2) >= argc) {
                usage(argv[0]);
                return 1;
            }

            statsInt = ::strtoul(argv[++i], NULL, 10);
        }
        if (key == "-o") {
            oldpkts = true;
        }
        if (key == "-t") {
            if ((i + 1) >= argc) {
                usage(argv[0]);
                return 1;
            }

            double testGenRate = std::max(0.1, ::strtod(argv[++i], NULL));
            registers[0x380] = 0x34000400;
            registers[0x384] = (uint32_t)(1000/testGenRate) & 0xFFF;
        }
        if (key[0] != '-') {
            devfile = argv[i];
        }
    }

    if (devfile == NULL) {
        std::vector<std::string> devices = findDevices();
        switch (devices.size()) {
        case 1:
            devfile = devices[0].c_str();
            std::cout << "No devices specified, using the single one available %s" << devfile << std::endl;
            break;
        case 0:
            usage(argv[0]);
            std::cout << "No devices available" << std::endl;
            return 1;
        default:
            usage(argv[0]);
            std::cout << "Devices available" << std::endl;
            for (auto it=devices.begin(); it!=devices.end(); it++) {
                std::cout << "  " << *it << std::endl;
            }
            return 1;
        }
    }

    try {
        analyzer = new GuiNcurses(devfile, oldpkts, registers, statsInt);
        analyzer->run();
        delete analyzer;
    } catch (std::exception &e) {
        delete analyzer;
        std::cout << "ERROR: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
