#!/usr/bin/env python

# generate_packets.py
#
# Copyright (c) 2018 Oak Ridge National Laboratory.
# All rights reserved.
# See file LICENSE that is included with this distribution.
#
# @author Klemen Vodopivec
# @date Nov 2018

import argparse
import os
import struct
import sys
import time

def writeMPS(f, seq, src, dest, addr, data=None):
    hdr1 = 0x11800000 | (seq & 0xFF)
    length = 5 * 4
    hdr3 = (dest & 0xFFFF) << 16 | (src & 0xFFFF)
    if data:
        data = data[:4092]
        length += len(data)
        hdr4 = 0x22000000 | (len(data)/4)
        f.write( struct.pack("<IIIII", hdr1, length, hdr3, hdr4, addr) )
        f.write(data)
    else:
        hdr4 = 0x02000000
        f.write( struct.pack("<IIIII", hdr1, length, hdr3, hdr4, addr) )

def writeDAS(f, seq, length):
    # Align length and number of events
    nevents = min(length / 8, 65535)
    length = (5 * 4) + (nevents * 8)

    hdr1 = 0x10700000 | (seq & 0xFF)
    hdr3 = 0x00020000 | nevents
    now = time.time()
    timestamp_sec = int(now)
    timestamp_nsec = int((now - int(now)) * 1e9)

    f.write( struct.pack("<IIIII", hdr1, length, hdr3, timestamp_sec, timestamp_nsec) )
    f.write( os.urandom(nevents*8) )

def main():
    parser = argparse.ArgumentParser(description="Generate OCC packets and write them to file")
    parser.add_argument("type", help="Packet type", choices=["DAS", "MPS"])
    parser.add_argument("outfile", help="Output file")
    parser.add_argument("-c", "--count", help="Number of packets to generate", type=int, default=100)
    parser.add_argument("-s", "--size", help="Packet payload size in bytes, gets aligned to 4", type=lambda x: (int(x) + 3) // 4 * 4, default=2048)
    parser.add_argument("-d", "--dest", help="Destination node for MPS packets", type=int, default=0x0)
    parser.add_argument("-a", "--address", help="Address for MPS packets", type=int, default=0x0)

    args = parser.parse_args(sys.argv[1:])
    with open(args.outfile, "w") as f:
        for i in range(args.count):
            if args.type == "MPS":
                data = None
                if args.size > 0:
                    data = os.urandom(args.size)
                writeMPS(f, i, 0x0, args.dest, args.address, data)
            else:
                writeDAS(f, i, args.size)

if __name__ == "__main__":
    main()
