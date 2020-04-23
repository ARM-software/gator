/* Copyright (C) 2020 by Arm Limited. All rights reserved. */

#ifndef ARMNN_IPACKETDECODER_H_
#define ARMNN_IPACKETDECODER_H_

#include "PacketUtility.h"

namespace armnn
{
    //Handles arm-nn packet decoding
    class IPacketDecoder
    {
    public:
        /**
         * type - defined on packet family and id
         * payload - the body of the packet
         * @return - DecodingStatus
         */
        virtual DecodingStatus decodePacket(std::uint32_t type, Bytes payload) = 0;
        virtual ~IPacketDecoder() = default;
    };
}

#endif /* ARMNN_IPACKETDECODER_H_ */
