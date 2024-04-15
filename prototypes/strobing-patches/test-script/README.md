Counter Strobing Patch Utilities
================================

Test benchmark and metrics processing script:

To compile the benchmark (with a recent version of g++ / clang++):

        g++ -std=c++20 -O2 benchmark.cpp -o benchmark

To run the benchmark, typically use something like:

        ./benchmark 0 1

which will use a randomized order for the sequence of sub-benchmarks in
each iteration, and will run only a single level deep benchmark. The
second argument, can be changed to 2 or 3 for further nested
sub-benchmark levels.

To capture a simple recording use something like:

        perf record -T --sample-cpu --call-graph fp,4 --user-callchains -k CLOCK_MONOTONIC_RAW -c 99700 \
            -e '{cycles/period=99700,alt-period=300/,instructions,branch-misses,cache-misses,cache-references}:uS' \
            ~/benchmark 0 1

NB: With older versions of the patches `alt-period=<n>` should be
replaced with `strobe_period=<n>`.


To capture the complete set of Neoverse-N1 metrics, use something like:

        perf record -T --sample-cpu --call-graph fp,4 --user-callchains -k CLOCK_MONOTONIC_RAW -c 99700 \
            -e '{r0011/period=99700,alt-period=300/,r0008,r0023,r0024}:uS' \
            -e '{r0011/period=99700,alt-period=300/,r0003,r0004,r0005,r0008,r0025,r0034}:uS' \
            -e '{r0011/period=99700,alt-period=300/,r0001,r0002,r0008,r0014,r0026,r0035}:uS' \
            -e '{r0011/period=99700,alt-period=300/,r0008,r001b,r0021,r0022,r0078,r007a}:uS' \
            -e '{r0011/period=99700,alt-period=300/,r0008,r0016,r0017,r002d,r002f}:uS' \
            -e '{r0011/period=99700,alt-period=300/,r001b,r0073,r0074,r0075,r0077}:uS' \
            -e '{r0011/period=99700,alt-period=300/,r0008,r0036,r0037}:uS' \
            -e '{r0011/period=99700,alt-period=300/,r001b,r0070,r0071}:uS' \
            ./benchmark 0 1

Of course, other test applications can also be substituted for the
`./benchmark 0 1` command

To process the results using the provided script:

        ./perf script -s ./generate-function-metrics.py -- -s discard

Which will print a tab-separated table to the console. You may capture
this into a file for processing with some spreadsheet tool that supports
this format, e.g.:

        ./perf script -s ./generate-function-metrics.py -- -s discard > out.csv
        soffice out.csv


Results
=======

Some high level results from using the patch with the provided
post-processing script are provided below.

The results are captured with the following commands:

        perf record -T --sample-cpu --call-graph fp,4 --user-callchains -k CLOCK_MONOTONIC_RAW \
            -e '{cycles/period=<PERIOD>,alt-period=<ALT>/,instructions,branch-misses,cache-references,cache-misses,r001b}:uS' -c <PERIOD> \
            benchmark 0 1
        perf script -s generate-function-metrics.py -- -s <MODE>

(Some cleanup of the raw output is applied for presentation in the
tables below)


Key
----

| Col   | Title                                   | Formula                                     |
|-------|-----------------------------------------|---------------------------------------------|
| #     | Number of samples                       |                                             |
| CPI   | Cycles / Instruction                    | `(cycles / instructions)`                   |
| %RI   | %Retired instructions                   | `((instructions / r001b) * 100)`            |
| BMKI  | Branch Mispredicts / 1000 Instructions  | `((branch-misses / instructions) * 1000)`   |
| CMKI  | Cache Misses / 1000 Instructions        | `((cache-misses / instructions) * 1000)`    |
| %CM   | Cache Misses / Cache Access             | `((cache-misses / cache-references) * 100)` |
| %CY   | % of total Cycles in symbol                                                       |   |
| %I    | % of total Instructions Retired in symbol                                         |   |
| %BM   | % of total Branch Mispredicts in symbol                                           |   |
| %L1DA | % of total Cache Accesses in symbol                                               |   |
| %L1DM | % of total Cache Misses in symbol                                                 |   |
| %IS   | % of total Instructions Speculated in symbol                                      |   |
| DIS   | % of samples who's PMU data was discarded due to overlapping more than one symbol |   |


N1SDP (Neoverse-N1)
-------------------

*Test PERIOD=1000000, ALT=0, MODE=none*

| Symbol             | #    | CPI | %RI  | BMKI | CMKI | %CM | %CY  | %I   | %BM  | %L1DA | %L1DM | %IS  |
|--------------------|------|-----|------|------|------|-----|------|------|------|-------|-------|------|
| fp_divider_stalls  | 6551 | 2.2 | 84.4 | 5.0  | 0.7  | 3.7 | 37.8 | 37.6 | 37.5 | 37.6  | 37.7  | 37.6 |
| int_divider_stalls | 4562 | 2.2 | 84.5 | 4.9  | 0.7  | 3.8 | 26.3 | 26.3 | 26.0 | 25.9  | 26.2  | 26.2 |
| isb                | 3474 | 2.2 | 84.3 | 5.0  | 0.7  | 3.7 | 20.0 | 20.0 | 20.0 | 19.8  | 19.8  | 20.0 |
| branch_mispredicts | 1264 | 2.1 | 83.4 | 5.3  | 0.7  | 3.6 | 7.3  | 7.4  | 7.9  | 7.5   | 7.4   | 7.5  |
| double_to_int      | 766  | 2.1 | 84.6 | 4.9  | 0.7  | 3.7 | 4.4  | 4.5  | 4.5  | 4.5   | 4.4   | 4.5  |
| nops               | 437  | 2.1 | 84.6 | 4.9  | 0.7  | 3.8 | 2.5  | 2.6  | 2.5  | 2.5   | 2.6   | 2.6  |
| dcache_miss        | 153  | 2.1 | 84.0 | 5.1  | 0.7  | 3.7 | 0.9  | 0.9  | 0.9  | 0.9   | 0.9   | 0.9  |

Size of perf.data: 4.370MB


*Test PERIOD=1000, ALT=0, MODE=none*

| Symbol             | #      | CPI  | %RI  | BMKI | CMKI  | %CM  | %CY  | %I   | %BM  | %L1DA | %L1DM | %IS  |
|--------------------|--------|------|------|------|-------|------|------|------|------|-------|-------|------|
| fp_divider_stalls  | 273751 | 4.6  | 98.8 | 0.3  | 0.3   | 6.6  | 36.7 | 18.1 | 1.0  | 5.0   | 2.1   | 15.3 |
| int_divider_stalls | 200422 | 3.3  | 98.8 | 0.3  | 0.3   | 6.5  | 26.9 | 18.5 | 1.0  | 5.0   | 2.1   | 15.5 |
| isb                | 147699 | 14.2 | 94.3 | 1.7  | 1.9   | 6.5  | 19.8 | 3.1  | 1.0  | 5.0   | 2.1   | 2.8  |
| branch_mispredicts | 53846  | 1.0  | 47.6 | 29.5 | 0.6   | 1.2  | 7.2  | 16.5 | 92.4 | 45.3  | 3.3   | 28.8 |
| double_to_int      | 32735  | 0.5  | 98.2 | 0.3  | 0.3   | 6.9  | 4.4  | 20.8 | 1.1  | 5.1   | 2.2   | 17.7 |
| nops               | 18271  | 0.3  | 98.2 | 0.3  | 0.3   | 6.7  | 2.4  | 19.4 | 1.0  | 4.9   | 2.1   | 16.4 |
| dcache_miss        | 14319  | 2.4  | 81.6 | 3.2  | 129.5 | 58.6 | 1.9  | 1.8  | 1.1  | 21.7  | 81.1  | 1.8  |

Size of perf.data: 190.793MB


*Test PERIOD=300, ALT=0, MODE=none*

| Symbol             | #      | CPI  | %RI  | BMKI | CMKI  | %CM  | %CY  | %I   | %BM  | %L1DA | %L1DM | %IS  |
|--------------------|--------|------|------|------|-------|------|------|------|------|-------|-------|------|
| fp_divider_stalls  | 270045 | 4.9  | 97.7 | 0.2  | 0.1   | 3.9  | 36.2 | 17.5 | 0.5  | 3.3   | 0.8   | 14.5 |
| int_divider_stalls | 199493 | 3.6  | 98.3 | 0.2  | 0.1   | 4.0  | 26.8 | 17.9 | 0.5  | 3.3   | 0.8   | 14.7 |
| isb                | 148410 | 18.7 | 95.0 | 1.0  | 1.1   | 4.4  | 19.9 | 2.5  | 0.4  | 3.4   | 0.9   | 2.2  |
| branch_mispredicts | 55842  | 1.1  | 44.8 | 33.5 | 0.5   | 1.0  | 7.5  | 16.2 | 94.8 | 45.6  | 2.8   | 29.1 |
| double_to_int      | 32990  | 0.5  | 96.2 | 0.1  | 0.1   | 3.8  | 4.4  | 21.1 | 0.5  | 3.3   | 0.8   | 17.7 |
| nops               | 19343  | 0.3  | 96.4 | 0.1  | 0.1   | 3.9  | 2.6  | 21.3 | 0.5  | 3.4   | 0.8   | 17.9 |
| dcache_miss        | 14029  | 3.8  | 58.1 | 2.2  | 214.4 | 68.1 | 1.9  | 1.2  | 0.5  | 20.2  | 86.5  | 1.6  |

Size of perf.data: 190.751MB


*Test PERIOD=300, ALT=0, MODE=discard*

| Symbol             | #      | CPI  | %RI  | BMKI | CMKI  | %CM  | %CY  | %I   | %BM  | %L1DA | %L1DM | %IS  | DIS |
|--------------------|--------|------|------|------|-------|------|------|------|------|-------|-------|------|-----|
| fp_divider_stalls  | 270045 | 5.0  | 98.1 | 0.0  | 0.0   | 6.4  | 36.7 | 18.1 | 0.0  | 0.2   | 0.1   | 14.9 | 1.5 |
| int_divider_stalls | 199493 | 3.6  | 98.7 | 0.0  | 0.0   | 6.7  | 27.1 | 18.5 | 0.0  | 0.3   | 0.1   | 15.2 | 1.6 |
| isb                | 148410 | 20.6 | 98.2 | 0.1  | 0.2   | 7.3  | 20.1 | 2.4  | 0.1  | 0.6   | 0.2   | 2.0  | 1.8 |
| branch_mispredicts | 55842  | 1.1  | 44.5 | 34.0 | 0.0   | 0.1  | 7.5  | 16.6 | 99.5 | 63.4  | 0.3   | 30.2 | 3.2 |
| double_to_int      | 32990  | 0.5  | 96.6 | 0.0  | 0.0   | 1.9  | 4.3  | 21.5 | 0.1  | 0.4   | 0.0   | 18.0 | 4.8 |
| nops               | 19343  | 0.3  | 96.9 | 0.0  | 0.0   | 2.1  | 2.5  | 21.5 | 0.1  | 0.6   | 0.1   | 18.0 | 7.4 |
| dcache_miss        | 14029  | 4.5  | 56.5 | 0.3  | 257.6 | 77.1 | 1.8  | 1.0  | 0.1  | 26.9  | 98.5  | 1.4  | 8.7 |

Size of perf.data: 190.751MB


*Test PERIOD=999700, ALT=300, MODE=discard*

| Symbol             | #      | CPI  | %RI  | BMKI | CMKI  | %CM  | %CY  | %I   | %BM  | %L1DA | %L1DM | %IS  | DIS  |
|--------------------|--------|------|------|------|-------|------|------|------|------|-------|-------|------|------|
| fp_divider_stalls  | 6553   | 4.9  | 99.1 | 0.0  | 0.0   | 0.0  | 41.8 | 22.9 | 0.1  | 0.6   | 0.0   | 19.2 | 0.7  |
| int_divider_stalls | 4741   | 3.5  | 99.1 | 0.0  | 0.0   | 1.1  | 28.3 | 21.5 | 0.1  | 1.9   | 0.2   | 18.1 | 0.7  |
| isb                | 3414   | 20.1 | 99.6 | 0.2  | 0.0   | 0.4  | 17.6 | 2.3  | 0.1  | 0.8   | 0.0   | 1.9  | 1.0  |
| branch_mispredicts | 1234   | 1.1  | 45.2 | 33.0 | 0.0   | 0.0  | 6.1  | 15.2 | 99.0 | 71.6  | 0.1   | 28.1 | 3.2  |
| double_to_int      | 694    | 0.5  | 97.7 | 0.0  | 0.0   | 0.6  | 3.4  | 19.1 | 0.1  | 1.2   | 0.1   | 16.2 | 4.9  |
| nops               | 417    | 0.3  | 97.2 | 0.2  | 0.0   | 2.8  | 1.9  | 18.3 | 0.6  | 0.4   | 0.1   | 15.7 | 8.6  |
| dcache_miss        | 185    | 3.6  | 66.7 | 0.4  | 184.7 | 53.8 | 0.7  | 0.5  | 0.0  | 18.4  | 99.1  | 0.7  | 24.9 |

Size of perf.data: 8.739MB


Juno (Cortex-A53)
-----------------

*Test PERIOD=999700, ALT=300, MODE=discard*

| Symbol             | #      | CPI  | BMKI  | CMKI  | %CM  | %CY  | %I   | %BM  | %L1DA | %L1DM | DIS |
|--------------------|--------|------|-------|-------|------|------|------|------|-------|-------|-----|
| fp_divider_stalls  | 547940 | 14.4 | 48.1  | 0.0   | 0.0  | 59.1 | 18.4 | 26.2 | 0.0   | 0.0   | 5.1 |
| dcache_miss        | 165035 | 10.7 | 36.2  | 320.5 | 99.9 | 18.0 | 7.5  | 8.0  | 99.6  | 68.2  | 5.4 |
| int_divider_stalls | 74495  | 2.1  | 6.9   | 0.0   | 0.0  | 8.0  | 17.2 | 3.5  | 0.0   | 0.0   | 6.1 |
| double_to_int      | 44977  | 1.0  | 3.5   | 0.0   | 0.0  | 4.8  | 20.5 | 2.1  | 0.0   | 0.0   | 6.8 |
| branch_mispredicts | 42475  | 1.3  | 39.7  | 0.2   | 0.5  | 4.5  | 15.3 | 17.9 | 0.1   | 19.6  | 6.8 |
| isb                | 28962  | 6.6  | 670.9 | 0.0   | 0.0  | 3.0  | 2.1  | 41.0 | 0.0   | 0.0   | 7.7 |
| nops               | 21776  | 0.6  | 1.9   | 0.0   | 0.0  | 2.3  | 18.3 | 1.0  | 0.0   | 0.0   | 8.5 |

Juno (Cortex-A57)
-----------------

*Test PERIOD=999700, ALT=300, MODE=discard*

| Symbol             | #      | CPI  | CMKI | %CM   | %RI  | BMKI | %CY  | %I   | %BM  | %L1DA | %L1DM | %IS  | DIS |
|--------------------|--------|------|------|-------|------|------|------|------|------|-------|-------|------|-----|
| fp_divider_stalls  | 139160 | 6.3  | 0.0  | 0.0   | 69.8 | 0.9  | 25.9 | 16.6 | 2.0  | 13.0  | 0.0   | 0.0  | 5.3 |
| dcache_miss        | 102032 | 8.9  | 1.0  | 337.7 | 46.2 | 0.1  | 19.0 | 8.5  | 0.2  | 10.1  | 95.4  | 77.0 | 5.5 |
| int_divider_stalls | 99031  | 4.5  | 0.0  | 0.0   | 75.9 | 0.0  | 18.4 | 16.5 | 0.1  | 11.9  | 0.0   | 0.0  | 5.5 |
| isb                | 82538  | 28.5 | 0.0  | 0.0   | 6.7  | 2.8  | 15.3 | 2.2  | 0.8  | 17.7  | 0.0   | 0.0  | 5.6 |
| double_to_int      | 42261  | 1.6  | 0.0  | 0.0   | 76.5 | 0.0  | 7.8  | 19.6 | 0.1  | 14.0  | 0.0   | 0.0  | 6.2 |
| branch_mispredicts | 41681  | 2.1  | 0.0  | 0.1   | 40.0 | 45.6 | 7.7  | 15.1 | 96.7 | 20.6  | 0.1   | 19.6 | 6.2 |
| nops               | 29735  | 1.1  | 0.0  | 0.0   | 93.8 | 0.0  | 5.5  | 20.3 | 0.0  | 11.8  | 0.0   | 0.0  | 6.7 |
