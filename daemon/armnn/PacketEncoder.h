/* Copyright (C) 2019-2020 by Arm Limited. All rights reserved. */

#ifndef ARMNN_PACKETENCODER_H_
#define ARMNN_PACKETENCODER_H_
#include "ByteOrder.h"
#include "IEncoder.h"
#include "PacketUtilityModels.h"
#include "PacketUtility.h"

#include <set>
#include <vector>
#include <map>

namespace armnn {
    class PacketEncoder : public IEncoder {
    public:
        PacketEncoder(ByteOrder byteOrder);

        std::vector<std::uint8_t> encodePeriodicCounterSelectionRequest(std::uint32_t period,
                                                                        const std::set<std::uint16_t> & eventUids) override;
        std::vector<std::uint8_t> encodePerJobCounterSelectionRequest(std::uint64_t objectId,
                                                                      const std::set<std::uint16_t> & eventUids) override;
        std::vector<std::uint8_t> encodeConnectionAcknowledge() override;
        std::vector<std::uint8_t> encodeCounterDirectoryRequest() override;
        /**
         *Currently support 1.x.x version of packet decoders only.
         */
        static bool isValidPacketVersions(std::vector<PacketVersionTable> pktVersionTable);
    private:
        ByteOrder byteOrder;
        void appendHeader(const std::uint32_t packetIdentifier,
                          const std::uint32_t dataLength,
                          std::vector<std::uint8_t> & payload);
    };

} /* namespace armnn */

#endif /* ARMNN_PACKETENCODER_H_ */
