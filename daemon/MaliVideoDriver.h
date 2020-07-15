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
    using super = SimpleDriver;

public:
    MaliVideoDriver();

    void readEvents(mxml_node_t * xml) override;

    int writeCounters(mxml_node_t * root) const override;
    bool claimCounter(Counter & counter) const override;

    bool start(int mveUds);
    void stop(int mveUds);

private:
    void marshalEnable(MaliVideoCounterType type, char * buf, int & pos);

    // Intentionally unimplemented
    MaliVideoDriver(const MaliVideoDriver &) = delete;
    MaliVideoDriver & operator=(const MaliVideoDriver &) = delete;
    MaliVideoDriver(MaliVideoDriver &&) = delete;
    MaliVideoDriver & operator=(MaliVideoDriver &&) = delete;
};

#endif // MALIVIDEODRIVER_H
