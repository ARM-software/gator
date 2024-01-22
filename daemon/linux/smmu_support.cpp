/* Copyright (C) 2022-2023 by Arm Limited. All rights reserved. */
#include "linux/smmu_support.h"

#include "Logging.h"
#include "lib/Format.h"
#include "lib/FsEntry.h"
#include "lib/Utils.h"
#include "linux/perf/PerfDriverConfiguration.h"
#include "linux/smmu_identifier.h"
#include "xml/PmuXML.h"

#include <algorithm>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gator::smmuv3 {

    namespace {
        enum class pmu_type_t { tcu, tbu };

        constexpr std::string_view smmuv3_device_prefix = "smmuv3_pmcg_";
        constexpr std::string_view sysfs_event_devices = "/sys/bus/event_source/devices";
        constexpr std::string_view tcu_specific_event = "/events/config_struct_access";
        constexpr std::string_view fallback_smmu_model_name = "SMMUv3";

        [[nodiscard]] pmu_type_t detect_pmu_device_type(const std::string & device_path)
        {
            std::string event_path = device_path + std::string(tcu_specific_event);
            if (lib::FsEntry::create(event_path).exists()) {
                return pmu_type_t::tcu;
            }
            return pmu_type_t::tbu;
        }

        [[nodiscard]] std::optional<smmuv3_identifier_t> read_pmu_identifier(const std::string & device_path)
        {
            auto identifier_file = lib::FsEntry::create(device_path + "/identifier");
            if (identifier_file.exists()) {
                auto identifier_str = identifier_file.readFileContentsSingleLine();
                return {smmuv3_identifier_t {identifier_str}};
            }
            return std::nullopt;
        }

        [[nodiscard]] int read_perf_event_type(const std::string & device_path)
        {
            auto type_path = device_path + "/type";
            int result = 0;
            if (lib::FsEntry::create(type_path).exists()) {
                lib::readIntFromFile(type_path.c_str(), result);
            }
            return result;
        }

        [[nodiscard]] UncorePmu smmu_pmu_to_uncore(std::string_view perf_device_name, const smmu_v3_pmu_t & smmu_pmu)
        {
            auto instance_name = perf_device_name.substr(smmuv3_device_prefix.size());
            return {std::string(smmu_pmu.get_core_name()),
                    std::string(perf_device_name),
                    std::string(smmu_pmu.get_counter_set()),
                    std::string(instance_name),
                    smmu_pmu.get_pmnc_counters(),
                    false};
        }

        [[nodiscard]] std::optional<PerfUncore> lookup_pmu_by_model(const PmuXML & xml,
                                                                    std::string_view device_name,
                                                                    pmu_type_t device_type,
                                                                    std::string_view model,
                                                                    int perf_type)
        {
            auto match_counter_set_name = std::string(model);
            auto match_core_name = std::string(model);
            if (device_type == pmu_type_t::tcu) {
                match_counter_set_name += "_TCU";
                match_core_name += " (TCU)";
            }
            else {
                match_counter_set_name += "_TBU";
                match_core_name += " (TBU)";
            }

            auto it = std::find_if(xml.smmu_pmus.begin(), xml.smmu_pmus.end(), [&](const auto & value) {
                return value.get_counter_set() == match_counter_set_name || value.get_core_name() == match_core_name;
            });

            if (it == xml.smmu_pmus.end()) {
                return std::nullopt;
            }

            return {{smmu_pmu_to_uncore(device_name, *it), perf_type}};
        }

        [[nodiscard]] std::optional<PerfUncore> lookup_pmu_by_iidr(const PmuXML & xml,
                                                                   std::string_view device_name,
                                                                   const iidr_t & iidr,
                                                                   int perf_type)
        {
            auto exact_matches = std::vector<std::reference_wrapper<const smmu_v3_pmu_t>> {};
            auto partial_matches = std::vector<std::reference_wrapper<const smmu_v3_pmu_t>> {};

            // search for possible matches
            for (const auto & it : xml.smmu_pmus) {
                if (!it.get_iidr()) {
                    continue;
                }

                const auto & other_iidr = it.get_iidr().value();
                if (other_iidr.has_full_iidr() && other_iidr == iidr) {
                    exact_matches.push_back(std::ref(it));
                    // no point continuing the search if we've got an exact match
                    break;
                }
                if (other_iidr.get_wildcard_value() == iidr.get_wildcard_value()) {
                    partial_matches.push_back(std::ref(it));
                }
            }

            if (exact_matches.size() == 1) {
                return {{smmu_pmu_to_uncore(device_name, exact_matches[0]), perf_type}};
            }

            if (exact_matches.size() > 1) {
                std::string msg = lib::Format() << "Multiple PMU XML entries were found with ID ["
                                                << iidr.get_full_value() << "]. Please correct the XML document";
                LOG_ERROR("%s", msg.c_str());
                return std::nullopt;
            }

            if (partial_matches.size() == 1) {
                return {{smmu_pmu_to_uncore(device_name, partial_matches[0]), perf_type}};
            }

            if (partial_matches.size() > 1) {
                std::string msg = lib::Format()
                               << "Multiple PMU XML entries match the provided SMMUv3 IIDR pattern ["
                               << iidr.get_wildcard_value()
                               << "]. Please specify the full IIDR value to select a single PMU XML entry.";
                LOG_ERROR("%s", msg.c_str());
            }

            return std::nullopt;
        }

        [[nodiscard]] std::optional<PerfUncore> lookup_pmu(const PmuXML & xml,
                                                           std::string_view device_name,
                                                           pmu_type_t device_type,
                                                           const smmuv3_identifier_t & identifier,
                                                           int perf_type)
        {
            if (identifier.get_category() == smmuv3_identifier_t::category_t::iidr) {
                const auto & iidr = identifier.get_iidr();
                auto result = lookup_pmu_by_iidr(xml, device_name, iidr, perf_type);
                if (result) {
                    return result;
                }
                // else just use the default fallback
                return lookup_pmu_by_model(xml, device_name, device_type, fallback_smmu_model_name, perf_type);
            }

            return lookup_pmu_by_model(xml, device_name, device_type, identifier.get_model(), perf_type);
        }
    }
    bool detect_smmuv3_pmus(const PmuXML & pmu_xml,
                            const default_identifiers_t & default_identifiers,
                            PerfDriverConfiguration & config,
                            std::string_view pmu_name)
    {
        // check that this is actually an SMMUv3 device of some kind
        if (pmu_name.rfind(smmuv3_device_prefix) == std::string::npos) {
            return false;
        }

        auto device_path_str = std::string(lib::Format() << sysfs_event_devices << "/" << pmu_name);

        const int perf_type = read_perf_event_type(device_path_str);
        if (perf_type == 0) {
            std::string msg = lib::Format() << "SMMUv3 device [" << pmu_name
                                            << "] does not have a [type] file - cannot determine perf event";
            LOG_ERROR("%s", msg.c_str());
            return false;
        }

        // check to see whether this is a TCU or TBU PMU
        const auto pmu_type = detect_pmu_device_type(device_path_str);

        // if the driver exposes the IIDR in the identifier file then we can use that to
        // pick from the PMU XML.
        auto device_identifier = read_pmu_identifier(device_path_str);

        // use either the detected identifier or a manually specified one
        smmuv3_identifier_t const * identifier_to_lookup = nullptr;
        if (device_identifier) {
            identifier_to_lookup = &device_identifier.value();

            if (default_identifiers.get_tbu_identifier() || default_identifiers.get_tcu_identifier()) {
                std::string msg = lib::Format()
                               << "An SMMUv3 identifier command line argument was provided "
                                  "but the device was identified via sysfs. Detected id ["
                               << device_identifier.value() << "], provided IDs [" << default_identifiers << "]";
                LOG_FINE("%s", msg.c_str());
            }
        }
        else if (pmu_type == pmu_type_t::tcu) {
            if (default_identifiers.get_tcu_identifier()) {
                identifier_to_lookup = &default_identifiers.get_tcu_identifier().value();
            }
        }
        else {
            if (default_identifiers.get_tbu_identifier()) {
                identifier_to_lookup = &default_identifiers.get_tbu_identifier().value();
            }
        }

        if (identifier_to_lookup == nullptr) {
            std::string msg = lib::Format() << "Cannot determine SMMUv3 PMU type for device [" << pmu_name
                                            << "]. No identifier file found and no manual identifier specified";
            LOG_ERROR("%s", msg.c_str());
            return false;
        }

        auto uncore = lookup_pmu(pmu_xml, pmu_name, pmu_type, *identifier_to_lookup, perf_type);
        if (uncore) {
            config.uncores.emplace_back(std::move(*uncore));
            return true;
        }

        std::string msg = lib::Format() << "Could not find a suitable counter set for SMMUv3 PMU device [" << pmu_name
                                        << "]";
        LOG_WARNING("%s", msg.c_str());
        return false;
    }
}
