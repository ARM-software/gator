/**
 * Copyright (C) 2020 by Arm Limited. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef ARMNN_SOCKET_IO_H
#define ARMNN_SOCKET_IO_H

#include "lib/AutoClosingFd.h"
#include "lib/Span.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

struct sockaddr;

namespace armnn {
    using lib::AutoClosingFd;

    class SocketIO {
    public:
        /**
         * Construct a SocketIO object for a unix-domain socket
         *
         * @param address The socket identifier string
         * @param use_struct_size Uses sizeof(sockaddr_un) instead of length for socket identifier size
         */
        static SocketIO udsClientConnect(lib::Span<const char> address, bool useStructSize);

        /**
         * Construct a SocketIO object for a unix-domain socket
         *
         * @param address The socket identifier string
         * @param use_struct_size Uses sizeof(sockaddr_un) instead of length for socket identifier size
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
         *
         * @param timeout Value in milliseconds to wait for connection, negative value means infinite wait
         * @return The socket, or empty if timed out
         */
        std::unique_ptr<SocketIO> accept(int timeout) const;

        /**
         * Close the connection
         */
        inline void close() { fd.close(); }

        /**
         * @return True if the connection is open
         */
        inline bool isOpen() const { return !!fd; }

        /**
         * Write exactly the number of bytes contained in the Span.
         * @param buffer The data to write to the socket.
         * @return false if not all bytes in the Span could be written to the socket for whatever reason.  True otherwise.
         */
        bool writeExact(lib::Span<const std::uint8_t> buffer);

        /**
         * Read bytes into the Span.  The number of desired bytes is dictated by the Span's size() method.
         * @param buffer The buffer to populate
         * @return false if we could not read buffer.size() bytes from the socket for whatever reason.  True otherwise.
         */
        bool readExact(lib::Span<std::uint8_t> buffer);

        /**
         * Query size of socket send/receive buffer
         *
         * @param recv True for receive, false for send
         * @return The size
         */
        std::size_t queryBufferSize(bool recv) const;

        /**
         * Interrupts the connection by shutting down the fd (stopping both receptions and transmissions)
         * Use this instead of close to avoid race condtitions because close will free OS level fd
         **/
        void interrupt();

    private:
        int write(const std::uint8_t * buffer, std::size_t length, int timeout = 1000);
        int read(std::uint8_t * buffer, std::size_t length, int timeout = 1000);

        static std::unique_ptr<SocketIO> doAccept(const SocketIO & host, int timeout);
        static int doRead(SocketIO & host, std::uint8_t * buffer, std::size_t length, int timeout);
        static int doWrite(SocketIO & host, const std::uint8_t * buffer, std::size_t length, int timeout);

        AutoClosingFd fd;
        int type;

        SocketIO(AutoClosingFd && fd, int type);
    };
}

#endif /*  ARMNN_SOCKET_IO_H */
