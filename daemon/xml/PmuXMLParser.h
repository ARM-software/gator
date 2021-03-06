/* Copyright (C) 2018-2021 by Arm Limited. All rights reserved. */

#ifndef PMUXML_PARSER_H
#define PMUXML_PARSER_H

#include "xml/PmuXML.h"

/**
 * Parse pmus.xml file and return parsed items
 * @param path The path to the file to parse
 * @return The parse result
 */
PmuXML readPmuXml(const char * path);

/**
 * Parse pmus.xml
 * @param xml [IN]  The XML as a string
 * @param pmuXml [OUT] The object to modify with the parse results
 * @return The parse result (true if parsed successfully, false if not)
 */
bool parseXml(const char * xml, PmuXML & pmuXml);

#endif // PMUXML_PARSER_H
