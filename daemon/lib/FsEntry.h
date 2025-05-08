/* Copyright (C) 2016-2024 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LIB_FSENTRY_H
#define INCLUDE_LIB_FSENTRY_H

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <dirent.h>

namespace lib {
    /* forward decls */
    class FsEntryDirectoryIterator;

    /**
     * @brief   Represents a file system entry
     */
    class FsEntry {
    public:
        /**
         * Enumerate file type
         */
        enum class Type : uint8_t { UNKNOWN, FILE, DIR, CHAR_DEV, BLOCK_DEV, FIFO, SOCKET };

        /**
         * Stats about file (type, whether it exists etc)
         */
        class Stats {
        public:
            Stats();
            Stats(Type type, bool exists, bool symlink);

            [[nodiscard]] Type type() const { return type_; }
            [[nodiscard]] bool exists() const { return exists_; }
            [[nodiscard]] bool is_symlink() const { return symlink_; }

        private:
            friend class FsEntry;

            Type type_;
            bool exists_;
            bool symlink_;
        };

        /**
         * Factory method
         * @param path The path the entry should point to. If the path is not rooted, will use CWD.
         */
        static FsEntry create(const std::string & path) { return {path}; }

        /**
         * Factory method, for sub path
         * @param parent The parent path
         * @param path The sub path string (any leading '/' is ignored)
         */
        static FsEntry create(const FsEntry & parent, const std::string & path) { return {parent, path}; }

        static std::optional<FsEntry> create_unique_file(const FsEntry & parent);

        /** @return Object representing the parent directory for some path, or empty for root directory */
        [[nodiscard]] std::optional<FsEntry> parent() const;
        /** @return The name of the entry */
        [[nodiscard]] std::string name() const;
        /** @return The full path of the entry */
        [[nodiscard]] std::string path() const;

        /** @return True if is the root entry (e.g. '/') */
        [[nodiscard]] bool is_root() const;

        /** @return True if is absolute (e.g. starts with '/') */
        [[nodiscard]] bool is_absolute() const;

        /** @return An iterator object for enumerating the children of a directory entry */
        [[nodiscard]] FsEntryDirectoryIterator children() const;
        /** @return The contents of a link */
        [[nodiscard]] std::optional<FsEntry> readlink() const;
        /** @return The absolute, cannonical path, or nothing if it was not possible to resolve the read path */
        [[nodiscard]] std::optional<FsEntry> realpath() const;

        /** Equality operator */
        bool operator==(const FsEntry & that) const;
        /** Less operator (for use in sorted collections such as std::map); result is equivalent to `this->path() < that.path()` */
        bool operator<(const FsEntry & that) const;

        /** @return Current stats for file */
        [[nodiscard]] Stats read_stats() const;

        /**
         * Check if file can be accessed for a certain kind of operation. If all arguments are false, just checks for existances
         * @param read True to check if readable
         * @param write True to check if writable
         * @param exec True to check if executable
         * @return True if *all* the requested access modes are valid (e.g. 'readable and writable' rather than 'readable or writable')
         */
        [[nodiscard]] bool canAccess(bool read, bool write, bool exec) const;

        /** @return True if the file exists */
        [[nodiscard]] bool exists() const { return canAccess(false, false, false); }

        /**
         * Checks if the path has any children whose name is prefixed with @a prefix.
         * @param prefix Prefix to match
         * @return True if any results
         */
        [[nodiscard]] bool hasChildWithNamePrefix(const char * prefix) const;

        /**
         * Read the contents of a file and return it as a std::string. The file is read as a text file and each line is delimited by '\n'
         * @return  The contents of that file
         */
        [[nodiscard]] std::string readFileContents() const;

        /**
         * Read the contents of a file and return it as a std::vector<std::uint8_t>.
         * @return  The contents of that file
         */
        [[nodiscard]] std::vector<std::uint8_t> readFileContentsAsBytes() const;

        /**
         * Read the contents of a file and return it as a std::string. Only the first line is read and returned without any '\n'
         * @return  The contents of that file
         */
        [[nodiscard]] std::string readFileContentsSingleLine() const;

        /**
         * Write the contents of a file
         * @return  true if successful
         */
        bool writeFileContents(const char * data) const;

        /**
         * Copy the contents of this file to the destination.
         */
        void copyTo(const FsEntry & dest) const;

        bool remove() const; // NOLINT(modernize-use-nodiscard)

        /**
         * Removes all the content of path and finally the path itself.
         * @return The number of files removed.
         */
        uintmax_t remove_all() const; // NOLINT(modernize-use-nodiscard)

        bool create_directory() const; // NOLINT(modernize-use-nodiscard)

    private:
        std::string path_;
        std::string::size_type name_offset;

        friend class FsEntryDirectoryIterator;

        /**
         * Constructor
         * @param path The path the entry should point to. If the path is not rooted, will use CWD.
         */
        FsEntry(std::string path);

        /**
         * Constructor, for sub path
         * @param parent The parent path
         * @param n The sub path string (any leading '/' is ignored)
         */
        FsEntry(const FsEntry & parent, const std::string & n);
    };

    /**
     * @brief   Allows enumeration of children of a directory.
     */
    class FsEntryDirectoryIterator {
    public:
        /**
         * Constructor
         * @param parent The directory to iterate the children of
         */
        FsEntryDirectoryIterator(const FsEntry & parent);

        /* Copy operations are not available */
        FsEntryDirectoryIterator(const FsEntryDirectoryIterator &) = delete;
        FsEntryDirectoryIterator & operator=(const FsEntryDirectoryIterator &) = delete;

        /* But move are */
        FsEntryDirectoryIterator(FsEntryDirectoryIterator &&) = default;
        FsEntryDirectoryIterator & operator=(FsEntryDirectoryIterator &&) = default;

        /**
         * Get the next child entry. Users should repeatedly call this function to enumerate children until it returns nothing.
         * @return The next child, or nothing in the case the list is complete
         */
        std::optional<FsEntry> next();

    private:
        FsEntry parent_;
        std::unique_ptr<DIR, int (*)(DIR *)> directory_;
    };

    /**
     * Read the contents of a file and return it as a std::string. The file is read as a text file and each line is delimited by '\n'
     * @param entry The file entry to read
     * @return  The contents of that file
     */
    inline std::string readFileContents(const FsEntry & entry)
    {
        return entry.readFileContents();
    }

    /**
     * Write the contents of a file
     * @param entry The file entry to write
     * @return  true if successful
     */
    inline bool writeFileContents(const FsEntry & entry, const char * data)
    {
        return entry.writeFileContents(data);
    }
}

#endif /* INCLUDE_LIB_FSENTRY_H */
