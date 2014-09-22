#!/bin/bash

# Assign isolated CPU(s) to given driver's IRQ handlers only.
# Non-driver existing and future interrupts are assigned to non-isolated CPUs
# except for some system interrupts which must run on all CPUs (ie. timer).
#
# September 2014
# Klemen Vodopivec <klemen.vodopivec@cosylab.com>

DRIVER=sns_ocb # No '-' in driver name, replace '-' with '_'

all_cpus=`lscpu | grep '^CPU(s):' | awk -F':' '{print $2}' | tr -d ' '`
all_mask="0x`echo "obase=16; (2 ^ $all_cpus) - 1" | bc | tr A-F a-f`"

# Find mask of available CPUs by looking at init's mask
init_pid=`pidof init`
init_mask="0x`taskset -p $init_pid | awk -F':' '{print $2}' | tr -d ' '`"
let "isol_mask = ~($all_mask & $init_mask) & $all_mask"
if (($isol_mask == 0)); then
	echo "No isolated CPU found!"
	echo "Try using isolcpus=<cpu id> kernel parameter"
	exit 1
fi
init_mask=${init_mask:2} # strip away leading 0x
isol_mask=`echo "obase=16; $isol_mask" | bc` # convert to hex

# Find drivers interrupts, more than 1 possible
irqs=`grep "$DRIVER" /proc/interrupts | awk -F':' '{print $1}' | tr -d ' ' | tr '\n' ' '`
if [ "x$irqs" = "x" ]; then
	echo "Driver \`$DRIVER' not loaded!"
	exit 1
fi

# Set existing IRQ handlers to non-isolated mask
for file in /proc/irq/[0-9]*/smp_affinity; do
	echo $init_mask > $file
done

# Also all future loaded drivers and their IRQs
echo $init_mask > /proc/irq/default_smp_affinity

# Put our driver on isolated CPU
for irq in $irqs; do
	echo $isol_mask > /proc/irq/$irq/smp_affinity
done

# Print success message
cpus=""
isol_mask="0x$isol_mask"
for ((cpu=0; cpu<$all_cpus; cpu++)); do
	if ((((1 << $cpu) & $isol_mask) != 0)); then
		cpus="$cpus$cpu "
	fi
done
echo "Isolated IRQ(s) $irqs to CPU(s) $cpus"
