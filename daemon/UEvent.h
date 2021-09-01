/* Copyright (C) 2013-2021 by Arm Limited. All rights reserved. */

#ifndef UEVENT_H
#define UEVENT_H

struct UEventResult {
    const char * mAction;
    const char * mDevPath;
    const char * mSubsystem;
    char mBuf[1 << 13];
};

class UEvent {
public:
    UEvent();
    ~UEvent();

    // Intentionally undefined
    UEvent(const UEvent &) = delete;
    UEvent & operator=(const UEvent &) = delete;
    UEvent(UEvent &&) = delete;
    UEvent & operator=(UEvent &&) = delete;

    bool init();
    bool read(UEventResult * result);

    int getFd() const { return mFd; }

    bool enabled() const { return mFd >= 0; }

private:
    int mFd;
};

#endif // UEVENT_H
