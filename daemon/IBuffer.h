/* Copyright (c) 2019 by Arm Limited. All rights reserved. */

#ifndef IBUFFER_H_
#define IBUFFER_H_

#include "ISender.h"
#include <cstdint>

class IBuffer {

public:
    virtual ~IBuffer() = default;
    /**
     * Write data to Sender, like socket
     */
    virtual void write(ISender *sender) = 0;
    /**
     *
     */
    virtual bool event64(int key, int64_t value) = 0;
    /*
     *
     */
    virtual int bytesAvailable() const = 0;

    /**
     *
     */
    virtual void setDone() = 0;
    /**
     * Is buffer write/commit done ?
     */
    virtual bool isDone() const = 0 ;

    // Block Counter messages
    virtual bool eventHeader(uint64_t curr_time) = 0;

    /**
     *
     */
    virtual bool check(const uint64_t time) = 0;
};

#endif /* IBUFFER_H_ */
