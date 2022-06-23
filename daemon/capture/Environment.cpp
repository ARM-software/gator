/* Copyright (C) 2021-2022 by Arm Limited. All rights reserved. */
#include "capture/Environment.h"

#include "GatorException.h"
#include "Logging.h"
#include "SessionData.h"
#include "android/GatorAndroidSetupHandler.h"
#include "lib/FsEntry.h"
#include "lib/Resource.h"
#include "lib/Utils.h"
#include "linux/perf/PerfUtils.h"

#include <array>
#include <cstring>

#include <pwd.h>
#include <sys/resource.h>
#include <unistd.h>

using namespace capture;

namespace {

    constexpr int MIN_PAGE_SIZE = 1024;
    constexpr int DEFAULT_MMAP_SIZE_PAGES = 128;
    constexpr rlim_t DEFAULT_MIN_RLIM_CUR = rlim_t(1) << 15;

    auto classifyUser()
    {
        using namespace gator::android;

        constexpr uid_t uid_of_root = 0;
        constexpr uid_t usual_uid_of_shell = 2000;

        auto id = getuid();

        // is it root
        if (id == uid_of_root) {
            return GatorAndroidSetupHandler::UserClassification::root;
        }

        // is it shell

        // NOLINTNEXTLINE(readability-magic-numbers) - some arbitrarily large buffer
        std::array<char, 4096> buffer {};
        passwd result {};
        passwd * matched = nullptr;

        // Look up the shell user
        if (getpwnam_r("shell", &result, buffer.data(), buffer.size(), &matched) == 0) {
            LOG_DEBUG("getpwnam_r returned success, %p", matched);
            if (matched != nullptr) {
                LOG_DEBUG("shell uid=%u", matched->pw_uid);
                if (id == matched->pw_uid) {
                    return GatorAndroidSetupHandler::UserClassification::shell;
                }
            }
        }
        else {
            // NOLINTNEXTLINE(concurrency-mt-unsafe)
            LOG_DEBUG("getpwnam_r errored %d (%s)", errno, strerror(errno));
        }

        //
        if (id == usual_uid_of_shell) {
            return GatorAndroidSetupHandler::UserClassification::shell;
        }

        return GatorAndroidSetupHandler::UserClassification::other;
    }

    void configureRlimit()
    {
        struct rlimit rlim;
        memset(&rlim, 0, sizeof(rlim));
        if (lib::getrlimit(RLIMIT_NOFILE, &rlim) != 0) {
            LOG_DEBUG("Unable to get the maximum number of files");
            // Not good, but not a fatal error either
        }
        else {
            rlim.rlim_max = std::max(rlim.rlim_cur, rlim.rlim_max);
            rlim.rlim_cur = std::min(std::max(DEFAULT_MIN_RLIM_CUR, rlim.rlim_cur), rlim.rlim_max);
            if (lib::setrlimit(RLIMIT_NOFILE, &rlim) != 0) {
                LOG_DEBUG("Unable to increase the maximum number of files (%" PRIuMAX ", %" PRIuMAX ")",
                          static_cast<uintmax_t>(rlim.rlim_cur),
                          static_cast<uintmax_t>(rlim.rlim_max));
                // Not good, but not a fatal error either
            }
        }
    }

    void configurePerfMmapSize(SessionData & sessionData)
    {
        // use value from perf_event_mlock_kb
        if ((sessionData.mPerfMmapSizeInPages <= 0) && (geteuid() != 0) && (sessionData.mPageSize >= MIN_PAGE_SIZE)) {

            // the default seen on most setups is 516kb, if user cannot read the file it is probably
            // because they are on Android in locked down setup so use default value of 128 pages
            sessionData.mPerfMmapSizeInPages = DEFAULT_MMAP_SIZE_PAGES;

            const std::optional<std::int64_t> perfEventMlockKb = perf_utils::readPerfEventMlockKb();

            if (perfEventMlockKb && (*perfEventMlockKb > 0)) {
                const int perfMmapSizeInPages = lib::calculatePerfMmapSizeInPages(std::uint64_t(*perfEventMlockKb),
                                                                                  std::uint64_t(sessionData.mPageSize));

                if (perfMmapSizeInPages > 0) {
                    sessionData.mPerfMmapSizeInPages = perfMmapSizeInPages;
                }
            }

            LOG_INFO("Default perf mmap size set to %d pages (%llukb)",
                     sessionData.mPerfMmapSizeInPages,
                     sessionData.mPerfMmapSizeInPages * sessionData.mPageSize / 1024ULL);
        }
    }
}

LinuxEnvironmentConfig::LinuxEnvironmentConfig() noexcept
{
    configureRlimit();
}

LinuxEnvironmentConfig::~LinuxEnvironmentConfig() noexcept
{
}

void LinuxEnvironmentConfig::postInit(SessionData & sessionData)
{
    configurePerfMmapSize(sessionData);
}

OsType capture::detectOs()
{
#ifdef __ANDROID__
    return OsType::Android;
#else
    // maybe musl libc statically linked gatord: probe the filesystem
    lib::FsEntry app_process = lib::FsEntry::create("/system/bin/app_process");
    if (app_process.exists()) {
        return OsType::Android;
    }

    app_process = lib::FsEntry::create("/system/bin/app_process32");
    if (app_process.exists()) {
        return OsType::Android;
    }

    app_process = lib::FsEntry::create("/system/bin/app_process64");
    if (app_process.exists()) {
        return OsType::Android;
    }

    return OsType::Linux;
#endif
}

std::unique_ptr<CaptureEnvironment> capture::prepareCaptureEnvironment()

{
    switch (detectOs()) {
        case OsType::Android: {
            return std::make_unique<gator::android::GatorAndroidSetupHandler>(classifyUser());
        }
        case OsType::Linux:
            return std::make_unique<capture::LinuxEnvironmentConfig>();
    }
    throw GatorException("Invalid capture environment");
}
