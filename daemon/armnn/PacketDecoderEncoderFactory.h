/* Copyright (C) 2019-2020 by Arm Limited. All rights reserved. */

#ifndef ARMNN_PACKETDECODERENCODERFACTORY_H_
#define ARMNN_PACKETDECODERENCODERFACTORY_H_

#include "../Logging.h"
#include "DecoderUtility.h"
#include "IEncoder.h"
#include "IPacketConsumer.h"
#include "IPacketDecoder.h"
#include "PacketDecoder.h"
#include "PacketEncoder.h"
#include "PacketUtility.h"
#include "PacketUtilityModels.h"
#include "lib/Optional.h"

#include <memory>
#include <tuple>

namespace armnn {
    /**
     * Checks if the stream meta data version is in the supported versions
     */
    bool validateStreamMetadataVersion(std::uint32_t streamMetaVersion);
    /**
     * get stream meta data packet body based on the stream meta data version
     */
    lib::Optional<StreamMetadataContent> getStreamMetadata(Bytes packetBodyAfterMagic, ByteOrder byteOrder);

    /**
     * Create decoders based on PacketVersionTable
     *
     */
    std::unique_ptr<IPacketDecoder> createDecoder(const std::vector<PacketVersionTable> & pktVersionTable,
                                                  ByteOrder order,
                                                  IPacketConsumer & consumer);
    /**
     * Create encoder based on PacketVersionTable
     */
    std::unique_ptr<IEncoder> createEncoder(const std::vector<PacketVersionTable> & pktVersionTable, ByteOrder order);
}

#endif /* ARMNN_PACKETDECODERENCODERFACTORY_H_ */
