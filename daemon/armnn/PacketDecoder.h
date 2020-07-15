/* Copyright (C) 2019-2020 by Arm Limited. All rights reserved. */

#ifndef ARMNN_PACKETDECODER_H_
#define ARMNN_PACKETDECODER_H_

#include "../Logging.h"
#include "ByteOrder.h"
#include "IPacketConsumer.h"
#include "IPacketDecoder.h"
#include "PacketUtility.h"
#include "PacketUtilityModels.h"
#include "lib/Optional.h"

namespace armnn {

    //Handles arm-nn packet decoding
    class PacketDecoder : public IPacketDecoder {

    public:
        PacketDecoder(ByteOrder byteOrder_, IPacketConsumer & consumer_);
        /**
         * type - defined on packet family and id
         * payload - the body of the packet
         * @return - PacketDecodeErrorCode
         */
        DecodingStatus decodePacket(std::uint32_t type, Bytes payload) override;

        /**
         *Currently support 1.x.x version of packet decoders only.
         */
        static bool isValidPacketVersions(const std::vector<PacketVersionTable> & pktVersionTable);

    private:
        ByteOrder byteOrder;
        IPacketConsumer & consumer;
    };
}

#endif /* ARMNN_PACKETDECODER_H_ */
