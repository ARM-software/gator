/* Copyright (C) 2019-2020 by Arm Limited. All rights reserved. */

#ifndef ARMNN_PACKETUTILITYMODELS_H_
#define ARMNN_PACKETUTILITYMODELS_H_

#include <cstdint>
#include <string>
#include <vector>

namespace armnn {
    struct PacketVersionTable {
        std::uint32_t packetVersion;
        std::uint16_t packetId;
        std::uint8_t packetFamily;
    };

    struct StreamMetadataContent {
        std::uint32_t pid;
        std::string processName;
        std::string info;
        std::string hwVersion;
        std::string swVersion;
        std::uint32_t streamMetaVersion;
        std::vector<PacketVersionTable> pktVersionTables;
    };
}

#endif /* ARMNN_PACKETUTILITYMODELS_H_ */
