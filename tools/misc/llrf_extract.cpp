#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <string>

#include <occlib.h>

int main(int argc, char **argv) {
    int ret;
    struct occ_handle *occ;

    if (argc < 3) {
        printf("Usage: %s <device file> <num samples>\n", argv[0]);
        return 1;
    }

    uint32_t remain_samples = std::stoi(argv[2]);

    ret = occ_open(argv[1], OCC_INTERFACE_OPTICAL, &occ);
    if (ret != 0) {
        fprintf(stderr, "ERROR: Failed to open OCC device %s: %s\n", argv[1], strerror(-ret));
        return 1;
    }
    occ_enable_old_packets(occ, false);
    occ_enable_rx(occ, 1);

    printf("Channel 1");
    for (uint32_t i = 2; i <= 16; i++) {
        printf("\tChannel %d", i);
    }
    // Skip adding new line, samples below will do it

    while (remain_samples > 0) {
        uint32_t *data = nullptr;
        size_t size;
        ret = occ_data_wait(occ, reinterpret_cast<void**>(&data), &size, 0);
        if (ret != 0) {
            fprintf(stderr, "ERROR: Failed to receive data from OCC: %s\n", strerror(-ret));
            break;
        }

        if (size < 16 || data[1] < 16) {
            fprintf(stderr, "ERROR: Inbound packet too short, must be at least 16 bytes\n");
            break;
        }

        uint32_t length = data[1] - 16;
        data += 4;
        for (uint32_t i = 0; i < length/4; i++) {
            printf("%c%u", (i%16)==0 ? '\n' : '\t', data[i]);

            if ((i%16) == 0 && --remain_samples == 0)
                break;
        }

        occ_data_ack(occ, 16+length);
    }

    occ_close(occ);

    return 0;
}
