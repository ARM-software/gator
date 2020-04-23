/* Copyright (C) 2013-2020 by Arm Limited. All rights reserved. */

#ifndef DYNBUF_H
#define DYNBUF_H

#include <stdarg.h>
#include <stdlib.h>

class DynBuf {
public:
    DynBuf() : capacity(0), length(0), buf(NULL) {}
    ~DynBuf() { reset(); }

    inline void reset()
    {
        capacity = 0;
        length = 0;
        if (buf != NULL) {
            free(buf);
            buf = NULL;
        }
    }

    bool read(const char * const path);
    // On error instead of printing the error and returning false, this returns -errno
    int readlink(const char * const path);
    __attribute__((format(printf, 2, 3))) bool printf(const char * format, ...);
    __attribute__((format(printf, 2, 3))) bool append(const char * format, ...);
    bool append(const char * format, va_list ap);
    bool appendStr(const char * str);

    size_t getLength() const { return length; }
    const char * getBuf() const { return buf; }
    char * getBuf() { return buf; }

private:
    int resize(const size_t minCapacity);

    size_t capacity;
    size_t length;
    char * buf;

    // Intentionally undefined
    DynBuf(const DynBuf &) = delete;
    DynBuf & operator=(const DynBuf &) = delete;
    DynBuf(DynBuf &&) = delete;
    DynBuf & operator=(DynBuf &&) = delete;
};

#endif // DYNBUF_H
