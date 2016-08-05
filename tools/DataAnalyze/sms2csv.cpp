/**
 * sms2csv converts PreProcessor ADARA stream into a CSV stream.
 */

#include <fcntl.h>
#include <cstdio>
#include <cstdint>
#include <cstring>

#include <string>
#include <iostream>

#define BUFFER_SIZE     (10*1024*1024)  // What's the SMS upper limit? Not documented

static void usage(const char *progname) {
    printf("Usage: %s [input file] [output file]\n", progname);
    printf("Convert SMS input stream into a CSV output\n");
    printf("\n");
    printf("Example output: %s < sms_stream.raw\n", progname);
    printf("ID;Timestamp;Length;Type;Source;Total count;Sub count;EOP\n");
    printf("1;0.000000000;0;HEARTBEAT\n");
    printf("2;0.000000000;0;HEARTBEAT\n");
    printf("3;771516938.022997666;120;RTDL\n");
    printf("4;771516938.006331666;3600;DATA;4;1634;24;1\n");
    printf("5;771516938.006331666;3600;DATA;4;97;0;0\n");
    printf("6;771516938.006331666;3600;DATA;4;99;1;0\n");
    printf("7;771516938.006331666;3600;DATA;4;101;2;0\n");
    printf("8;771516937.972998666;24;DATA;4;103;3;0\n");
    printf("9;771516937.989664666;24;DATA;0;1638;1;1\n");
    printf("10;771516938.006331666;3600;DATA;0;5;0;0\n");
    printf("\n");
}

struct sms_header {
    uint32_t length;
    uint32_t type;
    uint32_t time_sec;
    uint32_t time_nsec;
};

int main(int argc, char **argv) {
    FILE *infd = stdin;
    FILE *outfd = stdout;
    uint32_t packet_id = 0;
    uint32_t *payload = new uint32_t[BUFFER_SIZE/4];
    int ret = 0;
    struct sms_header header;

    for (int i = 1; i < argc; i++) {
        const char *key = argv[i];

        if (strncmp(key, "-h", 2) == 0 || strncmp(key, "--help", 6) == 0) {
            usage(argv[0]);
            return 1;
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

    fprintf(outfd, "ID;Timestamp;Length;Type;Source;Total count;Sub count;EOP\n");

    while ((ret = fread(&header, sizeof(header), 1, infd)) > 0) {
        // Only valid for DATA type
        uint32_t source;
        bool eop;
        uint16_t subpacket_cnt;
        uint16_t total_cnt;

        if (header.length > BUFFER_SIZE) {
            fprintf(stderr, "ERROR: packet exceeds internal buffer (%u > %u)\n", header.length, BUFFER_SIZE);
            ret = 1;
            break;
        }

        ret = fread(payload, header.length, 1, infd);
        if (ret == -1 && (uint32_t)ret != 1)
            break;

        source = payload[0];
        eop = (payload[1] >> 31);
        subpacket_cnt = (payload[1] >> 16) & 0x7FF;
        total_cnt = payload[1] & 0xFFFF;

        fprintf(outfd, "%u;%u.%09u;%u;", ++packet_id, header.time_sec, header.time_nsec, header.length);
        if (header.type == 0x00000000)      fprintf(outfd, "DATA;%u;%u;%u;%u\n", source, total_cnt, subpacket_cnt, eop);
        else if (header.type == 0x00000100) fprintf(outfd, "RTDL\n");
        else if (header.type == 0x00000200) fprintf(outfd, "SOURCE LIST\n");
	else if (header.type == 0x00000300) fprintf(outfd, "DATA MAPPED;%u;%u;%u;%u\n", source, total_cnt, subpacket_cnt, eop);
        else if (header.type == 0x00400900) fprintf(outfd, "HEARTBEAT\n");
        else                                fprintf(outfd, "UNSUPPORTED(0x%.08X)\n", header.type);
    }

    fclose(outfd);

    return ret;
}
