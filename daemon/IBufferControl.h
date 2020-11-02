/* Copyright (C) 2019-2020 by Arm Limited. All rights reserved. */

#pragma once

class ISender;

/**
 * A means of interacting with the state of a buffer and consuming it
 */
class IBufferControl {

public:
    virtual ~IBufferControl() = default;

    /**
     * Write data to Sender, like socket
     * @return true if no more data, i.e., EOF
     */
    virtual bool write(ISender & sender) = 0;

    /**
     * Checks if the buffer is full which means it can't accept any more data until write is called.
     */
    virtual bool isFull() const = 0;

    /**
     * Commits the current data and sets "done"
     */
    virtual void setDone() = 0;
};
