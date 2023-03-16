#!/bin/bash

# SPDX-FileCopyrightText: Copyright 2019-2023 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: BSD-3-Clause

#
# run-200mb.bandwidth-latency.sh - invoke loaded-latency with common flags
#

# XXX: remember to add a space at the end of each += line!!!


# Bandwidth: run on CPU0; pass additional CPUs using brace expansion, e.g. -B{2..10..2}
BANDWIDTH_FLAGS+="-B 0 "

# Bandwidth: use a 200 MB buffer per thread
BANDWIDTH_FLAGS+="-L $((200*1000000)) "

# Bandwidth: print bandwidth every 10 calls to my_read()
BANDWIDTH_FLAGS+="-I 10 "

# Bandwidth: enable use of 2MB hugepages
#BANDWIDTH_FLAGS+="--bw-use-hugepages 2MB "



# Latency: Run on CPU1
LATENCY_FLAGS+="-l 1 "

# Latency: Use 200 MB buffer
LATENCY_FLAGS+="-n $(((200*1000000)/64)) "

# Latency: Print latency every 100000 iterations of latency loop (i.e., every 1000000 deploads)
LATENCY_FLAGS+="-i 100000 "

# Latency: Randomize pointer loop to defeat hardware prefetch
LATENCY_FLAGS+="-r "

# Latency: Allocate memory using hugepages (do sudo hugeadm --pool-pages-max DEFAULT:+1000 before running)
#LATENCY_FLAGS+="--lat-use-hugepages 2MB "


# Hardware clock frequency (CNTVCT_EL0 on aarch64, TSC on x86)

# XXX: If loaded-latency does not determine the correct frequency, uncomment
# the next line and write the correct frequency. Use --estimate-hwclock-freq
# to get an approximate value or read the kernel boot log for the frequency.

#HWCLOCK_FREQ=2199998000

if [ -n "$HWCLOCK_FREQ" ]; then
	MORE_FLAGS+="--hwclock-freq $HWCLOCK_FREQ "
fi

# Delay time for all threads to finish setting up

# XXX: Adjust DELAY_TIME_SECONDS so that the latency thread gets enough time
# to set up its loop, or pass a new "--delay-seconds seconds" to this script

DELAY_TIME_SECONDS=6

if [ -n "$DELAY_TIME_SECONDS" ]; then
	MORE_FLAGS+="--delay-seconds $DELAY_TIME_SECONDS "
fi

# Run measurement for 5 seconds
DURATION_FLAGS="-D 5 "

# XXX: adjust CPU_FREQ_MHZ to have correct cycle count calculations
CPU_FREQ_MHZ=2200

MORE_FLAGS+="--cpu-freq-mhz $CPU_FREQ_MHZ "

set -x

./loaded-latency \
 $BANDWIDTH_FLAGS \
 $LATENCY_FLAGS \
 $DURATION_FLAGS $MORE_FLAGS $*
