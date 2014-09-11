#include <occlib.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <cstdio>
#include <iostream>

#define OCC_MAX_PACKET_SIZE 1800
#define TX_MAX_SIZE         1800        // Maximum packet size in bytes to be send over OCC
#define RX_BUF_SIZE         1800        // Size in bytes of the receive buffer e
#define OCC_SOURCE          0x1         // Id to used as source field in outgoing messages
#define OCC_DESTINATION     0x2         // Id to used as destination field in outgoing messages
#define OCC_INFO            0x10000000  // Info field in outgoing messages

using namespace std;

static bool shutdown = false;

static void usage(const char *progname) {
    cout << "Usage: " << progname << " [OPTIONS]" << endl;
    cout << "occ_dump tool dumps data as received from OCC board to a file" << endl;
    cout << endl;
    cout << "Options:" << endl;
    cout << "  -d, --device-file FILE   Full path to OCC board device file" << endl;
    cout << "  -o, --output-file FILE   Filename to store incoming data or - for stdout" << endl;
    cout << endl;
}

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

/**
 * Receive data from OCC board and dump it to a file.
 *
 * Function stops when the shutdown global flag gets
 * set.
 */
uint64_t receive_from_occ(struct occ_handle *occ, int outfd) {
    uint64_t rxbytes = 0;

    if (occ_enable_rx(occ, true) != 0) {
        cerr << "ERROR: cannot enable RX" << endl;
        return 0;
    }

    while (!shutdown) {
        unsigned char *data = NULL;
        size_t datalen = 0;

        int ret = occ_data_wait(occ, reinterpret_cast<void **>(&data), &datalen, 100);
        if (ret != 0) {
            if (ret == -ETIME || ret == -EINTR)
                continue;
            cerr << "ERROR: cannot read from OCC device - " << strerror(-ret) << ret << endl;
            break;
        }

        ret = write(outfd, data, datalen);
        if (ret == -1) {
            cerr << "ERROR: cannot write to output file - " << strerror(errno) << outfd << endl;
            break;
        }

        ret = occ_data_ack(occ, ret);
        if (ret != 0) {
            cerr << "ERROR: cannot advance consumer index - " << strerror(-ret) << endl;
            break;
        }

        rxbytes += datalen;
    }

    if (occ_enable_rx(occ, false) != 0) {
        cerr << "ERROR: cannot disable RX" << endl;
    }

    return rxbytes;
}

int main(int argc, char **argv) {
    struct sigaction sigact;
    const char *outfile = NULL;
    const char *devfile = NULL;
    struct occ_handle *occ;
    uint64_t rxbytes;
    int outfd;

    for (int i = 1; i < argc; i++) {
        string key(argv[i]);

        if (key == "-h" || key == "--help") {
            usage(argv[0]);
            return 0;
        }
        if (key == "-d" || key == "--device-file") {
            if ((i + 1) >= argc)
                break;
            devfile = argv[++i];
        }
        if (key == "-o" || key == "--output-file") {
            if ((i + 1) >= argc)
                break;
            outfile = argv[++i];
        }
    }
    if (devfile == NULL || outfile == NULL) {
        usage(argv[0]);
        return 3;
    }

    if (string(outfile) == "-") {
        outfd = fileno(stdout);
    } else {
        outfd = open(outfile, O_WRONLY | O_CREAT, S_IRUSR);
        if (outfd == -1) {
            cerr << "ERROR: cannot open output file" << endl;
            return 3;
        }
    }

    if (occ_open(devfile, OCC_INTERFACE_OPTICAL, &occ) != 0) {
        cerr << "ERROR: cannot initialize OCC interface" << endl;
        return 3;
    }

    if (occ_enable_rx(occ, 1) != 0) {
        cerr << "ERROR: cannot enable RX on OCC interface" << endl;
        return 3;
    }

    sigact.sa_handler = &sighandler;
    sigact.sa_flags = 0;
    sigemptyset(&sigact.sa_mask);
    sigaction(SIGTERM, &sigact, NULL);
    sigaction(SIGINT, &sigact, NULL);

    rxbytes = receive_from_occ(occ, outfd);
    occ_close(occ);

    cout << "Received and saved " << rxbytes << " bytes" << endl;

    return 0;
}
