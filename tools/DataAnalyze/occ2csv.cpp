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
    printf("Usage: %s [OPTION]\n", progname);
    printf("Convert raw OCC data stream into CSV format.\n");
    printf("\n");
    printf("Options:\n");
    printf("  -i, --input-file FILE   Full path to file to be read or - for stdin\n");
    printf("  -o, --output-file FILE  Full path fo file to be writen to or - for stdout\n");
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

int main(int argc, char **argv) {
    const char *infile = NULL;
    const char *outfile = NULL;
    int ret;
    struct occ_header header;
    FILE *infd, *outfd;
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
        if (strncmp(key, "-e", 2) == 0 || strncmp(key, "--events", 8) == 0) {
            events = 1;
        }
        if (strncmp(key, "-r", 2) == 0 || strncmp(key, "--rtdl", 6) == 0) {
            rtdl = 1;
        }
        if (strncmp(key, "-i", 2) == 0 || strncmp(key, "--input-file", 12) == 0) {
            if ((i + 1) >= argc)
                break;
            infile = argv[++i];
        }
        if (strncmp(key, "-o", 2) == 0 || strncmp(key, "--output-file", 13) == 0) {
            if ((i + 1) >= argc)
                break;
            outfile = argv[++i];
        }
    }
    if (infile == NULL || std::string(infile) == "-") {
        infd = stdin;
    } else {
        infd = fopen(infile, "r");
        if (infd == NULL) {
            std::cerr << "ERROR: cannot open input file" << std::endl;
            return 3;
        }
    }

    if (outfile == NULL || std::string(outfile) == "-") {
        outfd = stdout;
    } else {
        outfd = fopen(outfile, "w+");
        if (outfd == NULL) {
            std::cerr << "ERROR: cannot open output file" << std::endl;
            return 3;
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
        else if ((header.info & 0x80000084) == 0x80000084)  fprintf(outfd, ";TSYNC;0;0\n");
        else if (header.info & 0x80000000)                  fprintf(outfd, ";CMD(0x%.02X);0;%u.%09u\n", header.info & 0xFF, 0, 0);
        else if ((header.info & 0xC) == 0xC)                fprintf(outfd, ";NEUTRON;%u;%u.%09u\n",     subpacket_id, buf[0], buf[1]);
        else if ((header.info & 0xC) == 0x4)                fprintf(outfd, ";NEUTRON;%u;missing RTDL\n",subpacket_id);
        else if ((header.info & 0x8) == 0x8)                fprintf(outfd, ";META;%u;%u.%09u\n",        subpacket_id, buf[0], buf[1]);
        else                                                fprintf(outfd, ";META;%u;missing RTDL\n",   subpacket_id);

        if (rtdl && (header.info & 0x80000008) == 0x8) {
            uint32_t i;
            for (i = 0; i < 6; i++) {
                if (i == 0)          fprintf(outfd,   "  ");
                else if (i % 4 == 0) fprintf(outfd, "\n  ");
                fprintf(outfd, "%08X ", buf[i]);
            }
            fprintf(outfd, "\n");
        }

        if (events && (header.info & 0x800000F0) == 0) {
            uint32_t skip = (header.info & 0x8) ? 6 : 0;
            uint32_t i = skip;
            for (; i < header.length/4; i++) {
                if (i == skip)             fprintf(outfd,   "  ");
                else if ((i - skip) % 4 == 0) fprintf(outfd, "\n  ");
                fprintf(outfd, "%08X ", buf[i]);
            }
            if (i > skip) fprintf(outfd, "\n");
        }
    }

    fclose(outfd);

    return 0;
}
