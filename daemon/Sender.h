/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
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

#include "ClassBoilerPlate.h"

class OlySocket;

enum
{
    RESPONSE_XML = 1,
    RESPONSE_APC_DATA = 3,
    RESPONSE_ACK = 4,
    RESPONSE_NAK = 5,
    RESPONSE_ERROR = 0xFF
};

class Sender
{
public:
    Sender(OlySocket* socket);
    ~Sender();
    void writeData(const char* data, int length, int type, bool ignoreLockErrors = false);
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
