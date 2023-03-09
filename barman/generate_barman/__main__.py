# Copyright (C) 2023 by Arm Limited
#
# SPDX-License-Identifier: BSD-3-Clause

import argparse
import os
import os.path
import sys
from typing import List

from .config import *
from .customcounters import CustomCounterTokenProcessor
from .parser import BadBarmanXmlError, BarmanConfig, BarmanXmlParser
from .snippets import *
from .sourceprocessor import *


CYCLE_COUNTER_KEY = -1

base_path = os.path.dirname(os.path.abspath(__file__))
res_dir = os.path.join(base_path, 'resources')


class ApiFunctionsTokenProcessor(TokenDirectiveProcessor):
    """
    Generates the function that initializes the PMUs based on the CPUs and
    events that the user specified in the Barman config XML.
    """
    def __init__(self, config: BarmanConfig):
        self._config = config

    def replace_token(self, token:str) -> List[str]:
        lines = []

        lines.append('bm_bool barman_generated_initialize(void)')
        lines.append('{')
        lines.append('    bm_bool initialize_success = BM_FALSE;')
        lines.append('')

        for processor in self._config.processor_configs:
            events_str = ', '.join(str(x) for x in processor.events if x != CYCLE_COUNTER_KEY)
            lines.append('    /* {} */'.format(processor.name))
            lines.append('    {')
            lines.append('        const bm_uint32 pmu_event_types[{}] = {{{}}};'
                .format(len(processor.events), events_str))
            lines.append('')

            midr = ((processor.cpuid & 0xFF000) << 12) | ((processor.cpuid & 0xFFF) << 4)
            lines.append('        if (barman_initialize_pmu_family(0x{:x}, {}, pmu_event_types, BM_NULL)) {{'
                .format(midr, len(processor.events)))
            lines.append('            initialize_success = BM_TRUE;')
            lines.append('        }')

        lines.append('    }')
        lines.append('')
        lines.append('    return initialize_success;')
        lines.append('}')
        lines.append('')
        return lines


def generate_barman_c_template(config: BarmanConfig) -> str:
    template = barman_c_heading_snippet
    for ds in DataStorageBackend:
        template += ds.get_required_source_lines()

    template += streaming_iface_snippet

    template += "#if !BM_CONFIG_USER_SUPPLIED_PMU_DRIVER\n"
    template += pmu_driver_snippet

    if config.dwt_sampling_period > 0:
        template += '#include "pmu/barman-arm-dwt.c"\n'
    else:
        template += '#include "pmu/barman-arm-pmu.c"\n'
    template += "#endif\n"
    template += barman_c_footer_snippet
    return template


def generate_source_file(resolver: FileContentResolver, config: BarmanConfig, out_dir:str):
    resolver.register_content('barman.c', generate_barman_c_template(config))

    processor = SourceProcessor(resolver)
    # Add the token processor that generates the initialization routines for the cores/PMUs
    processor.register_token_processor('API_FUNCTIONS_IMPL', ApiFunctionsTokenProcessor(config))

    # Add the token processor that generates the code for custom counters
    custom_counters_processor = CustomCounterTokenProcessor(config)
    custom_counters_processor.register_with_source_processor(processor)


    with open(os.path.join(out_dir, 'barman.c'), 'w') as f:
        processor.process_file('barman.c', f)


def generate_header_file(content_resolver: FileContentResolver, config: BarmanConfig, out_dir: str):
    processor = SourceProcessor(content_resolver)

    # Add the token processor that generates the code for custom counters
    custom_counters_processor = CustomCounterTokenProcessor(config)
    custom_counters_processor.register_with_source_processor(processor)

    processor.register_definition('BM_CONFIG_ENABLE_LOGGING', config.runtime_config_defaults.enable_logging)
    processor.register_definition('BM_CONFIG_ENABLE_DEBUG_LOGGING', config.runtime_config_defaults.enable_debug_logging)
    processor.register_definition('BM_CONFIG_ENABLE_BUILTIN_MEMFUNCS', config.runtime_config_defaults.enable_builtin_mem_funcs)
    processor.register_definition('BM_CONFIG_MAX_CORES', config.runtime_config_defaults.max_cores)
    processor.register_definition('BM_CONFIG_MAX_MMAP_LAYOUTS', config.runtime_config_defaults.max_mmap_layout_entries)
    processor.register_definition('BM_CONFIG_MAX_TASK_INFOS', config.runtime_config_defaults.max_task_entries)
    processor.register_definition('BM_CONFIG_MIN_SAMPLE_PERIOD', config.runtime_config_defaults.minimum_sample_period)
    processor.register_definition('BM_CONFIG_USE_DATASTORE', config.data_storage_backend.get_define_constant())
    processor.register_definition('BM_CONFIG_NUM_PMU_TYPES', len(config.processor_configs))

    stm_config = config.stm_config
    if stm_config is not None:
        processor.register_definition('BM_CONFIG_STM_MIN_CHANNEL_NUMBER', stm_config.min_channel_number)
        processor.register_definition('BM_CONFIG_STM_NUMBER_OF_CHANNELS', stm_config.number_of_channels)

    if config.streaming_header_every_n_records is not None:
        processor.register_definition('BM_CONFIG_RECORDS_PER_HEADER_SENT', config.streaming_header_every_n_records)

    itm_config = config.itm_config
    if itm_config is not None:
        processor.register_definition('BM_CONFIG_ITM_MIN_PORT_NUMBER', itm_config.port_number)
        processor.register_definition('BM_CONFIG_ITM_NUMBER_OF_PORTS', itm_config.num_ports_used)

    if config.dwt_sampling_period > 0:
        processor.register_definition('BM_CONFIG_DWT_SAMPLE_PERIOD', config.dwt_sampling_period)

    with open(os.path.join(out_dir, 'barman.h'), 'w') as f:
        processor.process_file('barman.h.template', f)


def generate_merged_files(barman_dir: str, config: BarmanConfig, out_dir: str):
    resolver = FileContentResolver([res_dir, barman_dir])
    generate_header_file(resolver, config, out_dir)
    generate_source_file(resolver, config, out_dir)


def main():
    arg_parser = argparse.ArgumentParser(prog='generate_barman')
    arg_parser.add_argument('-d', '--barman-dir', required=True,
        help='Path to the directory containing the Barman sources.')
    arg_parser.add_argument('-o', '--output-dir', default='generated',
        help='Store the generated files in this directory.')
    arg_parser.add_argument('-c', '--barman-cfg', default='barman.xml',
        help='Path to the barman.xml configuration file.')

    args = arg_parser.parse_args()

    src_dir = os.path.join(args.barman_dir, 'src')
    if not os.path.exists(src_dir):
        print('Barman source directory could not be found at {}'.format(src_dir))
        return -1

    if not os.path.isfile(args.barman_cfg):
        print('Could not find barman XML configuration file at {}'.format(os.path.abspath(args.barman_cfg)))
        return -1

    if not os.path.exists(args.output_dir):
        os.makedirs(args.output_dir)

    config = BarmanXmlParser().parse(args.barman_cfg)
    generate_merged_files(src_dir, config, args.output_dir)

    print('Generated Barman files written to {}'.format(os.path.abspath(args.output_dir)))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except ProcessingError as e:
        print('Error processing barman source files: {}'.format(e))
    except BadBarmanXmlError as e:
        print('Error reading barman config file: {}'.format(e))
    sys.exit(-1)