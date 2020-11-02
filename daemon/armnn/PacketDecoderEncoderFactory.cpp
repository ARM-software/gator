/* Copyright (C) 2020 by Arm Limited. All rights reserved. */

#ifndef ARMNN_PACKETDECODERENCODERFACTORY_CPP_
#define ARMNN_PACKETDECODERENCODERFACTORY_CPP_

#include "armnn/PacketDecoderEncoderFactory.h"

namespace armnn {
    /**
     * Checks if the stream meta data version is in the supported versions
     */
    bool validateStreamMetadataVersion(const std::uint32_t streamMetaVersion)
    {
        bool matchFound = false;
        std::uint32_t majorNumber = armnn::getBits(streamMetaVersion, 22, 31);
        for (auto supportedVersion : SUPPORTED_VERSION) {
            //check if major number is in with in the supported versions
            std::uint32_t supportedVersionMajorNumber = armnn::getBits(supportedVersion, 22, 31);
            if (supportedVersionMajorNumber == majorNumber) {
                matchFound = true;
                break;
            }
        }
        return matchFound;
    }

    /**
     * Returns a tuple, if the stream metadata version of the packet matched the available decoder for the
     * packet to the metadata version from the packetBodyAfterMagic passed as argument.
     */
    static lib::Optional<std::uint32_t> isMatchedStreamMetadata(Bytes packetBodyAfterMagic, const ByteOrder byteOrder)
    {
        if (packetBodyAfterMagic.length >= 4) {
            const std::uint32_t streamMetaVersion = byte_order::get_32(byteOrder, packetBodyAfterMagic, 0);
            if (validateStreamMetadataVersion(streamMetaVersion)) {
                return streamMetaVersion;
            }
        }
        return lib::Optional<std::uint32_t>();
    }

    /**
     * get stream meta data packet body based on the stream meta data version
     */
    lib::Optional<StreamMetadataContent> getStreamMetadata(Bytes packetBodyAfterMagic, const ByteOrder byteOrder)
    {
        auto matchedStreamMetadata = isMatchedStreamMetadata(packetBodyAfterMagic, byteOrder);
        if (matchedStreamMetadata.valid()) {
            //check for major version only
            std::uint32_t majorNumber = armnn::getBits(matchedStreamMetadata.get(), 22, 31);
            if (majorNumber == 1) { //1.0.0 - return decoder for
                return armnn::decodeStreamMetaData(packetBodyAfterMagic, byteOrder);
            }
        }
        // add support for newer versions
        logg.logError("Unsupported stream metadata version");
        return lib::Optional<StreamMetadataContent>();
    }

    /**
     * Create decoders based on PacketVersionTable
     *
     */
    std::unique_ptr<IPacketDecoder> createDecoder(const std::vector<PacketVersionTable> & pktVersionTable,
                                                  ByteOrder order,
                                                  IPacketConsumer & consumer)
    {
        if (!pktVersionTable.empty()) {
            if (PacketDecoder::isValidPacketVersions(pktVersionTable)) {
                std::unique_ptr<IPacketDecoder> decoder(new PacketDecoder(order, consumer));
                return decoder;
            }
            else {
                logg.logError("Cannot create decoder, as invalid versions in packet version table");
                return std::unique_ptr<IPacketDecoder> {};
            }
        }
        logg.logError("Cannot create decoder, as packet version table was empty");
        return std::unique_ptr<IPacketDecoder> {};
    }

    /**
     * Create encoder based on PacketVersionTable
     */
    std::unique_ptr<IEncoder> createEncoder(const std::vector<PacketVersionTable> & pktVersionTable, ByteOrder order)
    {
        if (!pktVersionTable.empty()) {
            if (PacketEncoder::isValidPacketVersions(pktVersionTable)) {
                std::unique_ptr<IEncoder> encoder(new PacketEncoder(order));
                return encoder;
            }
            else {
                logg.logError("Cannot create encoder, as invalid versions in packet version table");
                return std::unique_ptr<IEncoder> {};
            }
        }
        logg.logError("Cannot create encoder, as packet version table was empty");
        return std::unique_ptr<IEncoder> {};
    }
}

#endif /* ARMNN_PACKETDECODERENCODERFACTORY_CPP_ */
