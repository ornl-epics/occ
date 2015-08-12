#include "AnalyzeOutput.hpp"

#include <cstdlib>      // for strtof, in contrary to boost::lexical_cast this one is available everywhere
#include <signal.h>
#include <iostream>

using namespace std;

bool shutdown;
AnalyzeOutput *ao = NULL;

static void sighandler(int signal) {
    switch (signal) {
    case SIGTERM:
    case SIGINT:
        shutdown = true;
        break;
    case SIGUSR1:
        if (ao) ao->dumpOccRegs();
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
    cout << "  -d, --device-file FILE   Full path to OCC board device file" << endl;
    cout << "                           Defaults to /dev/snsocb0" << endl;
    cout << "  -o, --output-file FILE   File to save bad packets to" << endl;
    cout << "                           Don't save if not specified" << endl;
    cout << "  -e, --pcie-gen-rate RATE Enable the onboard PCIe FPGA data generator with" << endl;
    cout << "                           selected neutron data rate (multiples of 122,189 Hz)" << endl;
    cout << "  -s, --pcie-gen-size SIZE Specify FPGA data generator packet payload size." << endl;
    cout << "                           Unit is dword, number of 2 means 8 bytes of payload or" << endl;
    cout << "                           24+8=32 bytes for complete packet. Default is 1024." << endl;
    cout << "  -n, --no-analyze         Don't analyze packets, only consume them" << endl;
    cout << "  -c, --dmadump            Enable DMA memory dump in case of an error" << endl;
    cout << endl;
}

int main(int argc, char **argv)
{
    const char *devfile = "/dev/snsocb0";
    const char *dumpfile = "";
    struct sigaction sigact;
    uint32_t pcie_generator_rate = 0;
    uint32_t pcie_generator_pkt_size = 1024;
    bool no_analyze = false;
    bool dmadump = false;

    sigact.sa_handler = &sighandler;
    sigact.sa_flags = 0;
    sigemptyset(&sigact.sa_mask);
    sigaction(SIGTERM, &sigact, NULL);
    sigaction(SIGINT, &sigact, NULL);
    sigaction(SIGUSR1, &sigact, NULL);

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
        if (key == "-s" || key == "--pcie-gen-size") {
            if ((i + 1) >= argc)
                return false;
            pcie_generator_pkt_size = ::strtoul(argv[++i], NULL, 10);
        }
        if (key == "-n" || key == "--no-analyze") {
            no_analyze = true;
        }
        if (key == "-m" || key == "--dmadump") {
            dmadump = true;
        }
    }

    if (devfile == NULL) {
        usage(argv[0]);
        return 1;
    }

    try {
        shutdown = false;
        AnalyzeOutput analyzer(devfile, dumpfile, dmadump);
        ao = &analyzer;
        if (pcie_generator_rate != 0)
            analyzer.enablePcieGenerator(pcie_generator_rate, pcie_generator_pkt_size);
        analyzer.process(no_analyze);
    } catch (exception &e) {
        if (!shutdown)
            cerr << "ERROR: " << e.what() << endl;
        return 1;
    }

    return 0;
}
