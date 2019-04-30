/**
 * Copyright (C) Arm Limited 2010-2018. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __SENDER_H__
#define __SENDER_H__

#include <stdio.h>
#include <pthread.h>

#include <memory>

#include "ISender.h"

#include "ClassBoilerPlate.h"

class OlySocket;


class Sender : public ISender
{
public:
    Sender(OlySocket* socket);
    ~Sender();
    void writeDataParts(lib::Span<const lib::Span<const char, int>> dataParts, ResponseType type, bool ignoreLockErrors = false) override;
    void createDataFile(const char* apcDir);

private:
    OlySocket* mDataSocket;
    std::unique_ptr<FILE, int(*)(FILE*)> mDataFile;
    std::unique_ptr<char[]> mDataFileName;
    pthread_mutex_t mSendMutex;

    // Intentionally unimplemented
    CLASS_DELETE_COPY_MOVE(Sender);
};

#endif //__SENDER_H__
