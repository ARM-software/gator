/* Copyright (c) 2017 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LINUX_PROC_PROCLOADAVGFILERECORD_H
#define INCLUDE_LINUX_PROC_PROCLOADAVGFILERECORD_H

namespace lnx
{
    /**
     * The parsed contents of /proc/loadavg as per `man proc.5`
     */
    class ProcLoadAvgFileRecord
    {
    public:

        /**
         * Parse the contents of loadavg file, modifying the fields in 'result' if successful.
         *
         * @param result The object to store the extracted fields in
         * @param loadavg_contents The text contents of the stat file
         * @return True if the contents were successfully parsed, false otherwise
         */
        static bool parseLoadAvgFile(ProcLoadAvgFileRecord & result, const char * loadavg_contents);

        /**
         * Create an empty record with all fields null/zero/empty
         */
        ProcLoadAvgFileRecord();

        /**
         * Construct a record populated with the specified values
         */
        ProcLoadAvgFileRecord(double loadavg_1m, double loadavg_5m, double loadavg_15m, unsigned long num_runnable_threads,
                              unsigned long num_threads, unsigned long newest_pid);

        double getLoadAvgOver15Minutes() const
        {
            return loadavg_15m;
        }

        double getLoadAvgOver1Minutes() const
        {
            return loadavg_1m;
        }

        double getLoadAvgOver5Minutes() const
        {
            return loadavg_5m;
        }

        unsigned long getNewestPid() const
        {
            return newest_pid;
        }

        unsigned long getNumThreads() const
        {
            return num_threads;
        }

        unsigned long getNumRunnableThreads() const
        {
            return num_runnable_threads;
        }

    private:

        double loadavg_1m;
        double loadavg_5m;
        double loadavg_15m;
        unsigned long num_runnable_threads;
        unsigned long num_threads;
        unsigned long newest_pid;
    };
}

#endif /* INCLUDE_LINUX_PROC_PROCLOADAVGFILERECORD_H */
