/* Copyright (c) 2016 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LIB_FSENTRY_H
#define INCLUDE_LIB_FSENTRY_H

#include "ClassBoilerPlate.h"
#include "lib/Optional.h"

#include <memory>
#include <string>
#include <dirent.h>

namespace lib
{
    /* forward decls */
    class FsEntryDirectoryIterator;

    /**
     * @class   FsEntry
     * @brief   Represents a file system entry
     */
    class FsEntry
    {
    public:

        /**
         * Enumerate file type
         */
        enum class Type
        {
            UNKNOWN,
            FILE,
            DIR,
            CHAR_DEV,
            BLOCK_DEV,
            FIFO,
            SOCKET
        };

        /**
         * Stats about file (type, whether it exists etc)
         */
        class Stats
        {
        public:

            Stats();
            Stats(Type type, bool exists, bool symlink);

            Type type() const
            {
                return type_;
            }

            bool exists() const
            {
                return exists_;
            }

            bool is_symlink() const
            {
                return symlink_;
            }

        private:

            friend class FsEntry;

            Type type_;
            bool exists_;
            bool symlink_;
        };

        /**
         * Factory method
         *
         * @param   path    The path the entry should point to. If the path is not rooted, will use CWD.
         */
        inline static FsEntry create(const std::string & path)
        {
            return FsEntry(path);
        }

        /**
         * Factory method, for sub path
         *
         * @param   parent  The parent path
         * @param   path    The sub path string (any leading '/' is ignored)
         */
        inline static FsEntry create(const FsEntry & parent, const std::string & path)
        {
            return FsEntry(parent, path);
        }

        /** @return Object representing the parent directory for some path, or empty for root directory */
        Optional<FsEntry> parent() const;
        /** @return The name of the entry */
        std::string name() const;
        /** @return The full path of the entry */
        std::string path() const;

        /** @return True if is the root entry (e.g. '/') */
        bool is_root() const;

        /** @return An iterator object for enumerating the children of a directory entry */
        FsEntryDirectoryIterator children() const;
        /** @return The absolute, cannonical path, or nothing if it was not possible to resolve the read path */
        Optional<FsEntry> realpath() const;

        /** Equality operator */
        bool operator ==(const FsEntry & that) const;
        /** Less operator (for use in sorted collections such as std::map); result is equivalent to `this->path() < that.path()` */
        bool operator <(const FsEntry & that) const;

        /** @return Current stats for file */
        Stats read_stats() const;

        /**
         * Check if file can be accessed for a certain kind of operation. If all arguments are false, just checks for existances
         *
         * @param read True to check if readable
         * @param write True to check if writable
         * @param exec True to check if executable
         * @return True if *all* the requested access modes are valid (e.g. 'readable and writable' rather than 'readable or writable')
         */
        bool canAccess(bool read, bool write, bool exec) const;

        /** @return True if the file exists */
        bool exists() const
        {
            return canAccess(false, false, false);
        }

        /**
         * Read the contents of a file and return it as a std::string. The file is read as a text file and each line is delimited by '\n'
         *
         * @param   entry   The file entry to read
         * @return  The contents of that file
         */
        std::string readFileContents() const;

        /**
         * Read the contents of a file and return it as a std::string. Only the first line is read and returned without any '\n'
         *
         * @param   entry   The file entry to read
         * @return  The contents of that file
         */
        std::string readFileContentsSingleLine() const;

        /**
         * Write the contents of a file
         *
         * @return  true if successful
         */
        bool writeFileContents(const char * data) const;

    private:

        std::string path_;
        std::string::size_type name_offset;

        friend class FsEntryDirectoryIterator;

        /**
         * Constructor
         *
         * @param   path    The path the entry should point to. If the path is not rooted, will use CWD.
         */
        FsEntry(const std::string & path);

        /**
         * Constructor, for sub path
         *
         * @param   parent  The parent path
         * @param   path    The sub path string (any leading '/' is ignored)
         */
        FsEntry(const FsEntry & parent, const std::string & path);
    };

    /**
     * @class   FsEntryDirectoryIterator
     * @brief   Allows enumeration of children of a directory.
     */
    class FsEntryDirectoryIterator
    {
    public:

        /**
         * Constructor
         * @param   parent  The directory to iterate the children of
         */
        FsEntryDirectoryIterator(const FsEntry & parent);

        /* Copy operations are not available */
        CLASS_DELETE_COPY(FsEntryDirectoryIterator);

        /* But move are */
        FsEntryDirectoryIterator(FsEntryDirectoryIterator &&) = default;
        FsEntryDirectoryIterator& operator=(FsEntryDirectoryIterator &&) = default;

        /**
         * Get the next child entry. Users should repeatedly call this function to enumerate children until it returns nothing.
         *
         * @return The next child, or nothing in the case the list is complete
         */
        Optional<FsEntry> next();

    private:

        Optional<FsEntry> parent_;
        std::unique_ptr<DIR, int(*)(DIR *)> directory_;

    };

    /**
     * Read the contents of a file and return it as a std::string. The file is read as a text file and each line is delimited by '\n'
     *
     * @param   entry   The file entry to read
     * @return  The contents of that file
     */
    static inline std::string readFileContents(const FsEntry & entry)
    {
        return entry.readFileContents();
    }

    /**
     * Write the contents of a file
     *
     * @param   entry   The file entry to write
     * @return  true if successful
     */
    static inline bool writeFileContents(const FsEntry & entry, const char * data)
    {
        return entry.writeFileContents(data);
    }
}

#endif /* INCLUDE_LIB_FSENTRY_H */
