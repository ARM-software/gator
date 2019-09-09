/* Copyright (c) 2019 by Arm Limited. All rights reserved. */

#include "xml/PmuXML.h"
#include "pmus_xml.h" // defines and initializes char defaults_xml[] and int defaults_xml_len
const char * const PmuXML::DEFAULT_XML = reinterpret_cast<const char *>(pmus_xml);
const unsigned PmuXML::DEFAULT_XML_LEN = pmus_xml_len;
