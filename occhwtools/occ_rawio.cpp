#include <occlib_hw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> // strerror

using namespace std;

static void usage(const char *progname) {
    printf("Usage: %s [OPTION]\n", progname);
    printf("\n");
    printf("Options:\n");
    printf("  -d, --device-file FILE   Full path to OCC board device file\n");
    printf("  -r, --read               Read from device (mutually exclusive with write)\n");
    printf("  -w, --write VALUE        Write dword value to device\n");
    printf("  -b, --bar                Select device PCI BAR\n");
    printf("  -o, --offset             Offset to the base address\n");
    printf("  -l, --length             Number of dwords to read/write (defaults to 1)\n");
    printf("\n");
}

int main(int argc, char **argv) {
    const char *device_file = NULL;
    struct occ_handle *occ;
    bool write = false;
    uint8_t bar = -1;
    uint32_t offset = -1;
    uint32_t length = 1;
    uint32_t write_value = 0;

    for (int i = 1; i < argc; i++) {
        const char *key = argv[i];

        if (strncmp(key, "-h", 2) == 0 || strncmp(key, "--help", 6) == 0) {
            usage(argv[0]);
            return 1;
        }
        if (strncmp(key, "-d", 2) == 0 || strncmp(key, "--device-file", 13) == 0) {
            if ((i + 1) >= argc)
                break;
            device_file = argv[++i];
        }
        if (strncmp(key, "-r", 2) == 0 || strncmp(key, "--read", 6) == 0) {
            write = false;
        }
        if (strncmp(key, "-w", 2) == 0 || strncmp(key, "--write", 7) == 0) {
            if ((i + 1) >= argc) {
                usage(argv[0]);
                return 1;
            }
            write_value = strtol(argv[++i], NULL, 0);
            write = true;
        }
        if (strncmp(key, "-b", 2) == 0 || strncmp(key, "--bar", 5) == 0) {
            if ((i + 1) >= argc)
                break;
            bar = strtol(argv[++i], NULL, 0);
        }
        if (strncmp(key, "-o", 2) == 0 || strncmp(key, "--offset", 8) == 0) {
            if ((i + 1) >= argc)
                break;
            offset = strtol(argv[++i], NULL, 0);
        }
        if (strncmp(key, "-l", 2) == 0 || strncmp(key, "--length", 7) == 0) {
            if ((i + 1) >= argc)
                break;
            length = strtol(argv[++i], NULL, 0);
        }
    }
    if (device_file == NULL || bar == (uint8_t)-1 || offset == (uint32_t)-1) {
        usage(argv[0]);
        return 1;
    }
    if (offset % 4 != 0) {
        fprintf(stderr, "ERROR: offset parameter must be aligned to 4 bytes");
        return 1;
    }

    if (occ_open(device_file, OCC_INTERFACE_OPTICAL, &occ) != 0) {
        fprintf(stderr, "ERROR: cannot initialize OCC interface\n");
        return 3;
    }

    if (write) {
        uint32_t data[length];
        for (uint32_t i = 0; i < length; i++)
            data[i] = write_value;

        int ret = occ_io_write(occ, bar, offset, data, length);
        if (ret < 0) {
            fprintf(stderr, "ERROR: cannot read BAR%d at offset 0x%08X - %s\n", bar, offset, strerror(-ret));
        } else {
            printf("Written %d dwords to BAR%d at offset 0x%08X\n", ret, bar, offset);
        }

    } else {
        uint32_t data[length];

        int ret = occ_io_read(occ, bar, offset, data, length);
        if (ret < 0) {
            fprintf(stderr, "ERROR: cannot read BAR%d at offset 0x%08X - %s\n", bar, offset, strerror(-ret));
        } else {
            printf("%s BAR%d dword data:\n", device_file, bar);
            for (int i = 0; i < ret; i++) {
                printf("0x%08X: 0x%08X\n", offset, data[i]);
            }
        }
    }

    occ_close(occ);

    return 0;
}
