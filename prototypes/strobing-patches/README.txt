Patches
-------

This set of files contains the prototype event strobing kernel patches.
These patches may be used by the next version of gator to support 
efficient collection of per-function metrics.

To apply on top of v6.7, use 

    git am v6.7/0001-arm_pmu-Allow-the-PMU-to-alternate-between-two-sampl.patch
    git am v6.7/0002-arm_pmuv3-Add-config-bits-for-sample-period-strobing.patch
    git am v6.7/0003-tools-perf-Expose-sample-ID-stream-ID-to-python-scri.patch
   
or using patch:

    patch -p 1 -i v6.7/0001-arm_pmu-Allow-the-PMU-to-alternate-between-two-sampl.patch
    patch -p 1 -i v6.7/0002-arm_pmuv3-Add-config-bits-for-sample-period-strobing.patch
    patch -p 1 -i v6.7/0003-tools-perf-Expose-sample-ID-stream-ID-to-python-scri.patch

Backported versions for v5.15, v6.1, and v6.6 are provided in the respective
directories. NB: that the v5.15 patch has only been compile tested.

These patches are licensed as per the Linux kernel under GPL 2.0.

Test Script
-----------

The directory test-script/ contains a prototype python script that can 
be used with the `perf script` command. This prototype demonstrates
generation of simple metrics for Arm Neoverse-N1 and V1 may be created. 
For N1, it additionally demonstrates how multiple event groups can be 
used to derive a larger set of metrics.

To collect the simple set of metrics on N1, run the following:

    perf record -o $CAPTURE_FILE -T --sample-cpu --call-graph fp,4 --user-callchains -a -c 999700 -e "{r0011/period=999700,config2=300/:u,r0008:u,r001b:u,r0004:u,r0003:u,r0010:u,r0024:u}:uS" $BENCH_COMMAND
    perf script -i $CAPTURE_FILE --comm=$COMM_NAME -s generate-function-metrics.py -- -s discard

Or to collect the full set of metrics use:

    perf record -o $CAPTURE_FILE -T --sample-cpu --call-graph fp,4 --user-callchains -a -c 999700 -e '{r0011/period=999700,config2=300/:u,r0008:u,r0023:u,r0024:u}:uS' -e '{r0011/period=999700,config2=300/:u,r0003:u,r0004:u,r0005:u,r0008:u,r0025:u,r0034:u}:uS' -e '{r0011/period=999700,config2=300/:u,r0001:u,r0002:u,r0008:u,r0014:u,r0026:u,r0035:u}:uS' -e '{r0011/period=999700,config2=300/:u,r0008:u,r001b:u,r0021:u,r0022:u,r0078:u,r007a:u}:uS' -e '{r0011/period=999700,config2=300/:u,r0008:u,r0016:u,r0017:u,r002d:u,r002f:u}:uS' -e '{r0011/period=999700,config2=300/:u,r001b:u,r0073:u,r0074:u,r0075:u,r0077:u}:uS' -e '{r0011/period=999700,config2=300/:u,r0008:u,r0036:u,r0037:u}:uS' -e '{r0011/period=999700,config2=300/:u,r001b:u,r0070:u,r0071:u}:uS' $BENCH_COMMAND
    perf script -i $CAPTURE_FILE --comm=$COMM_NAME -s generate-function-metrics.py -- -s discard

This file is provided under the BSD-3-Clause license.

