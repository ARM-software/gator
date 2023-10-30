/* Copyright (C) 2023 by Arm Limited. All rights reserved. */

#pragma once

#include "lib/Span.h"

namespace armnn {
    /** Socket IO interface*/
    class ISocketIO {
    public:
        virtual ~ISocketIO() noexcept = default;

        /**
         * Close the connection
         */
        virtual void close() = 0;

        /**
         * @return True if the connection is open
         */
        [[nodiscard]] virtual bool isOpen() const = 0;

        /**
         * Write exactly the number of bytes contained in the Span.
         * @param buffer The data to write to the socket.
         * @return false if not all bytes in the Span could be written to the socket for whatever reason.  True otherwise.
         */
        [[nodiscard]] virtual bool writeExact(lib::Span<const std::uint8_t> buffer) = 0;

        /**
         * Read bytes into the Span.  The number of desired bytes is dictated by the Span's size() method.
         * @param buffer The buffer to populate
         * @return false if we could not read buffer.size() bytes from the socket for whatever reason.  True otherwise.
         */
        [[nodiscard]] virtual bool readExact(lib::Span<std::uint8_t> buffer) = 0;

        /**
         * Interrupts the connection by shutting down the fd (stopping both receptions and transmissions)
         * Use this instead of close to avoid race condtitions because close will free OS level fd
         **/
        virtual void interrupt() = 0;
    };
}
