/* Copyright (C) 2010-2020 by Arm Limited. All rights reserved. */

#ifndef __STREAMLINE_SETUP_H__
#define __STREAMLINE_SETUP_H__

#include "ISender.h"
#include "lib/Span.h"

#include <stdint.h>
#include <string.h>
#include <vector>

class OlySocket;
class Drivers;
class ICpuInfo;
struct CapturedSpe;

// Commands from Streamline
enum {
    COMMAND_REQUEST_XML = 0,
    COMMAND_DELIVER_XML = 1,
    COMMAND_APC_START = 2,
    COMMAND_APC_STOP = 3,
    COMMAND_DISCONNECT = 4,
    COMMAND_PING = 5
};

class StreamlineSetup {
public:
    StreamlineSetup(OlySocket * socket, Drivers & drivers, lib::Span<const CapturedSpe> capturedSpes);
    ~StreamlineSetup();

private:
    OlySocket * mSocket;
    Drivers & mDrivers;
    lib::Span<const CapturedSpe> mCapturedSpes;

    std::vector<char> readCommand(int *);
    void handleRequest(char * xml);
    void handleDeliver(char * xml);
    void sendData(const char * data, uint32_t length, ResponseType type);
    void sendString(const char * string, ResponseType type) { sendData(string, strlen(string), type); }
    void sendDefaults();
    void writeConfiguration(char * xml);

    // Intentionally unimplemented
    StreamlineSetup(const StreamlineSetup &) = delete;
    StreamlineSetup & operator=(const StreamlineSetup &) = delete;
    StreamlineSetup(StreamlineSetup &&) = delete;
    StreamlineSetup & operator=(StreamlineSetup &&) = delete;
};

#endif //__STREAMLINE_SETUP_H__
