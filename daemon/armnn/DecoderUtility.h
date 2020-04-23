/* Copyright (C) 2019-2020 by Arm Limited. All rights reserved. */
#ifndef ARMNN_DECODERUTILITY_H_
#define ARMNN_DECODERUTILITY_H_

#include "../Logging.h"
#include "PacketDecoder.h"
#include "IPacketConsumer.h"
#include "PacketUtility.h"

#include <map>
#include <sstream>
#include <string>

namespace armnn {

    bool readCString(Bytes bytes, std::uint32_t offset, std::string & out);

    bool fillPacketVersionTable(Bytes bytes,
                                std::uint32_t offset,
                                const ByteOrder byteOrder,
                                std::vector<PacketVersionTable> & out);

    lib::Optional<StreamMetadataContent> decodeStreamMetaData(Bytes bytes, const ByteOrder byteOrder);
    bool decodeAndConsumePeriodicCounterSelectionPkt(Bytes bytes,
                                                     const ByteOrder byteOrder,
                                                     IPacketConsumer & consumer);
    bool decodeAndConsumePerJobCounterSelectionPkt(Bytes bytes,
                                                   const ByteOrder byteOrder,
                                                   IPacketConsumer & consumer);

    bool decodeAndConsumePeriodicCounterCapturePkt(const Bytes & bytes,
                                                   const ByteOrder byteOrder,
                                                   IPacketConsumer & consumer);
    bool decodeAndConsumePerJobCounterCapturePkt(bool isPreJob,
                                                 const Bytes & bytes,
                                                 const ByteOrder byteOrder,
                                                 IPacketConsumer & consumer);

}
#endif // end of ARMNN_DECODERUTILITY_H_
