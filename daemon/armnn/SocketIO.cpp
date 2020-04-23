/**
 * Copyright (C) 2020 by Arm Limited. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "armnn/SocketIO.h"
#include "OlySocket.h"
#include "Logging.h"

#include <cassert>
#include <cerrno>
#include <cstring>
#include <exception>
#include <utility>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

/** The number of connections to queue whilst waiting for accept */
constexpr int MAX_LISTEN_BACKLOG = 128;

constexpr int READ_TIMEDOUT = 0;
constexpr int READ_ERROR = -1;
constexpr int READ_EOF = -2;

constexpr int DEFAULT_READ_TIMEOUT_MILLIS = 100;

extern void handleException();

namespace armnn
{
    /**
     * Initialize a socket, make it cloexec
     *
     * @param domain
     * @param type
     * @param protocol
     * @return the socket AutoClosingFd
     */
    static AutoClosingFd socket_cloexec(int domain, int type, int protocol)
    {
        return  { ::socket_cloexec(domain, type, protocol) };
    }

    /**
     * Initialize a sockaddr_un
     *
     * @param uds_address
     * @param address
     * @param length
     * @param use_struct_size
     * @return The length to pass to bind/connect
     */
    static socklen_t init_sockaddr_un(sockaddr_un & udsAddress, const char * const address,
                                      const std::size_t length, const bool useStructSize)
    {
        memset(&udsAddress, 0, sizeof(sockaddr_un));
        memcpy(udsAddress.sun_path, address, length);
        udsAddress.sun_family = AF_UNIX;

        return (useStructSize ? offsetof(struct sockaddr_un, sun_path) + length - 1 : sizeof(sockaddr_un));
    }

    /**
     * Set a file descriptor as non-blocking
     *
     * @param fd
     * @return True on success, false on failure
     */
    static bool setNonBlocking(int fd)
    {
        const int flags = fcntl(fd, F_GETFL);
        if (flags >= 0) {
            if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
                // Unable to set socket flags, running in blocking mode
                logg.logWarning("Failed to set non-blocking socket due to %s (%d)", std::strerror(errno), errno);
                return false;
            }
        }
        else {
            // Unable to get socket flags, running in blocking mode
            logg.logWarning("Failed to set non-blocking socket due to %s (%d)", std::strerror(errno), errno);
            return false;
        }

        return true;
    }

    /**
     * Calling this function disables the SIGPIPE signal usually sent to our process
     * when we attempt to write to a closed file descriptor.
     * Should call this function if a SIGPIPE would terminate the application prematurely.
     *
     * @param fd
     * @return True if success, false on failure
     */
    static bool setNoSigPipe(int fd)
    {
#ifdef USE_SO_NOSIGPIPE
        const int set = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, (void *) &set, sizeof(set)) < 0) {
            logg.logWarning("Failed to set no sigpipe socket due to %s (%d)", std::strerror(errno), errno);
            return false;
        }
#endif
        return true;
    }

    template<typename RTYPE, typename ACTION, typename ... ARGS>
    static RTYPE pollAction(int socket, bool pollIn, int timeout, RTYPE defaultReturnValue, ACTION action,
                             ARGS && ... args)
    {
        const short int pollFlag = (pollIn ? POLLIN : POLLOUT);

        // Poll parameter
        struct pollfd pollFds[1] = { { socket, pollFlag, 0 } };

        // Wait on socket
        int pollResult = poll(pollFds, 1, timeout);

        if (pollResult == 0) {
            return defaultReturnValue;
        }
        else if (pollResult < 0) {
            // Ignore these error codes
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == EINTR)) {
                return defaultReturnValue;
            }
            else {
                logg.logError("Failed to poll socket due to %s (%d)", std::strerror(errno), errno);
            }
        }
        else {
            if (((pollFds[0].revents & POLLERR) == POLLERR) || ((pollFds[0].revents & POLLNVAL) == POLLNVAL)) {
                logg.logError("Remote closed failed as error/invalid");
            }
            // can we perform action
            else if ((pollFds[0].revents & pollFlag) == pollFlag) {
                return action(std::forward<ARGS>(args)...);
            }
            // timeout
            else {
                // Nothing to do
                return defaultReturnValue;
            }
        }

        handleException();
    }

    SocketIO SocketIO::udsClientConnect(lib::Span<const char> address, bool useStructSize)
    {
        const std::size_t length { address.size() };

        assert((length < sizeof(sockaddr_un::sun_path)) && "Socket name is too long");

        // open it
        AutoClosingFd fd { armnn::socket_cloexec(PF_UNIX, SOCK_STREAM, 0) };
        if (!fd) {
            logg.logError("Failed to create client socket");
            handleException();
        }

        sockaddr_un udsAddress;
        const socklen_t addressLength = init_sockaddr_un(udsAddress, address.data, length, useStructSize);

        if (connect(*fd, reinterpret_cast<const sockaddr *>(&udsAddress), addressLength) < 0) {
            logg.logError("Failed to connect socket due to %s (%d)", std::strerror(errno), errno);
            handleException();
        }

        // disable SIGPIPE
        setNoSigPipe(*fd);

        if (!setNonBlocking(*fd)) {
            logg.logError("Failed to set non-blocking flag when connecting socket");
            handleException();
        }

        return SocketIO(std::move(fd), AF_UNIX);

    }

    SocketIO SocketIO::udsServerListen(lib::Span<const char> address, bool useStructSize)
    {
        const std::size_t length {address.size()};
        assert((length < sizeof(sockaddr_un::sun_path)) && "Socket name is too long");

        // open it
        AutoClosingFd fd { armnn::socket_cloexec(PF_UNIX, SOCK_STREAM, 0) };
        if (!fd) {
            logg.logError("Failed to obtain file descriptor when preparing to listen on socket due to %s (%d)", std::strerror(errno), errno);
            handleException();
        }

        sockaddr_un udsAddress;
        const socklen_t addressLength = init_sockaddr_un(udsAddress, address.data, length, useStructSize);

        // bind socket to address
        if (bind(*fd, reinterpret_cast<const sockaddr *>(&udsAddress), addressLength) < 0) {
            logg.logError("Failed to bind socket due to %s (%d)", std::strerror(errno), errno);
            handleException();
        }

        if (listen(*fd, MAX_LISTEN_BACKLOG) < 0) {
            logg.logError("Failed to listen socket due to %s (%d)", std::strerror(errno), errno);
            handleException();
        }

        if (!setNonBlocking(*fd)) {
            logg.logError("Failed to set non-blocking flag when creating listening socket");
            handleException();
        }

        return SocketIO(std::move(fd), AF_UNIX);
    }

    std::unique_ptr<SocketIO> SocketIO::doAccept(const SocketIO & host, int timeout)
    {
        AutoClosingFd acceptFd { ::accept_cloexec(*host.fd, nullptr, nullptr) };

        if (!!acceptFd) {
            setNoSigPipe(*acceptFd);

            if (!setNonBlocking(*acceptFd)) {
                logg.logError("Failed to set non-blocking flag for accepted socket");
                handleException();
            }

            return std::unique_ptr<SocketIO>(new SocketIO(std::move(acceptFd), host.type));
        }
        // Ignore these error codes
        else if ((errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == EINTR)) {
            return nullptr;
        }
        else {
            logg.logError("Failed to accept socket due to %s (%d)", std::strerror(errno), errno);
            handleException();
        }
    }

    std::unique_ptr<SocketIO> SocketIO::accept(int timeout) const
    {
        return pollAction<std::unique_ptr<SocketIO>>(*fd, true, timeout, nullptr, &SocketIO::doAccept, *this,
                                                       timeout);
    }

    int SocketIO::doWrite(SocketIO & host, const std::uint8_t * buffer, std::size_t length, int timeout)
    {
        int bytesSent = send(*host.fd, buffer, length, MSG_NOSIGNAL);

        if (bytesSent >= 0) {
            return bytesSent;
        }

        // Ignore these codes
        else if ((errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == EINTR)) {
            return 0;
        }

        // Failure
        else {
            return bytesSent;
        }
    }

    int SocketIO::write(const std::uint8_t * buffer, std::size_t length, int timeout)
    {
        return pollAction<int>(*fd, false, timeout, 0, &SocketIO::doWrite, *this, buffer, length, timeout);
    }

    bool SocketIO::writeExact(lib::Span<const std::uint8_t> buf)
    {
        const std::uint8_t * const buffer { buf.data };
        std::size_t length { buf.size() };
        int timeoutMillis = 100;
        int bytesRemaining = length;
        int bytesWritten = 0;

        while (bytesWritten < bytesRemaining)
        {
            int wrote = write(buffer + bytesWritten, bytesRemaining - bytesWritten, timeoutMillis);
            if (wrote > 0)
            {
                bytesWritten += wrote;
            }
            else
            {
                return false;
            }
        }
        return true;
    }

    int SocketIO::doRead(SocketIO & host, std::uint8_t * buffer, std::size_t length, int timeout)
    {
        int bytesRead = recv(*host.fd, buffer, length, 0);

        if (bytesRead > 0) {
            return bytesRead;
        }

        // If bytes == 0 then we are closed
        else if (bytesRead == 0) {
            host.close();
            return READ_EOF; // Indicate we can't poll this socket anymore.
        }

        // Ignore these error codes, allow another poll
        else if ((errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == EINTR)) {
            return READ_TIMEDOUT;
        }

        return bytesRead;
    }

    int SocketIO::read(std::uint8_t * buffer, std::size_t length, int timeout)
    {
        return pollAction<int>(*fd, true, timeout, 0, &SocketIO::doRead, *this, buffer, length, timeout);
    }

    bool SocketIO::readExact(lib::Span<std::uint8_t> buf)
    {
        std::uint8_t * const buffer { buf.data };
        const std::size_t length { buf.size() };
        std::size_t accumulatedBytes = 0;

        while (accumulatedBytes < length)
        {
            int result = read(buffer + accumulatedBytes, length - accumulatedBytes, DEFAULT_READ_TIMEOUT_MILLIS);
            if (result <= 0)
            {
                if (result == READ_TIMEDOUT)
                {
                    continue;
                }
                return false;
            }
            else
            {
                accumulatedBytes += result;
            }
        }

        return true;
    }

    std::size_t SocketIO::queryBufferSize(bool recv) const
    {
        int result = 0;
        socklen_t length = sizeof(int);

        if (getsockopt(*fd, SOL_SOCKET, (recv ? SO_RCVBUF : SO_SNDBUF), &result, &length) != 0) {
            return 0;
        }

        if (result <= 0) {
            return 0;
        }

        return result;
    }

    SocketIO::SocketIO(AutoClosingFd && fd, int type)
            : fd(std::move(fd)),
              type(type)
    {
    }

}
