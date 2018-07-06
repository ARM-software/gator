/**
 * Copyright (C) Arm Limited 2011-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef COUNTERXML_H_
#define COUNTERXML_H_
#include "mxml/mxml.h"

class CounterXML
{
public:
    CounterXML();
    ~CounterXML();
    char* getXML(); // the string should be freed by the caller
    void write(const char* path);
private:
    mxml_node_t* getTree();
};
#endif /* COUNTERXML_H_ */
