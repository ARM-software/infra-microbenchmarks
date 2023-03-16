#!/bin/sh

# SPDX-FileCopyrightText: Copyright 2019-2023 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: BSD-3-Clause

# This script runs a subordinate_script (run-200mb.bandwidth-latency.sh by
# default) across a range of values for loaded-latency's -F / --bw-fine-delay
# to vary the memory bandwidth from very low to very high.
#
#   sweep.finedelay.sh [subordinate_script] [flags to pass to loaded-latency]
#
# The number of bandwidth CPUs can be specified as additional parameters, which
# will be further passed on to loaded-latency.  This can be easily expressed using
# bash brace expansion, e.g. for additional CPUs starting from CPU 2 thru CPU 8
# in steps of 3 (i.e. CPU2, CPU5, and CPU8), it can be invoked using
#
#   ./sweep.finedelay.sh -B{2..8..3}
#
# which expands the flags into
#
#   ./sweep.finedelay.sh -B2 -B5 -B8
#

MYSCRIPT="./run-200mb.bandwidth-latency.sh"

# if the first argument is executable, use it as the script
if [ $# -gt 0 -a -x "$1" ]
then
	MYSCRIPT=./$1
	shift
fi

# sweep bandwidth fine interval downwards to increase memory bandwidth
for F in 100 50 30 20 15 10 5 0; do
	echo $MYSCRIPT -F $F -C 0 $*
	     $MYSCRIPT -F $F -C 0 $*
done
