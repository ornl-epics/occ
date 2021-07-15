#!/bin/sh
#
# Load configuration to PCIe device
#
# Originally John Eric Breeding, August 2014

if [ ! "x$1" = "x" ]; then
	DEV=$1
else
	# Find list of PCIe SNS OCC devices
	if [ `lspci -d "10ee:7014" | wc -l` -eq 1 ]; then
		DEV=`lspci -D -d "10ee:7014" | awk '{print $1}'`
	else
		echo "Choose PCIe SNS OCC device or press ENTER to abort:"
		lspci -D -d "10ee:7014" | awk '{print $1}'
		read DEV
		[ "x$DEV" = "x" ] && exit 1
	fi
fi

ls "/sys/bus/pci/devices/$DEV" >/dev/null 2>&1
if [ $? -ne 0 ]; then
	echo "ERROR: No such device \`$DEV'"
	exit 1
fi

od -x `dirname $0`/PCI_Config_Space
cp PCI_Config_Space /sys/bus/pci/devices/$DEV/config
echo "Configuration loaded to \`$DEV'"
