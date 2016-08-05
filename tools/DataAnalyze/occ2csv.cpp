/**
 * The occ2csv reads raw OCC stream from either input file or stdin. Each packet
 * is processed and formated into CSV format. It's finally printed to output file
 * or stdout.
 */

#include <fcntl.h>
#include <cstdio>
#include <cstdint>
#include <cstring>

#include <string>
#include <iostream>

#define BUFFER_SIZE     (18000)     // Max payload sent by DSP is 3600

struct occ_header {
    uint32_t dest_id;
    uint32_t src_id;
    uint32_t info;
    uint32_t length;
    uint32_t reserved1;
    uint32_t reserved2;
};

static void usage(const char *progname) {
    printf("Usage: %s [OPTION] [input file] [output file]\n", progname);
    printf("Convert raw OCC data stream into CSV format.\n");
    printf("\n");
    printf("Options:\n");
    printf("  -e, --events            Print events as 32 bit HEX numbers\n");
    printf("  -r, --rtdl              Print RTDL header as 32 bit HEX when available\n");
    printf("\n");
    printf("Example output: %s < occ-dump.raw\n", progname);
    printf("ID;Destination;Source;Cmdinfo;Length;Subpacket id;Timestamp\n");
    printf("1;0x000F10CC;0x15FABD04;0x0000DC0C;3600;NEUTRON;220;772645950.988011666\n");
    printf("2;0x000F10CC;0x15FABD04;0x0000DD0C;3600;NEUTRON;221;772645950.988011666\n");
    printf("3;0x00000000;0x15FABD04;0x80000085;128;RTDL(cmd);0;772645951.021344666\n");
    printf("4;0x000F10CC;0x15FABD04;0x200000FF;128;RTDL(data);0;772645951.021344666\n");
    printf("5;0x000F10CC;0x15FABD04;0x0000DE0C;3600;NEUTRON;222;772645950.988011666\n");
    printf("6;0x000F10CC;0x15FABD04;0x0000DF0C;3600;NEUTRON;223;772645950.988011666\n");
    printf("\n");
}

std::string packetCmd(struct occ_header &header, const uint32_t *buf) {
    std::string acknack = "";
    if (!(header.info & 0x80000000)) {
        return "data";
    }
    uint32_t cmd = header.info & 0xFF;
    if (cmd == 0x40 || cmd == 0x41) {
        if (header.info  & 0x40000000) {
            // LVDS pass thru
            cmd = buf[2] & 0xFF;
        } else {
            cmd = buf[0] & 0xFF;
        }
        acknack = (cmd == 0x40 ? "NACK " : "ACK ");
    }

    switch (cmd) {
    case 0x20: return acknack + "READ VERSION";
    case 0x21: return acknack + "READ CFG";
    case 0x22: return acknack + "READ STATUS";
    case 0x23: return acknack + "READ TEMPERATURE";
    case 0x24: return acknack + "READ COUNTERS";
    case 0x25: return acknack + "RESET COUNTERS";
    case 0x27: return acknack + "RESET LVDS";
    case 0x28: return acknack + "RESET T&C LVDS";
    case 0x29: return acknack + "RESET T&C";
    case 0x30: return acknack + "WRITE CFG";
    case 0x31: return acknack + "WRITE CFG 1";
    case 0x32: return acknack + "WRITE CFG 2";
    case 0x33: return acknack + "WRITE CFG 3";
    case 0x34: return acknack + "WRITE CFG 4";
    case 0x35: return acknack + "WRITE CFG 5";
    case 0x36: return acknack + "WRITE CFG 6";
    case 0x37: return acknack + "WRITE CFG 7";
    case 0x38: return acknack + "WRITE CFG 8";
    case 0x39: return acknack + "WRITE CFG 9";
    case 0x3A: return acknack + "WRITE CFG A";
    case 0x3B: return acknack + "WRITE CFG B";
    case 0x3C: return acknack + "WRITE CFG C";
    case 0x3D: return acknack + "WRITE CFG D";
    case 0x3E: return acknack + "WRITE CFG E";
    case 0x3F: return acknack + "WRITE CFG F";
    case 0x50: return acknack + "HV SEND";
    case 0x51: return acknack + "HV RECV";
    case 0x80: return acknack + "DISCOVER";
    case 0x81: return acknack + "RESET";
    case 0x82: return acknack + "START";
    case 0x83: return acknack + "STOP";
    case 0x84: return acknack + "TSYNC";
    case 0x85: return acknack + "RTDL";
    default:   return acknack + "unknown";
    }
}

void dumpHex(FILE *outfd, const uint32_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (i == 0)          fprintf(outfd,   "  ");
        else if (i % 4 == 0) fprintf(outfd, "\n  ");
        fprintf(outfd, "%08X ", data[i]);
    }
    fprintf(outfd, "\n");
}

int main(int argc, char **argv) {
    int ret;
    struct occ_header header;
    FILE *infd = stdin;
    FILE *outfd = stdout;
    uint32_t packet_id = 0;
    uint32_t buf[BUFFER_SIZE];
    int events = 0;
    int rtdl = 0;

    for (int i = 1; i < argc; i++) {
        const char *key = argv[i];

        if (strncmp(key, "-h", 2) == 0 || strncmp(key, "--help", 6) == 0) {
            usage(argv[0]);
            return 1;
        }
        else if (strncmp(key, "-e", 2) == 0 || strncmp(key, "--events", 8) == 0) {
            events = 1;
        }
        else if (strncmp(key, "-r", 2) == 0 || strncmp(key, "--rtdl", 6) == 0) {
            rtdl = 1;
        }
        else if (key[0] == '-') {
            std::cerr << "ERROR: unsupported switch '" << key << "'" << std::endl;
            return 3;
        }
        else if (infd == stdin) {
            infd = fopen(argv[i], "r");
            if (infd == NULL) {
                std::cerr << "ERROR: cannot open input file" << std::endl;
                return 3;
            }
        } else if (outfd == stdout) {
            outfd = fopen(argv[i], "w+");
            if (outfd == NULL) {
                std::cerr << "ERROR: cannot open output file" << std::endl;
                return 3;
            }
        }
    }

    fprintf(outfd, "ID;Destination;Source;Cmdinfo;Payload Length (inc RTDL);Subpacket id;Timestamp\n");

    while ((ret = fread(&header, sizeof(header), 1, infd)) > 0) {
        uint16_t subpacket_id = (header.info >> 8) & 0xFFFF; // Only valid for data packets

        if (header.length & (0x1 << 31)) {
            // Workaround for the qword FPGA flagging us about dword packet
            header.length &= ~(0x1 << 31);
            header.length += 4;
        }

        ret = fread(buf, 1, header.length, infd);
        if (ret == -1 && (uint32_t)ret != header.length)
            break;

        fprintf(outfd, "%u;0x%.08X;0x%.08X;0x%.08X;%u", ++packet_id, header.dest_id, header.src_id, header.info, header.length);

        if ((header.info & 0x80000085) == 0x80000085)       fprintf(outfd, ";RTDL(cmd);0;%u.%09u\n",    buf[0], buf[1]);
        else if ((header.info & 0x200000FF) == 0x200000FF)  fprintf(outfd, ";RTDL(data);0;%u.%09u\n",   buf[0], buf[1]);
        else if ((header.info & 0x80000084) == 0x80000084)  fprintf(outfd, ";TSYNC;0;%u.%09u\n",        buf[0], buf[1]);
        else if (header.info & 0x80000000)                  fprintf(outfd, ";CMD(%s);0;no RTDL\n",      packetCmd(header, buf).c_str());
        else if ((header.info & 0xC) == 0xC)                fprintf(outfd, ";NEUTRON;%u;%u.%09u\n",     subpacket_id, buf[0], buf[1]);
        else if ((header.info & 0xC) == 0x4)                fprintf(outfd, ";NEUTRON;%u;missing RTDL\n",subpacket_id);
        else if ((header.info & 0x8) == 0x8)                fprintf(outfd, ";META;%u;%u.%09u\n",        subpacket_id, buf[0], buf[1]);
        else                                                fprintf(outfd, ";META;%u;missing RTDL\n",   subpacket_id);

        if (rtdl) {
            if ((header.info & 0x80000085) == 0x80000085 || (header.info & 0x200000FF) == 0x200000FF) {
                dumpHex(outfd, buf, header.length/4);
            } else if ((header.info & 0x80000008) == 0x8) {
                dumpHex(outfd, buf, 6);
            }
        }

        if (events && (header.info & 0x800000F0) == 0) {
            uint32_t skip = (header.info & 0x8) ? 6 : 0;
            dumpHex(outfd, &buf[skip], header.length/4 - skip);
        }
    }

    fclose(outfd);

    return 0;
}
