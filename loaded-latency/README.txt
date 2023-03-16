"loaded-latency" Benchmark
~~~~~~~~~~~~~~~~~~~~~~~~~~

SPDX-FileCopyrightText: Copyright 2019-2023 Arm Limited and/or its affiliates <open-source-office@arm.com>
SPDX-License-Identifier: BSD-3-Clause


The "loaded-latency" benchmark measures memory latency against a controlled
amount of concurrent memory bandwidth in a single measurement.


Building
========

Install and have gcc in the $PATH.

Build by typing 'make' at the directory containing the Makefile.

The target will be created as "loaded-latency" in the same directory.


Flags
=====


./loaded-latency [args]

 -D | --duration              seconds     how many seconds to run
 -S | --random-seed           seedval     set random seed to seedval
 -d | --delay-seconds         seconds     how many seconds to wait for threads to start together
      --delay-ticks           ticks       how many HWCOUNTER ticks to wait for threads to start together
      --show-per-thread-concurrency       show per-thread concurrency metrics
 -Q | --mitigate-spectre-v4               enable mitigation for Spectre v4 (SSBD=1 or SSBS=0) using prctl()
 -q | --hwclock-freq          freq_hz     frequency in Hz of the hwclock counter
      --estimate-hwclock-freq cpu_num     measure and estimate the hardware clock frequency in Hz on CPU cpu_num
 -f | --cpu-freq-mhz          frequency   CPU frequency in MHz for calculating cycles
 -t | --cpu-cycle-time-ns     nanoseconds CPU cycle time in nanoseconds for calculating cycles
             CPU frequency and cycle time are NOT auto-detected!
             The last -f or -t overrides earlier parameters for computing cycles.

latency flags:
 -l | --lat-cpu               cpu_num  CPU on which to run a latency thread.  Repeat for additional CPUs.
 -n | --lat-cacheline-count   count    number of sequential cachelines of memory to use for latency measurement
 -e | --lat-secondary-delay   ticks    how many additional ticks for secondary latency threads to start
 -i | --lat-iterations        iters    number of iterations between latency measurement interim reports
 -z | --lat-cacheline-bytes   bytes    cacheline length for latency measurement
 -j | --lat-cacheline-stride  count    number of cachelines to skip between loads for latency measurement
 -o | --lat-offset            count    number of deploads to advance secondary latency threads
 -c | --lat-clear-cache                clear caches before latency run
 -r | --lat-randomize                  randomize ordering of dependent loads
 -h | --lat-use-hugepages     size     hugepage size to use for latency. Use "-h help" to show known sizes
 -w | --lat-warmup-cpu        cpu_num  on which CPU to warm up latency loop (repeat for additional CPUs)
 -s | --lat-shared-memory              use the same memory for all latency threads
 -u | --lat-shared-memory-init-cpu cpu_num   on which CPU to initialize the latency shared memory

bandwidth flags:
 -B | --bw-cpu                cpu_num  CPU on which to run a bandwidth thread.  Repeat for additional CPUs.
 -L | --bw-buflen             bytes    memory buffer size for bandwidth loop
 -I | --bw-iterations         iters    iterations of the bandwidth loop to run between interim reports
 -F | --bw-fine-delay         count    bandwidth fine delay (inner loop nops).  Increase to slow bandwidth.
 -C | --bw-coarse-delay       count    bandwidth coarse delay (coarse loop nops).  Increase to slow bandwidth.
 -H | --bw-use-hugepages      size     hugepage size to use for bandwidth. Use "-H help" to show known sizes.
 -Z | --bw-cacheline-bytes    bytes    cacheline length for bandwidth memory region size
 -W | --bw-write                       instead of reads, use writes for memory bandwidth traffic

 --help                                this screen

Example using bash arithmetic for 64MB latency loop and 96MB bandwidth buffer:

./loaded-latency --lat-cpu 0 --lat-cacheline-count $((64*1024*1024/64)) --lat-iterations 100000 --lat-randomize \
   --bw-cpu 1 --bw-buflen $((96*1024*1024)) --bw-fine-delay 100 --bw-iterations 30


Functional Description
======================

"loaded-latency" consists of the main process, zero or more memory latency
measurement threads, and zero or more memory bandwidth measurement/generator
threads.  The main process parses command line arguments and creates the
latency and bandwidth threads.  The latency threads measure memory latency.
The bandwidth threads generate and measure memory bandwidth.  All threads are
to be run on separate CPUs, assigned at the start of the benchmark.

The amount of bandwidth generated is adjustable through the insertion of
fine and coarse delays between memory operations.  Typically, as the amount
of memory bandwidth increases and nears the system's maximum performance,
the memory latency increases due to reasons such as contention.

All of the measurement threads run concurrently, independently, and without
interdependent synchronization.  When each thread is ready to start
measuring, it polls the hardware clock for an anticipated start time and
then runs its configured operation.  Once a thread has exceeded the duration
of time for the measurement, it is joined back to the main process.  The
main process then summarizes the measurements from the latency and bandwidth
threads.

Since the number of bandwidth threads can be 0, loaded-latency can be used
as a memory latency benchmark.

Since the number of latency threads can be 0, loaded-latency can be used as
a memory bandwidth benchmark.


General Sense of the Flags
--------------------------

All of the command line flags (other than --estimate-hwclock-freq and
--help) are available as short or long names.

  - latency flags are single-letter lower case for short flags and use the
    prefix --lat- for long flags

  - bandwidth flags are single-letter upper case for short flags and use the
    prefix --bw- for long flags

The --duration flag specifies in seconds the duration of the measurement.
Important: on x86_64, see the next subsection "Synchronizing on the Hardware
Clock" and "Known Limitations" about using --hwclock-freq to override the TSC
frequency, which is used for timing the measurement duration.

The CPUs on which the latency and bandwidth threads run is specified using
the --lat-cpu and --bw-cpu flags.  The number of threads of each type to run
is specified by the number of times each flag is used.  For example, the
following set of flags runs latency threads on CPU 0 and CPU 1 and a
bandwidth thread on CPU 2 for a total of 3 threads:

    ./loaded-latency --lat-cpu 0 --lat-cpu 1 --bw-cpu 2

The --cpu-freq-mhz and --cpu-cycle-time-ns flags tell the latency threads
how to compute the latency in cycles from the time in nanoseconds.  See
"Known Limitations" for important details.



Synchronizing on the Hardware Clock
-----------------------------------

The CPU hardware clock (CNTVCT_EL0 on AArch64, TSC on x86-64) is assumed to
be synchronous and incrementing at a constant rate on all CPUs.  The hardware
clock is read by each thread to decide when to start and stop memory
operations.  See "Known Limitations" for more on the assumptions made.

At the start of the program, the hardware clock is read and the delay value
from the --delay-seconds (converted to hwclock ticks) or the --delay-ticks
flag is added to it.  This combined value is the hwcounter_start, and it is
passed to each thread that is created.  Each thread polls the hardware clock
to see if that value has been met, and when it has, it starts running the
workload.

The duration of the measurement (from the --duration flag) is converted to
hwclock cycles and added to the hwcounter_start value to form the
hwcounter_stop value.  hwcounter_stop is also passed to each thread, which
checks the hardware clock after each iteration of its work to see if the
requested duration has been met.  See "Known Limitations" about the possible
need to override the hardware clock frequency using --hwclock-freq.



Interim Performance and Iterations
----------------------------------

During the measurement operation, each thread periodically reports an interim
performance measurement.  Latency threads report interim latency and bandwidth
threads report interim bandwidth.  The interval between reports is based on the
number of iterations of the operation.  For latency threads, the
--lat-iterations flag specifies the number of iterations of the inner-most
latency loop.  For bandwidth threads, the --bw-iterations flag specifies the
number of times to read or write the entire memory buffer.

The following example shows the interim measurements from two bandwidth
threads (CPU2, CPU3) and one latency thread (CPU0):

---------------------------------------------------------------------------
./loaded-latency  -l 0 -B 3 -B 2 -d 6 -f 2395 --bw-buflen 1 --bw-iterations 100000000 --duration 1

   ...

CPU2 BWTHREAD0:  buflen = 1, iterations = 100000000, inner_nops = 0, outer_nops = 0, hwcounter_start = b9fec4c77e41, bw_cacheline_bytes = 64, bw_use_hugepages = 0, tid = 85151
CPU3 BWTHREAD1:  buflen = 1, iterations = 100000000, inner_nops = 0, outer_nops = 0, hwcounter_start = b9fec4c77e41, bw_cacheline_bytes = 64, bw_use_hugepages = 0, tid = 85152
CPU0 LATTHREAD0: cacheline_count = 16385, iterations = 10000000, mem = 0xffff94000900, randomize = 0, use_hugepages = 0, hwcounter_start = b9fec4c77e41, lat_offset = 0, tid = 85150
CPU0 LATTHREAD0: started at CNTVCT = b9fec4c77e41
CPU3 BWTHREAD1:  started at CNTVCT = b9fec4c77e41
CPU2 BWTHREAD0:  started at CNTVCT = b9fec4c77e41
CPU0 LATTHREAD0: 4.450940 ns, 10.660001 cycles       \
CPU3 BWTHREAD1: 170.714967 MB/sec                    |
CPU2 BWTHREAD0: 170.647317 MB/sec                    |
CPU0 LATTHREAD0: 4.432790 ns, 10.616532 cycles       |-- interim measurements
CPU3 BWTHREAD1: 170.721536 MB/sec                    |
CPU2 BWTHREAD0: 170.589660 MB/sec                    /
Joined BWTHREAD0, avg_bw = 170.618489 MB/sec
Joined BWTHREAD1, avg_bw = 170.718252 MB/sec
CPU0 LATTHREAD0: 4.383490 ns, 10.498459 cycles
Joined LATTHREAD0, avg_latency = 4.441865 ns

Total Bandwidth = 341.336740 MB/sec
Average Latency = 4.441865 ns

   ...

---------------------------------------------------------------------------

The appropriate value to give for these iteration flags is to be determined
by manually by trying different values and seeing if the interim reports
cause too much information to be printed too quickly or not.  If per-thread
interim information is printed too quickly (and possibly causing too much
overhead), increase the number of iterations to reduce how often they are
printed.  If it takes too long, decrease the number of iterations.

The values for these flags also affects the degree of concurrency between
threads because the hardware clock is read before running each measurement
loop.  If the number of iterations causes the loop to take a very long time
to run, other threads may finish sooner so the last interim measurement may
not experience as much contention and report a higher performance than from
other interim samples.  To compensate for this, each latency thread drops
the lowest interim latency sample from the calculation of the average
latency.  However, bandwidth threads do not compensate and report the
bandwidth for all iterations.


Concurrency Coverage Metrics
----------------------------

Hardware clock-based timestamp metrics are shown at the end of the
loaded-latency output that summarize how well synchronized the threads
actually operated.

For the bandwidth threads as a group, the max and min hardware clock values
(corresponding to the youngest and oldest) are shown to depict the range of
their start and stop times.  The same is done for the latency threads.  The
smaller the max-min diff, the better synchronized and concurrent that the
threads were running.

bandwidth start and stop synchronicity metrics
---------------------------------------------------------------------------
bw_hwcounter_start_max  = 0xd591d3144aca
bw_hwcounter_start_min  = 0xd591d3143c6f      max-min diff = 3675 (0.000073 seconds)
lat_hwcounter_start_max = 0xd591d2e3ce03
lat_hwcounter_start_min = 0xd591d2e3ce03      max-min diff = 0 (0.000000 seconds)

bw_hwcounter_stop_max   = 0xd591dcbc345a
bw_hwcounter_stop_min   = 0xd591dc24e850      max-min diff = 9915402 (0.198308 seconds)
lat_hwcounter_stop_max  = 0xd591dd01ea5b
lat_hwcounter_stop_min  = 0xd591dcd8cd8e      max-min diff = 2694349 (0.053887 seconds)
---------------------------------------------------------------------------

Finally, the spread between the oldest bandwidth and oldest latency threads
is shown.  A smaller bw_lat_start_spread_ticks means the bandwidth and
latency threads started more closely together.

bandwidth-vs-latency concurrency coverage
---------------------------------------------------------------------------
bw_lat_start_spread_ticks = 3173996 (0.063480 seconds)
bw_lat_stop_spread_ticks  = -11789630 (-0.235793 seconds)
---------------------------------------------------------------------------

The spreads should be considered in context with the --duration value and
the per-thread durations revealed from using --show-per-thread-concurrency
to have confidence that all of the requested threads have run concurrently
with each other.  See "Per-thread Concurrency Metrics" in the "Other Flags"
section below.



Memory Latency
==============

Memory latency is measured by traversing a circular loop of dependent
pointers, one per cache line, and computing the average time for each
pointer dereference.

  - Each latency thread makes its own latency loop, unless
    --lat-shared-memory-init-cpu and --lat-shared-memory is used.

  - Each latency thread measures its own latency loop, unless
    --lat-shared-memory is used.

Increasing the number of cache lines covered by a latency loop generally
increases the memory capacity pressure.  The expected result of measuring a
loop whose span is sized to completely fit into a level of a cache hierarchy
is the memory latency to that level of cache.

  - For example, measuring a loop that spans 900 KB should give the latency
    of a 1 MB cache

The amount of memory that the latency loop spans over is specified by the
--lat-cacheline-count flag.

  - Conversion of the amount of memory in units of KB or MB must be done
    using other means, such as using shell math operations.  For example, a
    900 KB span using 64-byte cachelines can be specified in bash using:

      --lat-cacheline-count $((900*1024/64))

  - The stride between pointers can be increased using the
    --lat-cacheline-stride flag with values greater than 1.  The default
    value is 1, which means the cache lines are sequentially addressed.

  - Note that --lat-cacheline-count is misleadingly named because it assumes
    the pointers are placed in sequentially addressed cachelines (the
    default).  If the stride is greater than 1, then the number of
    cachelines used in the loop is reduced because some are skipped because
    the span of memory covered by the loop is not reduced (other than the
    cache lines at the end of the span that are not terminated by a pointer
    to the first one of the span).

Using the --lat-randomize flag randomizes the placement of sequentially
dependent pointers using lrand48().  The seed for srand48() is computed from
gettimeofday() or given using --random-seed.


Shared Memory Latency
---------------------

The --lat-shared-memory flag has all of the latency threads use the same
latency loop instead of each thread making and running their own.

The --lat-shared-memory-init-cpu flag specifies the CPU on which to
initialize the loop.  It need not be one of the CPUs that runs a latency
thread.

The --lat-clear-cache flag flushes the loop of pointers to main memory to
ensure there are no modified lines in the cache after the loop
initialization.

The --lat-offset flag advances all of the secondary latency threads by
approximately the given number of pointers.  The intent is to not have the
threads competing for the same cache line, at least at the beginning of the
measurement.  However, all threads operate independently, and there is no
mechanism to maintain the offset.

The --lat-secondary-delay staggers the start time of all secondary latency
threads.



Memory Bandwidth
================

Memory bandwidth is generated by making sequential accesses through a
contiguous region of memory using one access per sequentially addressed
cache line.  The bandwidth reported is the length of memory in bytes times
the number of iterations divided by the time spent in seconds.

The --bw-cpu flag specifies on which CPUs to run bandwidth threads.

The --bw-buflen flag specifies the length in bytes of the memory region
processed by each bandwidth thread.

The --bw-iterations flag specifies the number of full iterations of the
memory region to process before showing an interim bandwidth measurement.

The --bw-write flag selects to use writes for the bandwidth memory
operation.  The default is to use reads.

The --bw-fine-delay and --bw-coarse-delay flags are used to throttle the
amount of bandwidth generated by adding delays.  Both flags can be specified
together.

  --bw-fine-delay adds delays between each memory access
  --bw-coarse-delay adds delays between full iterations of the memory region



Bandwidth Control
-----------------

The amount of memory bandwidth depends on the memory region length, the
number of bandwidth threads, and the throttling delays.

  - The memory region length affects whether or not it can fit into a level
    of cache.  For main memory, the length should be much larger than the
    size of the caches.

  - The number of bandwidth threads may limit bandwidth due to contention
    between threads.  Also, each bandwidth thread creates its own buffer, so
    the total amount of memory used is the number of threads times the
    memory region length.

  - The throttling delays directly affect the rate at which memory accesses
    are made per thread.  The delays are the primary control to adjust the
    amount of memory bandwidth generated.

The amount of bandwidth reduced by increasing the delay depends on many
factors, so it is best to try out various delays to see how much bandwidth
is provided and then remeasure with the latency against the delay
configurations.




Latency-vs-Bandwidth Characterization
=====================================

The main purpose of loaded-latency is to measure memory latency under memory
bandwidth load.  A characterization of the relationship between latency and
bandwidth can be drawn on a graph, and it will usually be in the shape of a
curve where the latency increases as the bandwidth nears a maximum.

A single invocation of loaded-latency provides one data point of the
latency-vs-bandwidth curve.  Collecting the latency against a range of
bandwidth is done by specifying different delay values to adjust the amount
of bandwidth and/or changing the number of bandwidth threads and their CPU
assignment.  There are many different possible arrangements afforded by
loaded-latency, but which may require many flags to be used.  To make
reproducibility tractable, the configurations and measurements should be
programmed using shell scripting.


Latency-vs-Bandwidth Characterization Using Provided Scripts
------------------------------------------------------------

Example scripts are provided for latency-vs-bandwidth characterization using
loaded-latency.

run-200mb.latency-only.sh
  - measures random memory latency over 200MB of memory from CPU1

run-200mb.bandwidth-only.sh
  - measures/generates bandwidth loading on 200MB of memory from CPU0

run-200mb.bandwidth-latency.sh
  - measures/generates bandwidth loading on 200MB of memory from CPU0 and
    measures latency on CPU1

bandwidth-scaling.sh
  - demonstration script for measuring bandwidth scaling

sweep.finedelay.sh
  - invokes run-200mb.bandwidth-latency.sh or a script with varying
    --bw-fine-delay values to adjust the amount of bandwidth from very low
    to very high

summarize.sh
  - filters the output of sweep.finedelay.sh to make a table of latency and
    bandwidth for each fine delay setting


Procedure:

1.  Choose a logical CPU on which to run a bandwidth thread and another
    logical CPU on which to run a latency thread.  The provided scripts use
    CPU0 for bandwidth and CPU1 for latency.

    On some systems, CPU0 and CPU1 are hardware threads/vCPUs of the same
    physical core.  Measuring both bandwidth and latency on the same
    physical core may have a different performance behavior than if they
    were to be run on different physical cores.  If this is an issue, the
    CPU for the latency thread should be changed by changing the value of
    the "-l" parameter in the LATENCY_FLAGS variable.

    For example:

      --------------------------------------------------------------------
      In run-200mb.bandwidth-latency.sh:

      # Latency: Run on CPU15, not CPU1
      LATENCY_FLAGS+="-l 15 "
      --------------------------------------------------------------------


2.  Ensure that loaded-latency knows the frequency of the system's hardware
    clock (TSC on x86 or CNTVCT_EL0 on aarch64).  This is necessary for an
    accurate measurement duration (-D|--duration seconds).

      - The hwclock frequency can be estimated for CPU 0 by running

	   ./loaded-latency --estimate-hwclock-freq 0"

      - On aarch64, the CNTVCT_EL0 frequency is read from the CNTFRQ_EL0
	register.  loaded-latency prints the frequency on the line that
	reports the delay_seconds parameter (-d).

      - On x86_64, the default frequency is hard-coded to 2900000000 Hz in
	get_default_cntfreq() in rdtsc.h.  The TSC frequency that is
	determined by the system kernel can be seen in the boot messages by
	running "sudo dmesg | grep TSC".

    If loaded-latency does not determine the frequency correctly, use the
    --hwclock-freq flag to set the correct value.

    In run-200mb.bandwidth-only.sh and run-200mb.bandwidth-latency.sh, the
    HWCLOCK_FREQ_HZ variable adjust this value.  For a system with a
    3417.600 MHz TSC, make this change:

      --------------------------------------------------------------------
      In run-200mb.bandwidth-latency.sh and run-200mb.bandwidth-only.sh:

      HWCLOCK_FREQ_HZ=3417600000
      --------------------------------------------------------------------


3.  Determine the CPU clock frequency in MHz, and set CPU_FREQ_MHZ to it in
    run-200mb.bandwidth-only.sh and run-200mb.bandwidth-latency.sh.  For a
    system with 5.2 GHz CPU clock frequency, make this change:

      --------------------------------------------------------------------
      In run-200mb.bandwidth-latency.sh and run-200mb.bandwidth-only.sh:

      CPU_FREQ_MHZ=5200
      --------------------------------------------------------------------

    This is cosmetic; it is only used for converting seconds to CPU cycles.


4.  Run run-200mb.bandwidth-latency.sh without any parameters to see if the
    delay time needs to be increased so that the latency thread starts at
    the same time as the bandwidth thread.  The delay time variable is
    DELAY_TIME_SECONDS and it is set to 6 seconds by default.

      ./run-200mb.bandwidth-latency.sh

    loaded-latency prints concurrency coverage metrics at the very end of
    its output.  The max start time difference between the bandwidth and
    latency threads is shown in bw_lat_start_spread_ticks.

    If bw_lat_start_spread_ticks is 0 (or close to 0), then the threads
    started together.

    GOOD starting concurrency example:

       bw_lat_start_spread_ticks   = 0 (0.000000 seconds)
       bw_lat_stop_spread_ticks    = -14939656 (-0.597586 seconds)

    If bw_lat_start_spread_ticks is not 0, then the threads did not start at
    the same time.

    BAD starting concurrency example:

       bw_lat_start_spread_ticks   = -583202197 (-11.664044 seconds)
       bw_lat_stop_spread_ticks    = -315400963 (-6.308019 seconds)

    In this case, increase the value of DELAY_TIME_SECONDS in
    run-200mb.bandwidth-latency.sh by the absolute value of the
    bw_lat_start_spread_ticks in seconds and a little margin.  For the
    values shown in this example, the latency thread started 11.66 seconds
    after the bandwidth thread, even with the default 6 seconds of delay, so
    edit run-200mb.bandwidth-latency.sh to have DELAY_TIME_SECONDS=18, which
    is 6 + ROUNDUP(11.66) = 18 seconds.

      --------------------------------------------------------------------
      In run-200mb.bandwidth-latency.sh:

      DELAY_TIME_SECONDS=18
      --------------------------------------------------------------------

    Except for extreme configurations, the delay time should not need to be
    increased in run-200mb.bandwidth-only.sh since only bandwidth threads
    are running and they don't usually need a very long setup time.


5.  Do a bandwidth scaling measurement to determine how many and on which
    CPUs to run bandwidth threads.

    bandwidth-scaling.sh does a bandwidth scaling measurement over a range
    and number of CPUs.  An example output of the script is shown below,
    which shows the bandwidth leveling off after using n=15 CPUs.  This
    means that "-B{0..14}" can be used as the bandwidth CPU specifier.

    Note that bandwidth-scaling.sh is provided only as an example and that
    other CPU arrangements may achieve a larger total bandwidth.  Additional
    configuration options (such as explicit CPU selection or numactl
    --membind settings) may be needed to elicit the desired system
    performance behavior.

      --------------------------------------------------------------------
      $ ./bandwidth-scaling.sh
      n       bandwidth
      + ./loaded-latency -B 0 -L 200000000 -I 10 --delay-seconds 6 --hwclock-freq 3417600000 -D 5 --cpu-freq-mhz 5200 -B1 -B2 -d 1
      3       52536.365556
      + ./loaded-latency -B 0 -L 200000000 -I 10 --delay-seconds 6 --hwclock-freq 3417600000 -D 5 --cpu-freq-mhz 5200 -B1 -B2 -B3 -B4 -d 1
      5       55290.059419
      + ./loaded-latency -B 0 -L 200000000 -I 10 --delay-seconds 6 --hwclock-freq 3417600000 -D 5 --cpu-freq-mhz 5200 -B1 -B2 -B3 -B4 -B5 -B6 -d 1
      7       57308.756810
      + ./loaded-latency -B 0 -L 200000000 -I 10 --delay-seconds 6 --hwclock-freq 3417600000 -D 5 --cpu-freq-mhz 5200 -B1 -B2 -B3 -B4 -B5 -B6 -B7 -B8 -d 1
      9       60201.285194
      + ./loaded-latency -B 0 -L 200000000 -I 10 --delay-seconds 6 --hwclock-freq 3417600000 -D 5 --cpu-freq-mhz 5200 -B1 -B2 -B3 -B4 -B5 -B6 -B7 -B8 -B9 -B10 -d 1
      11      65079.768756
      + ./loaded-latency -B 0 -L 200000000 -I 10 --delay-seconds 6 --hwclock-freq 3417600000 -D 5 --cpu-freq-mhz 5200 -B1 -B2 -B3 -B4 -B5 -B6 -B7 -B8 -B9 -B10 -B11 -B12 -d 1
      13      68009.702640
      + ./loaded-latency -B 0 -L 200000000 -I 10 --delay-seconds 6 --hwclock-freq 3417600000 -D 5 --cpu-freq-mhz 5200 -B1 -B2 -B3 -B4 -B5 -B6 -B7 -B8 -B9 -B10 -B11 -B12 -B13 -B14 -d 1
      15      69409.477689
      + ./loaded-latency -B 0 -L 200000000 -I 10 --delay-seconds 6 --hwclock-freq 3417600000 -D 5 --cpu-freq-mhz 5200 -B1 -B2 -B3 -B4 -B5 -B6 -B7 -B8 -B9 -B10 -B11 -B12 -B13 -B14 -B15 -d 1
      16      68546.612210
      + ./loaded-latency -B 0 -L 200000000 -I 10 --delay-seconds 6 --hwclock-freq 3417600000 -D 5 --cpu-freq-mhz 5200 -B1 -B2 -B3 -B4 -B5 -B6 -B7 -B8 -B9 -B10 -B11 -B12 -B13 -B14 -B15 -B16 -d 1
      17      69830.059886
      + ./loaded-latency -B 0 -L 200000000 -I 10 --delay-seconds 6 --hwclock-freq 3417600000 -D 5 --cpu-freq-mhz 5200 -B1 -B2 -B3 -B4 -B5 -B6 -B7 -B8 -B9 -B10 -B11 -B12 -B13 -B14 -B15 -B16 -B17 -d 1
      18      70831.429746
      + ./loaded-latency -B 0 -L 200000000 -I 10 --delay-seconds 6 --hwclock-freq 3417600000 -D 5 --cpu-freq-mhz 5200 -B1 -B2 -B3 -B4 -B5 -B6 -B7 -B8 -B9 -B10 -B11 -B12 -B13 -B14 -B15 -B16 -B17 -B18 -d 1
      19      71446.999147
      + ./loaded-latency -B 0 -L 200000000 -I 10 --delay-seconds 6 --hwclock-freq 3417600000 -D 5 --cpu-freq-mhz 5200 -B1 -B2 -B3 -B4 -B5 -B6 -B7 -B8 -B9 -B10 -B11 -B12 -B13 -B14 -B15 -B16 -B17 -B18 -B19 -d 1
      20      70787.891281
      + ./loaded-latency -B 0 -L 200000000 -I 10 --delay-seconds 6 --hwclock-freq 3417600000 -D 5 --cpu-freq-mhz 5200 -B1 -B2 -B3 -B4 -B5 -B6 -B7 -B8 -B9 -B10 -B11 -B12 -B13 -B14 -B15 -B16 -B17 -B18 -B19 -B20 -d 1
      21      71441.288851
      + ./loaded-latency -B 0 -L 200000000 -I 10 --delay-seconds 6 --hwclock-freq 3417600000 -D 5 --cpu-freq-mhz 5200 -B1 -B2 -B3 -B4 -B5 -B6 -B7 -B8 -B9 -B10 -B11 -B12 -B13 -B14 -B15 -B16 -B17 -B18 -B19 -B20 -B21 -d 1
      22      70733.766310
      + ./loaded-latency -B 0 -L 200000000 -I 10 --delay-seconds 6 --hwclock-freq 3417600000 -D 5 --cpu-freq-mhz 5200 -B1 -B2 -B3 -B4 -B5 -B6 -B7 -B8 -B9 -B10 -B11 -B12 -B13 -B14 -B15 -B16 -B17 -B18 -B19 -B20 -B21 -B22 -d 1
      23      71975.708254
      + ./loaded-latency -B 0 -L 200000000 -I 10 --delay-seconds 6 --hwclock-freq 3417600000 -D 5 --cpu-freq-mhz 5200 -B1 -B2 -B3 -B4 -B5 -B6 -B7 -B8 -B9 -B10 -B11 -B12 -B13 -B14 -B15 -B16 -B17 -B18 -B19 -B20 -B21 -B22 -B23 -d 1
      24      71415.340545
      --------------------------------------------------------------------


6.  Run sweep.finedelay.sh to do the latency-vs-bandwidth characterization.

    Use the bandwidth CPU specifier found from the previous step (i.e.,
    the set of -B flags of one run) as the argument to sweep.finedelay.sh
    and record the entire output to a log file.

      --------------------------------------------------------------------
      ./sweep.finedelay.sh -B{0..14} | tee latency-bandwidth.15cpus.log
      --------------------------------------------------------------------


7.  Summarize the latency-vs-bandwidth data from the log file

    The summarize.sh script takes the log file from the previous step and
    creates a data table.  An example:

      --------------------------------------------------------------------
      ./sumarize.sh latency-bandwidth.15cpus.log

      F       Bandwidth       Latency
      100     31514.617233    107.735700
      50      45840.835062    111.107000
      30      54363.198437    114.670600
      20      65255.176376    120.132875
      15      64394.863088    120.720600
      10      61902.045898    131.213967
      5       63540.847809    133.228933
      0       67308.731034    149.808167
      --------------------------------------------------------------------

    F is the --bw-fine-delay parameter.  As it is decreased, more total
    bandwidth is generated in the system, and a corresponding effect on
    latency can be measured.  The Bandwidth and Latency data can then be
    plotted on an X-Y chart to show their relationship.



Other Flags
===========


Per-thread Concurrency Metrics
------------------------------

The --show-per-thread-concurrency flag enables the display of per-thread
concurrency coverage metrics.  The requested hardware clock start time and
stop time are compared with the actual hardware clock start time and stop
time.  This shows how much long each thread runs while unencumbered by
other threads, or while encumbered by unfinished setup in other threads.


Example of --show-per-thread-concurrency for 2 bandwidth and 2 latency threads:
---------------------------------------------------------------------------
bw_tinfo[0].hwcounter_start        = 0xd591d2e3ce03
bw_tinfo[0].actual_hwcounter_start = 0xd591d3143c6f      diff = 3173996 (0.063480 seconds)
bw_tinfo[0].hwcounter_stop         = 0xd591dbd49f83
bw_tinfo[0].actual_hwcounter_stop  = 0xd591dc24e850      diff = 5261517 (0.105230 seconds), duration = 152087521 (3.041750 seconds)

bw_tinfo[1].hwcounter_start        = 0xd591d2e3ce03
bw_tinfo[1].actual_hwcounter_start = 0xd591d3144aca      diff = 3177671 (0.063553 seconds)
bw_tinfo[1].hwcounter_stop         = 0xd591dbd49f83
bw_tinfo[1].actual_hwcounter_stop  = 0xd591dcbc345a      diff = 15176919 (0.303538 seconds), duration = 161999248 (3.239985 seconds)

lat_tinfo[0].hwcounter_start        = 0xd591d2e3ce03
lat_tinfo[0].actual_hwcounter_start = 0xd591d2e3ce03      diff = 0 (0.000000 seconds)
lat_tinfo[0].hwcounter_stop         = 0xd591dbd49f83
lat_tinfo[0].actual_hwcounter_stop  = 0xd591dcd8cd8e      diff = 17051147 (0.341023 seconds), duration = 167051147 (3.341023 seconds)

lat_tinfo[1].hwcounter_start        = 0xd591d2e3ce03
lat_tinfo[1].actual_hwcounter_start = 0xd591d2e3ce03      diff = 0 (0.000000 seconds)
lat_tinfo[1].hwcounter_stop         = 0xd591dbd49f83
lat_tinfo[1].actual_hwcounter_stop  = 0xd591dd01ea5b      diff = 19745496 (0.394910 seconds), duration = 169745496 (3.394910 seconds)
---------------------------------------------------------------------------



Hugepage Support
----------------

Memory allocation for bandwidth and latency can use anonymous hugepages
provided by mmap() with the flags MAP_ANONYMOUS|MAP_HUGETLB.

  - This form of hugepages (hugetlbpage support) needs to be enabled in the
    kernel (CONFIG_HUGETLBFS and CONFIG_HUGETLB_PAGE) and sufficient number
    of hugepages of the requested size have to be available in the pool of
    hugepages.  See https://kernel.org/doc/Documentation/vm/hugetlbpage.txt

  - The --lat-use-hugepages and/or --bw-use-hugepages flags need to be
    specified with the desired hugepage size(s).  The sizes supported by
    loaded-latency can be shown by passing 'help' as the argument, e.g.,

      loaded-latency --lat-use-hugepages help

  - All latency threads will use the same hugepage size, and all bandwidth
    threads will use the same hugepage size, but the hugepage size need not
    be the same for latency and bandwidth threads, and hugepages need not be
    used for one or the other or both.

  - On systems with multiple NUMA nodes, there must be a sufficient number of
    hugepages (of the requested sizes) allocated on the nodes where the NUMA
    mempolicy would home the memory for a CPU on which a thread requests to
    use hugepage mapped memory.  However, the kernel's default distribution of
    hugepages is across all NUMA nodes, so a surfeit of hugepages may need to
    be allocated in the system.


Example of Using Hugepages
--------------------------

# use hugeadm to see what hugepage sizes are available on the system

$ hugeadm --pool-list
      Size  Minimum  Current  Maximum  Default
     65536        0        0        0
   2097152        0        0        0        *
  33554432        0        0        0
1073741824        0        0        0


# Increase the maximum number of huge pages of the page size desired, in
# this case, 100 of the 2MB hugepage size.  Due to external fragmentation,
# the hugepage limit may need to be higher than used.

$ sudo hugeadm --pool-pages-max 2M:100


# confirm that the maximum limit has been raised

$ hugeadm --pool-list
      Size  Minimum  Current  Maximum  Default
     65536        0        0        0
   2097152        0        0      100        *
  33554432        0        0        0
1073741824        0        0        0


# Use the 'help' value for --lat-use-hugepages (or --bw-use-hugepages)
# to list the sizes known by loaded-latency.  Either the index value
# '3' or the suffixed value '2M' can be used as the parameter value.

$ ./loaded-latency --lat-use-hugepages help
hugepage sizes:
0       = none
0       = 0
1       = default
1       = 1
2       = 64K
2       = 64KB
3       = 2M
3       = 2MB
4       = 32M
4       = 32MB
5       = 512M
5       = 512MB
6       = 1G
6       = 1GB
7       = 16G
7       = 16GB


# invoke loaded-latency with the available hugepage size

$ ./loaded-latency --lat-use-hugepages 2M --lat-cacheline-count $((100*1024*1024/64))

...

lat_use_hugepages   (-h) = 3 (hugepages = 2M)

...


# As a sanity check, confirm that the number of hugepages used during the
# measurement is non-zero.  In this case, the number of "Current" 2MB
# hugepages is 50, which matches the requested 100 MB of latency memory.

$ hugeadm --pool-list
      Size  Minimum  Current  Maximum  Default
     65536        0        0        0
   2097152        0       50      100        *
  33554432        0        0        0
1073741824        0        0        0




Spectre Variant 4 mitigation
----------------------------

Spectre Variant 4 is a security vulnerability that exploits processor
speculative bypassing of stores by younger loads.  Setting -Q or
--mitigate-spectre-v4 attempts to enable a mitigation, if available, using
prctl(PR_SET_SPECULATION_CTRL, PR_SPEC_STORE_BYPASS, PR_SPEC_DISABLE).  The
interface semantics enable or disable the speculation feature.  As shown,
disabling the speculation feature has the effect of requesting the
mitigation to be enabled.

Since a request to enable mitigation may be ignored, the status after the
request is read using prctl(PR_GET_SPECULATION_CTRL, PR_SPEC_STORE_BYPASS,
0,0,0) and reported.

Example outputs:

// --mitigate-spectre-v4 is NOT set on a system where the mitigation is not globally enabled
ssbs             (-Q) = speculation feature: requested enabled (retval = 0x0) status is enabled (retval = 0x3)

// --mitigate-spectre-v4 is set on a system that accepted the request to enable the mitigation
ssbs             (-Q) = speculation feature: requested disabled (retval = 0x0) status is disabled (retval = 0x5)

// --mitigate-spectre-v4 is set on a system that rejected the request to enable the mitigation
ssbs             (-Q) = speculation feature: requested disabled (retval = 0x0) status is enabled (retval = 0x2)



Cacheline Size
--------------

The --bw-cacheline-bytes and --lat-cacheline-bytes control the size of the
assumed cache line.  However, see "Known Limitations" that sizes other than
64 bytes have not been tested.



Known Limitations
=================

- The same CPU will be prevented from being allocated multiple latency
  threads or multiple bandwidth threads.  For example, the flags "--lat-cpu 3
  --lat-cpu 3" will only create 1 latency thread to run on CPU 3, not 2
  threads.  However, the same CPU can be allocated BOTH a bandwidth and latency
  thread (and a warning will be printed if configured as such).  Such a
  configuration is not interesting because the operating system would time
  slice between the threads at a granularity much greater than the overhead of
  an individual memory operation.

- The latency threads report latency in nanoseconds and CPU cycles.  The CPU
  cycles are reported only for convenience reasons; there is no calculation
  done based from them.  The cycles are computed from the time in nanoseconds
  multiplied by the CPU frequency.  The frequency of all CPUs is assumed to be
  the same.  The CPU frequency and cycle time are not determined by software,
  and the default value is 2600 MHz.  This is changed using the --cpu-freq-mhz
  or --cpu-cycle-time-ns flags, of which only one needs to be specified.

- The hardware clock (CNTVCT_EL0 on aarch64, TSC on x86_64) is used to
  synchronize and track the duration of the measurement.  The hardware clock
  is assumed to be synchronous and incrementing at a constant rate on all
  CPUs.

   - On aarch64, the frequency is read from the CNTFRQ_EL0 register.
   - On x86_64, the default frequency is hard-coded to 2900000000 Hz in
     get_default_cntfreq() in rdtsc.h.

  The --duration flag is given in seconds, and it is converted to hardware
  clock ticks by multiplying against a hardware counter frequency value.  If
  the hardware counter frequency is not determined correctly, the measurement
  duration will not be the requested duration.  In this situation, the
  frequency can be overridden using the --hwclock-freq flag to specify the
  right value.  For convenience, the --estimate-hwclock-freq runs a routine
  to estimate this value.  On x86, the TSC frequency may be printed early in
  the kernel console log.

- The supported hugepage sizes are hard-coded and are not filtered against
  the hugepage sizes supported by the system.  If an unsupported size is
  requested, the program will exit with a MAP_FAILED error.

- Cacheline sizes other than the default of 64 bytes have not been tested.

- No built-in support for controlling NUMA memory policy, i.e. no mbind(),
  set_mempolicy(), etc.  These policies for the entire benchmark process can
  be set through running loaded-latency under numactl.



Example Output
==============

This command line runs 4 bandwidth threads on CPU 16, 18, 20, and 22, and
a latency thread runs on CPU 15.

Command line
---------------------------------------------------------------------------
./loaded-latency --bw-cpu 16 --bw-cpu 18 --bw-cpu 20 --bw-cpu 22 \
--bw-buflen 200000000 --bw-iterations 10 --bw-iterations 70 \
--lat-cpu 15 --lat-cacheline-count 3125000 --lat-iterations 1000000 --lat-randomize \
--delay-seconds 6 --hwclock-freq 3417600000 --duration 5 --cpu-freq-mhz 4000



Output
---------------------------------------------------------------------------
Bandwidth thread 0 on CPU16 (-B 16)
Bandwidth thread 1 on CPU18 (-B 18)
Bandwidth thread 2 on CPU20 (-B 20)
Bandwidth thread 3 on CPU22 (-B 22)
Total of 4 bandwidth threads requested
Latency thread 0 on CPU15   (-l 15)
Total of 1 latency threads requested
main program pid         = 22315
duration            (-D) = 5.000000 seconds
delay_seconds       (-d) = 6.000000 seconds (20505600000 TSC ticks at cntfreq=3417600000)
random_seedval      (-S) = 1678305632783855
cycle_time_ns       (-t) = 0.250000 ((-f) 4000.000 MHz)
ssbs                (-Q) = speculation feature: requested enabled (retval = 0x0) status is enabled (retval = 0x3)

bandwidth settings:
bw_iterations       (-I) = 70
bw_buflen           (-L) = 200000000 (200.000 (1e6) megabytes)
fine loop delay     (-F) = 0
coarse loop delay   (-C) = 0
bw_cacheline_bytes  (-Z) = 64
bw_use_hugepages    (-H) = 0 (hugepages = none)
bw_write            (-W) = 0

latency settings:
lat_cacheline_count (-n) = 3125000 (200.000 (1e6) megabytes)
lat_iterations      (-i) = 1000000
lat_offset          (-o) = 0
lat_secondary_delay (-e) = 0
lat_randomize       (-r) = 1
lat_use_hugepages   (-h) = 0 (hugepages = none)
lat_shared_memory   (-s) = 0
lat_shared_memory_init_cpu(-u) = -1
lat_clear_cache     (-c) = 0
lat_cacheline_stride(-j) = 1
lat_cacheline_bytes (-z) = 64

CPU18 BWTHREAD1: buflen = 200000000, iterations = 70, inner_nops = 0, outer_nops = 0, hwcounter_start = 0xeb270c62182ac, bw_cacheline_bytes = 64, bw_use_hugepages = 0, tid = 22319
CPU16 BWTHREAD0: buflen = 200000000, iterations = 70, inner_nops = 0, outer_nops = 0, hwcounter_start = 0xeb270c62182ac, bw_cacheline_bytes = 64, bw_use_hugepages = 0, tid = 22318
CPU20 BWTHREAD2: buflen = 200000000, iterations = 70, inner_nops = 0, outer_nops = 0, hwcounter_start = 0xeb270c62182ac, bw_cacheline_bytes = 64, bw_use_hugepages = 0, tid = 22320
CPU22 BWTHREAD3: buflen = 200000000, iterations = 70, inner_nops = 0, outer_nops = 0, hwcounter_start = 0xeb270c62182ac, bw_cacheline_bytes = 64, bw_use_hugepages = 0, tid = 22321
CPU15 LATTHREAD0: cacheline_count = 3125000, iterations = 1000000, mem = 0x7fe9b0143040, randomize = 1, use_hugepages = 0, hwcounter_start = 0xeb270c62182ac, lat_offset = 0, tid = 22317
CPU15 LATTHREAD0: started at TSC = 0xeb270c62182b4
CPU22 BWTHREAD3: started at TSC = 0xeb270c62182ad
CPU16 BWTHREAD0: started at TSC = 0xeb270c62182bb
CPU20 BWTHREAD2: started at TSC = 0xeb270c62182af
CPU18 BWTHREAD1: started at TSC = 0xeb270c62182af
CPU20 BWTHREAD2: 15124.239562 MB/sec
CPU22 BWTHREAD3: 15118.219384 MB/sec
CPU18 BWTHREAD1: 15103.613098 MB/sec
CPU16 BWTHREAD0: 15097.590400 MB/sec
CPU15 LATTHREAD0: 113.882800 ns, 455.531200 cycles
CPU20 BWTHREAD2: 15109.215013 MB/sec
CPU22 BWTHREAD3: 15108.020900 MB/sec
CPU18 BWTHREAD1: 15090.507825 MB/sec
CPU16 BWTHREAD0: 15092.196278 MB/sec
CPU15 LATTHREAD0: 113.821600 ns, 455.286400 cycles
CPU20 BWTHREAD2: 15112.029488 MB/sec
CPU22 BWTHREAD3: 15109.425572 MB/sec
CPU18 BWTHREAD1: 15099.884632 MB/sec
CPU16 BWTHREAD0: 15101.068508 MB/sec
CPU15 LATTHREAD0: 113.829200 ns, 455.316800 cycles
CPU20 BWTHREAD2: 15118.727570 MB/sec
CPU22 BWTHREAD3: 15110.735977 MB/sec
CPU18 BWTHREAD1: 15107.296584 MB/sec
CPU16 BWTHREAD0: 15108.487748 MB/sec
CPU15 LATTHREAD0: 113.826200 ns, 455.304800 cycles
CPU20 BWTHREAD2: 15113.462127 MB/sec
CPU22 BWTHREAD3: 15106.944003 MB/sec
CPU18 BWTHREAD1: 15106.826017 MB/sec
CPU16 BWTHREAD0: 15106.838814 MB/sec
CPU20 BWTHREAD2: 15116.286552 MB/sec
CPU22 BWTHREAD3: 15118.433266 MB/sec
CPU16 BWTHREAD0: 15124.086124 MB/sec
CPU18 BWTHREAD1: 15122.060911 MB/sec
Joined BWTHREAD0, avg_bw = 15105.044646 MB/sec
Joined BWTHREAD1, avg_bw = 15105.031511 MB/sec
Joined BWTHREAD2, avg_bw = 15115.660052 MB/sec
Joined BWTHREAD3, avg_bw = 15111.963184 MB/sec
CPU15 LATTHREAD0: 111.458900 ns, 445.835600 cycles
Joined LATTHREAD0, avg_latency = 113.839950 ns

Total Bandwidth = 60437.699393 MB/sec
Average Latency = 113.839950 ns

concurrency coverage metrics:

bw_hwcounter_start_max  = 0xeb270c62182bb
bw_hwcounter_start_min  = 0xeb270c62182ad     max-min diff = 14 (0.000000 seconds)
lat_hwcounter_start_max = 0xeb270c62182b4
lat_hwcounter_start_min = 0xeb270c62182b4     max-min diff = 0 (0.000000 seconds)

bw_hwcounter_stop_max   = 0xeb27532f467d1
bw_hwcounter_stop_min   = 0xeb275322873ac     max-min diff = 13366309 (0.003911 seconds)
lat_hwcounter_stop_max  = 0xeb27548c22ad9
lat_hwcounter_stop_min  = 0xeb27548c22ad9     max-min diff = 0 (0.000000 seconds)

bw_lat_start_spread_ticks = -7 (-0.000000 seconds)
bw_lat_stop_spread_ticks  = -379172653 (-0.110947 seconds)
