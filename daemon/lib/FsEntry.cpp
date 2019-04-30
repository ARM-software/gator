/* Copyright (c) 2016 by Arm Limited. All rights reserved. */

#include "lib/FsEntry.h"
#include "lib/Assert.h"

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unistd.h>
#include <sys/stat.h>

namespace lib
{
    FsEntryDirectoryIterator::FsEntryDirectoryIterator(const FsEntry & parent)
            : parent_(parent),
              directory_(nullptr, ::closedir)
    {
        if (parent_->read_stats().type() == FsEntry::Type::DIR) {
            directory_.reset(::opendir(parent_->path().c_str()));
        }
    }

    Optional<FsEntry> FsEntryDirectoryIterator::next()
    {
        if (directory_ != nullptr) {
            ::dirent * entry = ::readdir(directory_.get());

            if (entry != nullptr) {
                // skip '.' and '..'
                if ((::strcmp(entry->d_name, ".") == 0) || (::strcmp(entry->d_name, "..") == 0))
                    return next();

                return FsEntry(parent_->path().append("/").append(entry->d_name));
            }
        }

        return Optional<FsEntry>();
    }

    FsEntry::Stats::Stats()
        :   Stats(Type::UNKNOWN, false, false)
    {
    }

    FsEntry::Stats::Stats(Type t, bool e, bool s)
            : type_(t),
              exists_(e),
              symlink_(s)
    {
    }

    FsEntry::FsEntry(const std::string & p)
            : path_(p),
              name_offset(std::string::npos)
    {
        // add CWD if not starting with '/'
        if ((path_.length() == 0) || (path_[0] != '/')) {
            std::string buffer(PATH_MAX, '\0');
            const char * const cwd = getcwd(&buffer.front(), PATH_MAX);

            runtime_assert(cwd != nullptr, "could not get CWD");

            path_ = std::string(cwd).append("/").append(path_);
        }

        // remove any double slashes
        for (auto pos = path_.find("//"); pos != std::string::npos; pos = path_.find("//")) {
            path_.replace(pos, 2, "/");
        }

        // remove trailing slash provided path is not '/'
        if ((path_.length() > 1) && (path_.rfind('/') == path_.length() - 1)) {
            path_.resize(path_.length() - 1);
        }

        runtime_assert((path_.length() > 0) && (path_[0] == '/'), "Invalid absolute path");

        // save location of last '/'
        name_offset = path_.rfind('/');
    }

    FsEntry::FsEntry(const FsEntry & p, const std::string & n)
            : FsEntry(p.path().append("/").append(n))
    {
    }

    Optional<FsEntry> FsEntry::parent() const
    {
        if (!is_root()) {
            return FsEntry(path_.substr(0, name_offset));
        }

        return Optional<FsEntry>();
    }

    std::string FsEntry::name() const
    {
        return path_.substr(name_offset + 1);
    }

    std::string FsEntry::path() const
    {
        return path_;
    }

    bool FsEntry::is_root() const
    {
        return path_.length() == 1;
    }

    FsEntryDirectoryIterator FsEntry::children() const
    {
        return FsEntryDirectoryIterator(*this);
    }

    Optional<FsEntry> FsEntry::realpath() const
    {
        std::unique_ptr<char[], void(*)(void*)> real_path { ::realpath(path_.c_str(), nullptr), std::free };

        if (real_path != nullptr) {
            return FsEntry(real_path.get());
        }

        return Optional<FsEntry>();
    }

    bool FsEntry::operator ==(const FsEntry & that) const
    {
        return (path_ == that.path_);
    }

    bool FsEntry::operator <(const FsEntry & that) const
    {
        return (path_ < that.path_);
    }

    FsEntry::Stats FsEntry::read_stats() const
    {
        FsEntry::Stats result;

        // determine type and existance of path by calling 'lstat'
        struct stat lstat_data;

        if (lstat(path_.c_str(), &lstat_data) == 0) {
            // the path exists
            result.exists_ = true;

            // determine type
            if (S_ISDIR(lstat_data.st_mode)) {
                result.type_ = Type::DIR;
            }
            else if (S_ISREG(lstat_data.st_mode)) {
                result.type_ = Type::FILE;
            }
            else if (S_ISLNK(lstat_data.st_mode)) {
                result.symlink_ = true;

                // get the actual type from 'stat'
                if (stat(path_.c_str(), &lstat_data) == 0) {
                    if (S_ISDIR(lstat_data.st_mode)) {
                        result.type_ = Type::DIR;
                    }
                    else if (S_ISREG(lstat_data.st_mode)) {
                        result.type_ = Type::FILE;
                    }
                }
            }
            else if (S_ISBLK(lstat_data.st_mode)) {
                result.type_ = Type::BLOCK_DEV;
            }
            else if (S_ISCHR(lstat_data.st_mode)) {
                result.type_ = Type::CHAR_DEV;
            }
            else if (S_ISFIFO(lstat_data.st_mode)) {
                result.type_ = Type::FIFO;
            }
            else if (S_ISSOCK(lstat_data.st_mode)) {
                result.type_ = Type::SOCKET;
            }
        }

        return result;
    }

    bool FsEntry::canAccess(bool read, bool write, bool exec) const
    {
        const int mode = F_OK | (read ? R_OK : 0) | (write ? W_OK : 0) | (exec ? X_OK : 0);

        return access(path_.c_str(), mode) == 0;
    }

    std::string FsEntry::readFileContents() const
    {
        std::ifstream stream(path(), std::ios::in);
        std::ostringstream buffer;

        // read contents
        bool first = true;
        while (stream) {
            if (!first) {
                buffer << '\n';
            }
            else {
                first = false;
            }

            std::string line;
            std::getline(stream, line);
            buffer << line;
        }

        return buffer.str();
    }

    std::string FsEntry::readFileContentsSingleLine() const
    {
        std::ifstream stream(path(), std::ios::in);
        std::string line;

        // read contents
        if (stream) {
            std::getline(stream, line);
        }

        return line;
    }

    bool FsEntry::writeFileContents(const char * data) const
    {
        std::ofstream stream(path());
        if (!stream)
            return false;
        stream << data;
        return bool(stream);
    }
}
