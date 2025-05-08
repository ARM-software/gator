/* Copyright (C) 2016-2024 by Arm Limited. All rights reserved. */

#include "lib/FsEntry.h"

#include "Logging.h"
#include "lib/Assert.h"
#include "lib/Error.h"

#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <boost/filesystem/exception.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>

#include <dirent.h>
#include <linux/limits.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = boost::filesystem;

namespace lib {
    FsEntryDirectoryIterator::FsEntryDirectoryIterator(const FsEntry & parent) // NOLINT(modernize-pass-by-value)
        : parent_(parent), directory_(nullptr, ::closedir)
    {
        if (parent_.read_stats().type() == FsEntry::Type::DIR) {
            directory_.reset(::opendir(parent_.path().c_str()));
        }
    }

    std::optional<FsEntry> FsEntryDirectoryIterator::next()
    {
        if (directory_ != nullptr) {
            ::dirent * entry = ::readdir(directory_.get());

            if (entry != nullptr) {
                // skip '.' and '..'
                if ((::strcmp(entry->d_name, ".") == 0)
                    || (::strcmp(entry->d_name, "..") == 0)
                    // this shouldn't happen but was seen on a device in /sys/bus/usb/devices
                    || (::strcmp(entry->d_name, "") == 0)) {
                    return next();
                }

                return FsEntry(parent_.path().append("/").append(entry->d_name));
            }
        }

        return {};
    }

    std::optional<FsEntry> FsEntry::create_unique_file(const FsEntry & parent)
    {
        if (parent.read_stats().type() != FsEntry::Type::DIR) {
            LOG_ERROR("Was asked to create a unique file under [%s] but it was not a directory", parent.path().c_str());
            return {};
        }

        if (!parent.exists()) {
            LOG_ERROR("Was asked to create a unique file under [%s] but the dir does not exist", parent.path().c_str());
            return {};
        }

        auto template_buffer = parent.path() + "/XXXXXX";
        auto result = ::mkstemp(template_buffer.data());
        if (result >= 0) {
            close(result);
            return FsEntry::create(template_buffer);
        }

        LOG_ERROR("Error generating unique filename. errno: %d (%s)", errno, lib::strerror());
        return {};
    }

    FsEntry::Stats::Stats() : Stats(Type::UNKNOWN, false, false)
    {
    }

    FsEntry::Stats::Stats(Type t, bool e, bool s) : type_(t), exists_(e), symlink_(s)
    {
    }

    FsEntry::FsEntry(std::string p) : path_(std::move(p)), name_offset(std::string::npos)
    {
        // add CWD if not starting with '/'
        if (path_.empty() || path_[0] != '/') {
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

        runtime_assert(!path_.empty() && path_[0] == '/', "Invalid absolute path");

        // save location of last '/'
        name_offset = path_.rfind('/');
    }

    FsEntry::FsEntry(const FsEntry & p, const std::string & n) : FsEntry(p.path().append("/").append(n))
    {
    }

    std::optional<FsEntry> FsEntry::parent() const
    {
        if (!is_root()) {
            return FsEntry(path_.substr(0, name_offset));
        }

        return {};
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

    bool FsEntry::is_absolute() const
    {
        return (!path_.empty()) && (path_.front() == '/');
    }

    FsEntryDirectoryIterator FsEntry::children() const
    {
        return {*this};
    }

    std::optional<FsEntry> FsEntry::readlink() const
    {
        struct stat lstat_data;
        if (lstat(path_.c_str(), &lstat_data) != 0) {
            return {};
        }

        auto const length = (lstat_data.st_size > 0 ? lstat_data.st_size : PATH_MAX);

        std::string result(std::size_t(length), '\0');

        auto const n = ::readlink(path_.c_str(), result.data(), result.size());

        // empty string and error are ignored
        if (n <= 0) {
            return {};
        }

        return FsEntry(result.substr(0, n));
    }

    std::optional<FsEntry> FsEntry::realpath() const
    {
        // NOLINTNEXTLINE(modernize-avoid-c-arrays)
        std::unique_ptr<char[], void (*)(void *)> real_path {::realpath(path_.c_str(), nullptr), std::free};

        if (real_path != nullptr) {
            return FsEntry(real_path.get());
        }

        return {};
    }

    bool FsEntry::operator==(const FsEntry & that) const
    {
        return (path_ == that.path_);
    }

    bool FsEntry::operator<(const FsEntry & that) const
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
        // NOLINTNEXTLINE(hicpp-signed-bitwise)
        const int mode = F_OK | (read ? R_OK : 0) | (write ? W_OK : 0) | (exec ? X_OK : 0);

        return access(path_.c_str(), mode) == 0;
    }

    bool FsEntry::hasChildWithNamePrefix(const char * prefix) const
    {
        const auto prefix_length = std::strlen(prefix);

        auto it = children();
        for (auto child = it.next(); child; child = it.next()) {
            if (std::strncmp(child->name().c_str(), prefix, prefix_length) == 0) {
                return true;
            }
        }

        return false;
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

    std::vector<std::uint8_t> FsEntry::readFileContentsAsBytes() const
    {
        std::ifstream stream(path(), std::ios::in | std::ios::binary);

        stream.unsetf(std::ios::skipws);

        stream.seekg(0, std::ios::end);
        auto const size = stream.tellg();
        stream.seekg(0, std::ios::beg);

        std::vector<std::uint8_t> result {};

        if (size > 0) {
            result.reserve(size);
        }

        result.insert(result.begin(),
                      std::istream_iterator<std::uint8_t>(stream),
                      std::istream_iterator<std::uint8_t>());

        return result;
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
        if (!stream) {
            return false;
        }
        stream << data;
        return bool(stream);
    }

    void FsEntry::copyTo(const FsEntry & dest) const
    {
        auto from = fs::path(path());
        auto to = fs::path(dest.path());

        fs::copy(from, to);
    }

    bool FsEntry::remove() const
    {
        try {
            return fs::remove(path());
        }
        catch (fs::filesystem_error const & e) {
            LOG_DEBUG("remove(%s) failed with error '%s'.", path().c_str(), e.code().message().c_str());
        }
        return false;
    }

    uintmax_t FsEntry::remove_all() const
    {
        try {
            return fs::remove_all(path());
        }
        catch (fs::filesystem_error const & e) {
            LOG_DEBUG("remove_all(%s) failed with error '%s'.", path().c_str(), e.code().message().c_str());
        }
        return 0;
    }

    bool FsEntry::create_directory() const
    {
        try {
            return fs::create_directory(path());
        }
        catch (fs::filesystem_error const & e) {
            LOG_DEBUG("create_directory(%s) failed with error '%s'.", path().c_str(), e.code().message().c_str());
        }
        return false;
    }
}
