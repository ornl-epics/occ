#include "AnalyzeOutput.hpp"

#include <cstdlib>      // for strtof, in contrary to boost::lexical_cast this one is available everywhere
#include <signal.h>
#include <iostream>

using namespace std;

bool shutdown;

static void sighandler(int signal) {
    switch (signal) {
    case SIGTERM:
    case SIGINT:
        shutdown = true;
        break;
    default:
        break;
    }
}

static void usage(const char *progname) {
    cout << "Usage: " << progname << " [OPTION]" << endl;
    cout << "Analyze incoming DAS data. " << endl;
    cout << endl;
    cout << "Options:" << endl;
    cout << "  -d, --device-file FILE   Full path to OCC board device file (defaults to /dev/snsocb0)" << endl;
    cout << "  -o, --output-file FILE   File to save bad packets to (don't save if not specified)" << endl;
    cout << "  -e, --pcie-gen-rate RATE Enable the onboard PCIe FPGA data generator with selected neutron data rate (in multiples of 122,189.64 Hz)" << endl;
    cout << endl;
}

int main(int argc, char **argv)
{
    const char *devfile = "/dev/snsocb0";
    const char *dumpfile = "";
    struct sigaction sigact;
    uint32_t pcie_generator_rate = 0;

    sigact.sa_handler = &sighandler;
    sigact.sa_flags = 0;
    sigemptyset(&sigact.sa_mask);
    sigaction(SIGTERM, &sigact, NULL);
    sigaction(SIGINT, &sigact, NULL);

    for (int i = 1; i < argc; i++) {
        string key(argv[i]);

        if (key == "-h" || key == "--help") {
            usage(argv[0]);
            return 1;
        }
        if (key == "-d" || key == "--device-file") {
            if ((i + 1) >= argc)
                return false;
            devfile = argv[++i];
        }
        if (key == "-o" || key == "--output-file") {
            if ((i + 1) >= argc)
                return false;
            dumpfile = argv[++i];
        }
        if (key == "-e" || key == "--pcie-gen-rate") {
            if ((i + 1) >= argc)
                return false;
            pcie_generator_rate = ::strtoul(argv[++i], NULL, 10);
        }
    }

    if (devfile == NULL) {
        usage(argv[0]);
        return 1;
    }

    try {
        shutdown = false;
        AnalyzeOutput analyzer(devfile, dumpfile);
        if (pcie_generator_rate != 0)
            analyzer.enablePcieGenerator(pcie_generator_rate);
        analyzer.process();
    } catch (exception &e) {
        if (!shutdown)
            cerr << "ERROR: " << e.what() << endl;
    }

    return 0;
}
