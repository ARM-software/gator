/* Copyright (C) 2020 by Arm Limited. All rights reserved. */

#include "armnn/DriverSourceIpc.h"

#include "Logging.h"

#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sstream>
#include <string>

namespace armnn {

    bool Pipe::writeAll(const Bytes buf)
    {
        const std::uint8_t * const buffer {buf.data};
        const std::size_t length = {buf.size()};
        std::size_t bytesWritten = 0;

        while (bytesWritten < length) {
            const ssize_t wrote = ::write(mWriteFd.get(), buffer + bytesWritten, length - bytesWritten);
            if (wrote == -1) {
                if (errno == EINTR) {
                    continue;
                }
                else {
                    return false;
                }
            }
            else if (wrote > 0) {
                bytesWritten += wrote;
            }
            else {
                return false;
            }
        }
        return true;
    }

    bool Pipe::readAll(MutBytes buf)
    {
        std::uint8_t * const buffer {buf.data};
        const std::size_t length {buf.size()};
        std::size_t accumulatedBytes = 0;

        while (accumulatedBytes < length) {
            const ssize_t result = ::read(mReadFd.get(), buffer + accumulatedBytes, length - accumulatedBytes);
            if (result == -1) {
                if (errno == EINTR) {
                    continue;
                }
                else {
                    return false;
                }
            }
            else if (result == 0) {
                return false;
            }
            else {
                accumulatedBytes += result;
            }
        }

        return true;
    }

    std::string Pipe::toString()
    {
        std::stringstream ss {};
        ss << "readFd: " << mReadFd.get() << ", writeFd: " << mWriteFd.get();
        std::string res = ss.str();
        return res;
    }

    static Pipe createPipe()
    {
        int fds[2] {-1, -1};
        int result = ::pipe2(fds, O_CLOEXEC);

        if (result < 0) {
            logg.logError("Could not create pipe for armnn, errcode from pipe(fds): %d", result);
            handleException();
        }
        return Pipe {fds[0], fds[1]};
    }

    // Possible message types that can be sent over the pipes
    // Between parent and child
    static const uint8_t START_MSG = 10;
    static const uint8_t STOP_MSG = 11;
    static const uint8_t CHILD_DEATH_MSG = 12;
    static const uint8_t INTERRUPT_MSG = 13;
    static const uint8_t COUNTERS_MSG = 14;

    ChildToParentController::ChildToParentController() : mChildToParent {createPipe()} {}

    bool ChildToParentController::consumeControlMsg(ICaptureStartStopHandler & handler)
    {
        uint8_t data[1] {0};
        bool result = mChildToParent.readAll(data);
        if (result) {
            switch (data[0]) {
                case START_MSG:
                    handler.startCapture();
                    mCalledStart = true;
                    return true;
                case STOP_MSG:
                    handler.stopCapture();
                    mCalledStart = false;
                    return true;
                case CHILD_DEATH_MSG:
                    if (mCalledStart) {
                        handler.stopCapture();
                        mCalledStart = false;
                    }
                    return false;
            }
            logg.logError("Received unexpected message type %d", data[0]);
        }
        else {
            std::string p {mChildToParent.toString()};
            logg.logError("Could not read control message from pipe: %s", p.c_str());
        }
        return false;
    }

    void ChildToParentController::startCapture()
    {
        uint8_t startmsg[1] = {START_MSG};
        bool result = mChildToParent.writeAll(startmsg);

        if (!result) {
            logg.logError("Failed to send start message to gator-main");
        }
    }

    void ChildToParentController::stopCapture()
    {
        uint8_t stopmsg[1] = {STOP_MSG};
        bool result = mChildToParent.writeAll(stopmsg);

        if (!result) {
            logg.logError("Failed to send stop message to gator-main");
        }
    }

    void ChildToParentController::onChildDeath()
    {
        uint8_t msg[1] = {CHILD_DEATH_MSG};
        bool result = mChildToParent.writeAll(msg);

        if (!result) {
            logg.logError("Failed to notify of child process's death to gator-main");
        }
    }

    ParentToChildCounterConsumer::ParentToChildCounterConsumer() : mToChild {createPipe()} {}

    struct CounterMsg {
        std::uint64_t timestamp;
        int counterKey;
        unsigned int core;
        std::uint32_t counterValue;
    } __attribute__((packed));

    bool ParentToChildCounterConsumer::interruptReader()
    {
        uint8_t msgtype[1] {INTERRUPT_MSG};
        return mToChild.writeAll(msgtype);
    }

    bool ParentToChildCounterConsumer::readMessage(ICounterConsumer & destination)
    {
        uint8_t msgtype[1] {0};
        bool result = mToChild.readAll(msgtype);
        if (result) {
            switch (msgtype[0]) {
                case INTERRUPT_MSG:
                    return false;
                case COUNTERS_MSG:
                    return readCounterStruct(destination);
            }
        }
        else {
            logg.logError("Failed to read message from gator-main");
        }

        return false;
    }

    bool ParentToChildCounterConsumer::readCounterStruct(ICounterConsumer & destination)
    {
        CounterMsg msg {};
        uint8_t buf[sizeof(msg)];
        bool readResult = mToChild.readAll(buf);
        if (readResult) {
            memcpy(&msg, &buf, sizeof(msg));
            destination.consumerCounterValue(msg.timestamp,
                                             ApcCounterKeyAndCoreNumber {msg.counterKey, msg.core},
                                             msg.counterValue);
            return true;
        }
        else {
            logg.logError("Failed to read counters from gator-main");
        }
        return readResult;
    }

    bool ParentToChildCounterConsumer::consumerCounterValue(std::uint64_t timestamp,
                                                            ApcCounterKeyAndCoreNumber keyAndCore,
                                                            std::uint32_t counterValue)
    {
        CounterMsg msg {};
        msg.timestamp = timestamp;
        msg.counterKey = keyAndCore.key;
        msg.core = keyAndCore.core;
        msg.counterValue = counterValue;

        uint8_t buf[1 + sizeof(msg)] {0};
        buf[0] = COUNTERS_MSG;
        memcpy(buf + 1, &msg, sizeof(msg));
        return mToChild.writeAll(buf);
    }

    DriverSourceIpc::DriverSourceIpc(ICaptureStartStopHandler & startStopHandler)
        : mControlChannel {}, mCountersChannel {}, mArmnnController {startStopHandler}
    {
    }

    void DriverSourceIpc::prepareForFork()
    {
        {
            std::lock_guard<std::mutex> guard(mParentMutex);
            mCountersChannel.set(ParentToChildCounterConsumer {});
        }

        mControlThread = std::thread {[&]() -> void {
            while (mControlChannel.consumeControlMsg(mArmnnController)) {
            }
            logg.logMessage("Finished listening for armnn start/stop messages");
        }};
    }

    void DriverSourceIpc::onChildDeath()
    {
        logg.logMessage("Detected gator-child has died");

        mControlChannel.onChildDeath();
        mControlThread.join();

        std::lock_guard<std::mutex> guard(mParentMutex);
        mCountersChannel.clear();
    }

    bool DriverSourceIpc::consumerCounterValue(std::uint64_t timestamp,
                                               ApcCounterKeyAndCoreNumber keyAndCore,
                                               std::uint32_t counterValue)
    {
        std::lock_guard<std::mutex> guard(mParentMutex);
        if (mCountersChannel.valid()) {
            return mCountersChannel.get().consumerCounterValue(timestamp, keyAndCore, counterValue);
        }
        else {
            return true;
        }
    }

    void DriverSourceIpc::run(ICounterConsumer & counterConsumer)
    {
        mControlChannel.startCapture();

        while (mCountersChannel.get().readMessage(counterConsumer)) {
        }
        mControlChannel.stopCapture();
    }

    void DriverSourceIpc::interrupt()
    {
        if (!mCountersChannel.get().interruptReader()) {
            logg.logError("Could not interrupt armnn::DriverSourceIpc");
            handleException();
        }
    }

}
