#include "AnalyzeOutput.hpp"

#include <signal.h>
#include <iostream>

using namespace std;

static bool shutdown = false;

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
    cout << "  -d, --device-file=FILE   Full path to OCC board device file" << endl;
    cout << "  -o, --output-file=FILE   File to save bad packets to (don't save if not specified)" << endl;
    cout << endl;
}

int main(int argc, char **argv)
{
    const char *devfile = NULL;
    const char *dumpfile = "";
    struct sigaction sigact;

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
    }

    if (devfile == NULL) {
        usage(argv[0]);
        return 1;
    }

    AnalyzeOutput analyzer(devfile, dumpfile);
    analyzer.process();

    return 0;
}
