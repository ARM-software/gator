/* Copyright (c) 2017 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LINUX_PROC_PROCPIDSTATMFILERECORD_H
#define INCLUDE_LINUX_PROC_PROCPIDSTATMFILERECORD_H

namespace lnx
{
    /**
     * The parsed contents of /proc/[pid]/statm as per `man proc.5`
     */
    class ProcPidStatmFileRecord
    {
    public:

        /**
         * Parse the contents of statm file, modifying the fields in 'result' if successful.
         *
         * @param result The object to store the extracted fields in
         * @param statm_contents The text contents of the stat file
         * @return True if the contents were successfully parsed, false otherwise
         */
        static bool parseStatmFile(ProcPidStatmFileRecord & result, const char * statm_contents);

        /**
         * Create an empty record with all fields null/zero/empty
         */
        ProcPidStatmFileRecord();

        /**
         * Construct a record populated with the specified values
         */
        ProcPidStatmFileRecord(unsigned long size, unsigned long resident, unsigned long shared, unsigned long text,
                           unsigned long lib, unsigned long data, unsigned long dt);

        unsigned long getData() const
        {
            return data;
        }

        unsigned long getDt() const
        {
            return dt;
        }

        unsigned long getLib() const
        {
            return lib;
        }

        unsigned long getResident() const
        {
            return resident;
        }

        unsigned long getShared() const
        {
            return shared;
        }

        unsigned long getSize() const
        {
            return size;
        }

        unsigned long getText() const
        {
            return text;
        }

    private:

        unsigned long size;
        unsigned long resident;
        unsigned long shared;
        unsigned long text;
        unsigned long lib;
        unsigned long data;
        unsigned long dt;
    };
}

#endif /* INCLUDE_LINUX_PROC_PROCPIDSTATMFILERECORD_H */
