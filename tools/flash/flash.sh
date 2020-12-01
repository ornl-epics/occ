#!/bin/sh

DIR=`dirname $0`

if [ -z $2 ]; then
	echo "Usage: $0 <device file> <bin file>"
	exit 1
fi
if [ ! -e $1 ]; then
	echo "ERROR: Invalid device $1"
	exit 1
fi
if [ ! -r $2 ]; then
	echo "ERROR: Can't read file $2"
	exit 1
fi

for i in `seq 0 3`; do
	$DIR/occ_flash -d $1 -e -s $i
done

$DIR/occ_flash -d $1 -s 0 -p $2
