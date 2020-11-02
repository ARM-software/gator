/* Copyright (C) 2010-2020 by Arm Limited. All rights reserved. */

#ifndef __STREAMLINE_SETUP_H__
#define __STREAMLINE_SETUP_H__

#include "ISender.h"
#include "StreamlineSetupLoop.h"
#include "lib/Span.h"

#include <cstdint>
#include <cstring>

class OlySocket;
class Drivers;
class ICpuInfo;
struct CapturedSpe;

class StreamlineSetup : private IStreamlineCommandHandler {
public:
    StreamlineSetup(OlySocket & socket, Drivers & drivers, lib::Span<const CapturedSpe> capturedSpes);

    // Intentionally unimplemented
    StreamlineSetup(const StreamlineSetup &) = delete;
    StreamlineSetup & operator=(const StreamlineSetup &) = delete;
    StreamlineSetup(StreamlineSetup &&) = delete;
    StreamlineSetup & operator=(StreamlineSetup &&) = delete;

private:
    OlySocket & mSocket;
    Drivers & mDrivers;
    lib::Span<const CapturedSpe> mCapturedSpes;

    State handleRequest(char * xml) override;
    State handleDeliver(char * xml) override;
    State handleApcStart() override;
    State handleApcStop() override;
    State handleDisconnect() override;
    State handlePing() override;

    void sendData(const char * data, uint32_t length, ResponseType type);
    void sendString(const char * string, ResponseType type) { sendData(string, strlen(string), type); }
    void sendDefaults();
    void writeConfiguration(char * xml);
};

#endif //__STREAMLINE_SETUP_H__
