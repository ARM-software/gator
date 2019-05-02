/**
 * Copyright (C) Arm Limited 2014-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "MaliVideoDriver.h"

#include <unistd.h>

#include "lib/FileDescriptor.h"
#include "BufferUtils.h"
#include "Counter.h"
#include "Logging.h"
#include "OlyUtility.h"
#include "SessionData.h"

// From instr/src/mve_instr_comm_protocol.h
typedef enum mve_instr_configuration_type
{
    MVE_INSTR_RAW = 1 << 0,
    MVE_INSTR_COUNTERS = 1 << 1,
    MVE_INSTR_EVENTS = 1 << 2,
    MVE_INSTR_ACTIVITIES = 1 << 3,

    // Raw always pushed regardless
    MVE_INSTR_PULL = 1 << 12,
    // Raw always unpacked regardless
    MVE_INSTR_PACKED_COMM = 1 << 13,
    // Don’t send ACKt response
    MVE_INSTR_NO_AUTO_ACK = 1 << 14,
} mve_instr_configuration_type_t;

static const char COUNTER[] = "ARM_Mali-V500_cnt";
static const char EVENT[] = "ARM_Mali-V500_evn";
static const char ACTIVITY[] = "ARM_Mali-V500_act";

class MaliVideoCounter : public DriverCounter
{
public:
    MaliVideoCounter(DriverCounter *next, const char *name, const MaliVideoCounterType type, const int id)
            : DriverCounter(next, name),
              mType(type),
              mId(id)
    {
    }

    ~MaliVideoCounter()
    {
    }

    MaliVideoCounterType getType() const
    {
        return mType;
    }
    int getId() const
    {
        return mId;
    }

private:
    const MaliVideoCounterType mType;
    // Mali Video id
    const int mId;
};

MaliVideoDriver::MaliVideoDriver()
    : SimpleDriver("MaliVideoDriver")
{
}

MaliVideoDriver::~MaliVideoDriver()
{
}

void MaliVideoDriver::readEvents(mxml_node_t * const xml)
{
    // Always create the counters as /dev/mv500 may show up after gatord starts
    mxml_node_t *node = xml;
    while (true) {
        node = mxmlFindElement(node, xml, "event", NULL, NULL, MXML_DESCEND);
        if (node == NULL) {
            break;
        }
        const char *counter = mxmlElementGetAttr(node, "counter");
        if (counter == NULL) {
            // Ignore
        }
        else if (strncmp(counter, COUNTER, sizeof(COUNTER) - 1) == 0) {
            int i;
            if (!stringToInt(&i, counter + sizeof(COUNTER) - 1, 10)) {
                logg.logError("The counter attribute of the Mali video counter %s is not an integer", counter);
                handleException();
            }
            setCounters(new MaliVideoCounter(getCounters(), counter, MVCT_COUNTER, i));
        }
        else if (strncmp(counter, EVENT, sizeof(EVENT) - 1) == 0) {
            int i;
            if (!stringToInt(&i, counter + sizeof(EVENT) - 1, 10)) {
                logg.logError("The event attribute of the Mali video counter %s is not an integer", counter);
                handleException();
            }
            setCounters(new MaliVideoCounter(getCounters(), counter, MVCT_EVENT, i));
        }
        else if (strncmp(counter, ACTIVITY, sizeof(ACTIVITY) - 1) == 0) {
            int i;
            if (!stringToInt(&i, counter + sizeof(ACTIVITY) - 1, 10)) {
                logg.logError("The activity attribute of the Mali video counter %s is not an integer", counter);
                handleException();
            }
            setCounters(new MaliVideoCounter(getCounters(), counter, MVCT_ACTIVITY, i));
        }
    }
}

int MaliVideoDriver::writeCounters(mxml_node_t *root) const
{
    if (access("/dev/mv500", F_OK) != 0) {
        // Don't show the counters in counter configuration
        return 0;
    }

    return super::writeCounters(root);
}

bool MaliVideoDriver::claimCounter(Counter &counter) const
{
    if (access("/dev/mv500", F_OK) != 0) {
        // Don't add the counters to captured XML
        return 0;
    }

    return super::claimCounter(counter);
}

void MaliVideoDriver::marshalEnable(const MaliVideoCounterType type, char * const buf, int &pos)
{
    // size
    int numEnabled = 0;
    for (MaliVideoCounter *counter = static_cast<MaliVideoCounter *>(getCounters()); counter != NULL;
            counter = static_cast<MaliVideoCounter *>(counter->getNext())) {
        if (counter->isEnabled() && (counter->getType() == type)) {
            ++numEnabled;
        }
    }
    buffer_utils::packInt(buf, pos, numEnabled * sizeof(uint32_t));
    for (MaliVideoCounter *counter = static_cast<MaliVideoCounter *>(getCounters()); counter != NULL;
            counter = static_cast<MaliVideoCounter *>(counter->getNext())) {
        if (counter->isEnabled() && (counter->getType() == type)) {
            buffer_utils::packInt(buf, pos, counter->getId());
        }
    }
}

bool MaliVideoDriver::start(const int mveUds)
{
    char buf[1 << 12];
    int pos = 0;

    // code - MVE_INSTR_STARTUP
    buf[pos++] = 'C';
    buf[pos++] = 'L';
    buf[pos++] = 'N';
    buf[pos++] = 'T';
    // size
    buffer_utils::packInt(buf, pos, sizeof(uint32_t));
    // client_version_number
    buffer_utils::packInt(buf, pos, 1);

    // code - MVE_INSTR_CONFIGURE
    buf[pos++] = 'C';
    buf[pos++] = 'N';
    buf[pos++] = 'F';
    buf[pos++] = 'G';
    // size
    buffer_utils::packInt(buf, pos, 5 * sizeof(uint32_t));
    // configuration
    buffer_utils::packInt(buf, pos,
                    MVE_INSTR_COUNTERS | MVE_INSTR_EVENTS | MVE_INSTR_ACTIVITIES | MVE_INSTR_PACKED_COMM);
    // communication_protocol_version
    buffer_utils::packInt(buf, pos, 1);
    // data_protocol_version
    buffer_utils::packInt(buf, pos, 1);
    // sample_rate - convert samples/second to ms/sample
    buffer_utils::packInt(buf, pos, gSessionData.mSampleRate / 1000);
    // live_rate - convert ns/flush to ms/flush
    buffer_utils::packInt(buf, pos, gSessionData.mLiveRate / 1000000);

    // code - MVE_INSTR_ENABLE_COUNTERS
    buf[pos++] = 'C';
    buf[pos++] = 'F';
    buf[pos++] = 'G';
    buf[pos++] = 'c';
    marshalEnable(MVCT_COUNTER, buf, pos);

    // code - MVE_INSTR_ENABLE_EVENTS
    buf[pos++] = 'C';
    buf[pos++] = 'F';
    buf[pos++] = 'G';
    buf[pos++] = 'e';
    marshalEnable(MVCT_EVENT, buf, pos);

    // code - MVE_INSTR_ENABLE_ACTIVITIES
    buf[pos++] = 'C';
    buf[pos++] = 'F';
    buf[pos++] = 'G';
    buf[pos++] = 'a';
    marshalEnable(MVCT_ACTIVITY, buf, pos);

    return lib::writeAll(mveUds, buf, pos);
}

void MaliVideoDriver::stop(const int mveUds)
{
    char buf[8];
    int pos = 0;

    // code - MVE_INSTR_STOP
    buf[pos++] = 'S';
    buf[pos++] = 'T';
    buf[pos++] = 'O';
    buf[pos++] = 'P';

    lib::writeAll(mveUds, buf, pos);

    close(mveUds);
}
