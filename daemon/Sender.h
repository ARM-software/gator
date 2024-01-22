/* Copyright (C) 2010-2023 by Arm Limited. All rights reserved. */

#ifndef __SENDER_H__
#define __SENDER_H__

#include "ISender.h"

#include <cstdio>
#include <memory>
#include <thread>

class OlySocket;

class Sender : public ISender {
public:
    Sender(OlySocket * socket);
    ~Sender() override;

    // Intentionally unimplemented
    Sender(const Sender &) = delete;
    Sender & operator=(const Sender &) = delete;
    Sender(Sender &&) = delete;
    Sender & operator=(Sender &&) = delete;

    void writeDataParts(lib::Span<const lib::Span<const char, int>> dataParts,
                        ResponseType type,
                        bool ignoreLockErrors = false) override;
    void createDataFile(const char * apcDir);

private:
    OlySocket * mDataSocket;
    std::unique_ptr<FILE, int (*)(FILE *)> mDataFile;
    std::unique_ptr<char[]> mDataFileName;
    pthread_mutex_t mSendMutex;
};

#endif //__SENDER_H__
