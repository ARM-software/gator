/* Copyright (C) 2010-2022 by Arm Limited. All rights reserved. */

#include "Sender.h"

#include "BufferUtils.h"
#include "Logging.h"
#include "OlySocket.h"
#include "ProtocolVersion.h"
#include "SessionData.h"
#include "lib/File.h"
#include "lib/String.h"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstring>

#include <unistd.h>

Sender::Sender(OlySocket * socket)
    : mDataSocket(socket), mDataFile(nullptr, fclose), mDataFileName(nullptr), mSendMutex()
{
    // Set up the socket connection
    if (socket != nullptr) {
        char streamline[64] = {0};
        mDataSocket = socket;

        // Receive magic sequence - can wait forever
        // Streamline will send data prior to the magic sequence for legacy support, which should be ignored for v4+
        while (strcmp("STREAMLINE", streamline) != 0) {
            if (mDataSocket->receiveString(streamline, sizeof(streamline)) == -1) {
                LOG_ERROR("Socket disconnected");
                handleException();
            }
        }

        // Send magic sequence - must be done first, after which error messages can be sent
        lib::printf_str_t<32> magic {"GATOR %i\n", PROTOCOL_VERSION};
        mDataSocket->send(magic, strlen(magic));

        gSessionData.mWaitingOnCommand = true;
        LOG_DEBUG("Completed magic sequence");
    }

    pthread_mutexattr_t attr;
    if (pthread_mutexattr_init(&attr) != 0 || pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK) != 0
        || pthread_mutex_init(&mSendMutex, &attr) != 0 || pthread_mutexattr_destroy(&attr) != 0) {
        LOG_ERROR("Unable to setup mutex");
        handleException();
    }
}

Sender::~Sender()
{
    // Just close it as the client socket is on the stack
    if (mDataSocket != nullptr) {
        mDataSocket->closeSocket();
        mDataSocket = nullptr;
    }
}

void Sender::createDataFile(const char * apcDir)
{
    if (apcDir == nullptr) {
        return;
    }

    mDataFileName.reset(new char[strlen(apcDir) + 12]);
    sprintf(mDataFileName.get(), "%s/0000000000", apcDir);
    mDataFile.reset(lib::fopen_cloexec(mDataFileName.get(), "wb"));
    if (!mDataFile) {
        LOG_ERROR("Failed to open binary file: %s", mDataFileName.get());
        handleException();
    }
}

void Sender::writeDataParts(lib::Span<const lib::Span<const char, int>> dataParts,
                            ResponseType type,
                            bool ignoreLockErrors)
{
    int length = 0;
    for (const auto & data : dataParts) {
        int d_length = data.size();
        length += d_length;
        if (d_length < 0) {
            LOG_ERROR("Negative length message part (%d)", d_length);
            handleException();
        }
        else if (d_length > MAX_RESPONSE_LENGTH) {
            LOG_ERROR("Message part too big (%d)", d_length);
            handleException();
        }
    }

    if (length > MAX_RESPONSE_LENGTH) {
        LOG_ERROR("Message too big (%d)", length);
        handleException();
    }

    // Multiple threads call writeData()
    if (pthread_mutex_lock(&mSendMutex) != 0) {
        if (ignoreLockErrors) {
            return;
        }
        LOG_ERROR("pthread_mutex_lock failed");
        handleException();
    }

    // Send data over the socket connection
    if (mDataSocket != nullptr) {
        // Start alarm
        const int alarmDuration = 1;
        alarm(alarmDuration);

        // Send data over the socket, sending the type and size first
        LOG_DEBUG("Sending data with length %d", length);
        if (type != ResponseType::RAW) {
            char header[5];
            header[0] = static_cast<char>(type);
            buffer_utils::writeLEInt(header + 1, length);
            mDataSocket->send(header, sizeof(header));
        }

        auto const startTime = getTime();
        auto totalSize = 0ULL;

        // 1MiB/sec * alarmDuration sec
        const int chunkSize = 1024 * 1024 * alarmDuration;
        for (const auto & data : dataParts) {
            totalSize += data.size();
            int pos = 0;
            while (true) {
                mDataSocket->send(data.data() + pos, std::min(data.size() - pos, chunkSize));
                pos += chunkSize;
                if (pos >= data.size()) {
                    break;
                }

                // Reset the alarm
                alarm(alarmDuration);
                LOG_DEBUG("Resetting the alarm");
            }
        }

        // Stop alarm
        alarm(0);

        auto const endTime = getTime();
        auto const duration = endTime - startTime;
        auto const bandwidth = (totalSize * 1000000000ULL) / duration;

        LOG_DEBUG("Sender bandwidth %lluB/s", static_cast<unsigned long long>(bandwidth));
    }

    // Write data to disk as long as it is not meta data
    if (mDataFile && (type == ResponseType::APC_DATA || type == ResponseType::RAW)) {
        LOG_DEBUG("Writing data with length %d", length);
        // Send data to the data file
        auto writeData = [this](lib::Span<const char, int> data) {
            if (fwrite(data.data(), 1, data.size(), mDataFile.get()) != static_cast<size_t>(data.size())) {
                LOG_ERROR("Failed writing binary file %s", mDataFileName.get());
                handleException();
            }
        };

        if (type != ResponseType::RAW) {
            char header[4];
            buffer_utils::writeLEInt(header, length);
            writeData(header);
        }

        auto const startTime = getTime();
        auto totalSize = 0ULL;

        for (const auto & data : dataParts) {
            totalSize += data.size();
            writeData(data);
        }

        auto const endTime = getTime();
        auto const duration = endTime - startTime;
        auto const bandwidth = (totalSize * 1000000000ULL) / duration;

        LOG_DEBUG("Disk write bandwidth %lluB/s", static_cast<unsigned long long>(bandwidth));
    }

    if (pthread_mutex_unlock(&mSendMutex) != 0) {
        LOG_ERROR("pthread_mutex_unlock failed");
        handleException();
    }
}
