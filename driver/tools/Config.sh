#!/bin/sh
#
# Load configuration to PCIe device
#
# Originally John Eric Breeding, August 2014

DEV=$1
BASEDIR=`dirname $0`

if [ "x$DEV" = "x" ]; then
	echo "Usage: %0 <device name>"
	echo ""
	echo "Example: %0 /dev/snsocb0"
	echo ""
	exit 1
fi

ls $DEV >/dev/null 2>/dev/null
if [ $? -ne 0 ]; then
	echo "ERROR: Device file \`$DEV' not found"
	exit 1
fi

DEVNAME=`basename $DEV`

od -x $BASEDIR/PCIe_Config_Space
cp PCI_Config_Space /sys/class/snsocb/$DEVNAME/device/config

