/* Copyright (C) 2018-2022 by Arm Limited. All rights reserved. */
#include "lib/Utils.h"

#include "ExitStatus.h"
#include "Logging.h"
#include "lib/FsEntry.h"
#include "lib/String.h"
#include "lib/Syscall.h"

#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>

//Works for Linux and Android
#define ROOT_UID 0
//Works for Android only
#define ANDROID_SHELL_UID 2000

namespace lib {
    namespace {
        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
        bool get_uid_from_stat(const char * const username, const char * const tmpDir, uid_t & uid)
        {
            // Lookups may fail when using a different libc or a statically compiled executable
            constexpr auto tmp_str_length = 32;
            lib::printf_str_t<tmp_str_length> gatorTemp {"%s/gator_temp", tmpDir};

            const int fd = lib::open(gatorTemp, O_CREAT | O_CLOEXEC, S_IRUSR | S_IWUSR);
            if (fd < 0) {
                return false;
            }
            close(fd);

            constexpr auto cmd_str_length = 128;
            lib::printf_str_t<cmd_str_length> cmd {"chown %s %s || rm -f %s",
                                                   username,
                                                   gatorTemp.c_str(),
                                                   gatorTemp.c_str()};

            const int pid = fork();
            if (pid < 0) {
                LOG_ERROR("fork failed");
                return false;
            }

            if (pid == 0) {
                execlp("sh", "sh", "-c", cmd, nullptr);
                lib::exit(COMMAND_FAILED_EXIT_CODE);
            }

            while ((waitpid(pid, nullptr, 0) < 0) && (errno == EINTR)) {
            }

            struct stat st;
            if (stat(gatorTemp, &st) != 0) {
                return false;
            }
            unlink(gatorTemp);
            uid = st.st_uid;
            return true;
        }
    }

    kernel_version_no_t parseLinuxVersion(struct utsname & utsname)
    {
        constexpr unsigned base = 10;

        std::array<unsigned, 3> version {{0, 0, 0}};

        int part = 0;
        char * ch = utsname.release;
        while (*ch >= '0' && *ch <= '9' && part < 3) {
            version[part] = (base * version[part]) + (*ch - '0');
            ++ch;
            if (*ch == '.') {
                ++part;
                ++ch;
            }
        }

        return KERNEL_VERSION(version[0], version[1], version[2]);
    }

    int readIntFromFile(const char * fullpath, int & value)
    {
        const std::string string = FsEntry::create(fullpath).readFileContents();
        const char * const data = string.c_str();

        char * endptr;
        errno = 0;
        value = strtol(data, &endptr, 10);
        if (errno != 0 || *endptr != '\n') {
            LOG_DEBUG("Invalid value in file %s: %s", fullpath, data);
            return -1;
        }

        return 0;
    }

    int readInt64FromFile(const char * fullpath, int64_t & value)
    {
        const std::string string = FsEntry::create(fullpath).readFileContents();
        const char * const data = string.c_str();

        char * endptr;
        errno = 0;
        value = strtoll(data, &endptr, 0);
        if (errno != 0 || (data == endptr) || (*endptr != '\n' && *endptr != '\0')) {
            LOG_DEBUG("Invalid value in file %s: %s", fullpath, data);
            return -1;
        }

        return 0;
    }

    int writeCStringToFile(const char * fullpath, const char * data)
    {
        const lib::FsEntry fsEntry = lib::FsEntry::create(fullpath);

        if (fsEntry.canAccess(false, true, false)) {
            if (lib::writeFileContents(fsEntry, data)) {
                return 0;
            }
            LOG_DEBUG("Opened but could not write to %s", fullpath);
            return -1;
        }
        return -1;
    }

    int writeIntToFile(const char * path, int value)
    {
        constexpr std::size_t buffer_size = 64; // Sufficiently large to hold any integer
        lib::printf_str_t<buffer_size> data {"%d", value};
        return writeCStringToFile(path, data);
    }

    int writeInt64ToFile(const char * path, int64_t value)
    {
        constexpr std::size_t buffer_size = 64; // Sufficiently large to hold any integer
        lib::printf_str_t<buffer_size> data {"%" PRIi64, value};
        return writeCStringToFile(path, data);
    }

    int writeReadIntInFile(const char * path, int & value)
    {
        if ((writeIntToFile(path, value) != 0) || (readIntFromFile(path, value) != 0)) {
            return -1;
        }
        return 0;
    }

    int writeReadInt64InFile(const char * path, int64_t & value)
    {
        if ((writeInt64ToFile(path, value) != 0) || (readInt64FromFile(path, value) != 0)) {
            return -1;
        }
        return 0;
    }

    std::set<int> readCpuMaskFromFile(const char * path)
    {
        std::set<int> result;

        const lib::FsEntry fsEntry = lib::FsEntry::create(path);

        if (fsEntry.canAccess(true, false, false)) {
            std::string contents = lib::readFileContents(fsEntry);

            LOG_DEBUG("Reading cpumask from %s", fsEntry.path().c_str());

            // split the input
            const std::size_t length = contents.length();
            std::size_t from = 0;
            std::size_t split = 0;
            std::size_t to = 0;

            while (to < length) {
                // move end pointer
                while (to < length) {
                    if ((contents[to] >= '0') && (contents[to] <= '9')) {
                        to += 1;
                    }
                    else if (contents[to] == '-') {
                        split = to;
                        to += 1;
                    }
                    else {
                        break;
                    }
                }

                // found a valid number (or range)
                if (from < to) {
                    if (split > from) {
                        // found range
                        contents[split] = 0;
                        contents[to] = 0;
                        int nf = (int) std::strtol(contents.c_str() + from, nullptr, 10);
                        const int nt = (int) std::strtol(contents.c_str() + split + 1, nullptr, 10);
                        while (nf <= nt) {
                            LOG_DEBUG("    Adding cpu %d to mask", nf);
                            result.insert(nf);
                            nf += 1;
                        }
                    }
                    else {
                        // found single item
                        contents[to] = 0;
                        const int n = (int) std::strtol(contents.c_str() + from, nullptr, 10);
                        LOG_DEBUG("    Adding cpu %d to mask", n);
                        result.insert(n);
                    }
                }

                // move to next item
                to += 1;
                from = to;
                split = to;
            }
        }

        return result;
    }

    uint64_t roundDownToPowerOfTwo(uint64_t in)
    {
        if (in == 0) {
            return 0;
        }

        in |= (in >> 1);
        in |= (in >> 2);
        in |= (in >> 4);
        in |= (in >> 8);
        in |= (in >> 16);
        in |= (in >> 32);

        return in - (in >> 1);
    }

    int calculatePerfMmapSizeInPages(const std::uint64_t perfEventMlockKb, const std::uint64_t pageSizeBytes)
    {
        constexpr std::uint64_t maxPerfEventMlockKb = std::numeric_limits<std::uint64_t>::max() / 1024ULL;

        if (perfEventMlockKb <= maxPerfEventMlockKb && pageSizeBytes > 0
            && perfEventMlockKb * 1024ULL > pageSizeBytes) {
            const std::uint64_t bufferSize = roundDownToPowerOfTwo(perfEventMlockKb * 1024ULL - pageSizeBytes);
            const std::uint64_t bufferPages = bufferSize / pageSizeBytes;
            return int(std::min<std::uint64_t>(bufferPages, std::numeric_limits<int>::max()));
        }
        return 0;
    }

    bool isRootOrShell()
    {
        const uint32_t uid = lib::geteuid();
        return (uid == ROOT_UID || uid == ANDROID_SHELL_UID);
    }

    std::optional<std::pair<uid_t, gid_t>> resolve_uid_gid(char const * username)
    {
        uid_t uid = geteuid();
        gid_t gid = getegid();
        // if name is null then just use the current user
        if (username != nullptr) {
            // for non root.
            // Verify root permissions
            auto is_root = (geteuid() == 0);
            if (!is_root) {
                LOG_ERROR("Unable to set user to %s for command because gatord is not running as root", username);
                return {};
            }

            // Look up the username
            // NOLINTNEXTLINE(concurrency-mt-unsafe)
            struct passwd * const user = getpwnam(username);
            if (user != nullptr) {
                uid = user->pw_uid;
                gid = user->pw_gid;
            }
            else {
                // Unable to get the user without getpwanm, so create a unique uid by adding a fixed number to the pid
                constexpr auto gid_random_constant = 0x484560f8;
                gid = gid_random_constant + getpid();

                std::string tmp_dir;
                if (access("/tmp", W_OK) == 0) {
                    // We are on Linux
                    tmp_dir = "/tmp";
                }
                else if (access("/data", W_OK) == 0) {
                    // We are on Android
                    tmp_dir = "/data";
                }

                if (tmp_dir.empty() || !get_uid_from_stat(username, tmp_dir.c_str(), uid)) {
                    LOG_ERROR("Unable to look up the user %s, please double check that the user exists", username);
                    return {};
                }
            }
        }
        return {{uid, gid}};
    }
}
