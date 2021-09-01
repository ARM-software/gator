/* Copyright (C) 2020-2021 by Arm Limited. All rights reserved. */
#include "armnn/DecoderUtility.h"

#include "Logging.h"

#include <sstream>

namespace armnn {
    static const std::size_t UINT32_SIZE = sizeof(std::uint32_t);
    static const std::size_t COUNTERINDEX_VALUE_SIZE = sizeof(std::uint16_t) + sizeof(std::uint32_t);

    bool readCString(Bytes bytes, std::uint32_t offset, std::string & out)
    {
        if (offset > bytes.length) {
            return false;
        }
        std::ostringstream convert;
        for (std::uint32_t i = offset; i < bytes.length; ++i) {
            if (bytes[i] == 0) {
                break;
            }
            convert << ((char) bytes[i]);
        }
        out = convert.str();
        return true;
    }

    bool fillPacketVersionTable(Bytes bytes,
                                std::uint32_t offset,
                                const ByteOrder byteOrder,
                                std::vector<PacketVersionTable> & out)
    {
        if (offset > bytes.length) {
            return false;
        }

        std::uint16_t packetVersionCount = (byte_order::get_32(byteOrder, bytes, offset) & 0xFFFF0000) >> 16;
        std::uint32_t position = offset + sizeof(std::uint32_t);
        std::uint32_t total = position + (packetVersionCount * (sizeof(std::uint64_t)));

        if (total > bytes.length) {
            return false;
        }
        while (total > position) {
            //0:31
            const std::uint32_t packetIdAndFamily = byte_order::get_32(byteOrder, bytes, position);
            //26:31
            std::uint8_t packetFamily = getBits(packetIdAndFamily, 26, 31);
            //16:25
            std::uint16_t packetId = getBits(packetIdAndFamily, 16, 25);

            ///read packetversion
            position += sizeof(std::uint32_t);
            std::uint32_t packetVersion = byte_order::get_32(byteOrder, bytes, position);
            PacketVersionTable table = {packetVersion, packetId, packetFamily};

            out.push_back(table);
            //read next table, increment position
            position += sizeof(std::uint32_t);
        }
        return true;
    }

    /**
     * @param startPosition start position of the counteruid index, when byte array is read as 16 bits.
     * Returns true if was able to decode the uids, if there are any malformed packets decoding
     * is not started and returns false
     */
    bool addCounterIds(int startPosition, const ByteOrder byteOrder, Bytes bytes, std::set<std::uint16_t> & counterIds)
    {
        const std::size_t sizeOfUint16 = sizeof(std::uint16_t);

        std::uint32_t position = startPosition * sizeOfUint16;

        int remaningBytes = bytes.length - position;
        if (remaningBytes > 0 && ((remaningBytes % sizeOfUint16) != 0)) {
            logg.logError("Malformed bytes received for counter ids");
            return false;
        }
        for (; position <= bytes.length - sizeOfUint16; position += sizeOfUint16) {
            const std::uint16_t data = byte_order::get_16(byteOrder, bytes, position);
            counterIds.insert(data);
        }
        return true;
    }
    lib::Optional<StreamMetadataContent> decodeStreamMetaData(Bytes packetBodyAfterMagic, const ByteOrder byteOrder)
    {
        if (packetBodyAfterMagic.length > 9 * 4) { //Excluding pipe magic message
            std::vector<std::uint32_t> offsets;

            //reading non offset values, pipe magic is removed from the bytes
            const std::uint32_t streamMetaVersion = byte_order::get_32(byteOrder, packetBodyAfterMagic, 0);
            //const std::uint32_t maxDataLength = byte_order::get_32(byteOrder, packetBodyAfterMagic, 1 * UINT32_SIZE);
            const std::uint32_t pid = byte_order::get_32(byteOrder, packetBodyAfterMagic, 2 * UINT32_SIZE);

            for (int i = 3; i < 8; i++) {
                //offset is the position in the byte array to read the actual value.
                const std::uint32_t offset = byte_order::get_32(byteOrder, packetBodyAfterMagic, i * UINT32_SIZE);
                offsets.emplace_back(offset);
            }

            const std::uint32_t offSetInfo = offsets.at(0);
            const std::uint32_t offsetHwVersion = offsets.at(1);
            const std::uint32_t offSetSwVersion = offsets.at(2);
            const std::uint32_t offSetProcessName = offsets.at(3);
            const std::uint32_t offSetPktVerTable = offsets.at(4);

            //Read pool to get the actual data from the offsets
            std::string info;        //7 bit null terminated ASCII
            std::string hwVersion;   //7 bit null terminated ASCII
            std::string swVersion;   //7 bit null terminated ASCII
            std::string processName; //7 bit null terminated ASCII
            std::vector<PacketVersionTable> pktVersionTables;

            if (offSetInfo >= 40) {
                //since the pipe_magic is not included in the byte array removing the 4 packetBodyAfterMagic from offset
                //doing this for all offsets
                if (!readCString(packetBodyAfterMagic, (offSetInfo - sizeof(std::uint32_t)), info)) {
                    logg.logError("Decoding Info from stream metadata failed");
                    return lib::Optional<StreamMetadataContent>();
                }
            }
            else {
                logg.logError("Offset for Info incorrect in stream metadata packet");
                return lib::Optional<StreamMetadataContent>();
            }
            if (offsetHwVersion >= 40) {
                if (!readCString(packetBodyAfterMagic, (offsetHwVersion - sizeof(std::uint32_t)), hwVersion)) {
                    logg.logError("Decoding HW version from stream metadata failed");
                    return lib::Optional<StreamMetadataContent>();
                }
            }
            else {
                logg.logError("Offset for HW version incorrect in stream metadata packet");
                return lib::Optional<StreamMetadataContent>();
            }
            if (offSetSwVersion >= 40) {
                if (!readCString(packetBodyAfterMagic, (offSetSwVersion - sizeof(std::uint32_t)), swVersion)) {
                    logg.logError("Decoding SW version from stream metadata failed");
                    return lib::Optional<StreamMetadataContent>();
                }
            }
            else {
                logg.logError("Offset for SW version incorrect in stream metadata packet");
                return lib::Optional<StreamMetadataContent>();
            }
            if (offSetProcessName >= 40) {
                if (!readCString(packetBodyAfterMagic, (offSetProcessName - sizeof(std::uint32_t)), processName)) {
                    logg.logError("Decoding Process name from stream metadata failed");
                    return lib::Optional<StreamMetadataContent>();
                }
            }
            else {
                logg.logError("Offset for Process name incorrect in stream metadata packet");
                return lib::Optional<StreamMetadataContent>();
            }
            if (offSetPktVerTable >= 40) {
                if (!fillPacketVersionTable(packetBodyAfterMagic,
                                            (offSetPktVerTable - sizeof(std::uint32_t)),
                                            byteOrder,
                                            pktVersionTables)) {
                    logg.logError("Decoding packet version table from stream metadata failed");
                    return lib::Optional<StreamMetadataContent>();
                }
            }
            else {
                logg.logError("Offset for packet version table incorrect in stream metadata packet");
                return lib::Optional<StreamMetadataContent>();
            }
            return StreamMetadataContent {pid,
                                          processName,
                                          info,
                                          hwVersion,
                                          swVersion,
                                          streamMetaVersion,
                                          pktVersionTables};
        }
        logg.logError("Insufficient number of bytes received for decoding steam metadata packet");
        return lib::Optional<StreamMetadataContent>();
    }

    bool decodeAndConsumePeriodicCounterSelectionPkt(Bytes bytes, const ByteOrder byteOrder, IPacketConsumer & consumer)
    {
        if (bytes.length == 0) {
            logg.logMessage("Data length is 0, hence counter collection is disabled.");
            if (!consumer.onPeriodicCounterSelection(0, {})) {
                logg.logError("Disable periodic counter selection consumer, failed");
                return false;
            }
            return true;
        }
        if (bytes.length < UINT32_SIZE) { //not even the size of period at the offset
            logg.logError("Insufficient number of bytes received for decoding Periodic counter selection packet");
            return false;
        }
        const std::uint32_t period = byte_order::get_32(byteOrder, bytes, 0);
        std::set<std::uint16_t> counterIds;
        if (!addCounterIds(2, byteOrder, bytes, counterIds)) {
            return false;
        }
        if (!consumer.onPeriodicCounterSelection(period, counterIds)) {
            return false;
        }
        return true;
    }

    bool decodeAndConsumePerJobCounterSelectionPkt(Bytes bytes, const ByteOrder byteOrder, IPacketConsumer & consumer)
    {
        if (bytes.length == 0) {
            logg.logMessage("Data length is 0, hence per job counter collection is disabled.");
            if (!consumer.onPerJobCounterSelection(0, {})) {
                logg.logError("Disable per job counter selection consumer, failed");
                return false;
            }
            return true;
        }
        if ((bytes.length < sizeof(std::uint64_t))) {
            logg.logMessage("Insufficient number of bytes passed for decoding Per job counter selection packet");
            return false;
        }
        const std::uint64_t objectId = byte_order::get_64(byteOrder, bytes, 0);
        std::set<std::uint16_t> counterIds;
        if (!addCounterIds(4, byteOrder, bytes, counterIds)) {
            return false;
        }
        if (!consumer.onPerJobCounterSelection(objectId, counterIds)) {
            return false;
        }
        return true;
    }

    /**
     *@param startPosition startPosition after reading bytes array as a 32bit data.
     *Returns true if was able to decode the  counterIndexValues, if there are any malformed packets decoding
     * is not started and returns false
     */
    bool addCounterIndexValues(int startPosition,
                               const ByteOrder byteOrder,
                               const Bytes & bytes,
                               std::map<std::uint16_t, std::uint32_t> & counterIndexValues)
    {
        std::uint32_t size = sizeof(std::uint16_t) + sizeof(std::uint32_t);

        std::uint32_t position = startPosition * UINT32_SIZE;
        int remaningBytes = bytes.length - position;

        if (remaningBytes > 0 && ((remaningBytes % size) != 0)) {
            logg.logError("Malformed bytes received for counter ids");
            return false;
        }
        for (; (position + size) <= bytes.length; position += size) {
            const std::uint16_t index = byte_order::get_16(byteOrder, bytes, position);
            const std::uint32_t value = byte_order::get_32(byteOrder, bytes, position + sizeof(std::uint16_t));
            counterIndexValues.insert({index, value});
        }
        return true;
    }

    bool decodeAndConsumePeriodicCounterCapturePkt(const Bytes & bytes,
                                                   const ByteOrder byteOrder,
                                                   IPacketConsumer & consumer)
    {
        std::size_t timestampSize = sizeof(std::uint64_t);

        if ((bytes.length < timestampSize) || ((bytes.length - timestampSize) % COUNTERINDEX_VALUE_SIZE != 0)) {
            logg.logError("Received a malformed periodic counter capture packet");
            return false;
        }

        const std::uint64_t timeStamp = byte_order::get_64(byteOrder, bytes, 0);
        std::map<std::uint16_t, std::uint32_t> counterIndexValueMap;

        if (!addCounterIndexValues(2, byteOrder, bytes, counterIndexValueMap)) {
            return false;
        }
        if (!consumer.onPeriodicCounterCapture(timeStamp, counterIndexValueMap)) {
            return false;
        }
        return true;
    }

    bool decodeAndConsumePerJobCounterCapturePkt(bool isPreJob,
                                                 const Bytes & bytes,
                                                 const ByteOrder byteOrder,
                                                 IPacketConsumer & consumer)
    {
        std::size_t timestampAndObjectRefSize = 2 * sizeof(std::uint64_t);

        if ((bytes.length < timestampAndObjectRefSize) ||
            ((bytes.length - timestampAndObjectRefSize) % COUNTERINDEX_VALUE_SIZE != 0)) {
            logg.logError("Received a malformed per job counter capture packet");
            return false;
        }
        const std::uint64_t timeStamp = byte_order::get_64(byteOrder, bytes, 0);
        const std::uint64_t objectRef = byte_order::get_64(byteOrder, bytes, 2 * UINT32_SIZE);
        std::map<std::uint16_t, std::uint32_t> counterIndexValueMap;
        if (!addCounterIndexValues(4, byteOrder, bytes, counterIndexValueMap)) {
            return false;
        }
        if (!consumer.onPerJobCounterCapture(isPreJob, timeStamp, objectRef, counterIndexValueMap)) {
            return false;
        }
        return true;
    }

}
