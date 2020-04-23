/* Copyright (C) 2014-2020 by Arm Limited. All rights reserved. */

#ifndef MALIVIDEODRIVER_H
#define MALIVIDEODRIVER_H

#include "SimpleDriver.h"

class MaliVideoCounter;

enum MaliVideoCounterType {
    MVCT_COUNTER,
    MVCT_EVENT,
    MVCT_ACTIVITY,
};

class MaliVideoDriver : public SimpleDriver {
private:
    typedef SimpleDriver super;

public:
    MaliVideoDriver();
    ~MaliVideoDriver();

    void readEvents(mxml_node_t * const root);

    int writeCounters(mxml_node_t * root) const;
    bool claimCounter(Counter & counter) const;

    bool start(const int mveUds);
    void stop(const int mveUds);

private:
    void marshalEnable(const MaliVideoCounterType type, char * const buf, int & pos);

    // Intentionally unimplemented
    MaliVideoDriver(const MaliVideoDriver &) = delete;
    MaliVideoDriver & operator=(const MaliVideoDriver &) = delete;
    MaliVideoDriver(MaliVideoDriver &&) = delete;
    MaliVideoDriver & operator=(MaliVideoDriver &&) = delete;
};

#endif // MALIVIDEODRIVER_H
