#!/bin/bash

# SPDX-FileCopyrightText: Copyright 2019-2023 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: BSD-3-Clause

# This script demonstrates one method of measuring bandwidth scaling vs CPU
# count by constructing a series of -B flags to specify on which CPUs to run
# bandwidth threads.  run-200mb.bandwidth-only.sh uses CPU0 by default, and the
# -B1 hard coded in the loop below has it also use CPU1.  The additional loop
# parameters build flags to use CPU2 through CPU14 in steps of 2, and then
# CPU15 through CPU23 in steps of 1.  The number of bandwidth threads and the
# total bandwidth are gathered and printed after each measurement.  The
# expected observation is a leveling-off of bandwidth after a number of CPUs,
# the configuration of which is to be used for the latency-vs-bandwidth
# characterization.

echo -e 'n\tbandwidth'
for n in {2..14..2} {15..23} ; do
    ./run-200mb.bandwidth-only.sh -B1 $(eval "echo -B{2..$n}") -d 1 | \
    perl -ne 'if (m/^Total of (\d+) bandwidth threads requested/) { print "$1\t"; } elsif (m/Total Bandwidth = (\S+) MB\/sec/) { print "$1\n"; }'
done

