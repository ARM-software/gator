/* Copyright (C) 2013-2022 by Arm Limited. All rights reserved. */

#include "DynBuf.h"

#include "Logging.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <unistd.h>

// Pick an aggressive size as buffer is primarily used for disk IO
#define MIN_BUFFER_FREE (1 << 12)

int DynBuf::resize(const size_t minCapacity)
{
    size_t scaledCapacity = 2 * capacity;
    if (scaledCapacity < minCapacity) {
        scaledCapacity = minCapacity;
    }
    if (scaledCapacity < 2 * MIN_BUFFER_FREE) {
        scaledCapacity = 2 * MIN_BUFFER_FREE;
    }
    capacity = scaledCapacity;

    buf = static_cast<char *>(realloc(buf, capacity));
    if (buf == nullptr) {
        return -errno;
    }

    return 0;
}

int DynBuf::ensureCapacity(size_t minCapacity)
{
    if (capacity < minCapacity) {
        return resize(minCapacity);
    }
    return 0;
}

bool DynBuf::read(const char * const path)
{
    bool result = false;

    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        LOG_DEBUG("open '%s' failed", path);
        return false;
    }

    length = 0;

    for (;;) {
        const size_t minCapacity = length + MIN_BUFFER_FREE + 1;
        if (capacity < minCapacity) {
            if (resize(minCapacity) != 0) {
                LOG_DEBUG("DynBuf::resize failed");
                goto fail;
            }
        }

        const ssize_t bytes = ::read(fd, buf + length, capacity - length - 1);
        if (bytes < 0) {
            LOG_DEBUG("read failed");
            goto fail;
        }
        else if (bytes == 0) {
            break;
        }
        length += bytes;
    }

    buf[length] = '\0';
    result = true;

fail:
    close(fd);

    return result;
}

int DynBuf::readlink(const char * const path)
{
    ssize_t bytes = MIN_BUFFER_FREE;

    for (;;) {
        if (static_cast<size_t>(bytes) >= capacity) {
            const int err = resize(2 * bytes);
            if (err != 0) {
                return err;
            }
        }
        bytes = ::readlink(path, buf, capacity);
        if (bytes < 0) {
            return -errno;
        }
        if (static_cast<size_t>(bytes) < capacity) {
            break;
        }
    }

    length = bytes;
    buf[bytes] = '\0';

    return 0;
}

// NOLINTNEXTLINE(cert-dcl50-cpp)
bool DynBuf::printf(const char * format, ...)
{
    va_list ap;

    length = 0;

    va_start(ap, format);
    const bool result = append(format, ap);
    va_end(ap);

    return result;
}

// NOLINTNEXTLINE(cert-dcl50-cpp)
bool DynBuf::append(const char * format, ...)
{
    va_list ap;

    va_start(ap, format);
    const bool result = append(format, ap);
    va_end(ap);

    return result;
}

bool DynBuf::append(const char * format, va_list ap)
{
    va_list dup;

    if (capacity <= 0) {
        if (resize(2 * MIN_BUFFER_FREE) != 0) {
            LOG_DEBUG("DynBuf::resize failed");
            return false;
        }
    }

    va_copy(dup, ap);
    int bytes = vsnprintf(buf + length, capacity - length, format, dup);
    if (bytes < 0) {
        LOG_DEBUG("fsnprintf failed");
        return false;
    }
    bytes += length;

    if (static_cast<size_t>(bytes) >= capacity) {
        if (resize(bytes + 1) != 0) {
            LOG_DEBUG("DynBuf::resize failed");
            return false;
        }

        bytes = vsnprintf(buf + length, capacity - length, format, ap);
        if (bytes < 0) {
            LOG_DEBUG("fsnprintf failed");
            return false;
        }
        bytes += length;
    }

    length = bytes;

    return true;
}

bool DynBuf::appendStr(const char * str)
{
    if (capacity <= 0) {
        if (resize(2 * MIN_BUFFER_FREE) != 0) {
            LOG_DEBUG("DynBuf::resize failed");
            return false;
        }
    }

    size_t bytes = strlen(str);
    if (length + bytes >= capacity) {
        if (resize(length + bytes + 1) != 0) {
            LOG_DEBUG("DynBuf::resize failed");
            return false;
        }
    }

    memcpy(buf + length, str, bytes + 1);

    length += bytes;

    return true;
}
