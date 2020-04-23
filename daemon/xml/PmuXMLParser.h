/* Copyright (C) 2018-2020 by Arm Limited. All rights reserved. */

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
