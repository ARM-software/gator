/* Copyright (C) 2010-2021 by Arm Limited. All rights reserved. */

#ifndef CONFIGURATION_XML_H
#define CONFIGURATION_XML_H

#include "Configuration.h"
#include "lib/Span.h"

#include <memory>
#include <set>
#include <string>
#include <vector>

class Drivers;
class Driver;
class GatorCpu;

namespace configuration_xml {
    std::unique_ptr<char, void (*)(void *)> getDefaultConfigurationXml(lib::Span<const GatorCpu> clusters);

    void getPath(char * path, size_t n);
    void remove();
    /**
     * @return An error or empty
     */
    std::string addCounterToSet(std::set<CounterConfiguration> & configs, CounterConfiguration && config);
    /**
     * @return An error or empty
     */
    std::string addSpeToSet(std::set<SpeConfiguration> & configs, SpeConfiguration && config);
    /**
     * @return An error or empty
     */
    std::string setCounters(const std::set<CounterConfiguration> & counterConfigurations,
                            bool printWarningIfUnclaimed,
                            Drivers & drivers);

    struct Contents {
        std::unique_ptr<char, void (*)(void *)> raw;
        bool isDefault;
        std::vector<CounterConfiguration> counterConfigurations;
        std::vector<SpeConfiguration> speConfigurations;
    };
    Contents getConfigurationXML(lib::Span<const GatorCpu> clusters);

}

#endif // CONFIGURATION_XML_H
