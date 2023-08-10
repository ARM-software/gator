/* Copyright (C) 2010-2023 by Arm Limited. All rights reserved. */

#ifndef CONFIGURATIONXMLPARSER_H_
#define CONFIGURATIONXMLPARSER_H_

#include "Configuration.h"
#include "Logging.h"
#include "OlyUtility.h"
#include "xml/MxmlUtils.h"

#include <regex>
#include <string>
#include <vector>

#include <mxml.h>

static const int PARSER_ERROR = -1;
static const int VERSION_ERROR = -2;

class ConfigurationXMLParser {
public:
    virtual ~ConfigurationXMLParser() = default;
    int parseConfigurationContent(const char * config_xml_content);
    const std::vector<CounterConfiguration> & getCounterConfiguration();
    const std::vector<SpeConfiguration> & getSpeConfiguration();
    const std::vector<TemplateConfiguration> & getTemplateConfiguration();

private:
    std::vector<CounterConfiguration> counterConfigurations {};
    std::vector<SpeConfiguration> speConfigurations {};
    std::vector<TemplateConfiguration> templateConfigurations {};

    int readSpe(mxml_node_t * node);
    int readCounter(mxml_node_t * node);
    int readTemplate(mxml_node_t * node);
};

#endif /* CONFIGURATIONXMLPARSER_H_ */
