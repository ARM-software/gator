/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef CONFIGURATION_XML_H
#define CONFIGURATION_XML_H

#include <memory>
#include <vector>
#include <string>
#include <set>

#include "lib/Span.h"

#include "Configuration.h"

class Drivers;
class Driver;
class GatorCpu;

namespace configuration_xml
{
    std::unique_ptr<char, void (*)(void*)> getDefaultConfigurationXml(lib::Span<const GatorCpu> clusters);

    void getPath(char* path);
    void remove();
    /**
     *
     * @param configs
     * @param config
     * @return An error or empty
     */
    std::string addCounterToSet(std::set<CounterConfiguration> & configs, CounterConfiguration && config);
    /**
     *
     * @param configs
     * @param config
     * @return An error or empty
     */
    std::string addSpeToSet(std::set<SpeConfiguration> & configs, SpeConfiguration && config);
    /**
     *
     * @param counterConfigurations
     * @param printWarningIfUnclaimed
     * @param drivers
     * @return An error or empty
     */
    std::string setCounters(const std::set<CounterConfiguration> & counterConfigurations, bool printWarningIfUnclaimed,
                            Drivers & drivers);

    struct Contents
    {
        std::unique_ptr<char, void (*)(void*)> raw;
        bool isDefault;
        std::vector<CounterConfiguration> counterConfigurations;
        std::vector<SpeConfiguration> speConfigurations;
    };
    Contents getConfigurationXML(lib::Span<const GatorCpu> clusters);

}

#endif // CONFIGURATION_XML_H
