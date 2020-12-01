/*
 * Copyright (c) 2018 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 *
 * @author Klemen Vodopivec <vodopiveck@ornl.gov>
 */

#include <occlib.h>

#include <cstdlib>
#include <errno.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <list>
#include <signal.h>
#include <unistd.h>
#include <vector>

#include <fcntl.h>
#include <string.h>

#define OCC_MAX_PACKET_SIZE 38000
#define TX_MAX_SIZE         38000        // Maximum packet size in bytes to be send over OCC

using namespace std;

static bool shutdown = false;

#ifdef TRACE
char outbuf[10000000];
#endif

struct program_context {
    const char *device_file;
    const char *input_file;
    const char *output_file;
    const char *capture_file;
    unsigned long send_rate;
    unsigned long payload_size;
    bool raw_mode;
    bool new_format;
    struct occ_handle *occ;
    list< vector<unsigned char> > queue;
    pthread_mutex_t queLock;

    program_context() :
        device_file(NULL),
        input_file("/dev/urandom"),
        output_file(NULL),
        capture_file(NULL),
        send_rate(0),
        payload_size(3000),
        raw_mode(false),
        new_format(false),
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

static unsigned long bytesSent = 0;
static unsigned long bytesReceived = 0;
static unsigned long sequence = 0;

#pragma pack(push)
#pragma pack(4)
struct das1_packet {
    uint32_t destination;       //<! Destination id
    uint32_t source;            //<! Sender id
    uint32_t info;              //<! field describing packet type and other info
    uint32_t payload_length;    //<! payload length
    uint32_t reserved1;
    uint32_t reserved2;
};
struct das2_packet {
    struct __attribute__ ((__packed__)) {
        uint8_t sequence;       //!< Packet sequence number, incremented by sender for each sent packet
        uint8_t type;           //!< Packet type
        bool priority:1;        //!< Flag to denote high priority handling, used by hardware to optimize interrupt handling
        unsigned __reserved1:11;
        unsigned version:4;     //<! Packet version
    };
    uint32_t length;            //!< Total number of bytes for this packet
    struct __attribute__ ((__packed__)) {
        uint8_t source;                 //!< Unique source id number
        unsigned subpacket:4;           //!< Subpacket count
        unsigned __data_rsv1:4;
        uint8_t format;                 //!< Data format
        unsigned __data_rsv2:8;
    };
    uint32_t timestamp_sec;             //!< Accelerator time (seconds) of event 39
    uint32_t timestamp_nsec;            //!< Accelerator time (nano-seconds) of event 39
};
#pragma pack(pop)

static void usage(const char *progname) {
    cout << "Usage: " << progname << " [OPTION]" << endl;
    cout << "Tool assumes established hardware loopback. Reads data from input file and" << endl;
    cout << "sends it to OCC device at specified rate. Expect the same data on the receive" << endl;
    cout << "side and abort if it differs." << endl;
    cout << endl;
    cout << "Options:" << endl;
    cout << "  -d, --device-file FILE   Full path to OCC board device file" << endl;
    cout << "  -i, --input-file FILE    File with data to be sent through OCC (defaults to /dev/urandom)" << endl;
    cout << "  -o, --output-file FILE   File to save received data to (default none)" << endl;
    cout << "  -t, --throughput BYTES/S Limit the sending throughput (defaults to 0, unlimited)" << endl;
    cout << "  -s, --payload-size SIZE  Size of data in each sent packet (defaults to 3000)" << endl;
    cout << "  -r, --raw-mode           Input file contains packetized data" << endl;
    cout << "  -n, --new-format         Use DAS 2.0 packet format" << endl;
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
        if (key == "-t" || key == "--throughput") {
            if ((i + 1) < argc)
                ctx->send_rate = atoi(argv[++i]);
        }
        if (key == "-s" || key == "--payload-size") {
            if ((i + 1) < argc)
                ctx->payload_size = min(atoi(argv[++i]), TX_MAX_SIZE);
        }
        if (key == "-r" || key == "--raw-mode") {
            ctx->raw_mode = true;
        }
        if (key == "-n" || key == "--new-format") {
            ctx->new_format = true;
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
    return (size + 3) & ~3;
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

    char buffer[TX_MAX_SIZE + 24];

    while (!shutdown && infile.good()) {

        if (ctx->raw_mode) {
            size_t hdr_len = (ctx->new_format ? sizeof(struct das2_packet) : sizeof(das1_packet));
            infile.read(buffer, sizeof(struct das2_packet));
            if (infile.gcount() != (int)hdr_len) {
                if (infile.gcount() > 0)
                    cerr << "ERROR: Not enough header data in input file" << endl;
                break;
            }
        } else {
            if (ctx->new_format) {
                struct das2_packet *packet = reinterpret_cast<struct das2_packet *>(buffer);
                struct timespec t;
                clock_gettime(CLOCK_REALTIME, &t);
                memset(packet, 0, sizeof(struct das2_packet));
                packet->version = 1;
                packet->sequence = sequence++;
                packet->type = 7;
                packet->length = sizeof(struct das2_packet) + __occ_align(ctx->payload_size);
                packet->timestamp_sec = 0;
                packet->timestamp_nsec = t.tv_nsec;
            } else {
                struct das1_packet *packet = reinterpret_cast<struct das1_packet *>(buffer);
                memset(packet, 0, sizeof(struct das1_packet));
                packet->destination = 0x2;
                packet->source = 0x1;
                packet->info = 0x10000000;
                packet->payload_length = __occ_align(ctx->payload_size);
            }
        }

        unsigned long packet_size;
        unsigned long payload_size = 0;
        char *payload = 0;
        if (ctx->new_format) {
            struct das2_packet *packet = reinterpret_cast<struct das2_packet *>(buffer);
            packet_size = packet->length;
            payload_size = packet_size - sizeof(struct das2_packet);
            payload = buffer + sizeof(struct das2_packet);
        } else {
            struct das1_packet *packet = reinterpret_cast<struct das1_packet *>(buffer);
            payload_size = packet->payload_length;
            packet_size = payload_size + sizeof(struct das1_packet);
            payload = buffer + sizeof(struct das1_packet);
        }

        // Would use readsome(), but apparently is implementation specific and
        // with no standard behaviour on EOF. This loop might well run forever
        // on some implementations.
        infile.read(payload, payload_size);

        // Enqueue packet for comparing purposes
        vector<unsigned char> v(buffer, buffer+packet_size);
        pthread_mutex_lock(&ctx->queLock);
        ctx->queue.push_back(v);
        pthread_mutex_unlock(&ctx->queLock);

#ifdef TRACE
        cout << "occ_send(" << packet_size << ")" << endl;
#endif
#ifdef TRACE1
        cout << hex;
        for (size_t i = 0; i < packet_size; i++) {
            cout << setw(2) << setfill('0') << uppercase << (int)(buffer[i] & 0xFF);
            if (i % 4 == 3)
                cout << endl;
            else
                cout << " ";
        }
        cout << dec << endl;
#endif

        if (occ_send(ctx->occ, buffer, packet_size) != 0) {
            if (bytesSent)
               status->n_bytes = bytesSent;
            else
               status->n_bytes = packet_size;
            break;
	}

        status->n_bytes += packet_size;
        bytesSent += packet_size;

        cout << "Sent: " << status->n_bytes << " bytes ";
        cout << "Received: " << bytesReceived << " bytes\r";

        ratelimit(ctx->send_rate, status->n_bytes, &starttime);
    }

    return status;
}

inline size_t __occ_packet_align(size_t size) {
    return (size + 3) & ~3;
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

#ifdef TRACE
    int offset = 0;
#endif

    if (ctx->output_file)
        outfile.open(ctx->output_file, ios_base::binary);
    else
        outfile.setstate(ios_base::eofbit);

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

#ifdef TRACE1
        cout << hex;
        for (size_t i = 0; i < datalen; i++) {
            cout << setw(2) << setfill('0') << uppercase << (int)(data[i] & 0xFF);
            if (i % 4 == 3)
                cout << endl;
            else
                cout << " ";
        }
        cout << dec << endl;
#endif
#ifdef TRACE
        unsigned char *data1 = data;
#endif
        size_t remain = datalen;
        while (remain > 0) {
            struct das2_packet *packet = reinterpret_cast<struct das2_packet *>(data);
            unsigned char *payload;
            size_t packet_len = 0;
            size_t payload_len = 0;
            if (ctx->new_format) {
                payload = data + sizeof(struct das2_packet);
                packet_len = packet->length;
                payload_len = packet_len - sizeof(struct das2_packet);
            } else {
                struct das1_packet *hdr = (struct das1_packet *)data;
                payload = data + sizeof(struct das1_packet);
                payload_len = hdr->payload_length;
                packet_len = sizeof(struct das1_packet) + payload_len;
            }
            if (packet_len > OCC_MAX_PACKET_SIZE) {
                // Acknowledge everything but skip processing the rest
                cerr << "Bad packet based on length check (" << packet_len << ">1800), skipping... (" << hex << data << dec << endl;
                remain = 0;
                break;
            }
            if (packet_len > remain)
                break;

            if (outfile.good()) {
                if (ctx->raw_mode)
                    outfile.write((const char *)(data), packet_len);
                else
                    outfile.write((const char *)(payload), payload_len);
            }

            if (!compareWithSent(ctx, data, packet_len)) {
                status->error = "Received data mismatch";
                shutdown = true;
                break;
            }

            remain -= packet_len;
            data += packet_len;
            bytesReceived += packet_len;
        }
        // Adjust to the actual processed data for acknowledgement
        datalen -= remain;

#ifdef TRACE
        cout << "occ_data_ack(" << datalen << ")" << endl;
#endif
        if (datalen > 0) {
#ifdef TRACE
            if ((offset + datalen) < sizeof(outbuf)) {
                memcpy(&outbuf[offset], data1, datalen);
                offset += datalen;
            }
#endif
            ret = occ_data_ack(ctx->occ, datalen);
            if (ret != 0) {
                status->error = "cannot advance consumer index";
                break;
            }
        }

        status->n_bytes += datalen;
    }

#ifdef TRACE
    int fd = open("/tmp/occ_dump.bin", O_CREAT | O_WRONLY | O_TRUNC);
    write(fd, outbuf, sizeof(outbuf));
    close(fd);
#endif

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
#ifdef TRACE
    memset(outbuf, '#', sizeof(outbuf));
#endif
    if (occ_open(ctx.device_file, OCC_INTERFACE_OPTICAL, &ctx.occ) != 0) {
        cerr << "ERROR: cannot initialize OCC interface" << endl;
        return 3;
    }

    if (occ_enable_old_packets(ctx.occ, ctx.new_format==0) != 0) {
        cerr << "ERROR: cannot disable old DAS packets" << endl;
        return 3;
    }

    if (occ_enable_rx(ctx.occ, true) != 0) {
        cerr << "ERROR: cannot enable RX" << endl;
        return 3;
    }
    usleep(1000);

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

        // Let receive thread some time to process the packets we sent recently
        sleep(1);
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

    if (receive_status->n_bytes == 0)
        ret = 2;

    return ret;
}
