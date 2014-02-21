#include <occlib.h>

#include <cstdlib>
#include <errno.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <list>
#include <signal.h>
#include <vector>

#define OCC_MAX_PACKET_SIZE 1800
#define TX_MAX_SIZE         1800        // Maximum packet size in bytes to be send over OCC
#define RX_BUF_SIZE         1800        // Size in bytes of the receive buffer e
#define OCC_SOURCE          0x1         // Id to used as source field in outgoing messages
#define OCC_DESTINATION     0x2         // Id to used as destination field in outgoing messages
#define OCC_INFO            0x10000000  // Info field in outgoing messages

using namespace std;

static bool shutdown = false;

struct program_context {
    const char *device_file;
    const char *input_file;
    const char *output_file;
    unsigned long send_rate;
    unsigned long packet_size;
    unsigned long data_length;
    struct occ_handle *occ;
    list< vector<unsigned char> > queue;
    pthread_mutex_t queLock;

    program_context() :
        device_file(NULL),
        input_file("/dev/urandom"),
        output_file(NULL),
        send_rate(0),
        packet_size(0),
        data_length(0),
        occ(NULL)
    {
        pthread_mutex_init(&queLock, NULL);
    }
};

struct transmit_status {
    const char *error;
    unsigned long n_bytes;

    transmit_status() :
        error(NULL),
        n_bytes(0)
    {}
};

#pragma pack(push)
#pragma pack(4)
struct occ_packet_header {
    uint32_t destination;       //<! Destination id
    uint32_t source;            //<! Sender id
    uint32_t info;              //<! field describing packet type and other info
    uint32_t payload_length;    //<! payload length
    uint32_t reserved1;
    uint32_t reserved2;
};
#pragma pack(pop)

static void usage(const char *progname) {
    cout << "Usage: " << progname << " [OPTION]" << endl;
    cout << "Tool assumes established hardware loopback. Reads data from input file and" << endl;
    cout << "sends it to OCC device at specified rate. Expect the same data on the receive" << endl;
    cout << "side and abort if it differs." << endl;
    cout << endl;
    cout << "Options:" << endl;
    cout << "  -d, --device-file=FILE   Full path to OCC board device file" << endl;
    cout << "  -i, --input-file=FILE    File with data to be sent through OCC (defaults to /dev/urandom)" << endl;
    cout << "  -o, --output-file=FILE   Where to store incoming data (don't save if not specified)" << endl;
    cout << "  -r, --rate=BYTES/S       Send data at this rate (defaults to 0, unlimited)" << endl;
    cout << "  -s, --packet-size=SIZE   Size of each sent packet (defaults to 0, random size)" << endl;
    cout << "  -l, --data-length=LENGTH Approximate amount of data to read from input file (defaults to 0, all data)" << endl;
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

bool parse_args(int argc, char **argv, struct program_context *ctx) {
    for (int i = 1; i < argc; i++) {
        string key(argv[i]);

        if (key == "-h" || key == "--help")
            return false;
        if (key == "-d" || key == "--device-file") {
            if ((i + 1) >= argc)
                return false;
            ctx->device_file = argv[++i];
        }
        if (key == "-i" || key == "--input-file") {
            if ((i + 1) >= argc)
                return false;
            ctx->input_file = argv[++i];
        }
        if (key == "-o" || key == "--output-file") {
            if ((i + 1) < argc)
                ctx->output_file = argv[++i];
        }
        if (key == "-r" || key == "--rate") {
            if ((i + 1) < argc)
                ctx->send_rate = atoi(argv[++i]);
        }
        if (key == "-s" || key == "--packet-size") {
            if ((i + 1) < argc)
                ctx->packet_size = max(atoi(argv[++i]), TX_MAX_SIZE);
        }
        if (key == "-l" || key == "--data-length") {
            if ((i + 1) < argc)
                ctx->data_length = atoi(argv[++i]);
        }
    }

    return true;
}

static inline void thread_sleep(uint64_t ns) {
    struct timespec ts;

    ts.tv_sec  = ns / 1000000000;
    ts.tv_nsec = ns % 1000000000;

    nanosleep(&ts, NULL);
}

/**
 * Slow down (sleep) if the processing rate is too high.
 *
 * From the arguments this function calculates actual rate and compares it to
 * the requested one. In case the actual rate is higher than requested, the
 * function will block to make the effective rate match the requested one.
 */
void ratelimit(unsigned long rate, unsigned long processed, struct timespec *starttime) {
    uint64_t start,now;
    struct timespec ts;

    if (starttime->tv_sec == 0 && starttime->tv_nsec == 0) {
        clock_gettime(CLOCK_MONOTONIC, starttime);
        thread_sleep(1000000);
        return;
    }

    if (rate == 0)
        return;

    start = (starttime->tv_sec * 1000000000) + (starttime->tv_nsec);

    clock_gettime(CLOCK_MONOTONIC, &ts);
    now = (ts.tv_sec * 1000000000) + (ts.tv_nsec);

    unsigned long actual_rate = processed / ((now - start) / 1e9);

    if (actual_rate > rate) {
        uint64_t ns = start + (1000000000 * processed / rate) - now;
        thread_sleep(ns);
    }
}

size_t __occ_align(size_t size) {
    return (size + 7) & ~7;
}

/**
 * Worker function for sending data to OCC.
 *
 * Can be called from pthread_create. It will read data from input file
 * at desired rate and send it to OCC.
 */
static void *send_to_occ(void *arg) {
    struct transmit_status *status = new transmit_status;
    struct program_context *ctx = (struct program_context *)arg;
    ifstream infile(ctx->input_file, ios_base::binary); // don't try to use basic_ifstream<uint8_t> here unless providing a uint8_t specialization on your own
    struct timespec starttime = { 0, 0 };

    if (!infile) {
        status->error = "cannot open input file";
        return status;
    }

    srand(time(NULL));

    unsigned char buffer[TX_MAX_SIZE];
    struct occ_packet_header *hdr = reinterpret_cast<struct occ_packet_header *>(buffer);
    unsigned char *payload = buffer + sizeof(struct occ_packet_header);

    while (!shutdown && infile.good() && (ctx->data_length == 0 || status->n_bytes <= ctx->data_length)) {
        unsigned long packet_size = ctx->packet_size;

        // Pick a random packet size in range [sizeof(hdr), TX_MAX_SIZE]
        if (packet_size == 0)
            packet_size = rand() % (TX_MAX_SIZE - sizeof(struct occ_packet_header)) + sizeof(struct occ_packet_header) + 1;

        // Align packet_size to 8 bytes
        packet_size = __occ_align(packet_size);

        // Would use readsome(), but apparently is implementation specific and
        // with no standard behaviour on EOF. This loop might well run forever
        // on some implementations.
        infile.read(reinterpret_cast<char *>(payload), packet_size - sizeof(struct occ_packet_header));

        hdr->payload_length = infile.gcount();
        hdr->destination = OCC_DESTINATION;
        hdr->source = OCC_SOURCE;
        hdr->info = OCC_INFO;
        hdr->reserved1 = 0;
        hdr->reserved2 = 0;

        if (hdr->payload_length < (packet_size - sizeof(struct occ_packet_header))) {
            uint32_t new_payload_length = __occ_align(hdr->payload_length);
            fill_n(payload + hdr->payload_length, new_payload_length - hdr->payload_length, 0);
            hdr->payload_length = new_payload_length;
            packet_size = sizeof(struct occ_packet_header) + hdr->payload_length;
        }

        // Enqueue one packet
        vector<unsigned char> v(buffer, buffer+packet_size);
        pthread_mutex_lock(&ctx->queLock);
        ctx->queue.push_back(v);
        pthread_mutex_unlock(&ctx->queLock);

#ifdef TRACE
        cout << "occ_send(" << packet_size << ")" << endl;
#endif
        if (occ_send(ctx->occ, buffer, packet_size) != 0)
            break;

#ifdef TRACE
        cout << hex << showbase << setw(4) << uppercase;
        for (size_t i = 0; i < packet_size; i++) {
            cout << (int)(buffer[i] & 0xFF) << " ";
            if (i % 8 == 7)
                cout << endl;
        }
        cout << dec << setw(1) << noshowbase << nouppercase << endl;
#endif

        status->n_bytes += packet_size;

        ratelimit(ctx->send_rate, status->n_bytes, &starttime);
    }

    return status;
}

inline size_t __occ_packet_align(size_t size) {
    return (size + 7) & ~7;
}

bool compareWithSent(struct program_context *ctx, unsigned char *data, size_t datalen) {
    vector<unsigned char> received(data, data + datalen);
    vector<unsigned char> sent;

    pthread_mutex_lock(&ctx->queLock);
    if (!ctx->queue.empty()) {
        sent = ctx->queue.front();
        ctx->queue.pop_front();
    }
    pthread_mutex_unlock(&ctx->queLock);

    return (sent == received);
}

/**
 * Worker function for receiving data from OCC.
 *
 * Can be called from pthread_create. It will read data from OCC device
 * at full speed. If output_file is defined, it will also dump data to
 * that file.
 */
void *receive_from_occ(void *arg) {
    struct transmit_status *status = new transmit_status;
    struct program_context *ctx = (struct program_context *)arg;
    ofstream outfile;

    if (ctx->output_file)
        outfile.open(ctx->output_file, ios_base::binary);

    while (!shutdown) {
        unsigned char *data = NULL;
        size_t datalen = 0;

        int ret = occ_data_wait(ctx->occ, reinterpret_cast<void **>(&data), &datalen, 100);
        if (ret != 0) {
            if (ret == -ETIME)
                continue;
            status->error = "cannot read from OCC device";
            break;
        }

#ifdef TRACE
        cout << "occ_data_wait() => " << datalen << endl;
        cout << hex << showbase << setw(4) << uppercase;
        for (size_t i = 0; i < datalen; i++) {
            cout << (int)(data[i] & 0xFF) << " ";
            if (i % 8 == 7)
                cout << endl;
        }
        cout << dec << setw(1) << noshowbase << nouppercase << endl;
#endif

        size_t remain = datalen;
        while (remain > 0) {
            struct occ_packet_header *hdr = (struct occ_packet_header *)data;
            unsigned char *payload = data + sizeof(struct occ_packet_header);
            size_t packet_len = __occ_packet_align(sizeof(struct occ_packet_header) + hdr->payload_length);
            if (packet_len > OCC_MAX_PACKET_SIZE) {
                // Acknowledge everything but skip processing the rest
                cerr << "Bad packet based on length check (" << packet_len << ">1800), skipping... (" << hex << data << dec << endl;
                remain = 0;
                break;
            }
            if (packet_len > remain)
                break;

            if (outfile.good()) {
                // trash the header when dumping to file
                outfile.write(reinterpret_cast<const char *>(payload), hdr->payload_length);
            }

            if (!compareWithSent(ctx, data, packet_len)) {
                status->error = "Received data mismatch";
                shutdown = true;
                break;
            }

            remain -= packet_len;
            data += packet_len;
        }
        // Adjust to the actual processed data for acknowledgement
        datalen -= remain;

#ifdef TRACE
        cout << "occ_data_ack(" << datalen << ")" << endl;
#endif
        ret = occ_data_ack(ctx->occ, datalen);
        if (ret != 0) {
            status->error = "cannot advance consumer index";
            break;
        }

        status->n_bytes += datalen;
    }

    outfile.close();

    return status;
}

int main(int argc, char **argv) {
    struct program_context ctx;
    pthread_t receive_thread;
    int ret = 0;
    struct transmit_status *send_status = NULL, *receive_status = NULL;
    struct sigaction sigact;

    if (!parse_args(argc, argv, &ctx) || !ctx.device_file || !ctx.input_file) {
        usage(argv[0]);
        return 1;
    }

    if (occ_open(ctx.device_file, OCC_INTERFACE_OPTICAL, &ctx.occ) != 0) {
        cerr << "ERROR: cannot initialize OCC interface" << endl;
        return 3;
    }

    do {

        sigact.sa_handler = &sighandler;
        sigact.sa_flags = 0;
        sigemptyset(&sigact.sa_mask);
        sigaction(SIGTERM, &sigact, NULL);
        sigaction(SIGINT, &sigact, NULL);

        if (pthread_create(&receive_thread, NULL, &receive_from_occ, &ctx) != 0) {
            cerr << "ERROR: Cannot start receive thread!" << endl;
            ret = 2;
            break;
        }

        send_status = (struct transmit_status *)send_to_occ(&ctx);
        if (send_status == NULL) {
            cout << "ERROR: Cannot send to OCC" << endl;
            ret = 3;
            break;
        }

        shutdown = true;
        if (pthread_join(receive_thread, (void **)&receive_status) != 0 || !receive_status) {
            cerr << "ERROR: Cannot stop receive thread!" << endl;
            ret = 2;
            break;
        }
        if (receive_status->error) {
            cerr << "ERROR: OCC RX failed - " << receive_status->error << endl;
            ret = 2;
            break;
        }

        if (send_status) {
            cout << "Sent: " << send_status->n_bytes << " bytes" << endl;
            delete send_status;
        }
        if (receive_status) {
            cout << "Received: " << receive_status->n_bytes << " bytes" << endl;
            delete receive_status;
        }
    } while (0);

    occ_close(ctx.occ);

    return ret;
}
