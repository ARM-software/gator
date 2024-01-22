/* Copyright (C) 2022-2023 by Arm Limited. All rights reserved. */

#include "agents/perf/capture_configuration.h"

#include "ICpuInfo.h"
#include "SessionData.h"
#include "agents/perf/events/event_configuration.hpp"
#include "agents/perf/events/types.hpp"
#include "agents/perf/record_types.h"
#include "ipc/messages.h"
#include "k/perf_event.h"
#include "lib/Assert.h"
#include "lib/Span.h"
#include "linux/perf/PerfConfig.h"
#include "linux/perf/PerfEventGroup.h"
#include "linux/perf/PerfEventGroupIdentifier.h"
#include "linux/perf/PerfGroups.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace agents::perf {
    namespace {
        /// ------------------------------ serializing

        void add_session_data(ipc::proto::shell::perf::capture_configuration_t::session_data_t & msg,
                              SessionData const & session_data)
        {
            msg.set_live_rate(session_data.mLiveRate);
            msg.set_total_buffer_size(session_data.mTotalBufferSize);
            msg.set_sample_rate(session_data.mSampleRate);
            msg.set_one_shot(session_data.mOneShot);
            msg.set_exclude_kernel_events(session_data.mExcludeKernelEvents);
            msg.set_stop_on_exit(session_data.mStopOnExit);
        }

        void add_perf_config(ipc::proto::shell::perf::capture_configuration_t::perf_config_t & msg,
                             PerfConfig const & perf_config)
        {
            msg.set_has_fd_cloexec(perf_config.has_fd_cloexec);
            msg.set_has_count_sw_dummy(perf_config.has_count_sw_dummy);
            msg.set_has_sample_identifier(perf_config.has_sample_identifier);
            msg.set_has_attr_comm_exec(perf_config.has_attr_comm_exec);
            msg.set_has_attr_mmap2(perf_config.has_attr_mmap2);
            msg.set_has_attr_clockid_support(perf_config.has_attr_clockid_support);
            msg.set_has_attr_context_switch(perf_config.has_attr_context_switch);
            msg.set_has_ioctl_read_id(perf_config.has_ioctl_read_id);
            msg.set_has_aux_support(perf_config.has_aux_support);
            msg.set_is_system_wide(perf_config.is_system_wide);
            msg.set_exclude_kernel(perf_config.exclude_kernel);
            msg.set_can_access_tracepoints(perf_config.can_access_tracepoints);
            msg.set_has_armv7_pmu_driver(perf_config.has_armv7_pmu_driver);
            msg.set_has_64bit_uname(perf_config.has_64bit_uname);
            msg.set_use_64bit_register_set(perf_config.use_64bit_register_set);
            msg.set_has_exclude_callchain_kernel(perf_config.has_exclude_callchain_kernel);
        }

#define SET_IF_NOT_NULL(ptr, object, method)                                                                           \
    do {                                                                                                               \
        if ((ptr) != nullptr) {                                                                                        \
            (object)->method(ptr);                                                                                     \
        }                                                                                                              \
    } while (false)

        void add_clusters(
            google::protobuf::RepeatedPtrField<ipc::proto::shell::perf::capture_configuration_t::cpu_cluster_t> & msg,
            ICpuInfo const & cpu_info,
            lib::Span<perf_capture_configuration_t::cpu_freq_properties_t> cluster_keys_for_cpu_frequency_counter)
        {
            auto clusters = cpu_info.getClusters();

            for (std::size_t index = 0; index < clusters.size(); ++index) {
                auto const & cpu = clusters[index];
                auto freq_key = cluster_keys_for_cpu_frequency_counter[index];
                auto * entry = msg.Add();
                // add properties
                auto * cluster = entry->mutable_properties();
                SET_IF_NOT_NULL(cpu.getCoreName(), cluster, set_core_name);
                SET_IF_NOT_NULL(cpu.getId(), cluster, set_id);
                SET_IF_NOT_NULL(cpu.getCounterSet(), cluster, set_counter_set);
                SET_IF_NOT_NULL(cpu.getDtName(), cluster, set_dt_name);
                SET_IF_NOT_NULL(cpu.getSpeName(), cluster, set_spe_name);
                cluster->set_pmnc_counters(cpu.getPmncCounters());
                cluster->set_is_v8(cpu.getIsV8());
                for (auto id : cpu.getCpuIds()) {
                    cluster->add_cpu_ids(id);
                }
                // add misc
                entry->set_keys_for_cpu_frequency_counter(freq_key.key);
                entry->set_cpu_frequency_counter_uses_cpu_info(freq_key.use_cpuinfo);
            }
        }

        void add_cpus(google::protobuf::RepeatedPtrField<
                          ipc::proto::shell::perf::capture_configuration_t::cpu_properties_t> & msg,
                      ICpuInfo const & cpu_info,
                      std::map<int, std::uint32_t> const & cpu_number_to_spe_type)
        {
            auto clusterIds = cpu_info.getClusterIds();
            auto cpuIds = cpu_info.getCpuIds();

            for (std::size_t index = 0; index < cpu_info.getNumberOfCores(); ++index) {
                auto * entry = msg.Add();
                entry->set_cluster_index(clusterIds[index]);
                entry->set_cpu_id(cpuIds[index]);
                auto it = cpu_number_to_spe_type.find(int(index));
                if (it != cpu_number_to_spe_type.end()) {
                    entry->set_spe_type(it->second);
                }
            }
        }

        void add_uncore_pmus(
            google::protobuf::RepeatedPtrField<ipc::proto::shell::perf::capture_configuration_t::uncore_pmu_t> & msg,
            lib::Span<UncorePmu const> uncore_pmus)
        {
            for (auto const & pmu : uncore_pmus) {
                auto * entry = msg.Add();
                SET_IF_NOT_NULL(pmu.getCoreName(), entry, set_core_name);
                SET_IF_NOT_NULL(pmu.getId(), entry, set_id);
                SET_IF_NOT_NULL(pmu.getCounterSet(), entry, set_counter_set);
                SET_IF_NOT_NULL(pmu.getDeviceInstance(), entry, set_device_instance);
                entry->set_pmnc_counters(pmu.getPmncCounters());
                entry->set_has_cycles_counter(pmu.getHasCyclesCounter());
            }
        }

        void add_cpuid_to_core_name(google::protobuf::Map<::google::protobuf::uint32, std::string> & map,
                                    lib::Span<GatorCpu const> all_known_cpu_pmus)
        {
            for (auto const & pmu : all_known_cpu_pmus) {
                for (auto cpuid : pmu.getCpuIds()) {
                    map[cpuid] = pmu.getCoreName();
                }
            }
        }

        template<typename T>
        std::size_t find_pmu_index(lib::Span<T const> pmus, T const * value)
        {
            runtime_assert(value != nullptr, "identifier value should not be nullptr");

            for (std::size_t n = 0; n < pmus.size(); ++n) {
                if (strcmp(pmus[n].getId(), value->getId()) == 0) {
                    return n;
                }
            }

            throw std::runtime_error("Matching pmu node not found");
        }

        void add_perf_event_attr(ipc::proto::shell::perf::capture_configuration_t::perf_event_attribute_t & msg,
                                 perf_event_attr const & attr)
        {
            msg.set_type(attr.type);
            msg.set_config(attr.config);
            msg.set_sample_period_or_freq(attr.freq ? attr.sample_freq : attr.sample_period);
            msg.set_sample_type(attr.sample_type);
            msg.set_read_format(attr.read_format);
            msg.set_disabled(attr.disabled);
            msg.set_inherit(attr.inherit);
            msg.set_pinned(attr.pinned);
            msg.set_exclusive(attr.exclusive);
            msg.set_exclude_user(attr.exclude_user);
            msg.set_exclude_kernel(attr.exclude_kernel);
            msg.set_exclude_hv(attr.exclude_hv);
            msg.set_exclude_idle(attr.exclude_idle);
            msg.set_mmap(attr.mmap);
            msg.set_comm(attr.comm);
            msg.set_freq(attr.freq);
            msg.set_inherit_stat(attr.inherit_stat);
            msg.set_enable_on_exec(attr.enable_on_exec);
            msg.set_task(attr.task);
            msg.set_watermark(attr.watermark);
            msg.set_precise_ip(ipc::proto::shell::perf::capture_configuration_t::perf_event_attribute_t::precise_ip_t(
                attr.precise_ip));
            msg.set_mmap_data(attr.mmap_data);
            msg.set_sample_id_all(attr.sample_id_all);
            msg.set_exclude_host(attr.exclude_host);
            msg.set_exclude_guest(attr.exclude_guest);
            msg.set_exclude_callchain_kernel(attr.exclude_callchain_kernel);
            msg.set_exclude_callchain_user(attr.exclude_callchain_user);
            msg.set_mmap2(attr.mmap2);
            msg.set_comm_exec(attr.comm_exec);
            msg.set_use_clockid(attr.use_clockid);
            msg.set_context_switch(attr.context_switch);
            msg.set_wakeup_events_or_watermark(attr.watermark ? attr.wakeup_watermark : attr.wakeup_events);
            msg.set_config1(attr.config1);
            msg.set_config2(attr.config2);
            msg.set_sample_regs_user(attr.sample_regs_user);
            msg.set_sample_stack_user(attr.sample_stack_user);
            msg.set_clockid(attr.clockid);
            msg.set_aux_watermark(attr.aux_watermark);
        }

        void add_perf_event(ipc::proto::shell::perf::capture_configuration_t::perf_event_definition_t & msg,
                            int key,
                            perf_event_attr const & attr)
        {
            msg.set_key(key);
            auto * msg_attr = msg.mutable_attr();
            add_perf_event_attr(*msg_attr, attr);
        }

        ipc::proto::shell::perf::capture_configuration_t_perf_event_definition_list_t &
        get_perf_event_configuration_event_list(
            ipc::proto::shell::perf::capture_configuration_t::perf_event_configuration_t & msg,
            PerfEventGroupIdentifier const & identifier,
            ICpuInfo const & cpu_info,
            lib::Span<UncorePmu const> uncore_pmus)
        {
            switch (identifier.getType()) {
                case PerfEventGroupIdentifier::Type::GLOBAL: {
                    return *msg.mutable_global_events();
                }
                case PerfEventGroupIdentifier::Type::SPE: {
                    return *msg.mutable_spe_events();
                }
                case PerfEventGroupIdentifier::Type::PER_CLUSTER_CPU: {
                    auto index = find_pmu_index(cpu_info.getClusters(), identifier.getCluster());
                    auto * map = msg.mutable_cluster_specific_events();
                    return (*map)[index];
                }
                case PerfEventGroupIdentifier::Type::UNCORE_PMU: {
                    auto index = find_pmu_index(uncore_pmus, identifier.getUncorePmu());
                    auto * map = msg.mutable_uncore_specific_events();
                    return (*map)[index];
                }
                case PerfEventGroupIdentifier::Type::SPECIFIC_CPU: {
                    auto index = static_cast<std::uint32_t>(identifier.getCpuNumber());
                    auto * map = msg.mutable_cpu_specific_events();
                    return (*map)[index];
                }
                default: {
                    throw std::runtime_error("Unexpected type");
                }
            }
        }

        void add_perf_group(ipc::proto::shell::perf::capture_configuration_t::perf_event_configuration_t & msg,
                            PerfEventGroupIdentifier const & identifier,
                            perf_event_group_configurer_state_t const & state,
                            ICpuInfo const & cpu_info,
                            lib::Span<UncorePmu const> uncore_pmus)
        {
            auto & list = get_perf_event_configuration_event_list(msg, identifier, cpu_info, uncore_pmus);
            auto * msg_events = list.mutable_events();
            for (auto const & event : state.events) {
                auto * msg_entry = msg_events->Add();
                add_perf_event(*msg_entry, event.key, event.attr);
            }
        }

        void add_event_configuration(ipc::proto::shell::perf::capture_configuration_t::perf_event_configuration_t & msg,
                                     perf_groups_configurer_state_t const & perf_groups,
                                     ICpuInfo const & cpu_info,
                                     lib::Span<UncorePmu const> uncore_pmus)
        {
            add_perf_event(*msg.mutable_header_event(), perf_groups.header.key, perf_groups.header.attr);

            for (auto const & groups_entry : perf_groups.perfEventGroupMap) {
                add_perf_group(msg, groups_entry.first, groups_entry.second, cpu_info, uncore_pmus);
            }
        }

        void add_ringbuffer_config(ipc::proto::shell::perf::capture_configuration_t::perf_ringbuffer_config_t & msg,
                                   agents::perf::buffer_config_t const & ringbuffer_config)
        {
            msg.set_page_size(ringbuffer_config.page_size);
            msg.set_data_size(ringbuffer_config.data_buffer_size);
            msg.set_aux_size(ringbuffer_config.aux_buffer_size);
        }

        void add_perf_pmu_type_to_name(google::protobuf::Map<::google::protobuf::uint32, std::string> & msg,
                                       std::map<std::uint32_t, std::string> const & perf_pmu_type_to_name)
        {
            for (auto const & entry : perf_pmu_type_to_name) {
                msg[entry.first] = entry.second;
            }
        }

        /// ------------------------------ deserializing

        void extract_session_data(ipc::proto::shell::perf::capture_configuration_t::session_data_t const & msg,
                                  perf_capture_configuration_t::session_data_t & session_data)
        {
            session_data.live_rate = msg.live_rate();
            session_data.total_buffer_size = msg.total_buffer_size();
            session_data.sample_rate = msg.sample_rate();
            session_data.one_shot = msg.one_shot();
            session_data.exclude_kernel_events = msg.exclude_kernel_events();
            session_data.stop_on_exit = msg.stop_on_exit();
        }

        void extract_perf_config(ipc::proto::shell::perf::capture_configuration_t::perf_config_t const & msg,
                                 perf_capture_configuration_t::perf_config_t & perf_config)
        {
            perf_config.has_fd_cloexec = msg.has_fd_cloexec();
            perf_config.has_count_sw_dummy = msg.has_count_sw_dummy();
            perf_config.has_sample_identifier = msg.has_sample_identifier();
            perf_config.has_attr_comm_exec = msg.has_attr_comm_exec();
            perf_config.has_attr_mmap2 = msg.has_attr_mmap2();
            perf_config.has_attr_clockid_support = msg.has_attr_clockid_support();
            perf_config.has_attr_context_switch = msg.has_attr_context_switch();
            perf_config.has_ioctl_read_id = msg.has_ioctl_read_id();
            perf_config.has_aux_support = msg.has_aux_support();
            perf_config.is_system_wide = msg.is_system_wide();
            perf_config.exclude_kernel = msg.exclude_kernel();
            perf_config.can_access_tracepoints = msg.can_access_tracepoints();
            perf_config.has_armv7_pmu_driver = msg.has_armv7_pmu_driver();
            perf_config.has_64bit_uname = msg.has_64bit_uname();
            perf_config.use_64bit_register_set = msg.use_64bit_register_set();
            perf_config.has_exclude_callchain_kernel = msg.has_exclude_callchain_kernel();
        }

        void extract_clusters(
            google::protobuf::RepeatedPtrField<ipc::proto::shell::perf::capture_configuration_t::cpu_cluster_t> & msg,
            std::vector<perf_capture_configuration_t::gator_cpu_t> & clusters,
            std::vector<perf_capture_configuration_t::cpu_freq_properties_t> & cluster_keys_for_cpu_frequency_counter)
        {
            for (auto & entry : msg) {
                auto * cluster = entry.mutable_properties();
                clusters.emplace_back(std::move(*cluster->mutable_core_name()),
                                      std::move(*cluster->mutable_id()),
                                      std::move(*cluster->mutable_counter_set()),
                                      std::move(*cluster->mutable_dt_name()),
                                      std::move(*cluster->mutable_spe_name()),
                                      std::vector<int>(cluster->cpu_ids().begin(), cluster->cpu_ids().end()),
                                      cluster->pmnc_counters(),
                                      cluster->is_v8());
                cluster_keys_for_cpu_frequency_counter.emplace_back(
                    perf_capture_configuration_t::cpu_freq_properties_t {entry.keys_for_cpu_frequency_counter(),
                                                                         entry.cpu_frequency_counter_uses_cpu_info()});
            }
        }

        void extract_cpus(google::protobuf::RepeatedPtrField<
                              ipc::proto::shell::perf::capture_configuration_t::cpu_properties_t> const & msg,
                          std::vector<std::int32_t> & per_core_cluster_index,
                          std::vector<std::int32_t> & per_core_cpuids,
                          std::map<core_no_t, std::uint32_t> & per_core_spe_type)
        {
            std::int32_t index = 0;
            for (auto const & cpu : msg) {
                per_core_cluster_index.emplace_back(cpu.cluster_index());
                per_core_cpuids.emplace_back(cpu.cpu_id());
                per_core_spe_type[core_no_t(index)] = cpu.spe_type();
                ++index;
            }
        }

        void extract_uncore_pmus(
            google::protobuf::RepeatedPtrField<ipc::proto::shell::perf::capture_configuration_t::uncore_pmu_t> & msg,
            std::vector<perf_capture_configuration_t::uncore_pmu_t> & uncore_pmus)
        {
            for (auto & entry : msg) {
                uncore_pmus.emplace_back(std::move(*entry.mutable_core_name()),
                                         std::move(*entry.mutable_id()),
                                         std::move(*entry.mutable_counter_set()),
                                         std::move(*entry.mutable_device_instance()),
                                         entry.pmnc_counters(),
                                         entry.has_cycles_counter());
            }
        }

        void extract_cpuid_to_core_name(google::protobuf::Map<::google::protobuf::uint32, std::string> & map,
                                        std::map<std::uint32_t, std::string> & cpuid_to_core_name)
        {
            for (auto & entry : map) {
                cpuid_to_core_name.emplace(entry.first, std::move(entry.second));
            }
        }

        template<typename A, typename B, typename C>
        void set_one_of(bool first, A & a, B & b, C c)
        {
            if (first) {
                a = c;
            }
            else {
                b = c;
            }
        }

        perf_event_attr extract_perf_event_attr(
            ipc::proto::shell::perf::capture_configuration_t::perf_event_attribute_t const & msg)
        {
            perf_event_attr result {};

            result.size = sizeof(perf_event_attr);
            result.type = msg.type();
            result.config = msg.config();
            result.sample_type = msg.sample_type();
            result.read_format = msg.read_format();
            result.disabled = msg.disabled();
            result.inherit = msg.inherit();
            result.pinned = msg.pinned();
            result.exclusive = msg.exclusive();
            result.exclude_user = msg.exclude_user();
            result.exclude_kernel = msg.exclude_kernel();
            result.exclude_hv = msg.exclude_hv();
            result.exclude_idle = msg.exclude_idle();
            result.mmap = msg.mmap();
            result.comm = msg.comm();
            result.freq = msg.freq();
            result.inherit_stat = msg.inherit_stat();
            result.enable_on_exec = msg.enable_on_exec();
            result.task = msg.task();
            result.watermark = msg.watermark();
            result.precise_ip = msg.precise_ip();
            result.mmap_data = msg.mmap_data();
            result.sample_id_all = msg.sample_id_all();
            result.exclude_host = msg.exclude_host();
            result.exclude_guest = msg.exclude_guest();
            result.exclude_callchain_kernel = msg.exclude_callchain_kernel();
            result.exclude_callchain_user = msg.exclude_callchain_user();
            result.mmap2 = msg.mmap2();
            result.comm_exec = msg.comm_exec();
            result.use_clockid = msg.use_clockid();
            result.context_switch = msg.context_switch();
            result.config1 = msg.config1();
            result.config2 = msg.config2();
            result.sample_regs_user = msg.sample_regs_user();
            result.sample_stack_user = msg.sample_stack_user();
            result.clockid = msg.clockid();
            result.aux_watermark = msg.aux_watermark();
            set_one_of(result.freq, result.sample_freq, result.sample_period, msg.sample_period_or_freq());
            set_one_of(result.watermark,
                       result.wakeup_watermark,
                       result.wakeup_events,
                       msg.wakeup_events_or_watermark());
            return result;
        }

        void extract_event_definition_list(
            ipc::proto::shell::perf::capture_configuration_t::perf_event_definition_list_t const & msg,
            std::vector<event_definition_t> & events)
        {
            for (auto const & entry : msg.events()) {
                events.emplace_back(event_definition_t {
                    extract_perf_event_attr(entry.attr()),
                    gator_key_t(entry.key()),
                });
            }
        }

        void extract_event_configuration(
            ipc::proto::shell::perf::capture_configuration_t::perf_event_configuration_t const & msg,
            event_configuration_t & event_configuration,
            std::vector<perf_capture_configuration_t::gator_cpu_t> const & clusters,
            std::vector<perf_capture_configuration_t::uncore_pmu_t> const & uncore_pmus,
            std::size_t num_cores)
        {
            runtime_assert(msg.has_header_event(), "missing header_event");

            auto const & hm = msg.header_event();
            event_configuration.header_event = event_definition_t {
                extract_perf_event_attr(hm.attr()),
                gator_key_t(hm.key()),
            };

            extract_event_definition_list(msg.global_events(), event_configuration.global_events);
            extract_event_definition_list(msg.spe_events(), event_configuration.spe_events);

            for (auto const & entry : msg.cluster_specific_events()) {
                runtime_assert(entry.first < clusters.size(), "Invalid cluster id received");
                auto id = cpu_cluster_id_t(entry.first);
                extract_event_definition_list(entry.second, event_configuration.cluster_specific_events[id]);
            }

            for (auto const & entry : msg.uncore_specific_events()) {
                runtime_assert(entry.first < uncore_pmus.size(), "Invalid uncore id received");
                auto id = uncore_pmu_id_t(entry.first);
                extract_event_definition_list(entry.second, event_configuration.uncore_specific_events[id]);
            }

            for (auto const & entry : msg.cpu_specific_events()) {
                runtime_assert(entry.first < num_cores, "Invalid core no received");
                auto id = core_no_t(entry.first);
                extract_event_definition_list(entry.second, event_configuration.cpu_specific_events[id]);
            }
        }

        void extract_ringbuffer_config(
            ipc::proto::shell::perf::capture_configuration_t::perf_ringbuffer_config_t const & msg,
            buffer_config_t & ringbuffer_config)
        {
            ringbuffer_config.page_size = msg.page_size();
            ringbuffer_config.data_buffer_size = msg.data_size();
            ringbuffer_config.aux_buffer_size = msg.aux_size();
        }

        std::vector<std::string> extract_args(google::protobuf::RepeatedPtrField<std::string> && args)
        {
            std::vector<std::string> result {};

            for (auto & arg : args) {
                result.emplace_back(std::move(arg));
            }

            return result;
        }

        void extract_command(ipc::proto::shell::perf::capture_configuration_t::command_t & msg,
                             std::optional<perf_capture_configuration_t::command_t> & command)
        {
            if (msg.command().empty()) {
                return;
            }

            command = perf_capture_configuration_t::command_t {
                std::move(*msg.mutable_command()),
                extract_args(std::move(*msg.mutable_args())),
                std::move(*msg.mutable_cwd()),
                msg.uid(),
                msg.gid(),
            };
        }

        void extract_wait_process(std::string & msg, std::string & wait_process)
        {
            if (!msg.empty()) {
                wait_process = msg;
            }
        }

        void extract_android_pkg(std::string & msg, std::string & android_pkg)
        {
            if (!msg.empty()) {
                android_pkg = msg;
            }
        }

        void extract_pids(ipc::proto::shell::perf::capture_configuration_t::pid_array_t const & msg,
                          std::set<pid_t> & pids)
        {
            for (auto pid : msg.pids()) {
                pids.insert(pid);
            }
        }

        void extract_perf_pmu_type_to_name(google::protobuf::Map<::google::protobuf::uint32, std::string> & msg,
                                           std::map<std::uint32_t, std::string> & perf_pmu_type_to_name)
        {
            for (auto & entry : msg) {
                perf_pmu_type_to_name.emplace(entry.first, std::move(entry.second));
            }
        }
    }

    /* create the message */
    ipc::msg_capture_configuration_t create_capture_configuration_msg(
        SessionData const & session_data,
        PerfConfig const & perf_config,
        ICpuInfo const & cpu_info,
        std::map<int, std::uint32_t> const & cpu_number_to_spe_type,
        lib::Span<perf_capture_configuration_t::cpu_freq_properties_t> cluster_keys_for_cpu_frequency_counter,
        lib::Span<UncorePmu const> uncore_pmus,
        lib::Span<GatorCpu const> all_known_cpu_pmus,
        perf_groups_configurer_state_t const & perf_groups,
        agents::perf::buffer_config_t const & ringbuffer_config,
        std::map<std::uint32_t, std::string> const & perf_pmu_type_to_name,
        bool enable_on_exec,
        bool stop_pids)
    {
        ipc::msg_capture_configuration_t result {};

        add_session_data(*result.suffix.mutable_session_data(), session_data);
        add_perf_config(*result.suffix.mutable_perf_config(), perf_config);
        add_clusters(*result.suffix.mutable_clusters(), cpu_info, cluster_keys_for_cpu_frequency_counter);
        add_cpus(*result.suffix.mutable_cpus(), cpu_info, cpu_number_to_spe_type);
        add_uncore_pmus(*result.suffix.mutable_uncore_pmus(), uncore_pmus);
        add_cpuid_to_core_name(*result.suffix.mutable_cpuid_to_core_name(), all_known_cpu_pmus);
        add_event_configuration(*result.suffix.mutable_event_configuration(), perf_groups, cpu_info, uncore_pmus);
        add_ringbuffer_config(*result.suffix.mutable_ringbuffer_config(), ringbuffer_config);
        add_perf_pmu_type_to_name(*result.suffix.mutable_perf_pmu_type_to_name(), perf_pmu_type_to_name);

        result.suffix.set_num_cpu_cores(cpu_info.getNumberOfCores());
        result.suffix.set_enable_on_exec(enable_on_exec);
        result.suffix.set_stop_pids(stop_pids);

        return result;
    }

    void add_command(ipc::msg_capture_configuration_t & msg,
                     lib::Span<std::string const> cmd_args,
                     char const * working_dir,
                     uid_t uid,
                     gid_t gid)
    {
        if (!cmd_args.empty()) {
            auto * cmd = msg.suffix.mutable_command();

            SET_IF_NOT_NULL(working_dir, cmd, set_cwd);

            cmd->set_command(cmd_args.front());
            cmd->set_uid(uid);
            cmd->set_gid(gid);

            for (std::size_t n = 1; n < cmd_args.size(); ++n) {
                cmd->add_args(cmd_args[n]);
            }
        }
    }

    void add_wait_for_process(ipc::msg_capture_configuration_t & msg, const char * command)
    {
        SET_IF_NOT_NULL(command, &msg.suffix, set_wait_process);
    }

    void add_android_package(ipc::msg_capture_configuration_t & msg, const char * android_pkg)
    {
        SET_IF_NOT_NULL(android_pkg, &msg.suffix, set_android_pkg);
    }

    void add_pids(ipc::msg_capture_configuration_t & msg, std::set<int> const & pids)
    {
        auto * msg_pids = msg.suffix.mutable_pids();
        for (auto pid : pids) {
            msg_pids->add_pids(pid);
        }
    }

    std::shared_ptr<perf_capture_configuration_t> parse_capture_configuration_msg(ipc::msg_capture_configuration_t msg)
    {
        auto result = std::make_shared<perf_capture_configuration_t>();

        extract_session_data(msg.suffix.session_data(), result->session_data);
        extract_perf_config(msg.suffix.perf_config(), result->perf_config);
        extract_clusters(*msg.suffix.mutable_clusters(),
                         result->clusters,
                         result->cluster_keys_for_cpu_frequency_counter);
        extract_cpus(msg.suffix.cpus(),
                     result->per_core_cluster_index,
                     result->per_core_cpuids,
                     result->per_core_spe_type);
        extract_uncore_pmus(*msg.suffix.mutable_uncore_pmus(), result->uncore_pmus);
        extract_cpuid_to_core_name(*msg.suffix.mutable_cpuid_to_core_name(), result->cpuid_to_core_name);

        result->num_cpu_cores = msg.suffix.num_cpu_cores();
        result->enable_on_exec = msg.suffix.enable_on_exec();
        result->stop_pids = msg.suffix.stop_pids();

        extract_event_configuration(msg.suffix.event_configuration(),
                                    result->event_configuration,
                                    result->clusters,
                                    result->uncore_pmus,
                                    result->num_cpu_cores);
        extract_ringbuffer_config(msg.suffix.ringbuffer_config(), result->ringbuffer_config);
        extract_command(*msg.suffix.mutable_command(), result->command);
        extract_wait_process(*msg.suffix.mutable_wait_process(), result->wait_process);
        extract_android_pkg(*msg.suffix.mutable_android_pkg(), result->android_pkg);
        extract_pids(msg.suffix.pids(), result->pids);
        extract_perf_pmu_type_to_name(*msg.suffix.mutable_perf_pmu_type_to_name(), result->perf_pmu_type_to_name);

        return result;
    }
}
