/* Copyright (C) 2014-2023 by Arm Limited. All rights reserved. */

#include "FSDriver.h"

#include "DriverCounter.h"
#include "Logging.h"
#include "PolledDriver.h"
#include "lib/Utils.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <mxml.h>
#include <regex.h>
#include <sys/types.h>
#include <unistd.h>

class FSCounter : public DriverCounter {
public:
    FSCounter(DriverCounter * next, const char * name, char * path, const char * regex);
    ~FSCounter() override;

    // Intentionally unimplemented
    FSCounter(const FSCounter &) = delete;
    FSCounter & operator=(const FSCounter &) = delete;
    FSCounter(FSCounter &&) = delete;
    FSCounter & operator=(FSCounter &&) = delete;

    [[nodiscard]] const char * getPath() const { return mPath; }

    int64_t read() override;

private:
    char * const mPath;
    regex_t mReg;
    bool mUseRegex;
};

FSCounter::FSCounter(DriverCounter * next, const char * name, char * path, const char * regex)
    : DriverCounter(next, name), mPath(path), mReg(), mUseRegex(regex != nullptr)
{
    if (mUseRegex) {
        int result = regcomp(&mReg, regex, REG_EXTENDED);
        if (result != 0) {
            char buf[128];
            regerror(result, &mReg, buf, sizeof(buf));
            LOG_ERROR("Invalid regex '%s': %s", regex, buf);
            handleException();
        }
    }
}

FSCounter::~FSCounter()
{
    free(mPath);
    if (mUseRegex) {
        regfree(&mReg);
    }
}

int64_t FSCounter::read()
{
    int64_t value;
    if (mUseRegex) {
        char buf[4096];
        size_t pos = 0;
        const int fd = open(mPath, O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            goto fail;
        }
        while (pos < sizeof(buf) - 1) {
            const ssize_t bytes = ::read(fd, buf + pos, sizeof(buf) - pos - 1);
            if (bytes < 0) {
                goto fail;
            }
            else if (bytes == 0) {
                break;
            }
            pos += bytes;
        }
        close(fd);
        buf[pos] = '\0';

        regmatch_t match[2];
        int result = regexec(&mReg, buf, 2, match, 0);
        if (result != 0) {
            // No match
            return 0;
        }

        if (match[1].rm_so < 0) {
            value = 1;
        }
        else {
            errno = 0;
            value = strtoll(buf + match[1].rm_so, nullptr, 0);
            if (errno != 0) {
                LOG_ERROR("Parsing %s failed: %s", mPath, strerror(errno));
                handleException();
            }
        }
    }
    else {
        if (lib::readInt64FromFile(mPath, value) != 0) {
            goto fail;
        }
    }
    return value;

fail:
    LOG_ERROR("Unable to read %s", mPath);
    handleException();
}

FSDriver::FSDriver() : PolledDriver("FS")
{
}

void FSDriver::readEvents(mxml_node_t * const xml)
{
    mxml_node_t * node = xml;
    while (true) {
        node = mxmlFindElement(node, xml, "event", nullptr, nullptr, MXML_DESCEND);
        if (node == nullptr) {
            break;
        }
        const char * counter = mxmlElementGetAttr(node, "counter");
        if (counter == nullptr) {
            continue;
        }

        if (counter[0] == '/') {
            LOG_ERROR("Old style filesystem counter (%s) detected, please create a new unique counter value and "
                      "move the filename into the path attribute, see events-Filesystem.xml for examples",
                      counter);
            handleException();
        }

        if (strncmp(counter, "filesystem_", 11) != 0) {
            continue;
        }

        const char * path = mxmlElementGetAttr(node, "path");
        if (path == nullptr) {
            LOG_ERROR("The filesystem counter %s is missing the required path attribute", counter);
            handleException();
        }
        const char * regex = mxmlElementGetAttr(node, "regex");
        setCounters(new FSCounter(getCounters(), counter, strdup(path), regex));
    }
}

int FSDriver::writeCounters(mxml_node_t * root) const
{
    int count = 0;
    for (auto * counter = static_cast<FSCounter *>(getCounters()); counter != nullptr;
         counter = static_cast<FSCounter *>(counter->getNext())) {
        if (access(counter->getPath(), R_OK) == 0) {
            mxml_node_t * node = mxmlNewElement(root, "counter");
            mxmlElementSetAttr(node, "name", counter->getName());
            ++count;
        }
    }

    return count;
}
