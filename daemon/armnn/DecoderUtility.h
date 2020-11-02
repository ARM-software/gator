/* Copyright (C) 2019-2020 by Arm Limited. All rights reserved. */
#ifndef ARMNN_DECODERUTILITY_H_
#define ARMNN_DECODERUTILITY_H_

#include "armnn/IPacketConsumer.h"
#include "armnn/PacketUtility.h"
#include "armnn/PacketUtilityModels.h"
#include "lib/Optional.h"

namespace armnn {

    bool readCString(Bytes bytes, std::uint32_t offset, std::string & out);

    bool fillPacketVersionTable(Bytes bytes,
                                std::uint32_t offset,
                                ByteOrder byteOrder,
                                std::vector<PacketVersionTable> & out);

    lib::Optional<StreamMetadataContent> decodeStreamMetaData(Bytes packetBodyAfterMagic, ByteOrder byteOrder);

    bool decodeAndConsumePeriodicCounterSelectionPkt(Bytes bytes, ByteOrder byteOrder, IPacketConsumer & consumer);

    bool decodeAndConsumePerJobCounterSelectionPkt(Bytes bytes, ByteOrder byteOrder, IPacketConsumer & consumer);

    bool decodeAndConsumePeriodicCounterCapturePkt(const Bytes & bytes,
                                                   ByteOrder byteOrder,
                                                   IPacketConsumer & consumer);

    bool decodeAndConsumePerJobCounterCapturePkt(bool isPreJob,
                                                 const Bytes & bytes,
                                                 ByteOrder byteOrder,
                                                 IPacketConsumer & consumer);

}
#endif // end of ARMNN_DECODERUTILITY_H_
