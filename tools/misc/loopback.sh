#!/bin/sh

# This little script is a wrapper around occ_proxy binary and makes it act
# as a loopback tester. It first generates a data file with OCC packets
# that are then sent through OCC device. Packets received from OCC are saved to a
# second file. Both files are expected to be the same which is ensured by
# byte-comparing them. An OCC loopback connection is expected for this test.
#
# Script takes following arguments
# 1 - OCC device file
# 2 - packet type, either DAS or MPS
# 3 - optional number of packets, defaults to 1000
# 4 - per packet payload size

OCCDEV=${1:-/dev/snsocc0}
TYPE=${2:-DAS}
COUNT=${3:-1000}
SIZE=${4:-2048}
TXFILE=data/tx_${TYPE}_${COUNT}x${SIZE}b.raw
RXFILE=data/rx_${TYPE}_${COUNT}x${SIZE}b.raw
OCCPROXY=$(dirname $(realpath $0))/../proxy/occ_proxy

mkdir -p data
./generate_packets.py -c $COUNT -s $SIZE $TYPE $TXFILE
$OCCPROXY $OCCDEV < $TXFILE > $RXFILE
diff -s $TXFILE $RXFILE
