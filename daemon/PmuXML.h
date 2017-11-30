/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef PMUXML_H
#define PMUXML_H

#include "ClassBoilerPlate.h"

class PmuXML
{
public:
    PmuXML();
    ~PmuXML();

    static void read(const char * const path);
    static void writeToKernel();

private:
    static void parse(const char * const xml);
    static void getDefaultXml(const char ** const xml, unsigned int * const len);

    // Intentionally unimplemented
    CLASS_DELETE_COPY_MOVE(PmuXML);
};

#endif // PMUXML_H
