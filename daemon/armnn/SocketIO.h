/**
 * Copyright (C) 2020-2023 by Arm Limited. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef ARMNN_SOCKET_IO_H
#define ARMNN_SOCKET_IO_H

#include "armnn/IAcceptingSocket.h"
#include "armnn/ISocketIO.h"
#include "lib/AutoClosingFd.h"
#include "lib/Span.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

struct sockaddr;

namespace armnn {
    using lib::AutoClosingFd;

    class SocketIO : public ISocketIO, public IAcceptingSocket {
    public:
        /**
         * Construct a SocketIO object for a unix-domain socket
         * @param address The socket identifier string
         * @param useStructSize Uses sizeof(sockaddr_un) instead of length for socket identifier size
         */
        static SocketIO udsClientConnect(lib::Span<const char> address, bool useStructSize);

        /**
         * Construct a SocketIO object for a unix-domain socket
         * @param address The socket identifier string
         * @param useStructSize Uses sizeof(sockaddr_un) instead of length for socket identifier size
         */
        static SocketIO udsServerListen(lib::Span<const char> address, bool useStructSize);

        /* socket is not copyable */
        inline SocketIO(const SocketIO & that) = delete;
        inline SocketIO & operator=(const SocketIO & that) = delete;

        /* but it is movable */
        inline SocketIO(SocketIO && that) noexcept : fd(std::move(that.fd)), type(that.type) {}

        inline SocketIO & operator=(SocketIO && that) noexcept
        {
            type = that.type;
            fd = std::move(that.fd);

            return *this;
        }

        /**
         * Accept a new connection for a server socket
         * @param timeout Value in milliseconds to wait for connection, negative value means infinite wait
         * @return The socket, or empty if timed out
         */
        [[nodiscard]] std::unique_ptr<ISocketIO> accept(int timeout) override;

        /**
         * Close the connection
         */
        void close() override { fd.close(); }

        /**
         * @return True if the connection is open
         */
        [[nodiscard]] bool isOpen() const override { return !!fd; }

        /**
         * Write exactly the number of bytes contained in the Span.
         * @param buffer The data to write to the socket.
         * @return false if not all bytes in the Span could be written to the socket for whatever reason.  True otherwise.
         */
        [[nodiscard]] bool writeExact(lib::Span<const std::uint8_t> buffer) override;

        /**
         * Read bytes into the Span.  The number of desired bytes is dictated by the Span's size() method.
         * @param buffer The buffer to populate
         * @return false if we could not read buffer.size() bytes from the socket for whatever reason.  True otherwise.
         */
        [[nodiscard]] bool readExact(lib::Span<std::uint8_t> buffer) override;

        /**
         * Interrupts the connection by shutting down the fd (stopping both receptions and transmissions)
         * Use this instead of close to avoid race condtitions because close will free OS level fd
         **/
        void interrupt() override;

    private:
        constexpr static int one_second = 1000;
        int write(const std::uint8_t * buffer, std::size_t length, int timeout = one_second);
        int read(std::uint8_t * buffer, std::size_t length, int timeout = one_second);

        static std::unique_ptr<SocketIO> doAccept(const SocketIO & host, int timeout);
        static int doRead(SocketIO & host, std::uint8_t * buffer, std::size_t length, int timeout);
        static int doWrite(SocketIO & host, const std::uint8_t * buffer, std::size_t length, int timeout);

        AutoClosingFd fd;
        int type;

        SocketIO(AutoClosingFd && fd, int type);
    };
}

#endif /*  ARMNN_SOCKET_IO_H */
