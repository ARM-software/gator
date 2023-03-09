# Copyright (C) 2023 by Arm Limited
#
# SPDX-License-Identifier: BSD-3-Clause

from enum import Enum
from typing import List, Optional, Set

class ConfigValidationError(Exception):
    def __init__(self, message):
        super().__init__(message)


class DataStorageBackend(str, Enum):
    CIRCULAR_RAM_BUFFER = "circular"
    ITM_INTERFACE = "itm_interface"
    LINEAR_RAM_BUFFER = "linear"
    STM_INTERFACE = "stm_interface"

    def get_define_constant(self):
        if self == DataStorageBackend.CIRCULAR_RAM_BUFFER:
            return 'BM_CONFIG_USE_DATASTORE_CIRCULAR_RAM_BUFFER'
        elif self == DataStorageBackend.LINEAR_RAM_BUFFER:
            return 'BM_CONFIG_USE_DATASTORE_LINEAR_RAM_BUFFER'
        elif self == DataStorageBackend.ITM_INTERFACE:
            return 'BM_CONFIG_USE_DATASTORE_ITM'
        elif self == DataStorageBackend.STM_INTERFACE:
            return 'BM_CONFIG_USE_DATASTORE_STM'
        else:
            raise ValueError('Unknown DataStorageBackend enum type: {}'.format(self))

    def get_required_source_lines(self):
        if self == DataStorageBackend.CIRCULAR_RAM_BUFFER:
            return \
                '#if BM_CONFIG_USE_DATASTORE == BM_CONFIG_USE_DATASTORE_CIRCULAR_RAM_BUFFER\n' + \
                '#include "data-store/barman-circular-ram-buffer.c"\n' + \
                '#endif\n\n'
        elif self == DataStorageBackend.LINEAR_RAM_BUFFER:
            return \
                '#if BM_CONFIG_USE_DATASTORE == BM_CONFIG_USE_DATASTORE_LINEAR_RAM_BUFFER\n' + \
                '#include "data-store/barman-linear-ram-buffer.c"\n' + \
                '#endif\n\n'
        elif self == DataStorageBackend.ITM_INTERFACE:
            return \
                '#if BM_CONFIG_USE_DATASTORE == BM_CONFIG_USE_DATASTORE_ITM\n' + \
                '#include "data-store/barman-itm.c"\n' + \
                '#endif\n\n'
        elif self == DataStorageBackend.STM_INTERFACE:
            return \
                '#if BM_CONFIG_USE_DATASTORE == BM_CONFIG_USE_DATASTORE_STM\n' + \
                '#include "data-store/barman-stm.c"\n' + \
                '#endif\n\n'

    def __str__(self):
        return self.value


class RuntimeConfigDefaults:
    def __init__(self):
        self.enable_builtin_mem_funcs: bool = True
        self.enable_debug_logging: bool = False
        self.enable_logging: bool = False
        self.max_cores: int = 1
        self.max_mmap_layout_entries: int = 0
        self.max_task_entries: int = 0
        self.minimum_sample_period: int = 0


class StmBackendConfig:
    def __init__(self):
        self.master_ids: List[int] = []
        self.min_channel_number: int = 50_000
        self.number_of_channels: int = 1


class ItmBackendConfig:
    def __init__(self):
        self.port_number: int = 16
        self.num_ports_used: int = 4


class ProcessorConfig:
    def __init__(self):
        self.name: str = None
        self.cpuid: str = None
        self.events: Set[int] = set()
        self.sample_cycle_counter = False


class ClockInfo:
    def __init__(self):
        self.timestamp_base: int = 0
        self.timestamp_divisor: int = 0
        self.timestamp_multiplier: int = 0
        self.unix_base_ns: int = 0



class SeriesComposition(Enum):
    LOG10 = 0
    OVERLAY = 1
    STACKED = 2
    # we don't include the VISUAL_ANNOTATION type because it's not valid for barman

    def from_string(name: str):
        for e in SeriesComposition:
            if e.name.lower() == name.lower():
                return e
        raise ValueError('Unknown SeriesComposition value "{}"'.format(name))


class RenderingType(Enum):
    FILLED = 0
    LINE = 1
    BAR = 2
    HEATMAP = 3

    def from_string(name: str):
        for e in RenderingType:
            if e.name.lower() == name.lower():
                return e
        raise ValueError('Unknown RenderingType value "{}"'.format(name))


class CounterClass(Enum):
    DELTA = 0
    INCIDENT = 1
    ABSOLUTE = 2
    #ACTIVITY = 3  -- activity series is not supported under barman
    #CONSTANT = 4  -- constant counters not supported either

    def from_string(name: str):
        for e in CounterClass:
            if e.name.lower() == name.lower():
                return e
        raise ValueError('Unknown CounterClass value "{}"'.format(name))


class CounterDisplay(Enum):
    ACCUMULATE = 0
    AVERAGE = 1
    HERTZ = 2
    MAXIMUM = 3
    MINIMUM = 4

    def check_compatible_with(self, counter_class: CounterClass) -> bool:
        if self == CounterDisplay.ACCUMULATE:
            return counter_class in [CounterClass.DELTA, CounterClass.INCIDENT]
        elif self == CounterDisplay.AVERAGE:
            return counter_class == CounterClass.ABSOLUTE
        elif self == CounterDisplay.HERTZ:
            return counter_class in [CounterClass.DELTA, CounterClass.INCIDENT]
        elif self == CounterDisplay.MAXIMUM:
            return counter_class == CounterClass.ABSOLUTE
        elif self == CounterDisplay.MINIMUM:
            return counter_class == CounterClass.ABSOLUTE
        return False


    def from_string(name: str):
        for e in CounterDisplay:
            if e.name.lower() == name.lower():
                return e
        raise ValueError('Unknown CounterDisplay value "{}"'.format(name))


def _format_name_as_identifier(name: str) -> str:
    if name is None or len(name) == 0:
        return "_"
    import re
    result = re.sub(r"(^[0-9])|([^0-9A-Za-z_])", '_', name)
    return result


class CustomChartSeries:
    def __init__(self):
        self.name = ''
        self.description = ''
        self.units = ''
        self.sample = False
        self.multiplier = 1
        self.colour: int = None
        self.counter_class = CounterClass.DELTA
        self._counter_display: CounterDisplay = None

    def set_display(self, display: CounterDisplay):
        self._counter_display = display

    def get_display(self) -> CounterDisplay:
        if self._counter_display is not None:
            return self._counter_display

        if self.counter_class is None:
            return CounterDisplay.ACCUMULATE
        if self.counter_class == CounterClass.ABSOLUTE:
            return CounterDisplay.MAXIMUM

        return CounterDisplay.ACCUMULATE

    def get_name_identifier(self):
        return _format_name_as_identifier(self.name)


class CustomChart:
    def __init__(self):
        self.name: str = None
        self.series_composition = SeriesComposition.STACKED
        self.rendering_type = RenderingType.FILLED
        self.average_cores = False
        self.average_selection = False
        self.percentage = False
        self.per_cpu = False
        self.series: List[CustomChartSeries] = []

    def get_name_identifier(self):
        return _format_name_as_identifier(self.name)


class BarmanConfig:
    def __init__(self):
        self.runtime_config_defaults = RuntimeConfigDefaults()
        self.data_storage_backend: DataStorageBackend = None
        self.stm_config: StmBackendConfig = None
        self.dwt_sampling_period: int = 0
        self.itm_config: ItmBackendConfig = None
        self.streaming_header_every_n_records: Optional[int] = None
        self.processor_configs: List[ProcessorConfig] = None
        self.target_name: str = None
        self.clock_info: ClockInfo = None
        self.custom_charts: List[CustomChart] = []