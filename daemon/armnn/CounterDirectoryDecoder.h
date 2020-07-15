/* Copyright (C) 2019-2020 by Arm Limited. All rights reserved. */
#ifndef INCLUDE_ARMNN_COUNTER_DIRECTORY_PACKET_DECODER_H
#define INCLUDE_ARMNN_COUNTER_DIRECTORY_PACKET_DECODER_H

#include "armnn/ByteOrder.h"
#include "armnn/ICounterDirectoryConsumer.h"
#include "lib/Span.h"

#include <cstdint>

namespace armnn {
    /**
     * Decoder class that decodes the counter directory packet
     */
    class CounterDirectoryDecoder {
    public:
        using Bytes = lib::Span<const std::uint8_t>;

        CounterDirectoryDecoder(ByteOrder byteOrder, ICounterDirectoryConsumer & consumer)
            : byteOrder(byteOrder), consumer(consumer) {};

        /**
         * Decode a packet
         *
         * @param bytes The byte data for the packet
         * @return True if data decoded correctly, false if error
         */
        bool decode(Bytes bytes) const;

    private:
        ByteOrder byteOrder;
        ICounterDirectoryConsumer & consumer;
    };
}

#endif // INCLUDE_ARMNN_COUNTER_DIRECTORY_PACKET_DECODER_H
