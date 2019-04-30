/**
 * Copyright (C) Arm Limited 2010-2018. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef CONFIGURATIONXMLPARSER_H_
#define CONFIGURATIONXMLPARSER_H_

#include <vector>
#include <regex>
#include <string>
#include "mxml/mxml.h"

#include "Logging.h"
#include "Configuration.h"
#include "OlyUtility.h"

static const int PARSER_ERROR = -1;
static const int VERSION_ERROR = -2;

class ConfigurationXMLParser
{
public:
    ConfigurationXMLParser();
    virtual ~ConfigurationXMLParser();
    int parseConfigurationContent(const char* config_xml_content);
    const std::vector<CounterConfiguration> & getCounterConfiguration();
    const std::vector<SpeConfiguration> & getSpeConfiguration();

private:
    std::vector<CounterConfiguration> counterConfigurations;
    std::vector<SpeConfiguration> speConfigurations;
    int readSpe(mxml_node_t *node);
    int readCounter(mxml_node_t *node);
};

#endif /* CONFIGURATIONXMLPARSER_H_ */
