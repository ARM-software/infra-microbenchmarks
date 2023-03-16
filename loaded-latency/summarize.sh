#!/bin/bash

# SPDX-FileCopyrightText: Copyright 2019-2023 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: BSD-3-Clause

# Run this on the output of sweep.finedelay.sh to create a table, e.g.
#
#  ./sweep.finedelay.sh ./run-200mb.bandwidth-latency.sh | tee bw-lat.log
#
#  ./summarize.sh bw-lat.log
#

set -e

echo -e "F\tBandwidth\tLatency"
paste \
<(grep -E "^fine loop delay" $1 | sed -e 's/.*= //g;s/\r//') \
<(grep -E "^Total Bandwidth" $1 | sed -e 's/.*= //;s/ MB\/sec//;s/\r//') \
<(grep -E "^Average Latency" $1 | sed -e 's/.*= //;s/ ns//;s/\r//')
