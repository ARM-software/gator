/* Copyright (C) 2019-2020 by Arm Limited. All rights reserved. */
#include "armnn/PacketDecoder.h"

#include "armnn/CounterDirectoryDecoder.h"
#include "armnn/DecoderUtility.h"
#include "armnn/PacketUtility.h"
#include "lib/EnumUtils.h"

#include <string>

namespace armnn {
    PacketDecoder::PacketDecoder(ByteOrder byteOrder_, IPacketConsumer & consumer_)
        : byteOrder(byteOrder_), consumer(consumer_)
    {
    }

    DecodingStatus PacketDecoder::decodePacket(std::uint32_t type, Bytes payload)
    {
        //omitted some types as no decoding included
        switch (type) {
            case lib::toEnumValue(PacketType::CounterDirectoryPkt): //
            {
                //1.x.x
                const CounterDirectoryDecoder cdd(byteOrder, consumer);
                if (!cdd.decode(payload)) {
                    logg.logError("Decode and consume of counter directory packet failed");
                    return DecodingStatus::Failed;
                }
            } break;

            case lib::toEnumValue(PacketType::PeriodicCounterSelectionPkt): //
            {
                //1.x.x
                if (!armnn::decodeAndConsumePeriodicCounterSelectionPkt(payload, byteOrder, consumer)) {
                    logg.logError("Decode and consume of period counter selection failed");
                    return DecodingStatus::Failed;
                }

            } break;
            case lib::toEnumValue(PacketType::PerJobCounterSelectionPkt): //
            {
                //1.x.x
                if (!armnn::decodeAndConsumePerJobCounterSelectionPkt(payload, byteOrder, consumer)) {
                    logg.logError("Decode and consume of per job counter selection failed");
                    return DecodingStatus::Failed;
                }

            } break;
            case lib::toEnumValue(PacketType::PeriodicCounterCapturePkt): //
            {
                //1.x.x
                if (!armnn::decodeAndConsumePeriodicCounterCapturePkt(payload, byteOrder, consumer)) {
                    logg.logError("Decode and consume of periodic counter capture failed");
                    return DecodingStatus::Failed;
                }

            } break;
            case lib::toEnumValue(PacketType::PrePerJobCounterCapturePkt): //
            {
                //1.x.x
                if (!armnn::decodeAndConsumePerJobCounterCapturePkt(true, payload, byteOrder, consumer)) {
                    logg.logError("Decode and consume of pre per job counter capture failed");
                    return DecodingStatus::Failed;
                }

            } break;
            case lib::toEnumValue(PacketType::PostPerJobCounterCapturePkt): //
            {
                //1.x.x
                if (!armnn::decodeAndConsumePerJobCounterCapturePkt(false, payload, byteOrder, consumer)) {
                    logg.logError("Decode and consume of post per job counter capture failed");
                    return DecodingStatus::Failed;
                }

            } break;
            // The timeline packets are decoded host side, so just forward on
            case lib::toEnumValue(PacketType::TimelineMessageDirectoryPkt): //
            case lib::toEnumValue(PacketType::TimelineMessagePkt):          //
            {
                return DecodingStatus::NeedsForwarding;
            }
            case lib::toEnumValue(PacketType::StreamMetadataPkt): //
            {
                // Ignored: The metadata packet should be decoded before this class is created.
                // This is to ignore any additional metadata packets (which should not happen).
                break;
            }
            default:
                logg.logError("Packet type unsupported by decoder 0x%08x (family=0x%02x, id=0x%03x)",
                              type,
                              getBits(type, 26, 31),
                              getBits(type, 16, 25));
                return DecodingStatus::Failed;
                break;
        }
        return DecodingStatus::Ok;
    }

    bool PacketDecoder::isValidPacketVersions(const std::vector<PacketVersionTable> & pktVersionTable)
    {
        bool validPacket = false;
        for (auto pktVersion : pktVersionTable) {
            auto packetVersion = pktVersion.packetVersion;
            auto packetType = makePacketType(pktVersion.packetFamily, pktVersion.packetId, 0);
            switch (packetType) {
                case lib::toEnumValue(PacketType::StreamMetadataPkt):
                case lib::toEnumValue(PacketType::CounterDirectoryPkt):
                case lib::toEnumValue(PacketType::PeriodicCounterSelectionPkt):
                case lib::toEnumValue(PacketType::PerJobCounterSelectionPkt):
                case lib::toEnumValue(PacketType::PeriodicCounterCapturePkt):
                case lib::toEnumValue(PacketType::PrePerJobCounterCapturePkt):
                case lib::toEnumValue(PacketType::PostPerJobCounterCapturePkt):
                case lib::toEnumValue(PacketType::TimelineMessageDirectoryPkt):
                case lib::toEnumValue(PacketType::TimelineMessagePkt): {
                    auto majorVersion = getBits(packetVersion, 22, 31);
                    if (majorVersion != SUPPORTED_PACKET_MAJOR_VERSION[0]) {
                        logg.logError(
                            "Unsupported packet version (%u:%u:%u) for packet type (family=0x%02x, id=0x%03x)",
                            majorVersion,
                            getBits(packetVersion, 12, 22),
                            getBits(packetVersion, 0, 11),
                            pktVersion.packetFamily,
                            pktVersion.packetId);
                        return false;
                    }
                    validPacket = true;
                    break;
                }
                case lib::toEnumValue(PacketType::ActivateTimelineReportingPkt):
                case lib::toEnumValue(PacketType::DeactivateTimelineReportingPkt):
                case lib::toEnumValue(PacketType::ConnectionAckPkt):
                case lib::toEnumValue(PacketType::CounterDirectoryReqPkt): {
                    // not transmitted by target
                    break;
                }
                default:
                    logg.logError("No decoder supported yet for packet type (family=0x%02x, id=0x%03x)",
                                  pktVersion.packetFamily,
                                  pktVersion.packetId);
                    break;
            }
        }
        return validPacket;
    }

}
