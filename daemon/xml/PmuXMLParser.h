/**
 * Copyright (C) Arm Limited 2018. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef PMUXML_PARSER_H
#define PMUXML_PARSER_H

#include "xml/PmuXML.h"

/**
 * Parse pmus.xml file and return parsed items
 * @param path The path to the file to parse
 * @return The parse result
 */
PmuXML readPmuXml(const char * const path);

/**
 * Parse pmus.xml
 * @param xml    [IN]  The XML as a string
 * @param result [OUT] The object to modify with the parse results
 * @return The parse result (true if parsed successfully, false if not)
 */
bool parseXml(const char * const xml, PmuXML & result);

#endif // PMUXML_PARSER_H
