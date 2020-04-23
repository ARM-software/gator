/* Copyright (C) 2019-2020 by Arm Limited. All rights reserved. */

#ifndef ARMNN_PACKETDECODERENCODERFACTORY_H_
#define ARMNN_PACKETDECODERENCODERFACTORY_H_

#include "PacketUtility.h"
#include "DecoderUtility.h"
#include "PacketUtilityModels.h"
#include "IPacketDecoder.h"
#include "IEncoder.h"
#include "lib/Optional.h"
#include "IPacketConsumer.h"
#include "PacketDecoder.h"
#include "PacketEncoder.h"
#include "../Logging.h"

#include <memory>
#include <tuple>

namespace armnn
{
    /**
     * Checks if the stream meta data version is in the supported versions
     */
    bool validateStreamMetadataVersion(const std::uint32_t streamMetaVersion);
    /**
     * get stream meta data packet body based on the stream meta data version
     */
    lib::Optional<StreamMetadataContent> getStreamMetadata(Bytes bytes, const ByteOrder byteOrder);

    /**
     * Create decoders based on PacketVersionTable
     *
     */
    std::unique_ptr<IPacketDecoder> createDecoder(std::vector<PacketVersionTable> pktVersionTable,
                                                  ByteOrder order, IPacketConsumer &consumer);
    /**
     * Create encoder based on PacketVersionTable
     */
    std::unique_ptr<IEncoder> createEncoder(std::vector<PacketVersionTable> pktVersionTable,
                                            ByteOrder order);
}

#endif /* ARMNN_PACKETDECODERENCODERFACTORY_H_ */
