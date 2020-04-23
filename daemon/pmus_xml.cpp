/* Copyright (C) 2019-2020 by Arm Limited. All rights reserved. */

#include "pmus_xml.h" // defines and initializes char defaults_xml[] and int defaults_xml_len

#include "xml/PmuXML.h"
const char * const PmuXML::DEFAULT_XML = reinterpret_cast<const char *>(pmus_xml);
const unsigned PmuXML::DEFAULT_XML_LEN = pmus_xml_len;
