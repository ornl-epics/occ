/*
 * Copyright (c) 2020 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 *
 * @author Matt Waddel <waddelmb@ornl.gov>
 * Heavily based on loopback.cpp by Klemen Vodopivec
 */

#include <occlib.h>

#include <cstdlib>
#include <errno.h>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <list>
#include <signal.h>
#include <unistd.h>
#include <vector>

#include <fcntl.h>
#include <string.h>

#define OCC_MAX_PACKET_SIZE 38000
#define MIN_PACKET_SIZE 24 // Size of MOD_ID+REG_STRT+CMD_LNGTH+BYTE_CNT 

using namespace std;

static bool shutdown = false;

struct program_context {
    const char *device_file;
    uint32_t read_reg;
    uint32_t write_reg;
    uint64_t value;
    bool dump;
    bool read;
    bool write;
    bool verbose;
    bool version;
    int loops;
    int reg_data;
    unsigned long payload_size;
    struct occ_handle *occ;
    list< vector<unsigned char> > queue;
    pthread_mutex_t queLock;

    program_context() :
        device_file(NULL),
        dump(false),
        read(false),
        write(false),
        verbose(false),
        version(false),
        loops(0),
	reg_data(0),
        payload_size(0),
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

static unsigned long bytesReceived = 0;

// Register layout of Read/Write request packet headers
// There are 6 32bit words for all requests
struct das_packet {
    struct __attribute__ ((__packed__)) {
        uint8_t sequence:8;	//!< Incremented by sender for each packet sent
        bool priority:1;	//!< Flag denotes priority handling, optimizes interrupt handling
        unsigned _reserved1:11;
        uint8_t type:8;		//!< Packet type
        unsigned version:4;	//!< Packet version
    };
    uint32_t length;            //!< Total number of bytes for this packet, only 24 bits used
    struct __attribute__ ((__packed__)) {
        unsigned cmd_length:12;	//!< Command length
        unsigned _reserved2:4;
        unsigned cmd_type:8;	//!< 1 = read, 2 = write
        unsigned verify_id:5;	//!< unique packet identifier, 
        bool ack:1;		//!< valid only in response packets: 1=pass, 0=fail
        bool rsp:1;		//!< response expected
        unsigned new1:1;		//!< always 1, 0 is for unsupported packets
    };
    uint32_t mod_id;		//!< bits 31:0 of module id and address
    uint32_t mod_id_start;	//!< bits 47:32 module id and 15:0 of register start addr
    uint32_t reg_end_count;	//!< bits 31:16 of reg start addr and byte count to transfer
};

static void usage(const char *progname) {
    cout << "Usage: " << progname << " [OPTION]" << endl;
    cout << endl;
    cout << "Using OCC hardware this tool reads and writes Modular Device registers." << endl;
    cout << "You can query specific registers or dump all 128 registers sequentually" << endl;
    cout << "(dump is the default when no registers are specified)." << endl;
    cout << "Read/Write requests can be appended." << endl;
    cout << "If the output and input are connected in a physical loopback mode," << endl;
    cout << "the packets are just mirrored back." << endl;
    cout << endl;
    cout << "Options:" << endl;
    cout << "  -d, --device-file FILE      Required OCC board device filename" << endl;
    cout << "  --version                   Request firmware version/revision" << endl;
    cout << "  -v, --verbose               Print packet communication data" << endl;
    cout << "  -r, --read LOCATION         Read specified LOCATION." << endl;
    cout << "                              Limits: 0<LOCATION<127." << endl;
    cout << "  -w, --write LOCATION VALUE  Write VALUE to R/W reg at 64+LOCATION." << endl;
    cout << "                              Limits: 64<LOCATION<127. 0<VALUE<0xFFFFFFFF." << endl;
    cout << endl;
    cout << "Example:" << endl;
    cout << "./reg_loop -d /dev/occ4       Dumps the entire register stack in /dev/occ4" << endl;
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
	if (key == "--version") {
            ctx->version = true;
            ctx->loops += 1;
        }
	if (key == "--verbose" || key == "-v") {
            ctx->verbose = true;
        }
	if (key == "--read" || key == "-r") {
            ctx->read = true;
            if ((i + 1) >= argc)
                return false;
            ctx->read_reg = strtod(argv[++i], NULL);
            if ((0 > ctx->read_reg) || (127 < ctx->read_reg))
                return false;
            ctx->loops += 1;
        }
        if (key == "--write" || key == "-w") {
            ctx->write = true;
            if ((i + 2) >= argc)
                return false;
            ctx->write_reg = strtod(argv[++i], NULL);
            if ((64 > ctx->write_reg) || (127 < ctx->write_reg))
                return false;
            ctx->value = strtol(argv[++i], NULL, 16);
            ctx->loops += 1;
        }
    }
    return true;
}

size_t __occ_align(size_t size) {
    return (size + 3) & ~3;
}

void print_results(const char *direction, size_t length, char *buffer) {
    // Print the contents of data packets
    cout << direction << length << " bytes" << endl << "--------------" << endl;
    cout << hex;
    for (size_t j=0; j < length; j+=4) {
        int marker = (j/4+1);

        // This bit of code will pretty print the complete register dump
        if (strncmp(direction, "Recv:", 5) == 0) {
            if (marker == 7)
                cout << endl;
            if (marker >= 7)
                marker -= 7;
        }

        for (int i = 3; i >= 0; i--) {
            if (i == 3)
                cout << dec << marker << ": " << hex;
            cout << setw(2) << setfill('0') << uppercase << (int)(buffer[j+i] & 0xFF);
            if (i == 0)
                cout << endl;
            else 
                cout << "_";
        }
    }
    cout << dec << endl;
}

// The setup_packet fields are common with all packets
void setup_packet(struct das_packet *packet, struct program_context *ctx) {
    /* Setup Packet Header 1 */
    packet->version = 1;
    packet->type = 8;
    packet->priority = true;
    packet->sequence = 0;

    /* Setup Packet Header 2 */
    packet->length = sizeof(struct das_packet);

    /* Setup Packet Header 3 */
    packet->new1 = true;
    packet->rsp = true;
    packet->ack = 1;
    packet->cmd_length = MIN_PACKET_SIZE + __occ_align(ctx->payload_size);

    /* Setup Packet Header 6 */
    packet->reg_end_count = 0x4;

    /* Print the results at register offset */
    ctx->reg_data = 0;
}

void read_version_packet(struct das_packet *packet, struct program_context *ctx) {
    /* Setup Packet Header 3 */
    packet->rsp = true;
    packet->ack = 1;
    packet->verify_id = 0xa;
    packet->cmd_type = 0x1;

    /* Setup Packet Header 4 */
    packet->mod_id = 0x000013AB;

    /* Setup Packet Header 5 */
    packet->mod_id_start = 0x7D8E0000;
}

void read_packet(struct das_packet *packet, struct program_context *ctx) {
    /* Setup Packet Header 3 */
    packet->rsp = true;
    packet->ack = 1;
    packet->verify_id = 0xb;
    packet->cmd_type = 0x1;

    /* Setup Packet Header 4 */
    packet->mod_id = 0x000013AB;

    /* Setup Packet Header 5 */
    packet->mod_id_start = 0x7D8E0004;
}

void write_packet(struct das_packet *packet, struct program_context *ctx) {
    /* Setup Packet Header 3 */
    packet->rsp = true;
    packet->ack = 1;
    packet->verify_id = 0xc;
    packet->cmd_type = 0x2;

    /* Setup Packet Header 4 */
    packet->mod_id = 0x000013AB;

    /* Setup Packet Header 5 */
    packet->mod_id_start = 0x7D8E0000;
}

void dump_regs(struct das_packet *packet, struct program_context *ctx) {
    /* Setup Packet Header 3 */
    packet->rsp = false;
    packet->ack = 0;
    packet->verify_id = 0xd;
    packet->cmd_type = 0x1;

    /* Setup Packet Header 4 */
    packet->mod_id = 0x0;

    /* Setup Packet Header 5 */
    packet->mod_id_start = 0x0;

    /* Setup Packet Header 6 */
    packet->reg_end_count = 0x200;
}

/* Worker function for sending data to OCC. */
static void *send_to_occ(void *arg) {
    struct transmit_status *status = new transmit_status;
    struct program_context *ctx = (struct program_context *)arg;
    char buffer[OCC_MAX_PACKET_SIZE + 24];
    struct das_packet *packet = reinterpret_cast<struct das_packet *>(buffer);
    int send_status = 0;

    memset(packet, 0, sizeof(struct das_packet));
    setup_packet(packet, ctx);
    if (ctx->version)
        read_version_packet(packet, ctx);
    else if (ctx->write) {
        write_packet(packet, ctx);
        packet->reg_end_count |= ((ctx->write_reg * 4) << 16);
        buffer[sizeof(struct das_packet)+3] = (uint8_t)((ctx->value >> 24) & 0xFF);
        buffer[sizeof(struct das_packet)+2] = (uint8_t)((ctx->value >> 16) & 0xFF);
        buffer[sizeof(struct das_packet)+1] = (uint8_t)((ctx->value >> 8) & 0XFF);
        buffer[sizeof(struct das_packet)+0] = (uint8_t)((ctx->value >> 0) & 0XFF);
	packet->length += 4;
        ctx->reg_data = ctx->value;
        }
    else if (ctx->read) {
        read_packet(packet, ctx);
        packet->reg_end_count |= ((ctx->read_reg * 4) << 16);
	packet->length += 4; 
        } 
    else {
        dump_regs(packet, ctx);
        ctx->dump = true;
        cout << endl << "Register Dump" << endl;
        }

    send_status = occ_send(ctx->occ, buffer, packet->length);

    if (send_status <= 0)
        cout << "Send error: " << send_status << endl;
    else {
        // Print the contents of the packet just sent
        status->n_bytes = send_status;
        if (ctx->verbose)
            print_results("Sent: ", send_status, buffer);
        }

    return status;
}

/* Worker function for receiving data from OCC.
 * Can be called from pthread_create. It will read data from OCC device.
 */
void *receive_from_occ(void *arg) {
    struct transmit_status *status = new transmit_status;
    struct program_context *ctx = (struct program_context *)arg;
    unsigned char *data = NULL;
    int ret;

    while (!shutdown) {
        size_t datalen = 0;

        ret = occ_data_wait(ctx->occ, reinterpret_cast<void **>(&data), &datalen, 100);
        if (ret != 0) {
            if (ret == -ETIME)
                continue;
            status->error = "cannot read from OCC device";
            break;
        }

        size_t remain = datalen;
        while (remain > 0) {
            struct das_packet *packet = reinterpret_cast<struct das_packet *>(data);
            uint32_t packet_len;

            packet_len = packet->length;
            if (packet_len > OCC_MAX_PACKET_SIZE) {
                // Acknowledge everything but skip processing the rest
                cerr << "Bad packet: (" << packet_len << ">1800), skipping(" << hex << data << dec << ")" << endl;
                remain = 0;
                break;
            }
            if (packet_len > remain)
                break;

            remain -= packet_len;
            data += packet_len;
            bytesReceived += packet_len;
        }
        // Adjust to the actual processed data for acknowledgement
        datalen -= remain;

        if (datalen > 0) {
            ret = occ_data_ack(ctx->occ, datalen);
            if (ret != 0) {
                status->error = "cannot advance read index";
            }
        }

        status->n_bytes += datalen;
    }

    // Print received data
    usleep(100000); // Give the send request output time to finish
    if (ctx->dump)
        print_results("Recv: ", status->n_bytes, (char *)data);
    else {
        if (ctx->verbose)
            print_results("Recv: ", sizeof(struct das_packet)+4, (char *)data);
        if (ctx->version) {
            cout << "Version: " << hex;
            cout << setw(2) << setfill('0') << uppercase << (int)data[sizeof(struct das_packet)+3];
            cout << ", Revision: ";
            cout << setw(2) << setfill('0') << uppercase << (int)data[sizeof(struct das_packet)+2];
            cout << dec << endl;
            ctx->version = false;
        } else if (ctx->write) {
            cout << "Wrote Reg: " << ctx->write_reg << ", Value: 0x" << hex;
            cout << setw(8) << setfill('0') << uppercase << ctx->value ;
            cout << dec << endl;
            //cout << ", Value: 0x" << hex << ctx->value << dec << endl;
            ctx->write = false;
        } else if (ctx->read) {
            cout << "Read Reg:  ";
            cout << setw(2) << setfill('0') << uppercase << ctx->read_reg << ", Value: 0x" << hex;
            for (int i = 3; i >= 0; i--) {
                cout << setw(2) << setfill('0') << uppercase << (int)data[sizeof(struct das_packet)+i];
                if (i == 0)
                    cout << dec << endl;
                }
            ctx->read = false;
        }
    }

    return status;
}

int main(int argc, char **argv) {
    struct program_context ctx;
    pthread_t receive_thread;
    int ret = 0;
    struct transmit_status *send_status = NULL, *receive_status = NULL;
    struct sigaction sigact;

    if (!parse_args(argc, argv, &ctx) || !ctx.device_file) {
        usage(argv[0]);
        return 1;
    }

    do {
        if (occ_open(ctx.device_file, OCC_INTERFACE_OPTICAL, &ctx.occ) != 0) {
            cerr << "ERROR: cannot initialize OCC interface:" << ctx.device_file << endl;
            return 3;
        }

        // occ defaults to old style packets, tests don't work without this setup
        occ_enable_old_packets(ctx.occ, false);

        if (occ_enable_rx(ctx.occ, true) != 0) {
            cerr << "ERROR: cannot enable RX" << endl;
            return 3;
        }
        usleep(1000);

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

        // Let receive thread have some time to process packets
        usleep(100000);
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

	ctx.loops -= 1;
        if (ctx.loops > 0)
            shutdown = false;

        occ_close(ctx.occ);
    } while ((ctx.loops > 0));

    return ret;
}
